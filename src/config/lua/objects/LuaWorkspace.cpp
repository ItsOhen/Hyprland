#include "LuaWorkspace.hpp"
#include "LuaMonitor.hpp"
#include "LuaWindow.hpp"
#include "LuaGroup.hpp"
#include "LuaObjectHelpers.hpp"

#include "../../../desktop/Workspace.hpp"
#include "../../../desktop/view/Group.hpp"
#include "../../../helpers/Monitor.hpp"
#include "../../../layout/space/Space.hpp"
#include "../../../layout/algorithm/Algorithm.hpp"
#include "../../../layout/algorithm/TiledAlgorithm.hpp"
#include "../../../layout/supplementary/WorkspaceAlgoMatcher.hpp"
#include "../../../Compositor.hpp"

#include <algorithm>
#include <string_view>
#include <memory>

using namespace Config::Lua;

static constexpr const char*         MT = "HL.Workspace";

SP<Objects::LuaSchema<PHLWORKSPACE>> Objects::CLuaWorkspace::s_schema;

//
static int workspaceEq(lua_State* L) {
    const auto* lhs = sc<PHLWORKSPACEREF*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<PHLWORKSPACEREF*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int workspaceToString(lua_State* L) {
    const auto* ref = sc<PHLWORKSPACEREF*>(luaL_checkudata(L, 1, MT));
    const auto  ws  = ref->lock();

    if (!ws || ws->inert())
        lua_pushstring(L, "HL.Workspace(expired)");
    else
        lua_pushfstring(L, "HL.Workspace(%d:%s)", ws->m_id, ws->m_name.c_str());

    return 1;
}

static int workspaceGetWindows(lua_State* L) {
    auto*      ref = sc<PHLWORKSPACEREF*>(luaL_checkudata(L, 1, MT));
    const auto ws  = ref->lock();
    if (!ws || ws->inert()) {
        lua_newtable(L);
        return 1;
    }

    lua_newtable(L);
    int idx = 1;
    for (auto const& w : g_pCompositor->m_windows) {
        if (w->m_workspace == ws) {
            Objects::CLuaWindow::push(L, w);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

static int workspaceGetGroups(lua_State* L) {
    auto*      ref = sc<PHLWORKSPACEREF*>(luaL_checkudata(L, 1, MT));
    const auto ws  = ref->lock();
    if (!ws || ws->inert()) {
        lua_newtable(L);
        return 1;
    }

    lua_newtable(L);
    int                                 idx = 1;

    std::vector<Desktop::View::CGroup*> pushedGroups;

    for (auto const& w : g_pCompositor->m_windows) {
        if (w->m_workspace != ws || !w->m_group)
            continue;

        if (std::ranges::find(pushedGroups, w->m_group.get()) != pushedGroups.end())
            continue;

        pushedGroups.push_back(w->m_group.get());

        Objects::CLuaGroup::push(L, w->m_group);

        lua_rawseti(L, -2, idx++);
    }

    return 1;
}

static int workspaceIndex(lua_State* L) {
    auto*      ref = sc<PHLWORKSPACEREF*>(luaL_checkudata(L, 1, MT));
    const auto ws  = ref->lock();
    if (!ws || ws->inert()) {
        Log::logger->log(Log::DEBUG, "[lua] Tried to access an expired object");
        lua_pushnil(L);
        return 1;
    }

    const std::string_view key = luaL_checkstring(L, 2);

    if (!Objects::CLuaWorkspace::s_schema || !Objects::CLuaWorkspace::s_schema->has(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    if (Objects::CLuaWorkspace::s_schema->hasMethod(std::string(key)))
        return Objects::CLuaWorkspace::s_schema->getMethod(L, std::string(key));

    return Objects::CLuaWorkspace::s_schema->getProperty(L, std::string(key), ws);
}

static int workspacePairs(lua_State* L) {
    return Objects::createPairs<PHLWORKSPACE, PHLWORKSPACEREF>(L, Objects::CLuaWorkspace::s_schema.get(), MT, [](PHLWORKSPACEREF* ref) { return ref->lock(); });
}

void Objects::CLuaWorkspace::setup(lua_State* L) {
    Objects::CLuaWorkspace::s_schema = makeShared<LuaSchema<PHLWORKSPACE>>();

    Objects::CLuaWorkspace::s_schema->addProperty("id", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushinteger(L, sc<lua_Integer>(ws->m_id));
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("name", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushstring(L, ws->m_name.c_str());
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("monitor", [](lua_State* L, PHLWORKSPACE ws) {
        const auto mon = ws->m_monitor.lock();
        if (mon)
            Objects::CLuaMonitor::push(L, mon);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("windows", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushinteger(L, sc<lua_Integer>(ws->getWindows()));
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("visible", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushboolean(L, ws->isVisible());
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("special", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushboolean(L, ws->m_isSpecialWorkspace);
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("active", [](lua_State* L, PHLWORKSPACE ws) {
        const auto mon = ws->m_monitor.lock();
        lua_pushboolean(L, mon && (mon->m_activeWorkspace == ws || mon->m_activeSpecialWorkspace == ws));
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("has_urgent", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushboolean(L, ws->hasUrgentWindow());
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("fullscreen_mode", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushinteger(L, sc<lua_Integer>(ws->m_fullscreenMode));
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("has_fullscreen", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushboolean(L, ws->m_hasFullscreenWindow);
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("is_persistent", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushboolean(L, ws->isPersistent());
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("is_empty", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushboolean(L, ws->getWindows() == 0);
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("config_name", [](lua_State* L, PHLWORKSPACE ws) {
        lua_pushstring(L, ws->getConfigName().c_str());
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("tiled_layout", [](lua_State* L, PHLWORKSPACE ws) {
        std::string layoutName = "unknown";
        if (ws->m_space && ws->m_space->algorithm() && ws->m_space->algorithm()->tiledAlgo()) {
            const auto& TILED_ALGO = ws->m_space->algorithm()->tiledAlgo();
            layoutName             = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(TILED_ALGO.get());
        }
        lua_pushstring(L, layoutName.c_str());
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("last_window", [](lua_State* L, PHLWORKSPACE ws) {
        const auto lastWindow = ws->m_lastFocusedWindow.lock();
        if (lastWindow)
            Objects::CLuaWindow::push(L, lastWindow);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWorkspace::s_schema->addProperty("fullscreen_window", [](lua_State* L, PHLWORKSPACE ws) {
        const auto fsWindow = ws->getFullscreenWindow();
        if (fsWindow)
            Objects::CLuaWindow::push(L, fsWindow);
        else
            lua_pushnil(L);
        return 1;
    });

    // Objects::CLuaWorkspace::s_schema->addProperty("get_windows", [](lua_State* L, PHLWORKSPACE ws) {
    //     lua_pushcfunction(L, workspaceGetWindows);
    //     return 1;
    // });

    // Objects::CLuaWorkspace::s_schema->addProperty("get_groups", [](lua_State* L, PHLWORKSPACE ws) {
    //     lua_pushcfunction(L, workspaceGetGroups);
    //     return 1;
    // });
    //
    // Objects::CLuaWorkspace::s_schema->addProperty("groups", [](lua_State* L, PHLWORKSPACE ws) {
    //     lua_pushinteger(L, sc<lua_Integer>(ws->getGroups()));
    //     return 1;
    // });

    Objects::CLuaWorkspace::s_schema->addMethod("get_windows", [](lua_State* L) { return workspaceGetWindows(L); });
    Objects::CLuaWorkspace::s_schema->addMethod("get_groups", [](lua_State* L) { return workspaceGetGroups(L); });

    registerMetatable(L, MT,
                      {
                          {"__index", workspaceIndex},
                          {"__gc", gcRef<PHLWORKSPACEREF>},
                          {"__eq", workspaceEq},
                          {"__tostring", workspaceToString},
                          {"__pairs", workspacePairs},
                      });
}

void Objects::CLuaWorkspace::push(lua_State* L, PHLWORKSPACE ws) {
    new (lua_newuserdata(L, sizeof(PHLWORKSPACEREF))) PHLWORKSPACEREF(ws ? ws->m_self : nullptr);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}

void Objects::CLuaWorkspace::push(lua_State* L, PHLWORKSPACEREF ws) {
    new (lua_newuserdata(L, sizeof(PHLWORKSPACEREF))) PHLWORKSPACEREF(ws ? ws->m_self : nullptr);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
