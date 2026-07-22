#include <QApplication>
#include <QFileInfo>

#include <exception>
#include <memory>
#include <optional>

#include "core/crash_guard.h"
#include "core/logging.h"
#include "database/db.h"
#include "database/schema.h"
#include "engine/inspection_engine.h"
#include "ml/embedding_extractor.h"
#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"
#include "repositories/settings_repository.h"
#include "repositories/tool_repository.h"
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PC Inspector"));
    QApplication::setOrganizationName(QStringLiteral("PCInspector"));

    const QString appDir = QCoreApplication::applicationDirPath();
    pci::core::Logger::instance().setLogFile(
        (appDir + QStringLiteral("/pc_inspector.log")).toStdString());
    // Manejador de último recurso: si un driver de captura mata el proceso a
    // nivel del SO, la causa queda escrita en el log de crash en vez de un
    // cierre silencioso.
    pci::core::installCrashHandler(
        (appDir + QStringLiteral("/pc_inspector_crash.log")).toStdString());
    pci::core::logInfo("Aplicación iniciada");

    // La BD vive junto al ejecutable (demo portable). Si falla, la app sigue
    // funcionando sin persistencia: error loggeado, nunca crash.
    std::unique_ptr<pci::database::Db> db;
    std::optional<pci::repositories::SettingsRepository> settings;
    std::optional<pci::repositories::PieceRepository> pieces;
    std::optional<pci::repositories::ToolRepository> tools;
    std::optional<pci::repositories::InspectionRepository> inspections;
    {
        auto opened = pci::database::Db::open(
            (appDir + QStringLiteral("/pc_inspector.db")).toStdString());
        if (opened.isOk()) {
            db = std::move(opened.value());
            if (auto migrated = pci::database::migrate(*db); migrated.isOk()) {
                settings.emplace(*db);
                pieces.emplace(*db);
                tools.emplace(*db);
                inspections.emplace(*db);
            } else {
                pci::core::logError("Migración de BD fallida: " + migrated.error().message);
                db.reset();
            }
        } else {
            pci::core::logError(opened.error().message);
        }
    }

    // Modelo de embeddings: junto al exe o en models/ del proyecto. Si falta,
    // la app degrada a solo herramientas geométricas (avisado en el log).
    std::unique_ptr<pci::ml::EmbeddingExtractor> extractor;
    for (const QString& candidate :
         {appDir + QStringLiteral("/models/embedding_model.onnx"),
          appDir + QStringLiteral("/../../models/embedding_model.onnx")}) {
        if (!QFileInfo::exists(candidate)) {
            continue;
        }
        auto created = pci::ml::EmbeddingExtractor::create(
            QFileInfo(candidate).absoluteFilePath().toStdString());
        if (created.isOk()) {
            extractor = std::move(created.value());
            break;
        }
        pci::core::logError(created.error().message);
    }
    if (extractor == nullptr) {
        pci::core::logWarning(
            "Modelo de embeddings no disponible: inspección solo con herramientas "
            "(ejecuta run.ps1 para descargarlo)");
    }

    pci::engine::EmbedFn embedFn;
    if (extractor != nullptr) {
        // El extractor no es thread-safe; los diálogos modales garantizan un
        // solo flujo (registro o inspección) a la vez.
        embedFn = [&extractor](const cv::Mat& image) { return extractor->extract(image); };
    }

    std::optional<pci::engine::InspectionEngine> engine;
    if (pieces.has_value() && tools.has_value() && inspections.has_value()) {
        engine.emplace(embedFn, pieces.value(), tools.value(), inspections.value());
    }

    pci::ui::AppRepositories repositories;
    repositories.settings = settings.has_value() ? &settings.value() : nullptr;
    repositories.pieces = pieces.has_value() ? &pieces.value() : nullptr;
    repositories.tools = tools.has_value() ? &tools.value() : nullptr;
    repositories.inspections = inspections.has_value() ? &inspections.value() : nullptr;
    repositories.engine = engine.has_value() ? &engine.value() : nullptr;
    repositories.embedFn = embedFn;

    try {
        pci::ui::MainWindow window(repositories);
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
