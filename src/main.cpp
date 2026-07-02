#include <QApplication>

#include <exception>

#include "core/logging.h"
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PC Inspector"));
    QApplication::setOrganizationName(QStringLiteral("PCInspector"));

    pci::core::Logger::instance().setLogFile(
        (QCoreApplication::applicationDirPath() + QStringLiteral("/pc_inspector.log"))
            .toStdString());
    pci::core::logInfo("Aplicación iniciada");

    try {
        pci::ui::MainWindow window;
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
