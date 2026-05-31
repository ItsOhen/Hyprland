#include "ReloadPipeline.hpp"
#include "ConfigManager.hpp"

#include "../shared/inotify/ConfigWatcher.hpp"
#include "../shared/animation/AnimationTree.hpp"
#include "../shared/workspace/WorkspaceRuleManager.hpp"
#include "../shared/monitor/MonitorRuleManager.hpp"

#include "../../desktop/rule/Engine.hpp"
#include "../../managers/KeybindManager.hpp"
#include "../../managers/input/trackpad/TrackpadGestures.hpp"
#include "../../debug/log/Logger.hpp"

#include <filesystem>

using namespace Config::Lua;

CReloadPipeline::CReloadPipeline(CConfigManager* mgr) : m_mgr(mgr) {}

void CReloadPipeline::execute(SReloadContext& ctx) {
    phaseEnter(ctx);
    phaseLoad(ctx);
    if (!ctx.syntaxCheckOk)
        return;

    if (ctx.scope == eReloadScope::FULL)
        phaseClearState();

    phaseExecute(ctx);
    phaseFinalize(ctx);
}

void CReloadPipeline::phaseEnter(SReloadContext& ctx) {
    if (ctx.scope == eReloadScope::FULL) {
        m_mgr->m_configPaths.clear();
        m_mgr->m_dependencyGraph->clear();
        m_mgr->m_fileGenerations.clear();
        m_mgr->m_configPaths.emplace_back(m_mgr->m_mainConfigPath);
        m_mgr->m_errors.clear();
    } else {
        m_mgr->m_errors.erase(ctx.filePath);
    }

    m_mgr->m_currentSourcePath = ctx.filePath;
    ctx.newGen                 = m_mgr->advanceFileGeneration(ctx.filePath);

    if (ctx.scope == eReloadScope::FULL) {
        auto mainName = std::filesystem::path(ctx.filePath).filename().string();
        Log::logger->log(Log::LUA, "[{}@{}]: full reload starting", mainName, ctx.newGen);
        m_mgr->logStateBeforeReload(ctx.filePath, ctx.newGen);
    }
}

void CReloadPipeline::phaseLoad(SReloadContext& ctx) {
    m_mgr->pushLuaTracebackHandler();

    if (ctx.scope == eReloadScope::FULL) {
        lua_getglobal(m_mgr->m_lua, "package");
        lua_getfield(m_mgr->m_lua, -1, "loaded");
        static constexpr std::string_view STDLIB[] = {"_G", "coroutine", "debug", "io", "math", "os", "package", "string", "table", "utf8", "bit32", "jit"};
        lua_pushnil(m_mgr->m_lua);
        while (lua_next(m_mgr->m_lua, -2)) {
            lua_pop(m_mgr->m_lua, 1);
            if (lua_isstring(m_mgr->m_lua, -1)) {
                std::string_view mod = lua_tostring(m_mgr->m_lua, -1);
                if (std::ranges::find(STDLIB, mod) == std::end(STDLIB)) {
                    lua_pushvalue(m_mgr->m_lua, -1);
                    lua_pushnil(m_mgr->m_lua);
                    lua_settable(m_mgr->m_lua, -4);
                }
            }
        }
        lua_pop(m_mgr->m_lua, 2);
    } else if (!ctx.moduleName.empty()) {
        lua_getglobal(m_mgr->m_lua, "package");
        lua_getfield(m_mgr->m_lua, -1, "loaded");
        lua_pushstring(m_mgr->m_lua, ctx.moduleName.c_str());
        lua_pushnil(m_mgr->m_lua);
        lua_settable(m_mgr->m_lua, -3);
        lua_pop(m_mgr->m_lua, 2);

        lua_getglobal(m_mgr->m_lua, "require");
        lua_pushstring(m_mgr->m_lua, ctx.moduleName.c_str());
        return;
    }

    if (luaL_loadfile(m_mgr->m_lua, m_mgr->m_mainConfigPath.c_str()) != LUA_OK) {
        if (ctx.scope == eReloadScope::FULL)
            m_mgr->m_errors.clear();
        else
            m_mgr->m_errors.erase(ctx.filePath);

        m_mgr->addError(lua_tostring(m_mgr->m_lua, -1));
        lua_pop(m_mgr->m_lua, 1);
        lua_remove(m_mgr->m_lua, 1);
        ctx.syntaxCheckOk                            = false;
        m_mgr->m_lastConfigVerificationWasSuccessful = false;
    }
}

void CReloadPipeline::phaseClearState() {
    Config::animationTree()->reset();
    Config::workspaceRuleMgr()->clear();
    Config::monitorRuleMgr()->clear();
    Desktop::Rule::ruleEngine()->clearAllRules();
    m_mgr->m_luaWindowRules.clear();
    m_mgr->m_luaWindowRuleGen.clear();
    m_mgr->m_luaLayerRules.clear();
    m_mgr->m_luaLayerRuleGen.clear();
    m_mgr->m_anonymousRuleCount = 0;
    g_pTrackpadGestures->clearGestures();
    m_mgr->m_luaGestures.clear();
    m_mgr->cleanTimers();
    m_mgr->clearLuaLayoutProviders();
    m_mgr->clearHeldLuaRefs();
    m_mgr->m_deviceConfigs.clear();
    m_mgr->m_deviceConfigGen.clear();
    m_mgr->m_registeredPlugins.clear();
    m_mgr->m_registeredPluginGen.clear();
    m_mgr->m_luaKeybindRefGen.clear();
    std::erase_if(g_pKeybindManager->m_keybinds, [](const auto& kb) { return kb->handler == "__lua"; });
    m_mgr->reregisterLuaPluginFns();
    for (const auto& [_, v] : m_mgr->m_configValues) {
        v->reset();
    }
}

void CReloadPipeline::phaseExecute(SReloadContext& ctx) {
    const char* logMsg = ctx.scope == eReloadScope::FULL ? "config reload" : (ctx.moduleName.empty() ? "main config cascade" : "module reload");
    int         nargs  = (ctx.scope != eReloadScope::FULL && !ctx.moduleName.empty()) ? 1 : 0;

    if (m_mgr->guardedPCall(nargs, 0, 1, CConfigManager::LUA_TIMEOUT_CONFIG_RELOAD_MS, logMsg) != LUA_OK) {
        std::string error = lua_tostring(m_mgr->m_lua, -1);
            m_mgr->addError(std::format("Module failed to compile: {}, probably a good time to do a full reload.\n{}", ctx.filePath, std::move(error)));
        lua_pop(m_mgr->m_lua, 1);
        if (ctx.scope == eReloadScope::FULL)
            m_mgr->m_lastConfigVerificationWasSuccessful = false;
    } else if (ctx.scope == eReloadScope::FULL) {
        m_mgr->m_lastConfigVerificationWasSuccessful = m_mgr->m_errors.empty();
    }

    lua_remove(m_mgr->m_lua, 1);
}

void CReloadPipeline::phaseFinalize(SReloadContext& ctx) {
    lua_gc(m_mgr->m_lua, LUA_GCCOLLECT, 0);
    Config::watcher()->update();
    m_mgr->sweepStaleRegistrations();

    if (ctx.scope != eReloadScope::FULL) {
        if (ctx.visited && !ctx.moduleName.empty())
            m_mgr->cascadeUpReload(ctx.filePath, *ctx.visited);
        m_mgr->logStateAfterReload(ctx.filePath, ctx.newGen);
    }
    m_mgr->postConfigReload();
}
