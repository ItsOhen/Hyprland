#include "LuaKeybind.hpp"

#include <optional>
#include <string_view>
#include <memory>

using namespace Config::Lua;
using namespace Config::Lua::Bindings;

static constexpr const char*         MT = "HL.Keybind";

SP<Objects::LuaSchema<SP<SKeybind>>> Objects::CLuaKeybind::s_schema;

namespace {
    std::optional<SP<SKeybind>> getKeybindFromUserdata(lua_State* L) {
        auto* ref = sc<WP<SKeybind>*>(luaL_checkudata(L, 1, MT));
        return ref->lock();
    }

    void pushDeviceList(lua_State* L, const SKeybind& keybind) {
        lua_newtable(L);
        int i = 1;
        for (const auto& device : keybind.devices) {
            lua_pushstring(L, device.c_str());
            lua_rawseti(L, -2, i++);
        }
    }
}

static int keybindEq(lua_State* L) {
    const auto* lhs = sc<WP<SKeybind>*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<WP<SKeybind>*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->lock() == rhs->lock());
    return 1;
}

static int keybindToString(lua_State* L) {
    const auto* ref     = sc<WP<SKeybind>*>(luaL_checkudata(L, 1, MT));
    const auto  keybind = ref->lock();

    if (!keybind)
        lua_pushstring(L, "HL.Keybind(expired)");
    else
        lua_pushfstring(L, "HL.Keybind(%p)", keybind.get());

    return 1;
}

static int keybindSetEnabled(lua_State* L) {
    luaL_checktype(L, 2, LUA_TBOOLEAN);

    const auto keybind = getKeybindFromUserdata(L);
    if (!keybind)
        return 0;

    (*keybind)->enabled = lua_toboolean(L, 2);
    return 0;
}

static int keybindIsEnabled(lua_State* L) {
    const auto keybind = getKeybindFromUserdata(L);
    if (!keybind) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushboolean(L, (*keybind)->enabled);
    return 1;
}

static int keybindRemove(lua_State* L) {
    const auto keybind = getKeybindFromUserdata(L);
    if (!keybind || !g_pKeybindManager)
        return 0;

    if ((*keybind)->handler == "__lua") {
        try {
            const int ref = std::stoi((*keybind)->arg);
            if (ref > 0)
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
        } catch (...) {
            // invalid ref, ignore
        }

        (*keybind)->arg = std::to_string(LUA_NOREF);
    }

    g_pKeybindManager->removeKeybind((*keybind)->modmask, SParsedKey{.key = (*keybind)->key, .keycode = (*keybind)->keycode, .catchAll = (*keybind)->catchAll});
    return 0;
}

static int keybindIndex(lua_State* L) {
    const auto keybind = getKeybindFromUserdata(L);
    auto       key_    = Check::string(L, 2);
    if (!key_)
        return Internal::configError(L, std::format("__index: bad argument 2: {}", key_.error()));
    const std::string_view key = *key_;

    // Check for methods first (these don't require schema lookup)
    if (key == "set_enabled") {
        lua_pushcfunction(L, keybindSetEnabled);
        return 1;
    } else if (key == "is_enabled") {
        lua_pushcfunction(L, keybindIsEnabled);
        return 1;
    } else if (key == "remove" || key == "unbind") {
        lua_pushcfunction(L, keybindRemove);
        return 1;
    }

    if (!keybind) {
        lua_pushnil(L);
        return 1;
    }

    if (!Objects::CLuaKeybind::s_schema || !Objects::CLuaKeybind::s_schema->hasProperty(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    return Objects::CLuaKeybind::s_schema->getProperty(L, std::string(key), keybind.value());
}

static int keybindPairs(lua_State* L) {
    return Objects::createPairs<SP<SKeybind>, WP<SKeybind>>(L, Objects::CLuaKeybind::s_schema.get(), MT, [](WP<SKeybind>* ref) { return ref->lock(); });
}

void Objects::CLuaKeybind::setup(lua_State* L) {
    Objects::CLuaKeybind::s_schema = makeShared<LuaSchema<SP<SKeybind>>>();

    Objects::CLuaKeybind::s_schema->addProperty("enabled", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->enabled);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("has_description", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->hasDescription);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("description", [](lua_State* L, SP<SKeybind> keybind) {
        if (!keybind->hasDescription)
            lua_pushnil(L);
        else
            lua_pushstring(L, keybind->description.c_str());
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("display_key", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushstring(L, keybind->displayKey.c_str());
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("submap", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushstring(L, keybind->submap.name.c_str());
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("handler", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushstring(L, keybind->handler.c_str());
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("arg", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushstring(L, keybind->arg.c_str());
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("modmask", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushinteger(L, sc<lua_Integer>(keybind->modmask));
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("key", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushstring(L, keybind->key.c_str());
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("keycode", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushinteger(L, sc<lua_Integer>(keybind->keycode));
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("catchall", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->catchAll);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("repeating", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->repeat);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("locked", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->locked);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("release", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->release);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("non_consuming", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->nonConsuming);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("auto_consuming", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->autoConsuming);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("transparent", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->transparent);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("ignore_mods", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->ignoreMods);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("long_press", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->longPress);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("dont_inhibit", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->dontInhibit);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("click", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->click);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("drag", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->drag);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("submap_universal", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->submapUniversal);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("mouse", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->mouse);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("device_inclusive", [](lua_State* L, SP<SKeybind> keybind) {
        lua_pushboolean(L, keybind->deviceInclusive);
        return 1;
    });

    Objects::CLuaKeybind::s_schema->addProperty("devices", [](lua_State* L, SP<SKeybind> keybind) {
        pushDeviceList(L, *keybind);
        return 1;
    });

    registerMetatable(L, MT,
                      {
                          {"__index", keybindIndex},
                          {"__gc", gcRef<WP<SKeybind>>},
                          {"__eq", keybindEq},
                          {"__tostring", keybindToString},
                          {"__pairs", keybindPairs},
                      });
}

void Objects::CLuaKeybind::push(lua_State* L, const SP<SKeybind>& keybind) {
    new (lua_newuserdata(L, sizeof(WP<SKeybind>))) WP<SKeybind>(keybind);
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
