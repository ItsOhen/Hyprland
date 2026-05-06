#include "LuaBindingsInternal.hpp"
#include "Check.hpp"

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
    auto* mgr = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));

    auto  str = Check::string(L, 1);
    if (!str)
        return Internal::configError(L, std::format("bind: bad argument 1: {}", str.error()));

    std::string_view keys = *str;

    SKeybind         kb;
    kb.submap.name  = mgr->m_currentSubmap;
    kb.submap.reset = mgr->m_currentSubmapReset;

    if (auto res = parseKeyString(kb, keys); !res)
        return Internal::configError(L, std::format("hl.bind: failed to parse key string: {}", res.error()));

    if (!Internal::pushDispatcherFunction(L, 2))
        return Internal::configError(L, "hl.bind: dispatcher must be a dispatcher (e.g. hl.dsp.window.close()) or a lua function");

    if (kb.catchAll && mgr->m_currentSubmap.empty())
        return Internal::configError(L, "hl.bind: catchall keybinds are only allowed in submaps.");

    int ref       = luaL_ref(L, LUA_REGISTRYINDEX);
    kb.handler    = "__lua";
    kb.arg        = std::to_string(ref);
    kb.displayKey = keys;

    int optsIdx = 3;

    if (lua_istable(L, optsIdx)) {
        auto getBool = [&](const char* field) -> bool {
            lua_getfield(L, optsIdx, field);
            const bool v = lua_toboolean(L, -1);
            lua_pop(L, 1);
            return v;
        };

        auto readOptString = [&](const char* field) -> std::optional<std::string> {
            lua_getfield(L, optsIdx, field);

            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                return std::nullopt;
            }

            if (!lua_isstring(L, -1)) {
                lua_pop(L, 1);
                Internal::configError(L, "hl.bind: opts.{} must be a string", field);
                return std::nullopt;
            }

            std::string result = lua_tostring(L, -1);
            lua_pop(L, 1);
            return result;
        };

        kb.repeat          = getBool("repeating");
        kb.locked          = getBool("locked");
        kb.release         = getBool("release");
        kb.nonConsuming    = getBool("non_consuming");
        kb.autoConsuming   = getBool("auto_consuming");
        kb.transparent     = getBool("transparent");
        kb.ignoreMods      = getBool("ignore_mods");
        kb.dontInhibit     = getBool("dont_inhibit");
        kb.longPress       = getBool("long_press");
        kb.submapUniversal = getBool("submap_universal");

        if (auto description = readOptString("description"); description.has_value()) {
            kb.description    = *description;
            kb.hasDescription = true;
        } else if (auto desc = readOptString("desc"); desc.has_value()) {
            kb.description    = *desc;
            kb.hasDescription = true;
        }

        bool click = false;
        bool drag  = false;

        if (getBool("click")) {
            click      = true;
            kb.release = true;
        }

        if (getBool("drag")) {
            drag       = true;
            kb.release = true;
        }

        if (click && drag)
            return Internal::configError(L, "hl.bind: click and drag are exclusive");

        if ((kb.longPress || kb.release) && kb.repeat)
            return Internal::configError(L, "hl.bind: long_press / release is incompatible with repeat");

        if (kb.mouse && (kb.repeat || kb.release || kb.locked))
            return Internal::configError(L, "hl.bind: mouse is exclusive");

        kb.click = click;
        kb.drag  = drag;

        lua_getfield(L, optsIdx, "device");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "inclusive");
            kb.deviceInclusive = lua_isnil(L, -1) ? true : lua_toboolean(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "list");
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    if (lua_isstring(L, -1))
                        kb.devices.emplace(lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    const auto BIND = g_pKeybindManager->addKeybind(kb);
    Objects::CLuaKeybind::push(L, BIND);
    return 1;
}

