#include "core/logging.h"

#include <chrono>
#include <ctime>
#include <iostream>

namespace pci::core {

namespace {

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

// Solo se llama con el mutex del logger tomado (std::localtime no es reentrante).
std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

    char buffer[32];
    if (const std::tm* tm = std::localtime(&seconds)) {
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);
    } else {
        buffer[0] = '\0';
    }

    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03d", buffer, static_cast<int>(ms.count()));
    return out;
}

}  // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLogFile(const std::string& path) {
    std::lock_guard lock(mutex_);
    file_.open(path, std::ios::app);
    if (!file_.is_open()) {
        std::clog << "[WARN ] No se pudo abrir el archivo de log: " << path << '\n';
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard lock(mutex_);
    const std::string line = "[" + timestamp() + "] [" + levelName(level) + "] " + message;

    std::clog << line << '\n';
    if (file_.is_open()) {
        // Flush por línea: si la app muere, el log debe estar completo en disco.
        file_ << line << std::endl;
    }
}

void logDebug(const std::string& message) { Logger::instance().log(LogLevel::Debug, message); }
void logInfo(const std::string& message) { Logger::instance().log(LogLevel::Info, message); }
void logWarning(const std::string& message) { Logger::instance().log(LogLevel::Warning, message); }
void logError(const std::string& message) { Logger::instance().log(LogLevel::Error, message); }

}  // namespace pci::core
