#include <QApplication>

#include <exception>
#include <memory>
#include <optional>

#include "core/logging.h"
#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PC Inspector"));
    QApplication::setOrganizationName(QStringLiteral("PCInspector"));

    const QString appDir = QCoreApplication::applicationDirPath();
    pci::core::Logger::instance().setLogFile(
        (appDir + QStringLiteral("/pc_inspector.log")).toStdString());
    pci::core::logInfo("Aplicación iniciada");

    // La BD vive junto al ejecutable (demo portable). Si falla, la app sigue
    // funcionando sin persistencia: error loggeado, nunca crash.
    std::unique_ptr<pci::database::Db> db;
    std::optional<pci::repositories::SettingsRepository> settings;
    {
        auto opened = pci::database::Db::open(
            (appDir + QStringLiteral("/pc_inspector.db")).toStdString());
        if (opened.isOk()) {
            db = std::move(opened.value());
            if (auto migrated = pci::database::migrate(*db); migrated.isOk()) {
                settings.emplace(*db);
            } else {
                pci::core::logError("Migración de BD fallida: " + migrated.error().message);
                db.reset();
            }
        } else {
            pci::core::logError(opened.error().message);
        }
    }

    try {
        pci::ui::MainWindow window(settings.has_value() ? &settings.value() : nullptr);
        window.show();
        const int code = QApplication::exec();
        pci::core::logInfo("Aplicación finalizada con código " + std::to_string(code));
        return code;
    } catch (const std::exception& e) {
        // Última línea de defensa: nunca un crash silencioso.
        pci::core::logError(std::string("Excepción no controlada: ") + e.what());
        return 1;
    } catch (...) {
        pci::core::logError("Excepción desconocida no controlada");
        return 1;
    }
}
