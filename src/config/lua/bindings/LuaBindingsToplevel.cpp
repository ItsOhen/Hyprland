#include "LuaBindingsInternal.hpp"

#include "../objects/LuaEventSubscription.hpp"
#include "../objects/LuaKeybind.hpp"
#include "../objects/LuaTimer.hpp"
#include "../objects/LuaProcessHandle.hpp"

#include "../../supplementary/executor/Executor.hpp"
#include "../../../devices/IKeyboard.hpp"
#include "../../../managers/eventLoop/EventLoopManager.hpp"
#include "../../../plugins/PluginSystem.hpp"

#include <hyprutils/string/Numeric.hpp>
#include <hyprutils/string/String.hpp>
#include <hyprutils/string/VarList.hpp>

using namespace Config;
using namespace Config::Lua;
using namespace Config::Lua::Bindings;
using namespace Hyprutils::String;

static std::optional<eKeyboardModifiers> modFromSv(std::string_view sv) {
    if (sv == "SHIFT")
        return HL_MODIFIER_SHIFT;
    if (sv == "CAPS")
        return HL_MODIFIER_CAPS;
    if (sv == "CTRL" || sv == "CONTROL")
        return HL_MODIFIER_CTRL;
    if (sv == "ALT" || sv == "MOD1")
        return HL_MODIFIER_ALT;
    if (sv == "MOD2")
        return HL_MODIFIER_MOD2;
    if (sv == "MOD3")
        return HL_MODIFIER_MOD3;
    if (sv == "SUPER" || sv == "WIN" || sv == "LOGO" || sv == "MOD4" || sv == "META")
        return HL_MODIFIER_META;
    if (sv == "MOD5")
        return HL_MODIFIER_MOD5;

    return std::nullopt;
}

static bool isSymSpecial(std::string_view sv) {
    if (sv == "mouse_down" || sv == "mouse_up" || sv == "mouse_left" || sv == "mouse_right")
        return true;

    return sv.starts_with("switch:") || sv.starts_with("mouse:");
}

static std::expected<void, std::string> parseKeyString(SKeybind& kb, std::string_view sv) {
    bool                                                modsEnded = false, specialSym = false;
    CVarList2                                           vl(sv, 0, '+', true);

    uint32_t                                            modMask = 0;
    std::vector<std::pair<xkb_keysym_t, xkb_keycode_t>> keysyms;
    std::string                                         lastKeyArg;

    if (sv == "catchall") {
        kb.catchAll = true;
        return {};
    }

    for (const auto& a : vl) {
        auto arg = Hyprutils::String::trim(a);

        auto mask = modFromSv(arg);

        if (!mask)
            modsEnded = true;

        if (modsEnded && mask)
            return std::unexpected("Modifiers must come first in the list");

        if (mask) {
            modMask |= *mask;
            continue;
        }

        if (specialSym)
            return std::unexpected("Cannot combine special syms (e.g. mouse_down + Q)");

        if (isSymSpecial(arg)) {
            if (!keysyms.empty())
                return std::unexpected("Cannot combine special syms (e.g. mouse_down + Q)");

            specialSym = true;
            kb.key     = arg;
            continue;
        }

        if (arg.starts_with("code:") && isNumber(std::string{arg.substr(5)})) {
            auto res = strToNumber<uint32_t>(arg.substr(5));

            if (!res)
                return std::unexpected(std::format("Invalid keycode: \"{}\".", arg));

            keysyms.emplace_back(XKB_KEY_NoSymbol, xkb_keycode_t{*res});
            continue;
        }

        auto sym = xkb_keysym_from_name(std::string{arg}.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);

        if (sym == XKB_KEY_NoSymbol) {
            if (arg.contains(' '))
                return std::unexpected(std::format("Unknown keysym: \"{}\", did you forget a +?", arg));

            if (arg == "Enter")
                return std::unexpected(std::format(R"(Unknown keysym: "{}", did you mean "Return"?)", arg));

            return std::unexpected(std::format("Unknown keysym: \"{}\"", arg));
        }

        lastKeyArg = arg;
        keysyms.emplace_back(sym, 0);
    }

    kb.modmask = modMask;
    kb.sMkKeys = std::move(keysyms);
    if (!specialSym && !lastKeyArg.empty())
        kb.key = lastKeyArg;
    return {};
}

