#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../../helpers/memory/Memory.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace Config::Lua {

    inline constexpr std::string_view LUA_THREAD_TAG_ASYNC_EXEC    = "async_exec";
    inline constexpr std::string_view LUA_THREAD_TAG_BIND_DISPATCH = "bind_dispatch";

    class CLuaCoroutineManager {
      public:
        explicit CLuaCoroutineManager(lua_State* L);
        ~CLuaCoroutineManager();

        uint64_t registerThread(lua_State* thread, int threadRegistryRef = LUA_NOREF, std::string_view tag = {});

        bool     resumeThread(uint64_t threadId, const std::any& result);

        void     clearAll();

      private:
        struct SThread {
            uint64_t    id     = 0;
            int         luaRef = LUA_NOREF;
            std::string tag;
        };

        lua_State*                            m_lua = nullptr;
        std::unordered_map<uint64_t, SThread> m_threads;
        uint64_t                              m_nextId = 1;
    };

}
