#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Config::Lua {

    struct SProcessResult {
        int         exitCode = -1;
        std::string stdout;
        std::string stderr;
        uint64_t    processId = 0;
        bool        timedOut  = false;
        std::string error;
    };

    void postToMain(std::function<void()>&& fn);

    class CProcessExecutor {
      public:
        CProcessExecutor();
        ~CProcessExecutor();

        uint64_t spawnAsync(const std::string& cmd, int timeoutMs, std::function<void(const SProcessResult&)>&& onComplete);
        void     cancel(uint64_t processId);

      private:
        class Impl;
        struct ImplDeleter {
            void operator()(Impl* p) const;
        };
        std::unique_ptr<Impl, ImplDeleter> m_impl;
    };

}