static int hlBind(lua_State* L) {
    auto*            mgr  = (CConfigManager*)lua_touserdata(L, lua_upvalueindex(1));
    std::string_view keys = luaL_checkstring(L, 1);

    SKeybind         kb;
    kb.submap = {mgr->m_currentSubmap, mgr->m_currentSubmapReset};

    if (auto res = parseKeyString(kb, keys); !res)
        return Internal::configError(L, std::format("hl.bind: failed parse: {}", res.error()));

    if (!Internal::pushDispatcherFunction(L, 2))
        return Internal::configError(L, "hl.bind: dispatcher must be a function or hl.dsp");

    if (kb.catchAll && kb.submap.name.empty())
        return Internal::configError(L, "hl.bind: catchall requires a submap.");

    SP<SKeybind> pTarget = nullptr;
    for (const auto& ex : g_pKeybindManager->m_keybinds) {
        if (ex->handler == "__lua" && ex->submap.name == kb.submap.name && ex->catchAll == kb.catchAll && (kb.catchAll || (ex->key == kb.key && ex->modmask == kb.modmask))) {
            int  ref   = std::stoi(ex->arg);
            auto genIt = mgr->m_luaKeybindRefGen.find(ref);
            if (genIt != mgr->m_luaKeybindRefGen.end() && mgr->isStale(genIt->second.generation, genIt->second.sourcePath)) {
                pTarget = ex;
                break;
            }
        }
    }

    int newRef = luaL_ref(L, LUA_REGISTRYINDEX);

    if (pTarget) {
        int oldRef = std::stoi(pTarget->arg);
        lua_rawgeti(L, LUA_REGISTRYINDEX, oldRef);
        lua_rawgeti(L, LUA_REGISTRYINDEX, newRef);
        CConfigManager::copyUpvalues(L, -2, -1, std::format("hl.bind(\"{}\")", keys));
        lua_pop(L, 2);

        luaL_unref(L, LUA_REGISTRYINDEX, oldRef);
        mgr->m_luaKeybindRefGen.erase(oldRef);
        pTarget->arg = std::to_string(newRef);
    } else {
        kb.handler    = "__lua";
        kb.arg        = std::to_string(newRef);
        kb.displayKey = keys;
        pTarget       = g_pKeybindManager->addKeybind(kb);
    }

    mgr->m_luaKeybindRefGen[newRef] = {.generation = !mgr->isSweepImmune() ? mgr->currentGeneration() : 0, .sourcePath = mgr->currentLuaSourcePath()};

    if (lua_istable(L, 3)) {
        auto getB = [&](const char* f) {
            lua_getfield(L, 3, f);
            bool v = lua_toboolean(L, -1);
            lua_pop(L, 1);
            return v;
        };
        auto getS = [&](const char* f) {
            lua_getfield(L, 3, f);
            std::optional<std::string> s;
            if (lua_isstring(L, -1))
                s = lua_tostring(L, -1);
            lua_pop(L, 1);
            return s;
        };

        pTarget->repeat          = getB("repeating");
        pTarget->locked          = getB("locked");
        pTarget->release         = getB("release");
        pTarget->nonConsuming    = getB("non_consuming");
        pTarget->autoConsuming   = getB("auto_consuming");
        pTarget->transparent     = getB("transparent");
        pTarget->ignoreMods      = getB("ignore_mods");
        pTarget->dontInhibit     = getB("dont_inhibit");
        pTarget->longPress       = getB("long_press");
        pTarget->submapUniversal = getB("submap_universal");

        if (auto d = getS("description"); d) {
            pTarget->description    = *d;
            pTarget->hasDescription = true;
        } else if (auto ds = getS("desc"); ds) {
            pTarget->description    = *ds;
            pTarget->hasDescription = true;
        }

        if (getB("click")) {
            pTarget->click   = true;
            pTarget->release = true;
        }
        if (getB("drag")) {
            pTarget->drag    = true;
            pTarget->release = true;
        }

        lua_getfield(L, 3, "device");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "inclusive");
            pTarget->deviceInclusive = lua_isnil(L, -1) || lua_toboolean(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "list");
            if (lua_istable(L, -1)) {
                pTarget->devices.clear();
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    if (lua_isstring(L, -1))
                        pTarget->devices.emplace(lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    Objects::CLuaKeybind::push(L, pTarget);
    return 1;
}

static int hlDefineSubmap(lua_State* L) {
    auto*       mgr  = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* name = luaL_checkstring(L, 1);

    std::string reset;
    int         fnIdx = 2;
    if (lua_gettop(L) >= 3 && lua_isstring(L, 2)) {
        reset = lua_tostring(L, 2);
        fnIdx = 3;
    }

    luaL_checktype(L, fnIdx, LUA_TFUNCTION);

    std::string prev          = mgr->m_currentSubmap;
    std::string prevReset     = mgr->m_currentSubmapReset;
    mgr->m_currentSubmap      = name;
    mgr->m_currentSubmapReset = reset;

    lua_pushvalue(L, fnIdx);
    if (mgr->guardedPCall(0, 0, 0, CConfigManager::LUA_TIMEOUT_DISPATCH_MS, std::format("hl.define_submap(\"{}\")", name)) != LUA_OK) {
        mgr->addError(std::format("hl.define_submap: error in submap \"{}\": {}", name, lua_tostring(L, -1)));
        lua_pop(L, 1);
    }

    mgr->m_currentSubmap      = prev;
    mgr->m_currentSubmapReset = prevReset;
    return 0;
}

static int hlVersion(lua_State* L) {
    lua_pushstring(L, HYPRLAND_VERSION);
    return 1;
}

static int hlGetPlugins(lua_State* L) {
    if (!g_pPluginSystem) {
        lua_newtable(L);
        return 1;
    }

    const auto PLUGINS = g_pPluginSystem->getAllPlugins();

    lua_createtable(L, PLUGINS.size(), 0);

    int i = 1;
    for (const auto& plugin : PLUGINS) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, plugin->m_name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, plugin->m_author.c_str());
        lua_setfield(L, -2, "author");
        lua_pushstring(L, plugin->m_version.c_str());
        lua_setfield(L, -2, "version");
        lua_pushstring(L, plugin->m_description.c_str());
        lua_setfield(L, -2, "description");
        lua_rawseti(L, -2, i++);
    }

    return 1;
}

