#include "LuaProcessHandle.hpp"
#include "../LuaEventHandler.hpp"
#include "../ConfigManager.hpp"

#include <lua.h>
#include <lauxlib.h>

using namespace Config::Lua::Objects;

namespace {
    constexpr const char* MT_ASYNC_EXEC       = "HL.AsyncExec";
    constexpr const char* REGISTRY_EV_HANDLER = "HL.EventHandler";
}

static int asyncExecContinue(lua_State* L, int status, lua_KContext ctx) {
    (void)status;
    (void)ctx;

    int nresults = lua_gettop(L);

    if (nresults >= 3) {
        return 3;
    }

    lua_pushnil(L);
    lua_pushstring(L, "");
    lua_pushstring(L, "HL.AsyncExec: continuation stack underflow");
    return 3;
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
