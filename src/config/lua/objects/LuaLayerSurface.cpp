#include "LuaLayerSurface.hpp"
#include "LuaMonitor.hpp"
#include "LuaObjectHelpers.hpp"

#include "../../../desktop/view/LayerSurface.hpp"

#include <format>
#include <string_view>
#include <memory>

using namespace Config::Lua;

static constexpr const char*  MT = "HL.LayerSurface";

SP<Objects::LuaSchema<PHLLS>> Objects::CLuaLayerSurface::s_schema;

//
static int layerSurfaceEq(lua_State* L) {
    const auto* lhs = sc<PHLLSREF*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<PHLLSREF*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int layerSurfaceToString(lua_State* L) {
    const auto* ref = sc<PHLLSREF*>(luaL_checkudata(L, 1, MT));
    const auto  ls  = ref->lock();

    if (!ls)
        lua_pushstring(L, "HL.LayerSurface(expired)");
    else
        lua_pushfstring(L, "HL.LayerSurface(%p)", ls.get());

    return 1;
}

static int layerSurfaceIndex(lua_State* L) {
    auto*      ref = sc<PHLLSREF*>(luaL_checkudata(L, 1, MT));
    const auto ls  = ref->lock();
    if (!ls) {
        Log::logger->log(Log::LUA, "Tried to access an expired object");
        lua_pushnil(L);
        return 1;
    }

    const std::string_view key = luaL_checkstring(L, 2);

    if (!Objects::CLuaLayerSurface::s_schema || !Objects::CLuaLayerSurface::s_schema->hasProperty(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    return Objects::CLuaLayerSurface::s_schema->getProperty(L, std::string(key), ls);
}

static int layerSurfacePairs(lua_State* L) {
    return Objects::createPairs<PHLLS, PHLLSREF>(L, Objects::CLuaLayerSurface::s_schema.get(), MT, [](PHLLSREF* ref) { return ref->lock(); });
}

void Objects::CLuaLayerSurface::setup(lua_State* L) {
    Objects::CLuaLayerSurface::s_schema = makeShared<LuaSchema<PHLLS>>();

    Objects::CLuaLayerSurface::s_schema->addProperty("address", [](lua_State* L, PHLLS ls) {
        lua_pushstring(L, std::format("0x{:x}", reinterpret_cast<uintptr_t>(ls.get())).c_str());
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("x", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, ls->m_geometry.x);
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("y", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, ls->m_geometry.y);
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("w", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, ls->m_geometry.width);
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("h", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, ls->m_geometry.height);
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("namespace", [](lua_State* L, PHLLS ls) {
        lua_pushstring(L, ls->m_namespace.c_str());
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("pid", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, sc<lua_Integer>(ls->getPID()));
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("monitor", [](lua_State* L, PHLLS ls) {
        const auto mon = ls->m_monitor.lock();
        if (mon)
            Objects::CLuaMonitor::push(L, mon);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("mapped", [](lua_State* L, PHLLS ls) {
        lua_pushboolean(L, ls->m_mapped);
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("layer", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, sc<lua_Integer>(ls->m_layer));
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("interactivity", [](lua_State* L, PHLLS ls) {
        lua_pushinteger(L, sc<lua_Integer>(ls->m_interactivity));
        return 1;
    });

    Objects::CLuaLayerSurface::s_schema->addProperty("above_fullscreen", [](lua_State* L, PHLLS ls) {
        lua_pushboolean(L, ls->m_aboveFullscreen);
        return 1;
    });

    registerMetatable(L, MT,
                      {
                          {"__index", layerSurfaceIndex},
                          {"__gc", gcRef<PHLLSREF>},
                          {"__eq", layerSurfaceEq},
                          {"__tostring", layerSurfaceToString},
                          {"__pairs", layerSurfacePairs},
                      });
}

void Objects::CLuaLayerSurface::push(lua_State* L, PHLLS ls) {
    new (lua_newuserdata(L, sizeof(PHLLSREF))) PHLLSREF(ls ? ls->m_self : nullptr);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
