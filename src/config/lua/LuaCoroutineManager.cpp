#include "LuaCoroutineManager.hpp"
#include "../../debug/log/Logger.hpp"
#include "LuaProcessExecutor.hpp"
#include "managers/eventLoop/EventLoopManager.hpp"

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
        lua_pushinteger(thread, res.exitCode);
        lua_pushstring(thread, res.stdout.c_str());
        lua_pushstring(thread, res.stderr.c_str());
        nargs = 3;
    }

    int nresults = 0;
    int status   = lua_resume(thread, m_lua, nargs, &nresults);
    lua_pop(m_lua, 1);

    if (status != LUA_YIELD) {
        g_pEventLoopManager->doLater([this, data]() { luaL_unref(m_lua, LUA_REGISTRYINDEX, data.luaRef); });
    } else
        m_threads[data.id] = std::move(data);

    return true;
}

void CLuaCoroutineManager::clearAll() {
    for (auto& entry : m_threads) {
        if (entry.second.luaRef != LUA_NOREF)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, entry.second.luaRef);
    }

    m_threads.clear();
}