static int hlExecCmd(lua_State* L) {
    auto cmd = Internal::argStr(L, 1);

    if (cmd.empty())
        return Internal::configError(L, "hl.exec_cmd: expected command as first argument");

    auto ruleRet = Internal::buildRuleFromTable(L, 2);
    if (!ruleRet)
        return ruleRet.error();

    auto                    rule = std::move(*ruleRet);

    std::optional<uint64_t> pid;
    if (rule)
        pid = Config::Supplementary::executor()->spawn(Config::Supplementary::SExecRequest{.exec = cmd, .rule = rule});
    else
        pid = Config::Supplementary::executor()->spawn(cmd);

    if (!pid.has_value()) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, (lua_Integer)*pid);
    }

    return 1;
}

static int hlExecRaw(lua_State* L) {
    auto cmd = Internal::argStr(L, 1);
    if (cmd.empty())
        return Internal::configError(L, "hl.exec_raw: expected command");

    auto pid = Config::Supplementary::executor()->spawnRaw(cmd);

    if (!pid || !*pid)
        lua_pushnil(L);
    else
        lua_pushinteger(L, (lua_Integer)*pid);

    return 1;
}

static int hlDispatch(lua_State* L) {
    int args = lua_gettop(L);
    if (args == 0)
        return Internal::pushSuccessResult(L);

    int targetIdx = args;

    if (Internal::pushDispatcherFunction(L, targetIdx)) {
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            return Internal::configError(L, "Dispatch failed: {}", err);
        }

        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return Internal::pushSuccessResult(L);
        }
        return 1;
    }

    if (lua_istable(L, targetIdx)) {
        lua_pushvalue(L, targetIdx);
        return 1;
    }

    return Internal::configError(L, "hl.dispatch: expected dispatcher object, got {}", lua_typename(L, lua_type(L, targetIdx)));
}

