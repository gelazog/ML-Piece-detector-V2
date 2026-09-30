// Cabecera PRIVADA de la implementación de MainWindow, repartida en
// main_window*.cpp. Solo la incluyen esos ficheros.
//
// Guarda los includes que usaba el main_window.cpp único y los ayudantes de
// ámbito de fichero que comparten varios de sus trozos. Los que usa uno solo
// viven en el espacio de nombres anónimo de ese fichero.
#pragma once

#include "inspection_editor/reference_advice.h"
#include "ui/measurements_panel.h"
#include "ui/theme.h"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDockWidget>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QWidgetAction>
#include <QClipboard>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QPixmap>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "camera/camera_enumerator.h"
#include "camera/file_sources.h"
#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/editor_window.h"
#include "repositories/config_io.h"
#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"
#include "repositories/settings_repository.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "inspection_editor/canvas/tool_palette.h"
#include "repositories/tool_repository.h"
#include "ui/calibration_dialog.h"
#include "ui/camera_image_page.h"
#include "ui/configure_dialog.h"
#include "ui/delete_scope.h"
#include "ui/background_patch_dialog.h"
#include "ui/detection_page.h"
#include "ui/inspection_result_dialog.h"
#include "ui/history_dialog.h"
#include "ui/lens_calibration_dialog.h"
#include "ui/piece_manager_dialog.h"
#include "ui/piece_mosaic.h"
#include "ui/setup_guide.h"
#include "ui/source_files.h"
#include "ui/measurement_mode_dialog.h"
#include "ui/preferences_page.h"
#include "ui/registration_wizard.h"
#include "ui/template_manager_dialog.h"
#include "vision/fixture_stabilizer.h"
#include "vision/frame_geometry.h"
#include <QApplication>

#include "vision/detection_tuning.h"
#include "vision/contour_analysis.h"
#include "vision/edge_segmentation.h"
#include "vision/outlined_piece.h"
#include "vision/pipeline.h"
#include "vision/plane_scale.h"
#include <opencv2/imgproc.hpp>

#include "ui/dialog_geometry.h"
#include "ui/piece_report_dialog.h"
#include "ui/performance_page.h"
#include "ui/rate_readout.h"
#include "ui/pieces_page.h"

#include "vision/auto_roi.h"
#include "vision/position_fixture.h"
#include "vision/quality_metrics.h"

namespace pci::ui {

namespace {

// Marcadores del desplegable de fuente, guardados en el DATO del elemento. Los
// valores negativos no chocan nunca con un índice de cámara, que es lo que
// permite preguntar «¿qué eligió?» sin depender de dónde caiga en la lista.
constexpr int kSourceOpenImage = -1;
constexpr int kSourceOpenVideo = -2;
// El fichero que está abierto AHORA. Se añade al desplegable al abrirlo y se
// quita al cerrar.
//
// Sin esto, tras abrir «pieza.png» el desplegable seguía diciendo «Abrir
// imagen…»: el operador no tenía dónde leer QUÉ está mirando. Saber siempre
// dónde estás es lo primero que una interfaz tiene que resolver, y aquí encima
// importa el doble, porque la mitad de las decisiones —recalibrar, comparar,
// registrar— dependen de con qué imagen se está trabajando.
constexpr int kSourceOpenedFile = -3;
// Los ficheros abiertos hace poco, una ruta por línea (los 5 últimos).
const char* const kSettingRecentFiles = "recent_files";
const char* const kSettingFreeZone = "det_roi_poly";
constexpr int kCaptureTarget = 30;

// La zona libre se guarda como texto «x,y x,y …». Una zona son unos pocos
// vértices después de simplificar, y darle una tabla propia en la base sería un
// esquema nuevo para un dato que cabe en una línea.
inline std::string encodeZonePolygon(const std::vector<cv::Point>& polygon) {
    std::string text;
    for (const auto& point : polygon) {
        if (!text.empty()) {
            text.push_back(' ');
        }
        text += std::to_string(point.x) + "," + std::to_string(point.y);
    }
    return text;
}

// Lo contrario, y a prueba de basura: cualquier par que no se entienda se salta
// en vez de tumbar el arranque. Un ajuste corrupto tiene que costar una zona,
// no una aplicación que no abre.
inline std::vector<cv::Point> decodeZonePolygon(const std::string& text) {
    std::vector<cv::Point> polygon;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        const auto comma = token.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        try {
            polygon.emplace_back(std::stoi(token.substr(0, comma)),
                                 std::stoi(token.substr(comma + 1)));
        } catch (const std::exception&) {
            continue;
        }
    }
    // Menos de tres vértices no es una zona; devolver «casi una» sería dejar que
    // el resto del programa tuviera que acordarse de comprobarlo.
    return polygon.size() >= 3 ? polygon : std::vector<cv::Point>{};
}

}  // namespace

}  // namespace pci::ui
