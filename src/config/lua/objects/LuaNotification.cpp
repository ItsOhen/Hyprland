#include "LuaNotification.hpp"

#include "../../../helpers/MiscFunctions.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <memory>

#include "../../shared/parserUtils/ParserUtils.hpp"

using namespace Config;
using namespace Config::Lua;
using namespace Config::Lua::Bindings;

static constexpr const char*                            MT = "HL.Notification";

SP<Objects::LuaSchema<SP<Notification::CNotification>>> Objects::CLuaNotification::s_schema;

namespace {
    struct SNotificationRef {
        WP<Notification::CNotification> notification;
        bool                            paused = false;
    };

    std::optional<CHyprColor> parseColor(lua_State* L, int idx) {
        if (lua_isnumber(L, idx))
            return CHyprColor(sc<uint64_t>(lua_tonumber(L, idx)));

        if (lua_isstring(L, idx)) {
            auto parsed = ParserUtils::parseColor(lua_tostring(L, idx));
            if (!parsed)
                return std::nullopt;

            return CHyprColor(sc<uint64_t>(*parsed));
        }

        return std::nullopt;
    }

    std::optional<eIcons> iconFromString(std::string iconName) {
        std::ranges::transform(iconName, iconName.begin(), [](const unsigned char c) { return std::tolower(c); });

        static constexpr std::array<std::pair<const char*, eIcons>, 10> ICON_NAMES = {
            std::pair{"warning", ICON_WARNING}, std::pair{"warn", ICON_WARNING}, std::pair{"info", ICON_INFO},         std::pair{"hint", ICON_HINT},
            std::pair{"error", ICON_ERROR},     std::pair{"err", ICON_ERROR},    std::pair{"confused", ICON_CONFUSED}, std::pair{"question", ICON_CONFUSED},
            std::pair{"ok", ICON_OK},           std::pair{"none", ICON_NONE},
        };

        for (const auto& [name, icon] : ICON_NAMES) {
            if (name == iconName)
                return icon;
        }

        return std::nullopt;
    }

    std::optional<eIcons> parseIcon(lua_State* L, int idx) {
        if (lua_isnumber(L, idx)) {
            const auto raw = sc<int>(lua_tonumber(L, idx));
            if (raw >= ICON_WARNING && raw <= ICON_NONE)
                return sc<eIcons>(raw);

            return std::nullopt;
        }

        if (lua_isstring(L, idx))
            return iconFromString(lua_tostring(L, idx));

        return std::nullopt;
    }
}

static int notificationEq(lua_State* L) {
    const auto* lhs = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));
    const auto* rhs = sc<SNotificationRef*>(luaL_checkudata(L, 2, MT));

    lua_pushboolean(L, lhs->notification.lock() == rhs->notification.lock());
    return 1;
}

static int notificationToString(lua_State* L) {
    const auto* ref          = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));
    const auto  notification = ref->notification.lock();

    if (!notification)
        lua_pushstring(L, "HL.Notification(expired)");
    else
        lua_pushfstring(L, "HL.Notification(%p)", notification.get());

    return 1;
}

static int notificationGC(lua_State* L) {
    auto* ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    if (ref->paused) {
        if (const auto notification = ref->notification.lock(); notification)
            notification->unlock();
    }

    ref->~SNotificationRef();
    return 0;
}

static int notificationPause(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification)
        return 0;

    if (ref->paused)
        return 0;

    notification->lock();
    ref->paused = true;
    return 0;
}

static int notificationResume(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification)
        return 0;

    if (!ref->paused)
        return 0;

    notification->unlock();
    ref->paused = false;
    return 0;
}

static int notificationSetPaused(lua_State* L) {
    luaL_checkudata(L, 1, MT);
    luaL_checktype(L, 2, LUA_TBOOLEAN);

    if (lua_toboolean(L, 2))
        return notificationPause(L);

    return notificationResume(L);
}

static int notificationIsPaused(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushboolean(L, notification->isLocked());
    return 1;
}

static int notificationSetText(lua_State* L) {
    auto* ref   = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));
    auto  text_ = Check::string(L, 2);
    if (!text_)
        return Internal::configError(L, std::format("HL.Notification:set_text: bad argument 2: {}", text_.error()));

    if (const auto notification = ref->notification.lock(); notification)
        notification->setText(std::move(*text_));

    return 0;
}

static int notificationSetTimeout(lua_State* L) {
    auto* ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    auto  timeoutMs_ = Check::number(L, 2);
    if (!timeoutMs_)
        return Internal::configError(L, std::format("HL.Notification:set_timeout: bad argument 2: {}", timeoutMs_.error()));
    const auto timeoutMs = sc<float>(*timeoutMs_);
    if (timeoutMs < 0.F)
        return Config::Lua::Bindings::Internal::configError(L, "HL.Notification:set_timeout: timeout must be >= 0");

    if (const auto notification = ref->notification.lock(); notification)
        notification->resetTimeout(timeoutMs);

    return 0;
}

static int notificationSetColor(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto color = parseColor(L, 2);
    if (!color)
        return Config::Lua::Bindings::Internal::configError(L, "HL.Notification:set_color: expected a color string or number");

    if (const auto notification = ref->notification.lock(); notification)
        notification->setColor(*color);

    return 0;
}

static int notificationSetIcon(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto icon = parseIcon(L, 2);
    if (!icon)
        return Config::Lua::Bindings::Internal::configError(L, "HL.Notification:set_icon: expected an icon name or number");

    if (const auto notification = ref->notification.lock(); notification)
        notification->setIcon(*icon);

    return 0;
}

