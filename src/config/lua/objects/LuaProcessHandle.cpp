#include "LuaProcessHandle.hpp"
#include "../LuaEventHandler.hpp"
#include "../ConfigManager.hpp"

#include <lua.h>
#include <lauxlib.h>

using namespace Config::Lua::Objects;

static int asyncExecContinue(lua_State* L, int status, lua_KContext ctx) {
    // Just the res table from resume
    return 1;
}

static int asyncExecCall(lua_State* L) {
    auto* handle = toProcessHandle(L, 1);
    if (!handle || handle->started)
        return luaL_error(L, "process already started");

    handle->started = true;

    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_EV_HANDLER);
    auto* ev = (Config::Lua::CLuaEventHandler*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (!ev)
        return luaL_error(L, "event handler missing");

    ev->yieldForProcess(L, handle->processId);
    return lua_yieldk(L, 0, 0, asyncExecContinue);
}

static int asyncExecGc(lua_State* L) {
    if (auto* handle = toProcessHandle(L, 1))
        handle->~SLuaProcessHandle();
    return 0;
}

static int asyncExecToString(lua_State* L) {
    auto* handle = toProcessHandle(L, 1);
    if (!handle) {
        lua_pushstring(L, "invalid");
        return 1;
    }

    if (handle->result.has_value())
        lua_pushfstring(L, "HL.AsyncExec(exit=%d)", handle->result->exitCode);
    else
        lua_pushfstring(L, "HL.AsyncExec(pending)");
    return 1;
}

void Config::Lua::Objects::registerProcessHandleMetatable(lua_State* L) {
    luaL_newmetatable(L, MT_ASYNC_EXEC);
    lua_pushcfunction(L, asyncExecCall);
    lua_setfield(L, -2, "__call");
    lua_pushcfunction(L, asyncExecGc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, asyncExecToString);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
}

SLuaProcessHandle* Config::Lua::Objects::pushNewProcessHandle(lua_State* L, const std::string& cmd) {
    auto* udata = (SLuaProcessHandle*)lua_newuserdata(L, sizeof(SLuaProcessHandle));
    new (udata) SLuaProcessHandle();
    udata->cmd = cmd;
    luaL_setmetatable(L, MT_ASYNC_EXEC);
    return udata;
}

SLuaProcessHandle* Config::Lua::Objects::toProcessHandle(lua_State* L, int index) {
    return (SLuaProcessHandle*)luaL_testudata(L, index, MT_ASYNC_EXEC);
}

void Config::Lua::Objects::registerProcessResultMetatable(lua_State* L) {
    luaL_newmetatable(L, MT_PROCESS_RESULT);

    lua_pushcfunction(L, [](lua_State* L) -> int {
        lua_getfield(L, 1, "type");
        lua_getfield(L, 1, "ok");
        const char* type = lua_tostring(L, -2);
        bool        ok   = lua_toboolean(L, -1);

        if (type && std::string(type) == "error") {
            lua_getfield(L, 1, "message");
            lua_pushfstring(L, "ProcessResult(ERROR: %s)", lua_tostring(L, -1));
        } else {
            lua_getfield(L, 1, "ec");
            lua_pushfstring(L, "ProcessResult(%s, ok=%s, ec=%d)", type ? type : "unknown", ok ? "true" : "false", (int)lua_tointeger(L, -1));
        }
        return 1;
    });

    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
}
