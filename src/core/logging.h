#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace pci::core {

enum class LogLevel { Debug, Info, Warning, Error };

// Logger mínimo thread-safe: consola siempre; archivo si setLogFile tuvo éxito.
// Sin dependencias externas para mantener el binario ligero.
class Logger {
public:
    static Logger& instance();

    // Si el archivo no puede abrirse lo reporta por consola y sigue solo en consola.
    void setLogFile(const std::string& path);
    void log(LogLevel level, const std::string& message);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;

    std::mutex mutex_;
    std::ofstream file_;
};

void logDebug(const std::string& message);
void logInfo(const std::string& message);
void logWarning(const std::string& message);
void logError(const std::string& message);

}  // namespace pci::core