static int notificationSetFontSize(lua_State* L) {
    auto* ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    auto  fontSize_ = Check::number(L, 2);
    if (!fontSize_)
        return Internal::configError(L, std::format("HL.Notification:set_font_size: bad argument 2: {}", fontSize_.error()));
    const auto fontSize = sc<float>(*fontSize_);
    if (fontSize <= 0.F)
        return Config::Lua::Bindings::Internal::configError(L, "HL.Notification:set_font_size: font size must be > 0");

    if (const auto notification = ref->notification.lock(); notification)
        notification->setFontSize(fontSize);

    return 0;
}

static int notificationDismiss(lua_State* L) {
    auto* ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    if (const auto notification = ref->notification.lock(); notification)
        Notification::overlay()->dismissNotification(notification);

    return 0;
}

static int notificationGetText(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, notification->text().c_str());
    return 1;
}

static int notificationGetTimeout(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, notification->timeMs());
    return 1;
}

static int notificationGetColor(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, sc<lua_Integer>(notification->color().getAsHex()));
    return 1;
}

static int notificationGetIcon(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, sc<lua_Integer>(notification->icon()));
    return 1;
}

static int notificationGetFontSize(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, notification->fontSize());
    return 1;
}

static int notificationGetElapsed(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, notification->timeElapsedMs());
    return 1;
}

static int notificationGetElapsedSinceCreation(lua_State* L) {
    auto*      ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    const auto notification = ref->notification.lock();
    if (!notification) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, notification->timeElapsedSinceCreationMs());
    return 1;
}

static int notificationIsAlive(lua_State* L) {
    auto* ref = sc<SNotificationRef*>(luaL_checkudata(L, 1, MT));

    lua_pushboolean(L, ref->notification.lock().get() != nullptr);
    return 1;
}

static int notificationIndex(lua_State* L) {
    luaL_checkudata(L, 1, MT);
    auto key_ = Check::string(L, 2);
    if (!key_)
        return Internal::configError(L, std::format("__index: bad argument 2: {}", key_.error()));
    const std::string_view key = *key_;

    if (!Objects::CLuaNotification::s_schema || !Objects::CLuaNotification::s_schema->has(std::string(key))) {
        lua_pushnil(L);
        return 1;
    }

    if (Objects::CLuaNotification::s_schema->hasMethod(std::string(key)))
        return Objects::CLuaNotification::s_schema->getMethod(L, std::string(key));

    lua_pushnil(L);
    return 1;
}

static int notificationPairs(lua_State* L) {
    return Objects::createPairs<SP<Notification::CNotification>, SNotificationRef>(L, Objects::CLuaNotification::s_schema.get(), MT,
                                                                                   [](SNotificationRef* ref) { return ref->notification.lock(); });
}

void Objects::CLuaNotification::setup(lua_State* L) {
    Objects::CLuaNotification::s_schema = makeShared<LuaSchema<SP<Notification::CNotification>>>();

    Objects::CLuaNotification::s_schema->addMethod("pause", [](lua_State* L) { return notificationPause(L); });
    Objects::CLuaNotification::s_schema->addMethod("resume", [](lua_State* L) { return notificationResume(L); });
    Objects::CLuaNotification::s_schema->addMethod("set_paused", [](lua_State* L) { return notificationSetPaused(L); });
    Objects::CLuaNotification::s_schema->addMethod("is_paused", [](lua_State* L) { return notificationIsPaused(L); });
    Objects::CLuaNotification::s_schema->addMethod("set_text", [](lua_State* L) { return notificationSetText(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_text", [](lua_State* L) { return notificationGetText(L); });
    Objects::CLuaNotification::s_schema->addMethod("set_timeout", [](lua_State* L) { return notificationSetTimeout(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_timeout", [](lua_State* L) { return notificationGetTimeout(L); });
    Objects::CLuaNotification::s_schema->addMethod("set_color", [](lua_State* L) { return notificationSetColor(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_color", [](lua_State* L) { return notificationGetColor(L); });
    Objects::CLuaNotification::s_schema->addMethod("set_icon", [](lua_State* L) { return notificationSetIcon(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_icon", [](lua_State* L) { return notificationGetIcon(L); });
    Objects::CLuaNotification::s_schema->addMethod("set_font_size", [](lua_State* L) { return notificationSetFontSize(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_font_size", [](lua_State* L) { return notificationGetFontSize(L); });
    Objects::CLuaNotification::s_schema->addMethod("dismiss", [](lua_State* L) { return notificationDismiss(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_elapsed", [](lua_State* L) { return notificationGetElapsed(L); });
    Objects::CLuaNotification::s_schema->addMethod("get_elapsed_since_creation", [](lua_State* L) { return notificationGetElapsedSinceCreation(L); });
    Objects::CLuaNotification::s_schema->addMethod("is_alive", [](lua_State* L) { return notificationIsAlive(L); });

    registerMetatable(L, MT,
                      {
                          {"__index", notificationIndex},
                          {"__gc", notificationGC},
                          {"__eq", notificationEq},
                          {"__tostring", notificationToString},
                          {"__pairs", notificationPairs},
                      });
}

void Objects::CLuaNotification::push(lua_State* L, const SP<Notification::CNotification>& notification) {
    new (lua_newuserdata(L, sizeof(SNotificationRef))) SNotificationRef{.notification = WP<Notification::CNotification>(notification)};
    luaL_getmetatable(L, MT);
    lua_setmetatable(L, -2);
}
