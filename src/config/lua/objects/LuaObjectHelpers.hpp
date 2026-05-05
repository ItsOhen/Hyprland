#pragma once

#include "../bindings/LuaBindingsInternal.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Config::Lua::Objects {

    struct LuaStackGuard {
        lua_State* L;
        int        top;
        LuaStackGuard(lua_State* L) : L(L), top(lua_gettop(L)) {}
        ~LuaStackGuard() {
            lua_settop(L, top);
        }
    };

    template <typename T>
    inline int gcRef(lua_State* L) {
        sc<T*>(lua_touserdata(L, 1))->~T();
        return 0;
    }

    inline int readOnlyNewIndex(lua_State* L) {
        return Config::Lua::Bindings::Internal::configError(L, "attempt to modify read-only hl object");
    }

    struct MetatableEntry {
        const char*   name;
        lua_CFunction fn;
    };

    template <typename T>
    class LuaSchema {
      public:
        using PropertyGetter = std::function<int(lua_State*, T)>;
        using MethodFunction = std::function<int(lua_State*)>;

        LuaSchema()  = default;
        ~LuaSchema() = default;

        void addProperty(const std::string& name, PropertyGetter getter) {
            m_properties[name] = getter;
        }
        bool hasProperty(const std::string& name) const {
            return m_properties.contains(name);
        }

        int getProperty(lua_State* L, const std::string& name, T obj) const {
            auto it = m_properties.find(name);
            return it != m_properties.end() ? it->second(L, obj) : 0;
        }

        const auto& properties() const {
            return m_properties;
        }

        void addMethod(const std::string& name, MethodFunction method) {
            m_methods[name] = method;
        }
        bool hasMethod(const std::string& name) const {
            return m_methods.contains(name);
        }

        int getMethod(lua_State* L, const std::string& name) const {
            auto it = m_methods.find(name);
            return it != m_methods.end() ? it->second(L) : 0;
        }

        const auto& methods() const {
            return m_methods;
        }

        bool has(const std::string& name) const {
            return hasProperty(name) || hasMethod(name);
        }

        std::vector<std::string> getAllKeys() const {
            std::vector<std::string> keys;
            for (const auto& [key, _] : m_properties)
                keys.emplace_back(key);
            for (const auto& [key, _] : m_methods)
                keys.emplace_back(key);
            return keys;
        }

      private:
        std::map<std::string, PropertyGetter> m_properties;
        std::map<std::string, MethodFunction> m_methods;
    };

    inline void registerMetatable(lua_State* L, const char* name, std::initializer_list<MetatableEntry> entries) {
        LuaStackGuard guard(L);
        luaL_newmetatable(L, name);

        for (const auto& [key, func] : entries) {
            if (func) {
                lua_pushcfunction(L, func);
                lua_setfield(L, -2, key);
            }
        }

        lua_pushcfunction(L, readOnlyNewIndex);
        lua_setfield(L, -2, "__newindex");
    }

    struct PairsState {
        std::vector<std::string> keys;
        void*                    schema;
        void*                    obj;
        int                      idx;
        using GetterFunc = int (*)(lua_State*, void*, const std::string&, void*);
        GetterFunc getter;
        using DeleterFunc = void (*)(void*);
        DeleterFunc deleter;
    };

    static int pairsIter(lua_State* L) {
        auto* state = *(PairsState**)lua_touserdata(L, lua_upvalueindex(1));

        if (!state) {
            lua_pushnil(L);
            return 1;
        }

        state->idx++;

        if (state->idx >= (int)state->keys.size()) {
            lua_pushnil(L);
            return 0;
        }

        const auto& key = state->keys[state->idx];

        lua_pushstring(L, key.c_str());

        state->getter(L, state->schema, key, state->obj);

        return 2;
    }

    template <typename T, typename RefType>
    static int createPairs(lua_State* L, LuaSchema<T>* schema, const char* metatableName, std::function<T(RefType*)> extractObj) {
        auto* ref = sc<RefType*>(luaL_checkudata(L, 1, metatableName));

        T     actualObj = extractObj(ref);

        if (!actualObj)
            return 0;

        auto* state   = new PairsState();
        state->keys   = schema->getAllKeys();
        state->schema = (void*)schema;
        state->obj    = new T(actualObj);
        state->idx    = -1;
        state->getter = [](lua_State* L, void* schemaPtr, const std::string& key, void* objPtr) -> int {
            auto* typedSchema = (LuaSchema<T>*)schemaPtr;
            auto* typedObj    = (T*)objPtr;
            if (typedSchema->hasProperty(key))
                return typedSchema->getProperty(L, key, *typedObj);
            if (typedSchema->hasMethod(key)) {
                lua_pushstring(L, "function");
                return 1;
            }

            return 0;
        };

        state->deleter = [](void* obj) { delete static_cast<T*>(obj); };

        auto** ud = (PairsState**)lua_newuserdata(L, sizeof(PairsState*));
        *ud       = state;

        if (luaL_newmetatable(L, "PairsStateMeta")) {
            lua_pushcfunction(L, [](lua_State* L) {
                auto** ud = (PairsState**)lua_touserdata(L, 1);
                if (*ud) {
                    if ((*ud)->deleter && (*ud)->obj) {
                        (*ud)->deleter((*ud)->obj);
                    }
                    delete *ud;
                    *ud = nullptr;
                }
                return 0;
            });
            lua_setfield(L, -2, "__gc");
        }
        lua_setmetatable(L, -2);

        lua_pushcclosure(L, pairsIter, 1);
        lua_pushvalue(L, -1);
        lua_pushnil(L);
        return 2;
    }

    class ILuaObjectWrapper {
      public:
        virtual ~ILuaObjectWrapper()     = default;
        virtual void setup(lua_State* L) = 0;
    };

}