static int hlOn(lua_State* L) {
    auto*       mgr       = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* eventName = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int        ref = luaL_ref(L, LUA_REGISTRYINDEX);

    const auto gen     = !mgr->isSweepImmune() ? mgr->currentGeneration() : 0;
    const auto srcPath = mgr->currentLuaSourcePath();

    // Copy upvalues from old callback for same event, then remove old subs
    if (mgr->m_eventHandler)
        mgr->m_eventHandler->sweepEvent(eventName, ref, [&](const std::string& sp) -> uint64_t { return mgr->isSweepImmune() ? 0 : mgr->currentGeneration(); }, srcPath);

    const auto handle = mgr->m_eventHandler->registerEvent(eventName, ref, gen, srcPath);
    if (!handle.has_value()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        const auto& known = CLuaEventHandler::knownEvents();
        std::string list;
        for (const auto& e : known) {
            list += e + ", ";
        }
        list.pop_back();
        list.pop_back();
        return Internal::configError(L, "hl.on: unknown event \"{}\". Known events:{}", eventName, list);
    }

    Objects::CLuaEventSubscription::push(L, mgr->m_eventHandler.get(), *handle);
    return 1;
}

static int hlUnbind(lua_State* L) {
    if (lua_gettop(L) == 1 && lua_isstring(L, 1) && std::string_view(lua_tostring(L, 1)) == "all") {
        Config::Lua::postToMain([]() { g_pKeybindManager->clearKeybinds(); });
        return 0;
    }

    std::string str = luaL_checkstring(L, 1);
    Config::Lua::postToMain([str]() { g_pKeybindManager->removeKeybind(str); });

    return 0;
}

