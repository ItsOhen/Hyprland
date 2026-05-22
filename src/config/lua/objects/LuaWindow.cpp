#include "LuaWindow.hpp"
#include "LuaWorkspace.hpp"
#include "LuaMonitor.hpp"
#include "LuaGroup.hpp"

#include "../../../desktop/view/Window.hpp"
#include "../../../desktop/Workspace.hpp"
#include "../../../desktop/state/FocusState.hpp"
#include "../../../desktop/history/WindowHistoryTracker.hpp"
#include "../../../layout/algorithm/Algorithm.hpp"
#include "../../../layout/algorithm/tiled/master/MasterAlgorithm.hpp"
#include "../../../layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp"
#include "../../../layout/algorithm/tiled/dwindle/DwindleAlgorithm.hpp"
#include "../../../layout/space/Space.hpp"
#include "../../../layout/supplementary/WorkspaceAlgoMatcher.hpp"
#include "../../../managers/input/InputManager.hpp"

#include <format>
#include <string_view>
#include <memory>

using namespace Config::Lua;
using namespace Config::Lua::Bindings;

static constexpr const char*      MT = "HL.Window";

SP<Objects::LuaSchema<PHLWINDOW>> Objects::CLuaWindow::s_schema;

//
static int getFocusHistoryID(PHLWINDOW wnd) {
    const auto& history = Desktop::History::windowTracker()->fullHistory();
    for (size_t i = 0; i < history.size(); ++i) {
        if (history[i].lock() == wnd)
            return sc<int>(history.size() - i - 1); // reverse order for backwards compat
    }

    return -1;
}

