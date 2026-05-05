#pragma once

#include "LuaObjectHelpers.hpp"
#include <hyprutils/memory/WeakPtr.hpp>
#include <lua.hpp>

#include "../../../desktop/view/Group.hpp"

namespace Config::Lua::Objects {
    class CLuaGroup {
      public:
        static void setup(lua_State* L);
        static void push(lua_State* L, SP<Desktop::View::CGroup> group);

        // Public for access from cpp file
        static std::shared_ptr<LuaSchema<SP<Desktop::View::CGroup>>> s_schema;
    };
};