static int hlTimer(lua_State* L) {
    auto* mgr = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));

    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "timeout");
    int timeoutMs = (int)luaL_checknumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 2, "type");
    std::string type = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    bool   repeat = (type == "repeat");

    size_t id = 0;
    lua_getfield(L, 2, "id");
    if (!lua_isnil(L, -1))
        id = (size_t)luaL_checknumber(L, -1);
    lua_pop(L, 1);

    // If id matches an existing timer, update its Lua refs and return it
    if (id > 0) {
        auto it = std::ranges::find_if(mgr->m_luaTimers, [&](const auto& lt) { return lt.id == id; });
        if (it != mgr->m_luaTimers.end()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->luaRef);
            luaL_unref(L, LUA_REGISTRYINDEX, it->coRef);

            lua_pushvalue(L, 1);
            it->luaRef = luaL_ref(L, LUA_REGISTRYINDEX);

            lua_State* co    = lua_newthread(L);
            int        coRef = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_rawgeti(co, LUA_REGISTRYINDEX, it->luaRef);
            it->co         = co;
            it->coRef      = coRef;
            it->repeat     = repeat;
            it->timeoutMs  = timeoutMs;
            it->generation = !mgr->isSweepImmune() ? mgr->currentGeneration() : 0;
            it->sourcePath = mgr->currentLuaSourcePath();

            Objects::CLuaTimer::push(L, it->timer, timeoutMs);
            return 1;
        }
    }

    lua_pushvalue(L, 1);
    int        fnRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_State* co    = lua_newthread(L);
    int        coRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(co, LUA_REGISTRYINDEX, fnRef);

    auto timer = makeShared<CEventLoopTimer>(
        std::chrono::milliseconds(timeoutMs),
        [mgr, L](SP<CEventLoopTimer> self, void* data) {
            auto it = std::ranges::find_if(mgr->m_luaTimers, [&](const auto& lt) { return lt.timer == self; });
            if (it == mgr->m_luaTimers.end())
                return;

            if (it->repeat)
                self->updateTimeout(std::chrono::milliseconds(it->timeoutMs));

            lua_State* co    = it->co;
            int        fnRef = it->luaRef;
            int        coRef = it->coRef;

            int        nres   = 0;
            int        status = lua_resume(co, L, 0, &nres);

            if (status != LUA_OK && status != LUA_YIELD) {
                Log::logger->log(Log::LUA, "Timer error: {}", lua_tostring(co, -1));
            }

            if (status != LUA_YIELD) {
                luaL_unref(L, LUA_REGISTRYINDEX, coRef);
                luaL_unref(L, LUA_REGISTRYINDEX, fnRef);

                auto it2 = std::ranges::find_if(mgr->m_luaTimers, [&](const auto& lt) { return lt.timer == self; });
                if (it2 != mgr->m_luaTimers.end()) {
                    mgr->m_luaTimers.erase(it2);
                }
            }
        },
        nullptr);

    mgr->m_luaTimers.emplace_back(
        CConfigManager::SLuaTimer{timer, fnRef, coRef, co, !mgr->isSweepImmune() ? mgr->currentGeneration() : 0, mgr->currentLuaSourcePath(), id, repeat, timeoutMs});

    if (g_pEventLoopManager)
        g_pEventLoopManager->addTimer(timer);

    Objects::CLuaTimer::push(L, timer, timeoutMs);
    return 1;
}

static int hlAsyncExecCmd(lua_State* L) {
    auto* mgr = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto  cmd = Internal::argStr(L, 1);

    if (cmd.empty())
        return Internal::configError(L, "hl.async_exec_cmd: expected command as first argument");

    if (!mgr || !mgr->m_processExecutor || !mgr->m_eventHandler)
        return Internal::configError(L, "hl.async_exec_cmd: executor/event handler unavailable");

    auto* handle = Objects::pushNewProcessHandle(L, cmd);

    lua_pushvalue(L, -1);
    int        ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_State* lua = L;

    uint64_t   processId = mgr->m_processExecutor->spawnAsync(cmd, 30000, [lua, ref](const Config::Lua::SProcessResult& result) {
        const int stackTop = lua_gettop(lua);

        lua_rawgeti(lua, LUA_REGISTRYINDEX, ref);
        if (auto* h = Objects::toProcessHandle(lua, -1); h) {
            h->isComplete = true;
            h->result     = result;
            h->processId  = result.processId;

            if (auto* cfg = CConfigManager::fromLuaState(lua); cfg && cfg->m_eventHandler)
                cfg->m_eventHandler->onProcessComplete(result.processId, std::any(result));
        }

        luaL_unref(lua, LUA_REGISTRYINDEX, ref);
        lua_settop(lua, stackTop);
    });

    handle->processId = processId;

    return 1;
}

void Internal::registerToplevelBindings(lua_State* L, CConfigManager* mgr) {
    Internal::setMgrFn(L, mgr, "on", hlOn);
    Internal::setMgrFn(L, mgr, "bind", hlBind);
    Internal::setMgrFn(L, mgr, "define_submap", hlDefineSubmap);
    Internal::setMgrFn(L, mgr, "timer", hlTimer);
    Internal::setMgrFn(L, mgr, "async_exec_cmd", hlAsyncExecCmd);

    Internal::setFn(L, "dispatch", hlDispatch);
    Internal::setFn(L, "version", hlVersion);
    Internal::setFn(L, "get_loaded_plugins", hlGetPlugins);
    Internal::setFn(L, "exec_cmd", hlExecCmd);
    Internal::setFn(L, "exec_raw", hlExecRaw);

    Internal::setFn(L, "unbind", hlUnbind);
}
