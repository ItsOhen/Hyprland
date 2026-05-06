#include "LuaProcessExecutor.hpp"
#include "../../debug/log/Logger.hpp"
#include "../../helpers/MainLoopExecutor.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <hyprutils/os/Process.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using namespace Config::Lua;

namespace Config::Lua {

    constexpr int WORKER_COUNT = 4;

    void          postToMain(std::function<void()>&& fn) {
        auto* ex = new CMainLoopExecutor(std::move(fn));
        ex->signal();
    }

}

class CProcessExecutor::Impl {
  public:
    Impl() : m_running(true), m_nextProcessId(1) {
        m_workers.reserve(WORKER_COUNT);
        for (int i = 0; i < WORKER_COUNT; ++i)
            m_workers.emplace_back([this] { workerLoop(); });
    }

    ~Impl() {
        m_running = false;
        m_cv.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable())
                worker.join();
        }
    }

    uint64_t spawnAsync(const std::string& cmd, std::function<void(const SProcessResult&)>&& onComplete) {
        const uint64_t processId = m_nextProcessId++;

        auto           task = [processId, cmd, callback = std::move(onComplete)]() mutable {
            SProcessResult result{};
            result.processId = processId;
            result.timedOut  = false;

            Hyprutils::OS::CProcess proc("/bin/sh", {"-c", cmd});

            if (!proc.runSync()) {
                result.error    = "Failed to run process";
                result.exitCode = -1;
                postToMain([callback, result]() { callback(result); });
                return;
            }

            result.exitCode = proc.exitCode();
            result.stdout   = proc.stdOut();
            result.stderr   = proc.stdErr();

            postToMain([callback, result]() { callback(result); });
        };

        {
            std::lock_guard<std::mutex> lk(m_taskMutex);
            m_tasks.emplace(std::move(task));
        }

        m_cv.notify_one();
        return processId;
    }

    void cancel(uint64_t processId) {
        (void)processId;
    }

  private:
    void workerLoop() {
        while (m_running) {
            std::unique_lock<std::mutex> lk(m_taskMutex);
            m_cv.wait(lk, [this] { return !m_tasks.empty() || !m_running; });

            if (!m_running)
                break;
            if (m_tasks.empty())
                continue;

            auto task = std::move(m_tasks.front());
            m_tasks.pop();
            lk.unlock();

            try {
                task();
            } catch (const std::exception& e) { Log::logger->log(Log::ERR, "CProcessExecutor: worker: {}", e.what()); }
        }
    }

    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_taskMutex;
    std::condition_variable           m_cv;
    std::atomic<bool>                 m_running{true};
    std::atomic<uint64_t>             m_nextProcessId{1};
};

void CProcessExecutor::ImplDeleter::operator()(Impl* p) const {
    delete p;
}

CProcessExecutor::CProcessExecutor() : m_impl(new Impl, ImplDeleter{}) {}
CProcessExecutor::~CProcessExecutor() = default;

uint64_t CProcessExecutor::spawnAsync(const std::string& cmd, int timeoutMs, std::function<void(const SProcessResult&)>&& onComplete) {
    (void)timeoutMs;
    return m_impl->spawnAsync(cmd, std::move(onComplete));
}

void CProcessExecutor::cancel(uint64_t processId) {
    m_impl->cancel(processId);
}
