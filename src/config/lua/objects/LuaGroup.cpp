#include "LuaGroup.hpp"
#include "LuaWindow.hpp"
#include "LuaObjectHelpers.hpp"

#include "../../../desktop/view/Group.hpp"

#include <string_view>
#include <memory>

using namespace Config::Lua;

static constexpr const char*                      MT = "HL.Group";

SP<Objects::LuaSchema<SP<Desktop::View::CGroup>>> Objects::CLuaGroup::s_schema;

static int                                        groupEq(lua_State* L) {
    const auto* lhs = sc<WP<Desktop::View::CGroup>*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<WP<Desktop::View::CGroup>*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int groupToString(lua_State* L) {
    const auto* ref   = sc<WP<Desktop::View::CGroup>*>(luaL_checkudata(L, 1, MT));
    const auto  group = ref->lock();

    if (!group)
        lua_pushstring(L, "HL.Group(expired)");
    else
        lua_pushfstring(L, "HL.Group(%p)", group.get());

    return 1;
}

static int groupIndex(lua_State* L) {
    auto*      ref   = sc<WP<Desktop::View::CGroup>*>(luaL_checkudata(L, 1, MT));
    const auto group = ref->lock();
    if (!group) {
        Log::logger->log(Log::DEBUG, "[lua] Tried to access an expired object");
        lua_pushnil(L);
        return 1;
    }

    const std::string_view key = luaL_checkstring(L, 2);

    if (!Objects::CLuaGroup::s_schema || !Objects::CLuaGroup::s_schema->hasProperty(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    return Objects::CLuaGroup::s_schema->getProperty(L, std::string(key), group);
}

static int groupPairs(lua_State* L) {
    return Objects::createPairs<SP<Desktop::View::CGroup>, WP<Desktop::View::CGroup>>(L, Objects::CLuaGroup::s_schema.get(), MT,
                                                                                      [](WP<Desktop::View::CGroup>* ref) { return ref->lock(); });
}

void Objects::CLuaGroup::setup(lua_State* L) {
    Objects::CLuaGroup::s_schema = makeShared<LuaSchema<SP<Desktop::View::CGroup>>>();

    Objects::CLuaGroup::s_schema->addProperty("locked", [](lua_State* L, SP<Desktop::View::CGroup> group) {
        lua_pushboolean(L, group->locked());
        return 1;
    });

    Objects::CLuaGroup::s_schema->addProperty("denied", [](lua_State* L, SP<Desktop::View::CGroup> group) {
        lua_pushboolean(L, group->denied());
        return 1;
    });

    Objects::CLuaGroup::s_schema->addProperty("size", [](lua_State* L, SP<Desktop::View::CGroup> group) {
        lua_pushinteger(L, sc<lua_Integer>(group->size()));
        return 1;
    });

    Objects::CLuaGroup::s_schema->addProperty("current_index", [](lua_State* L, SP<Desktop::View::CGroup> group) {
        lua_pushinteger(L, sc<lua_Integer>(group->getCurrentIdx()) + 1);
        return 1;
    });

    Objects::CLuaGroup::s_schema->addProperty("current", [](lua_State* L, SP<Desktop::View::CGroup> group) {
        const auto current = group->current();
        if (current)
            Objects::CLuaWindow::push(L, current);
        else
            lua_pushnil(L);
        return 1;
    });

    Objects::CLuaGroup::s_schema->addProperty("members", [](lua_State* L, SP<Desktop::View::CGroup> group) {
        lua_newtable(L);
        int i = 1;
        for (const auto& grouped : group->windows()) {
            const auto groupedWindow = grouped.lock();
            if (!groupedWindow)
                continue;

            Objects::CLuaWindow::push(L, groupedWindow);
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    registerMetatable(L, MT,
                      {
                          {"__index", groupIndex},
                          {"__gc", gcRef<WP<Desktop::View::CGroup>>},
                          {"__eq", groupEq},
                          {"__tostring", groupToString},
                          {"__pairs", groupPairs},
                      });
}

void Objects::CLuaGroup::push(lua_State* L, SP<Desktop::View::CGroup> group) {
    new (lua_newuserdata(L, sizeof(WP<Desktop::View::CGroup>))) WP<Desktop::View::CGroup>(group);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
