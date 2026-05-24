#pragma once

#include <shared_mutex>
#include <unordered_map>
#include <format>
#include <vector>
#include <string>

namespace Log {
    struct SRollingLogFollow {
        std::unordered_map<int, std::string> m_socketToRollingLogFollowQueue;
        std::unordered_map<int, bool>        m_socketShowLevel;
        std::unordered_map<int, std::string> m_socketLevelFilter;
        std::shared_mutex                    m_mutex;
        bool                                 m_running                  = false;
        static constexpr size_t              ROLLING_LOG_FOLLOW_TOO_BIG = 8192;

        // Returns true if the queue is empty for the given socket
        bool isEmpty(int socket) {
            std::shared_lock<std::shared_mutex> r(m_mutex);
            return m_socketToRollingLogFollowQueue[socket].empty();
        }

        std::string debugInfo() {
            std::shared_lock<std::shared_mutex> r(m_mutex);
            return std::format("RollingLogFollow, got {} connections", m_socketToRollingLogFollowQueue.size());
        }

        std::string getLog(int socket) {
            std::unique_lock<std::shared_mutex> w(m_mutex);

            const std::string                   ret = m_socketToRollingLogFollowQueue[socket];
            m_socketToRollingLogFollowQueue[socket] = "";

            return ret;
        };

        void addLog(const std::string_view& log, const std::string& s, const std::string& levelCode) {
            std::unique_lock<std::shared_mutex> w(m_mutex);
            m_running = true;
            for (const auto& p : m_socketToRollingLogFollowQueue) {
                if (!m_socketLevelFilter[p.first].empty() && m_socketLevelFilter[p.first] != levelCode)
                    continue;
                if (m_socketShowLevel[p.first] && !s.empty())
                    m_socketToRollingLogFollowQueue[p.first] += std::format("{}\n", s);
                else
                    m_socketToRollingLogFollowQueue[p.first] += std::format("{}\n", log);
            }
        }

        bool isRunning() {
            std::shared_lock<std::shared_mutex> r(m_mutex);
            return m_running;
        }

        void stopFor(int socket) {
            std::unique_lock<std::shared_mutex> w(m_mutex);
            m_socketToRollingLogFollowQueue.erase(socket);
            m_socketShowLevel.erase(socket);
            m_socketLevelFilter.erase(socket);
            if (m_socketToRollingLogFollowQueue.empty())
                m_running = false;
        }

        void startFor(int socket, bool showLevel = false, const std::string& levelFilter = "") {
            std::unique_lock<std::shared_mutex> w(m_mutex);
            m_socketToRollingLogFollowQueue[socket] = std::format("[LOG] Following log to socket: {} started\n", socket);
            m_socketShowLevel[socket]               = showLevel;
            m_socketLevelFilter[socket]             = levelFilter;
            m_running                               = true;
        }

        static SRollingLogFollow& get() {
            static SRollingLogFollow    instance;
            static std::mutex           gm;
            std::lock_guard<std::mutex> lock(gm);
            return instance;
        };
    };
}
