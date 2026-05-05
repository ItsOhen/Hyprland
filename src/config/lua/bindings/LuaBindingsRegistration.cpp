#include "LuaBindingsInternal.hpp"

#include "../LuaCoroutineManager.hpp"
#include "../objects/LuaEventSubscription.hpp"
#include "../objects/LuaKeybind.hpp"
#include "../objects/LuaLayerRule.hpp"
#include "../objects/LuaNotification.hpp"
#include "../objects/LuaTimer.hpp"
#include "../objects/LuaWindowRule.hpp"

using namespace Config;
using namespace Config::Lua;
using namespace Config::Lua::Bindings;

static int hlPrint(lua_State* L) {
    const int   n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; i++) {
        size_t      len = 0;
        const char* s   = luaL_tolstring(L, i, &len);
        if (i > 1)
            out += '\t';
        out.append(s, len);
        lua_pop(L, 1);
    }
    Log::logger->log(Log::INFO, "[Lua] {}", out);
    return 0;
}

static SDispatchResult dispatchResultFromLua(lua_State* L, int idx) {
    SDispatchResult result;

    if (!lua_istable(L, idx))
        return result;

    lua_getfield(L, idx, "pass_event");
    result.passEvent = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, idx, "ok");
    if (lua_isboolean(L, -1))
        result.success = lua_toboolean(L, -1);
    lua_pop(L, 1);

    if (!result.success) {
        lua_getfield(L, idx, "error");
        if (lua_isstring(L, -1))
            result.error = lua_tostring(L, -1);
        lua_pop(L, 1);
    }

    return result;
}

void Internal::registerBindingsImpl(lua_State* L, CConfigManager* mgr) {
    Objects::CLuaTimer{}.setup(L);
    Objects::CLuaEventSubscription{}.setup(L);
    Objects::CLuaWindowRule{}.setup(L);
    Objects::CLuaLayerRule{}.setup(L);
    Objects::CLuaKeybind{}.setup(L);
    Objects::CLuaNotification{}.setup(L);

    g_pKeybindManager->m_dispatchers["__lua"] = [L, mgr](std::string arg) -> SDispatchResult {
        int        ref   = std::stoi(arg);
        lua_State* co    = lua_newthread(L);
        int        coRef = luaL_ref(L, LUA_REGISTRYINDEX);

        lua_rawgeti(co, LUA_REGISTRYINDEX, ref);

        int nresults = 0;
        int status   = lua_resume(co, L, 0, &nresults);

        if (status == LUA_YIELD) {
            mgr->m_coroutineManager->registerThread(co, coRef, LUA_THREAD_TAG_BIND_DISPATCH);
            return {.success = true};
        }

        if (status != LUA_OK) {
            Config::Lua::Bindings::Internal::reportError(L,
                                                         Config::Actions::SActionError{std::format("error in keybind lambda: {}", lua_tostring(co, -1)),
                                                                                       Config::Actions::eActionErrorLevel::ERROR, Config::Actions::eActionErrorCode::LUA_ERROR});

            luaL_unref(L, LUA_REGISTRYINDEX, coRef);
            return {.success = false, .error = "lua keybind error"};
        }

        luaL_unref(L, LUA_REGISTRYINDEX, coRef);
        return {.success = true};
    };

    lua_newtable(L);

    Internal::registerConfigRuleBindings(L, mgr);
    Internal::registerToplevelBindings(L, mgr);
    Internal::registerLayoutBindings(L, mgr);
    Internal::registerQueryBindings(L);
    Internal::registerDispatcherBindings(L);
    Internal::registerNotificationBindings(L);

    lua_setglobal(L, "hl");

    lua_pushcfunction(L, hlPrint);
    lua_setglobal(L, "print");
}
