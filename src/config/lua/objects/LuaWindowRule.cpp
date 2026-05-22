#include "LuaWindowRule.hpp"

#include "../../../desktop/rule/Engine.hpp"

#include <string_view>
#include <memory>

using namespace Config::Lua;
using namespace Config::Lua::Bindings;

static constexpr const char*                           MT = "HL.WindowRule";

SP<Objects::LuaSchema<SP<Desktop::Rule::CWindowRule>>> Objects::CLuaWindowRule::s_schema;

//
static int windowRuleEq(lua_State* L) {
    const auto* lhs = sc<WP<Desktop::Rule::CWindowRule>*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<WP<Desktop::Rule::CWindowRule>*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int windowRuleToString(lua_State* L) {
    const auto* ref  = sc<WP<Desktop::Rule::CWindowRule>*>(luaL_checkudata(L, 1, MT));
    const auto  rule = ref->lock();

    if (!rule)
        lua_pushstring(L, "HL.WindowRule(expired)");
    else
        lua_pushfstring(L, "HL.WindowRule(%p)", rule.get());

    return 1;
}

static int windowRuleSetEnabled(lua_State* L) {
    auto* ref = sc<WP<Desktop::Rule::CWindowRule>*>(luaL_checkudata(L, 1, MT));
    luaL_checktype(L, 2, LUA_TBOOLEAN);

    const auto rule = ref->lock();
    if (!rule)
        return 0;

    rule->setEnabled(lua_toboolean(L, 2));
    Desktop::Rule::ruleEngine()->updateAllRules();
    return 0;
}

static int windowRuleIsEnabled(lua_State* L) {
    auto*      ref = sc<WP<Desktop::Rule::CWindowRule>*>(luaL_checkudata(L, 1, MT));

    const auto rule = ref->lock();
    if (!rule) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushboolean(L, rule->isEnabled());
    return 1;
}

static int windowRuleIndex(lua_State* L) {
    luaL_checkudata(L, 1, MT);
    auto key_ = Check::string(L, 2);
    if (!key_)
        return Internal::configError(L, std::format("__index: bad argument 2: {}", key_.error()));
    const std::string_view key = *key_;

    if (key == "set_enabled") {
        lua_pushcfunction(L, windowRuleSetEnabled);
        return 1;
    } else if (key == "is_enabled") {
        lua_pushcfunction(L, windowRuleIsEnabled);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

static int windowRulePairs(lua_State* L) {
    return Objects::createPairs<SP<Desktop::Rule::CWindowRule>, WP<Desktop::Rule::CWindowRule>>(L, Objects::CLuaWindowRule::s_schema.get(), MT,
                                                                                                [](WP<Desktop::Rule::CWindowRule>* ref) { return ref->lock(); });
}

void Objects::CLuaWindowRule::setup(lua_State* L) {
    Objects::CLuaWindowRule::s_schema = makeShared<LuaSchema<SP<Desktop::Rule::CWindowRule>>>();

    registerMetatable(L, MT,
                      {
                          {"__index", windowRuleIndex},
                          {"__gc", gcRef<WP<Desktop::Rule::CWindowRule>>},
                          {"__eq", windowRuleEq},
                          {"__tostring", windowRuleToString},
                          {"__pairs", windowRulePairs},
                      });
}

void Objects::CLuaWindowRule::push(lua_State* L, const SP<Desktop::Rule::CWindowRule>& rule) {
    new (lua_newuserdata(L, sizeof(WP<Desktop::Rule::CWindowRule>))) WP<Desktop::Rule::CWindowRule>(rule);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
