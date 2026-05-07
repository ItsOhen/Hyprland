#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../LuaProcessExecutor.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
    inline constexpr const char* MT_ASYNC_EXEC       = "HL.AsyncExec";
    inline constexpr const char* REGISTRY_EV_HANDLER = "HL.EventHandler";
    inline constexpr const char* MT_PROCESS_RESULT   = "HL.ProcessResult";
}

namespace Config::Lua::Objects {

    struct SLuaProcessHandle {
        std::string                   cmd;
        uint64_t                      processId = 0;
        std::optional<SProcessResult> result;
        bool                          started    = false;
        bool                          isComplete = false;

        SLuaProcessHandle() = default;
    };

    void               registerProcessHandleMetatable(lua_State* L);
    SLuaProcessHandle* pushNewProcessHandle(lua_State* L, const std::string& cmd);
    SLuaProcessHandle* toProcessHandle(lua_State* L, int index);
    void               registerProcessResultMetatable(lua_State* L);

}