static int hlDefineSubmap(lua_State* L) {
    auto* mgr  = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto  name = Check::string(L, 1);
    if (!name)
        return Internal::configError(L, std::format("define_submap: bad argument 1: {}", name.error()));

    std::string reset;
    int         fnIdx = 2;
    if (lua_gettop(L) >= 3 && lua_isstring(L, 2)) {
        reset = lua_tostring(L, 2);
        fnIdx = 3;
    }

    luaL_checktype(L, fnIdx, LUA_TFUNCTION);

    std::string prev          = mgr->m_currentSubmap;
    std::string prevReset     = mgr->m_currentSubmapReset;
    mgr->m_currentSubmap      = *name;
    mgr->m_currentSubmapReset = reset;

    lua_pushvalue(L, fnIdx);
    if (mgr->guardedPCall(0, 0, 0, CConfigManager::LUA_TIMEOUT_DISPATCH_MS, std::format("hl.define_submap(\"{}\")", *name)) != LUA_OK) {
        mgr->addError(std::format("hl.define_submap: error in submap \"{}\": {}", *name, lua_tostring(L, -1)));
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

    auto rule = Internal::buildRuleFromTable(L, 2);

    if (!rule)
        return rule.error();

    Config::Supplementary::executor()->spawn(Supplementary::SExecRequest{cmd, !*rule, *rule});

    return 0;
}

static int hlDispatch(lua_State* L) {
    if (lua_istable(L, 1))
        return Internal::pushSuccessResult(L);

    if (!lua_isfunction(L, 1))
        return Internal::configError(L, "hl.dispatch: expected function or result table");

    lua_pushvalue(L, 1);

    lua_call(L, 0, 1);

    if (lua_istable(L, -1)) {
        lua_pop(L, 1);
    }

    return Internal::pushSuccessResult(L);
}

static int hlOn(lua_State* L) {
    auto* mgr    = sc<CConfigManager*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto  evName = Check::string(L, 1);
    if (!evName)
        return Internal::configError(L, std::format("on: bad argument 1: {}", evName.error()));
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int        ref = luaL_ref(L, LUA_REGISTRYINDEX);

    const auto handle = mgr->m_eventHandler->registerEvent(*evName, ref);
    if (!handle.has_value()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        const auto& known = CLuaEventHandler::knownEvents();
        std::string list;
        for (const auto& e : known) {
            list += e + ", ";
        }
        list.pop_back();
        list.pop_back();
        return Internal::configError(L, "hl.on: unknown event \"{}\". Known events:{}", *evName, list);
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

    bool repeat = (type == "repeat");

    lua_pushvalue(L, 1);
    int        fnRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_State* co    = lua_newthread(L);
    int        coRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(co, LUA_REGISTRYINDEX, fnRef);

    auto timer = makeShared<CEventLoopTimer>(
        std::chrono::milliseconds(timeoutMs),
        [L, mgr, fnRef, coRef, co, repeat, timeoutMs](SP<CEventLoopTimer> self, void* data) {
            if (repeat)
                self->updateTimeout(std::chrono::milliseconds(timeoutMs));

            int nres   = 0;
            int status = lua_resume(co, L, 0, &nres);

            if (status != LUA_OK && status != LUA_YIELD) {
                Log::logger->log(Log::ERR, "[Lua] Timer error: {}", lua_tostring(co, -1));
            }

            if (status != LUA_YIELD) {
                luaL_unref(L, LUA_REGISTRYINDEX, coRef);
                luaL_unref(L, LUA_REGISTRYINDEX, fnRef);

                auto it = std::ranges::find_if(mgr->m_luaTimers, [&](const auto& lt) { return lt.timer == self; });
                if (it != mgr->m_luaTimers.end()) {
                    mgr->m_luaTimers.erase(it);
                }
            }
        },
        nullptr);

    mgr->m_luaTimers.emplace_back(CConfigManager::SLuaTimer{timer, fnRef, coRef, co});

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

    Internal::setFn(L, "unbind", hlUnbind);
}
