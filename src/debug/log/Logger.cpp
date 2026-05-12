#include "Logger.hpp"
#include "RollingLogFollow.hpp"

#include "../../event/EventBus.hpp"

#include "../../config/ConfigValue.hpp"

using namespace Log;

CLogger::CLogger() {
    const auto IS_TRACE = Env::isTrace();
    m_logger.setLogLevel(IS_TRACE ? Hyprutils::CLI::LOG_TRACE : Hyprutils::CLI::LOG_DEBUG);
}

void CLogger::log(Hyprutils::CLI::eLogLevel level, const std::string_view& str) {

    static bool TRACE = Env::isTrace();
    std::string s, levelCode;

    if (!m_logsEnabled)
        return;

    if (level == Hyprutils::CLI::LOG_TRACE && !TRACE)
        return;

    switch (level) {
        case LUA:
            s         = std::format("\r\033[1;33mLUA \033[0m]: {}", str);
            levelCode = "LUA";
            break;
        case Hyprutils::CLI::LOG_TRACE:
            s         = std::format("\r\033[1;34mTRACE \033[0m]: {}", str);
            levelCode = "TRACE";
            break;
        case Hyprutils::CLI::LOG_DEBUG:
            s         = std::format("\r\033[1;32mDEBUG \033[0m]: {}", str);
            levelCode = "DEBUG";
            break;
        case Hyprutils::CLI::LOG_WARN:
            s         = std::format("\r\033[1;33mWARN \033[0m]: {}", str);
            levelCode = "WARN";
            break;
        case Hyprutils::CLI::LOG_ERR:
            s         = std::format("\r\033[1;31mERR \033[0m]: {}", str);
            levelCode = "ERR";
            break;
        case Hyprutils::CLI::LOG_CRIT:
            s         = std::format("\r\033[1;35mCRIT \033[0m]: {}", str);
            levelCode = "CRIT";
            break;
        default:
            break;
    }

    if (SRollingLogFollow::get().isRunning()) {
        SRollingLogFollow::get().addLog(str, s, levelCode);
    }

    if (!s.empty())
        m_logger.log(level, s);
    else
        m_logger.log(level, str);
}

void CLogger::initIS(const std::string_view& IS) {
    if (auto res = m_logger.setOutputFile(std::string{IS} + (ISDEBUG ? "/hyprlandd.log" : "/hyprland.log")); !res) {}
    m_logger.setEnableRolling(true);
    m_logger.setEnableColor(false);
    m_logger.setEnableStdout(true);
    m_logger.setTime(false);
}

void CLogger::initCallbacks() {
    static auto P = Event::bus()->m_events.config.reloaded.listen([this]() { recheckCfg(); });
    recheckCfg();
}

void CLogger::recheckCfg() {
    static auto PDISABLELOGS  = CConfigValue<Config::INTEGER>("debug:disable_logs");
    static auto PDISABLETIME  = CConfigValue<Config::INTEGER>("debug:disable_time");
    static auto PENABLESTDOUT = CConfigValue<Config::INTEGER>("debug:enable_stdout_logs");
    static auto PENABLECOLOR  = CConfigValue<Config::INTEGER>("debug:colored_stdout_logs");

    m_logger.setEnableStdout(!*PDISABLELOGS && *PENABLESTDOUT);
    m_logsEnabled = !*PDISABLELOGS;
    m_logger.setTime(!*PDISABLETIME);
    m_logger.setEnableColor(*PENABLECOLOR);
}

const std::string& CLogger::rolling() {
    return m_logger.rollingLog();
}

Hyprutils::CLI::CLogger& CLogger::hu() {
    return m_logger;
}
