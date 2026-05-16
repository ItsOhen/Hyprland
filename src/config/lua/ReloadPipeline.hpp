#pragma once

#include <string>
#include <unordered_set>

namespace Config::Lua {

    class CConfigManager;

    enum class eReloadScope : uint8_t {
        FULL,
        MODULE
    };

    struct SReloadContext {
        eReloadScope                     scope = eReloadScope::FULL;
        std::string                      filePath;
        std::string                      moduleName;
        uint64_t                         newGen        = 0;
        std::unordered_set<std::string>* visited       = nullptr;
        bool                             syntaxCheckOk = true;
    };

    class CReloadPipeline {
      public:
        CReloadPipeline(CConfigManager* mgr);

        void execute(SReloadContext& ctx);

      private:
        void            phaseEnter(SReloadContext& ctx);
        void            phaseLoad(SReloadContext& ctx);
        void            phaseClearState();
        void            phaseExecute(SReloadContext& ctx);
        void            phaseFinalize(SReloadContext& ctx);

        CConfigManager* m_mgr;
    };

} // namespace Config::Lua