static int windowEq(lua_State* L) {
    const auto* lhs = sc<PHLWINDOWREF*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<PHLWINDOWREF*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int windowToString(lua_State* L) {
    const auto* ref = sc<PHLWINDOWREF*>(luaL_checkudata(L, 1, MT));
    const auto  w   = ref->lock();

    if (!w)
        lua_pushstring(L, "HL.Window(expired)");
    else
        lua_pushfstring(L, "HL.Window(%p)", w.get());

    return 1;
}

static int windowIndex(lua_State* L) {
    auto*      ref = sc<PHLWINDOWREF*>(luaL_checkudata(L, 1, MT));
    const auto w   = ref->lock();
    if (!w) {
        Log::logger->log(Log::LUA, "Tried to access an expired object");
        lua_pushnil(L);
        return 1;
    }

    auto key_ = Check::string(L, 2);
    if (!key_)
        return Internal::configError(L, std::format("__index: bad argument 2: {}", key_.error()));
    const std::string_view key = *key_;

    if (!Objects::CLuaWindow::s_schema || !Objects::CLuaWindow::s_schema->hasProperty(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    return Objects::CLuaWindow::s_schema->getProperty(L, std::string(key), w);
}

static int windowPairs(lua_State* L) {
    return Objects::createPairs<PHLWINDOW, PHLWINDOWREF>(L, Objects::CLuaWindow::s_schema.get(), "HL.Window", [](PHLWINDOWREF* ref) { return ref->lock(); });
}

void Objects::CLuaWindow::setup(lua_State* L) {
    Objects::CLuaWindow::s_schema = makeShared<LuaSchema<PHLWINDOW>>();

    Objects::CLuaWindow::s_schema->addProperty("address", [](lua_State* L, PHLWINDOW w) {
        lua_pushstring(L, std::format("0x{:x}", reinterpret_cast<uintptr_t>(w.get())).c_str());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("mapped", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->m_isMapped);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("hidden", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->isHidden());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("visible", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->visible());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("accepts_input", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->acceptsInput());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("at", [](lua_State* L, PHLWINDOW w) {
        lua_newtable(L);
        lua_pushinteger(L, sc<int>(w->m_realPosition->goal().x));
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, sc<int>(w->m_realPosition->goal().y));
        lua_setfield(L, -2, "y");
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("size", [](lua_State* L, PHLWINDOW w) {
        lua_newtable(L);
        lua_pushinteger(L, sc<int>(w->m_realSize->goal().x));
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, sc<int>(w->m_realSize->goal().y));
        lua_setfield(L, -2, "y");
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("workspace", [](lua_State* L, PHLWINDOW w) {
        if (w->m_workspace)
            Objects::CLuaWorkspace::push(L, w->m_workspace);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("floating", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->m_isFloating);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("monitor", [](lua_State* L, PHLWINDOW w) {
        const auto mon = w->m_monitor.lock();
        if (mon)
            Objects::CLuaMonitor::push(L, mon);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("class", [](lua_State* L, PHLWINDOW w) {
        lua_pushstring(L, w->m_class.c_str());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("title", [](lua_State* L, PHLWINDOW w) {
        lua_pushstring(L, w->m_title.c_str());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("initial_class", [](lua_State* L, PHLWINDOW w) {
        lua_pushstring(L, w->m_initialClass.c_str());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("initial_title", [](lua_State* L, PHLWINDOW w) {
        lua_pushstring(L, w->m_initialTitle.c_str());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("pid", [](lua_State* L, PHLWINDOW w) {
        lua_pushinteger(L, sc<lua_Integer>(w->getPID()));
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("xwayland", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->m_isX11);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("pinned", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->m_pinned);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("fullscreen", [](lua_State* L, PHLWINDOW w) {
        lua_pushinteger(L, sc<lua_Integer>(sc<uint8_t>(w->m_fullscreenState.internal)));
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("fullscreen_client", [](lua_State* L, PHLWINDOW w) {
        lua_pushinteger(L, sc<lua_Integer>(sc<uint8_t>(w->m_fullscreenState.client)));
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("over_fullscreen", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w->m_createdOverFullscreen);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("group", [](lua_State* L, PHLWINDOW w) {
        if (!w->m_group) {
            lua_pushnil(L);
            return 1;
        }

        Objects::CLuaGroup::push(L, w->m_group);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("tags", [](lua_State* L, PHLWINDOW w) {
        lua_newtable(L);

        int i = 1;
        for (const auto& tag : w->m_ruleApplicator->m_tagKeeper.getTags()) {
            lua_pushstring(L, tag.c_str());
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("swallowing", [](lua_State* L, PHLWINDOW w) {
        const auto swallowed = w->m_swallowed.lock();
        if (swallowed)
            Objects::CLuaWindow::push(L, swallowed);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("focus_history_id", [](lua_State* L, PHLWINDOW w) {
        lua_pushinteger(L, sc<lua_Integer>(getFocusHistoryID(w)));
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("inhibiting_idle", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, g_pInputManager && g_pInputManager->isWindowInhibiting(w, false));
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("xdg_tag", [](lua_State* L, PHLWINDOW w) {
        const auto xdgTag = w->xdgTag();
        if (xdgTag)
            lua_pushstring(L, xdgTag->c_str());
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("xdg_description", [](lua_State* L, PHLWINDOW w) {
        const auto xdgDescription = w->xdgDescription();
        if (xdgDescription)
            lua_pushstring(L, xdgDescription->c_str());
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("content_type", [](lua_State* L, PHLWINDOW w) {
        lua_pushstring(L, NContentType::toString(w->getContentType()).c_str());
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("stable_id", [](lua_State* L, PHLWINDOW w) {
        lua_pushinteger(L, sc<lua_Integer>(w->m_stableID));
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("layout", [](lua_State* L, PHLWINDOW w) {
        const auto target = w->layoutTarget();
        if (!target || target->floating() || !w->m_workspace || !w->m_workspace->m_space) {
            lua_pushnil(L);
            return 1;
        }

        const auto& algo = w->m_workspace->m_space->algorithm();
        if (!algo || !algo->tiledAlgo()) {
            lua_pushnil(L);
            return 1;
        }

        const auto&       tiledAlgo = algo->tiledAlgo();
        const std::string name      = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(tiledAlgo.get());

        lua_newtable(L);
        lua_pushstring(L, name.c_str());
        lua_setfield(L, -2, "name");

        if (const auto* master = dynamic_cast<Layout::Tiled::CMasterAlgorithm*>(tiledAlgo.get())) {
            const auto node = master->getNodeFromTarget(target);
            if (node) {
                lua_pushboolean(L, node->isMaster);
                lua_setfield(L, -2, "is_master");

                lua_pushnumber(L, node->percMaster);
                lua_setfield(L, -2, "perc_master");

                lua_pushnumber(L, node->percSize);
                lua_setfield(L, -2, "perc_size");
            }
        } else if (auto* scrolling = dynamic_cast<Layout::Tiled::CScrollingAlgorithm*>(tiledAlgo.get())) {
            const auto data = scrolling->dataFor(target);
            if (data) {
                const auto col = data->column.lock();
                if (col) {
                    const auto scrollingData = col->scrollingData.lock();

                    lua_newtable(L);

                    if (scrollingData) {
                        lua_pushinteger(L, sc<lua_Integer>(scrollingData->idx(col)));
                        lua_setfield(L, -2, "index");
                    }

                    lua_pushnumber(L, col->getColumnWidth());
                    lua_setfield(L, -2, "width");

                    lua_newtable(L);
                    int i = 1;
                    for (const auto& td : col->targetDatas) {
                        const auto t = td->target.lock();
                        if (t) {
                            const auto win = t->window();
                            if (win) {
                                Objects::CLuaWindow::push(L, win);
                                lua_rawseti(L, -2, i++);
                            }
                        }
                    }
                    lua_setfield(L, -2, "windows");

                    lua_setfield(L, -2, "column");

                    lua_pushinteger(L, sc<lua_Integer>(col->idx(target)));
                    lua_setfield(L, -2, "index_in_column");
                }
            }
        }
        return 1;
    });

    Objects::CLuaWindow::s_schema->addProperty("active", [](lua_State* L, PHLWINDOW w) {
        lua_pushboolean(L, w == Desktop::focusState()->window());
        return 1;
    });

    registerMetatable(L, MT,
                      {
                          {"__index", windowIndex},
                          {"__gc", gcRef<PHLWINDOWREF>},
                          {"__eq", windowEq},
                          {"__tostring", windowToString},
                          {"__pairs", windowPairs},
                      });
}

void Objects::CLuaWindow::push(lua_State* L, PHLWINDOW w) {
    new (lua_newuserdata(L, sizeof(PHLWINDOWREF))) PHLWINDOWREF(w ? w->m_self : nullptr);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
