#include "LuaCoroutineManager.hpp"
#include "../../debug/log/Logger.hpp"
#include "LuaProcessExecutor.hpp"
#include "managers/eventLoop/EventLoopManager.hpp"
#include "objects/LuaProcessHandle.hpp"

using namespace Config::Lua;

CLuaCoroutineManager::CLuaCoroutineManager(lua_State* L) : m_lua(L) {
    if (!m_lua)
        Log::logger->log(Log::WARN, "CLuaCoroutineManager: lua_State is null!");
}

CLuaCoroutineManager::~CLuaCoroutineManager() {
    clearAll();
}

uint64_t CLuaCoroutineManager::registerThread(lua_State* thread, int threadRegistryRef, std::string_view tag) {
    for (auto& [id, rec] : m_threads) {
        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, rec.luaRef);
        lua_State* existingThread = lua_tothread(m_lua, -1);
        lua_pop(m_lua, 1);

        if (existingThread == thread) {
            if (threadRegistryRef != LUA_NOREF) {
                luaL_unref(m_lua, LUA_REGISTRYINDEX, rec.luaRef);
                rec.luaRef = threadRegistryRef;
            }

            if (!tag.empty())
                rec.tag = std::string{tag};
            return id;
        }
    }

    const uint64_t id = m_nextId++;

    SThread        rec;
    rec.id = id;

    if (threadRegistryRef == LUA_NOREF) {
        lua_pushthread(thread);
        rec.luaRef = luaL_ref(thread, LUA_REGISTRYINDEX);
    } else
        rec.luaRef = threadRegistryRef;

    if (!tag.empty())
        rec.tag = std::string{tag};

    m_threads[id] = std::move(rec);

    return id;
}

bool CLuaCoroutineManager::resumeThread(uint64_t threadId, const std::any& result) {
    auto it = m_threads.find(threadId);
    if (it == m_threads.end())
        return false;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, it->second.luaRef);
    lua_State* thread = lua_tothread(m_lua, -1);

    if (!thread || lua_status(thread) != LUA_YIELD) {
        m_threads.erase(it);
        lua_pop(m_lua, 1);
        return false;
    }

    SThread data = std::move(it->second);
    m_threads.erase(it);

    int nargs = 0;
    if (result.type() == typeid(SProcessResult)) {
        const auto& res = std::any_cast<const SProcessResult&>(result);

        // Kinda messy return, but should make it easier to work with in lua
        lua_newtable(thread);

        bool isSystemError = !res.error.empty();
        bool isSuccess     = !isSystemError && !res.timedOut && res.exitCode == 0;

        if (isSystemError) {
            lua_pushstring(thread, "error");
        } else if (res.timedOut) {
            lua_pushstring(thread, "timeout");
        } else {
            lua_pushstring(thread, "success");
        }
        lua_setfield(thread, -2, "type");

        lua_pushboolean(thread, isSuccess);
        lua_setfield(thread, -2, "ok");

        if (isSystemError) {
            lua_pushstring(thread, res.error.c_str());
            lua_setfield(thread, -2, "message");
        } else {
            lua_pushinteger(thread, res.exitCode);
            lua_setfield(thread, -2, "ec");
            lua_pushstring(thread, res.stdout.c_str());
            lua_setfield(thread, -2, "out");
            lua_pushstring(thread, res.stderr.c_str());
            lua_setfield(thread, -2, "err");
        }

        luaL_getmetatable(thread, MT_PROCESS_RESULT);
        if (!lua_isnil(thread, -1)) {
            lua_setmetatable(thread, -2);
        } else {
            lua_pop(thread, 1);
        }
        nargs = 1;
    }

    int nresults = 0;
    int status   = lua_resume(thread, m_lua, nargs, &nresults);

    if (status != LUA_OK && status != LUA_YIELD) {
        const char* msg = lua_tostring(thread, -1);
        Log::logger->log(Log::ERR, "Lua Coroutine Error: {}", msg ? msg : "unknown");
    }

    lua_pop(m_lua, 1);

    if (status != LUA_YIELD) {
        g_pEventLoopManager->doLater([this, data]() { luaL_unref(m_lua, LUA_REGISTRYINDEX, data.luaRef); });
    } else {
        m_threads[data.id] = std::move(data);
    }

    return true;
}

void CLuaCoroutineManager::clearAll() {
    for (auto& entry : m_threads) {
        if (entry.second.luaRef != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, entry.second.luaRef);
    }

    m_threads.clear();
}
