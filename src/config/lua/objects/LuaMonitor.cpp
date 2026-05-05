#include "LuaMonitor.hpp"
#include "LuaWorkspace.hpp"
#include "LuaObjectHelpers.hpp"

#include "../../../helpers/Monitor.hpp"
#include "../../../desktop/state/FocusState.hpp"

#include <string_view>
#include <memory>

using namespace Config::Lua;

static constexpr const char* MT = "HL.Monitor";

// Static member definition
std::shared_ptr<Objects::LuaSchema<PHLMONITOR>> Objects::CLuaMonitor::s_schema;

//
static int monitorEq(lua_State* L) {
    const auto* lhs = sc<PHLMONITORREF*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<PHLMONITORREF*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int monitorToString(lua_State* L) {
    const auto* ref = sc<PHLMONITORREF*>(luaL_checkudata(L, 1, MT));
    const auto  mon = ref->lock();

    if (!mon)
        lua_pushstring(L, "HL.Monitor(expired)");
    else
        lua_pushfstring(L, "HL.Monitor(%d:%s)", mon->m_id, mon->m_name.c_str());

    return 1;
}

static int monitorIndex(lua_State* L) {
    auto*      ref = sc<PHLMONITORREF*>(luaL_checkudata(L, 1, MT));
    const auto mon = ref->lock();
    if (!mon) {
        Log::logger->log(Log::DEBUG, "[lua] Tried to access an expired object");
        lua_pushnil(L);
        return 1;
    }

    const std::string_view key = luaL_checkstring(L, 2);

    if (!Objects::CLuaMonitor::s_schema || !Objects::CLuaMonitor::s_schema->hasProperty(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    return Objects::CLuaMonitor::s_schema->getProperty(L, std::string(key), mon);
}

static int monitorPairs(lua_State* L) {
    return Objects::createPairs<PHLMONITOR, PHLMONITORREF>(
        L, Objects::CLuaMonitor::s_schema.get(), MT,
        [](PHLMONITORREF* ref) { return ref->lock(); });
}

void Objects::CLuaMonitor::setup(lua_State* L) {
    // Create and populate the schema
    Objects::CLuaMonitor::s_schema = std::make_shared<LuaSchema<PHLMONITOR>>();

    Objects::CLuaMonitor::s_schema->addProperty("id", [](lua_State* L, PHLMONITOR mon) {
        lua_pushinteger(L, sc<lua_Integer>(mon->m_id));
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("name", [](lua_State* L, PHLMONITOR mon) {
        lua_pushstring(L, mon->m_name.c_str());
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("description", [](lua_State* L, PHLMONITOR mon) {
        lua_pushstring(L, mon->m_shortDescription.c_str());
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("width", [](lua_State* L, PHLMONITOR mon) {
        lua_pushinteger(L, sc<int>(mon->m_pixelSize.x));
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("height", [](lua_State* L, PHLMONITOR mon) {
        lua_pushinteger(L, sc<int>(mon->m_pixelSize.y));
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("refresh_rate", [](lua_State* L, PHLMONITOR mon) {
        lua_pushnumber(L, mon->m_refreshRate);
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("x", [](lua_State* L, PHLMONITOR mon) {
        lua_pushinteger(L, sc<int>(mon->m_position.x));
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("y", [](lua_State* L, PHLMONITOR mon) {
        lua_pushinteger(L, sc<int>(mon->m_position.y));
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("active_workspace", [](lua_State* L, PHLMONITOR mon) {
        if (mon->m_activeWorkspace)
            Objects::CLuaWorkspace::push(L, mon->m_activeWorkspace);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("active_special_workspace", [](lua_State* L, PHLMONITOR mon) {
        if (mon->m_activeSpecialWorkspace)
            Objects::CLuaWorkspace::push(L, mon->m_activeSpecialWorkspace);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("position", [](lua_State* L, PHLMONITOR mon) {
        lua_newtable(L);
        lua_pushinteger(L, sc<int>(mon->m_position.x));
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, sc<int>(mon->m_position.y));
        lua_setfield(L, -2, "y");
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("size", [](lua_State* L, PHLMONITOR mon) {
        lua_newtable(L);
        lua_pushinteger(L, sc<int>(mon->m_pixelSize.x));
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, sc<int>(mon->m_pixelSize.y));
        lua_setfield(L, -2, "height");
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("scale", [](lua_State* L, PHLMONITOR mon) {
        lua_pushnumber(L, mon->m_scale);
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("transform", [](lua_State* L, PHLMONITOR mon) {
        lua_pushinteger(L, sc<int>(mon->m_transform));
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("dpms_status", [](lua_State* L, PHLMONITOR mon) {
        lua_pushboolean(L, mon->m_dpmsStatus);
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("vrr_active", [](lua_State* L, PHLMONITOR mon) {
        lua_pushboolean(L, mon->m_vrrActive);
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("is_mirror", [](lua_State* L, PHLMONITOR mon) {
        lua_pushboolean(L, mon->isMirror());
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("mirrors", [](lua_State* L, PHLMONITOR mon) {
        lua_newtable(L);

        int i = 1;
        for (const auto& mirrorRef : mon->m_mirrors) {
            const auto mirror = mirrorRef.lock();
            if (!mirror)
                continue;

            Objects::CLuaMonitor::push(L, mirror);
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("focused", [](lua_State* L, PHLMONITOR mon) {
        lua_pushboolean(L, mon == Desktop::focusState()->monitor());
        return 1;
    });

    Objects::CLuaMonitor::s_schema->addProperty("cm", [](lua_State* L, PHLMONITOR mon) {
        lua_pushstring(L, NCMType::toString(mon->m_cmType).c_str());
        return 1;
    });

    registerMetatable(L, MT, {
        {"__index",    monitorIndex},
        {"__gc",       gcRef<PHLMONITORREF>},
        {"__eq",       monitorEq},
        {"__tostring", monitorToString},
        {"__pairs",    monitorPairs},
    });
}

void Objects::CLuaMonitor::push(lua_State* L, PHLMONITOR mon) {
    new (lua_newuserdata(L, sizeof(PHLMONITORREF))) PHLMONITORREF(mon ? mon->m_self : nullptr);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
