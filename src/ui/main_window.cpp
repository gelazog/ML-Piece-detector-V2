#include "ui/main_window.h"
#include "ui/theme.h"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
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

const char* const kSettingCameraIndex = "camera_index";

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
const char* const kSettingLastSourceDir = "last_source_dir";
const char* const kSettingFreeZone = "det_roi_poly";
constexpr int kCaptureTarget = 30;
constexpr int kCaptureMinimum = 5;

// La zona libre se guarda como texto «x,y x,y …». Una zona son unos pocos
// vértices después de simplificar, y darle una tabla propia en la base sería un
// esquema nuevo para un dato que cabe en una línea.
std::string encodeZonePolygon(const std::vector<cv::Point>& polygon) {
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
std::vector<cv::Point> decodeZonePolygon(const std::string& text) {
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

// Corre en un hilo del pool de QtConcurrent; solo toca datos propios.
// El ancla (si la pieza tiene rasgo distintivo) fija la orientación del
// fixture aunque la pieza sea simétrica o llegue girada 180°. Las
// herramientas se miden sobre cada frame: las medidas salen en vivo.
AnalysisOverlay buildOverlay(const QImage& frame,
                             std::optional<vision::OrientationAnchor> anchor,
                             double orientationOffsetDeg,
                             const std::vector<inspection::ToolConfig>& tools,
                             const vision::PipelineConfig& pipeline,
                             std::optional<vision::Fixture> previousFixture,
                             double mmPerPixel, inspection::LengthUnit unit,
                             bool freezePose, double arucoMarkerMm,
                             vision::BoardConfig boardConfig, bool countPieces,
                             int wantedPiece, bool measureStages) {
    AnalysisOverlay overlay;
    overlay.frameSize = frame.size();

    // Blindaje total: una excepción que escape de un worker de QtConcurrent
    // se relanza en result() y tumbaría la aplicación.
    try {
        const cv::Mat image = camera::qImageToMat(frame);

        // Escala por marcador ArUco en vivo: si hay marcador de tamaño
        // conocido, la escala se recalcula este frame (se ajusta al acercar o
        // alejar). Si no, se usa la calibración manual pasada.
        double effMm = mmPerPixel;
        cv::Mat imageToMm;  // homografía del plano (D4): mm por-punto en herramientas
        if (arucoMarkerMm > 0.0) {
            if (auto marker = vision::detectMarkerScale(image, arucoMarkerMm)) {
                effMm = marker->mmPerPixel;
                overlay.liveMmPerPixel = marker->mmPerPixel;
                overlay.liveScaleQuality = marker->quality;
                imageToMm = marker->imageToMm;
            }
        }
        mmPerPixel = effMm;

        // Pose congelada (contorno oculto): las herramientas NO se mueven —
        // se miden sobre el frame actual con el fixture del último frame. Ideal
        // para inspeccionar una pieza fija en su jig sin que nada tiemble.
        if (freezePose && previousFixture.has_value()) {
            overlay.valid = true;
            overlay.centroid = QPointF(previousFixture->origin.x, previousFixture->origin.y);
            overlay.angleDeg = previousFixture->angleDeg;
            if (!tools.empty()) {
                // El tablero se resuelve con el mismo fixture con el que se
                // miden las herramientas, para que la lectura de Posición
                // coincida con lo que el operador ve dibujado.
                const vision::BoardFrame board = vision::resolveBoardFrame(
                    boardConfig, *previousFixture, true,
                    cv::Size(image.cols, image.rows));
                // La calidad solo significa algo si SE DETECTÓ el marcador:
                // sin él, el campo vale 0 y las herramientas avisarían de
                // "cámara inclinada" en cada medición. -1 = no se sabe.
                overlay.toolResults = inspection::runTools(
                    image, *previousFixture, tools, mmPerPixel, unit, imageToMm, &board,
                    overlay.liveMmPerPixel > 0.0 ? overlay.liveScaleQuality : -1.0);
            }
            return overlay;
        }

        // Contar piezas usa el mismo análisis, no uno aparte: `analyzeFrames`
        // devuelve todas y la mayor es exactamente la que daría `analyzeFrame`.
        core::Result<vision::PieceAnalysis> analysis =
            core::Result<vision::PieceAnalysis>::err("sin analizar");
        if (countPieces) {
            auto all = vision::analyzeFrames(image, pipeline, &overlay.piecesTooSmall);
            if (all.isOk()) {
                overlay.piecesFound = static_cast<int>(all.value().size());
                // EL NUMERO DECLARADO MANDA SOBRE QUE SE TRATA COMO PIEZA.
                //
                // De una queja de uso: «detecta muchos cuando en las
                // configuraciones solo deberia de detectar uno; dependiendo de
                // lo que ponga el usuario, eso deberia detectar». Hasta ahora el
                // numero esperado solo servia para juzgar el recuento al final:
                // la deteccion seguia tratando como pieza a cualquier mancha que
                // pasara el filtro de area, y una sombra se numeraba, se
                // dibujaba y se podia llegar a medir.
                //
                // Con un numero declarado se trabaja con las N MAYORES. Las
                // demas no desaparecen del informe —`piecesFound` sigue diciendo
                // cuantas manchas se vieron, y el veredicto sigue pudiendo dar
                // NG por el recuento—, pero dejan de ser piezas.
                if (pipeline.expectedPieces >= 1 &&
                    static_cast<int>(all.value().size()) > pipeline.expectedPieces) {
                    std::vector<double> areas;
                    areas.reserve(all.value().size());
                    for (const auto& piece : all.value()) {
                        areas.push_back(piece.contour.area);
                    }
                    // El area de la que hace de corte: la N-esima mayor.
                    std::nth_element(areas.begin(),
                                     areas.begin() + pipeline.expectedPieces - 1, areas.end(),
                                     std::greater<double>());
                    const double cutoff =
                        areas[static_cast<std::size_t>(pipeline.expectedPieces - 1)];
                    std::vector<vision::PieceAnalysis> kept;
                    kept.reserve(static_cast<std::size_t>(pipeline.expectedPieces));
                    for (auto& piece : all.value()) {
                        // Se conserva el ORDEN DE LECTURA al filtrar: el numero
                        // de cada pieza tiene que seguir significando su sitio.
                        if (piece.contour.area >= cutoff &&
                            static_cast<int>(kept.size()) < pipeline.expectedPieces) {
                            kept.push_back(std::move(piece));
                        }
                    }
                    all.value() = std::move(kept);
                }
                overlay.piecesUsed = static_cast<int>(all.value().size());
                // LA MAYOR, PEDIDA POR SU NOMBRE.
                //
                // Antes esto era `front()`, y funcionaba porque la lista venía
                // ordenada por área. Ahora viene en orden de lectura para que el
                // número de cada pieza signifique algo, así que `front()` sería
                // la de arriba a la izquierda. Cambiar en silencio QUÉ pieza se
                // mide es el tipo de fallo que nadie ve hasta que compara dos
                // informes de la misma bandeja.
                // La regla vive en `vision::measuredPieceIndex` y no aquí.
                //
                // Aquí estaba escrita a mano, y era el ÚNICO sitio que la
                // conocía: «Medir pieza» y el editor llamaban a `analyzeFrame`,
                // que devuelve la mayor y no sabe de navegadores. El operador
                // señalaba la pieza 3, la veía medida en pantalla, y el informe
                // le llegaba de otra.
                const std::size_t chosen =
                    vision::measuredPieceIndex(all.value(), wantedPiece);
                overlay.measuredPiece = static_cast<int>(chosen) + 1;
                // El contorno de TODAS, para poder dibujarlas y numerarlas. Se
                // copian antes de mover la elegida fuera de la lista.
                overlay.pieceContours.reserve(all.value().size());
                for (const auto& piece : all.value()) {
                    QPolygonF outline;
                    outline.reserve(static_cast<int>(piece.contour.points.size()));
                    for (const auto& point : piece.contour.points) {
                        outline << QPointF(point.x, point.y);
                    }
                    overlay.pieceContours.push_back(std::move(outline));
                }
                analysis = core::Result<vision::PieceAnalysis>::ok(
                    std::move(all.value()[chosen]));
            } else {
                overlay.piecesFound = 0;
                analysis = core::Result<vision::PieceAnalysis>::err(all.error().message);
            }
        } else {
            analysis = vision::analyzeFrame(image, pipeline,
                                            measureStages ? &overlay.timings : nullptr);
            overlay.timed = measureStages;
        }
        // A partir de aquí el frame se ha segmentado, haya pieza o no.
        overlay.analysed = true;
        if (!analysis.isOk()) {
            overlay.error = QString::fromStdString(analysis.error().message);
            // Sin pieza todavía se puede enfocar: se mide el centro del
            // encuadre —no el frame entero, donde el fondo manda— y se marca
            // como tal para que el asistente no lo llame "nitidez de la pieza".
            const cv::Rect centre(image.cols / 4, image.rows / 4, image.cols / 2,
                                  image.rows / 2);
            overlay.sharpness = vision::sharpnessOf(image, centre);
            return overlay;
        }
        // El rasgo distintivo solo tiene sentido si se sigue la rotación.
        if (anchor.has_value() && pipeline.autoOrient) {
            if (auto applied = vision::applyAnchor(image, *anchor, analysis.value());
                !applied.isOk()) {
                core::logWarning(applied.error().message);
            }
        }
        if (auto applied = vision::applyOrientationOffset(image, orientationOffsetDeg,
                                                          analysis.value());
            !applied.isOk()) {
            core::logWarning(applied.error().message);
        }

        // Estabilización temporal: quieto = clavado, movimiento real =
        // seguimiento suave, y continuidad anti-giro de 180° cuando la pieza
        // no tiene rasgo distintivo. Trazos, medidas y overlay comparten el
        // mismo fixture estabilizado.
        if (previousFixture.has_value()) {
            vision::StabilizerOptions stabilizer;
            stabilizer.resolveFlips = !anchor.has_value();
            bool flipped180 = false;
            const vision::Fixture stable = vision::stabilizeFixture(
                *previousFixture, analysis.value().fixture, stabilizer, flipped180);
            if (flipped180) {
                if (auto applied =
                        vision::applyOrientationOffset(image, 180.0, analysis.value());
                    !applied.isOk()) {
                    core::logWarning(applied.error().message);
                }
            }
            analysis.value().fixture = stable;
        }

        overlay.valid = true;
        overlay.contour.reserve(
            static_cast<qsizetype>(analysis.value().contour.points.size()));
        for (const cv::Point& p : analysis.value().contour.points) {
            overlay.contour << QPointF(p.x, p.y);
        }
        overlay.centroid = QPointF(analysis.value().fixture.origin.x,
                                   analysis.value().fixture.origin.y);
        overlay.boundsCenter = QPointF(analysis.value().contour.rotatedRect.center.x,
                                       analysis.value().contour.rotatedRect.center.y);
        overlay.angleDeg = analysis.value().fixture.angleDeg;
        overlay.normalized = camera::matToQImage(analysis.value().normalized);
        // Asistente de enfoque (C2): la nitidez se mide SOBRE LA PIEZA. Sobre el
        // frame entero, un fondo texturizado o la regla graduada dominan el
        // Laplaciano y el número deja de hablar de lo que se va a medir.
        overlay.sharpness =
            vision::sharpnessOf(image, cv::boundingRect(analysis.value().contour.points));
        overlay.sharpnessOnPiece = true;
        if (!tools.empty()) {
            const cv::Point2f bounds = analysis.value().contour.rotatedRect.center;
            const vision::BoardFrame board =
                vision::resolveBoardFrame(boardConfig, analysis.value().fixture, true,
                                          cv::Size(image.cols, image.rows), &bounds);
            const auto toolsStarted = std::chrono::steady_clock::now();
            overlay.toolResults = inspection::runTools(
                image, analysis.value().fixture, tools, mmPerPixel, unit, imageToMm, &board,
                overlay.liveMmPerPixel > 0.0 ? overlay.liveScaleQuality : -1.0);
            // CADA MEDIDA SABE DE QUÉ PIEZA ES, también en vivo.
            //
            // El motor ya numera así —posición en orden de lectura— y el vivo se
            // quedaba con el 0 de «sin poner». Mientras el lienzo filtraba las
            // etiquetas por «pieza 0» daba lo mismo; en cuanto el operador puede
            // enfocar la 3, dos convenciones para el mismo campo son dos formas
            // de que las cotas se pinten sobre la pieza equivocada.
            if (overlay.measuredPiece >= 1) {
                for (auto& result : overlay.toolResults) {
                    result.pieceIndex = overlay.measuredPiece - 1;
                }
            }
            if (measureStages) {
                // Se suma al total para que el reparto sea el del frame entero:
                // `analyzeFrame` ya terminó cuando esto empieza, así que su
                // total no lo incluye.
                overlay.timings.tools =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - toolsStarted)
                        .count();
                overlay.timings.total += overlay.timings.tools;
            }
        }
    } catch (const std::exception& e) {
        overlay.valid = false;
        overlay.error = QStringLiteral("Error interno de análisis");
        core::logError(std::string("Excepción en análisis en vivo: ") + e.what());
    } catch (...) {
        overlay.valid = false;
        overlay.error = QStringLiteral("Error interno de análisis");
        core::logError("Excepción desconocida en análisis en vivo");
    }
    return overlay;
}

// El nombre corto sale de `inspection::toolTypeLabel` (lista única compartida
// con el editor); aquí solo se envuelve en QString.
QString typeLabel(inspection::ToolType type) {
    return QString::fromUtf8(inspection::toolTypeLabel(type));
}

double wrapAngleDeg(double angle) {
    while (angle >= 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

}  // namespace

MainWindow::MainWindow(AppRepositories repositories, QWidget* parent)
    : QMainWindow(parent), repos_(repositories) {
    setWindowTitle(tr("PC Inspector — Demo de inspección visual"));
    resize(1100, 760);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    // Separador vertical entre grupos de la barra.
    //
    // Trece botones repartidos en tres filas, todos del mismo peso y a la misma
    // distancia unos de otros, se leen como una lista de trece cosas sin
    // relación. Con una línea entre grupos se leen como tres decisiones: qué
    // miro, qué mido y qué hago. No es adorno — es lo único que dice dónde
    // acaba un grupo y empieza el siguiente.
    const auto separator = [central] {
        auto* line = new QFrame(central);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        return line;
    };

    // --- Fila 1: cámara ---
    auto* cameraLayout = new QHBoxLayout();
    cameraLayout->addWidget(new QLabel(tr("Fuente:"), central));
    cameraCombo_ = new QComboBox(central);
    cameraCombo_->setObjectName(QStringLiteral("sourceCombo"));
    cameraCombo_->setMinimumWidth(200);
    // Ancho ACOTADO, y no estirado hasta donde llegue.
    //
    // Con factor de estiramiento, el desplegable se quedaba con todo el hueco
    // sobrante: «Integrated Camera» ocupaba media ventana y empujaba los
    // botones contra el borde derecho, lejos del combo al que se refieren. Un
    // desplegable no se lee mejor por ser cuatro veces más ancho que su texto;
    // los botones sí se encuentran mejor si están juntos.
    cameraCombo_->setMaximumWidth(320);
    cameraCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    cameraLayout->addWidget(cameraCombo_);
    startStopButton_ = new QPushButton(tr("Iniciar"), central);
    // El botón MÁS pulsado de la ventana y no decía nada. Su rótulo además
    // cambia solo —«Iniciar», «Detener», «Abrir…»— según la fuente elegida, así
    // que leerlo no basta para saber qué va a pasar.
    startStopButton_->setToolTip(
        tr("Arranca o detiene la fuente elegida en la lista de al lado.\n\n"
           "Con una cámara: empieza o para el vídeo en directo.\n"
           "Con «Abrir imagen…» o «Abrir vídeo…»: pide el fichero.\n\n"
           "Mientras está parado se puede seguir dibujando herramientas sobre\n"
           "el último fotograma."));
    cameraLayout->addWidget(startStopButton_);
    // El botón dice lo que va a hacer. Con «Abrir imagen…» elegido, «Iniciar»
    // no describe la acción —lo siguiente que pasa es que se abre un diálogo de
    // fichero— y un botón que no anuncia su efecto se pulsa con recelo.
    connect(cameraCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (streaming_) {
            // Con una fuente en marcha, elegir otra la CAMBIA. Antes esto no
            // podía pasar porque el desplegable estaba apagado, y para abrir una
            // imagen había que saber que primero hay que pulsar «Detener».
            //
            // El cambio no es inmediato: parar la fuente es asíncrono (une el
            // hilo de captura), así que se apunta la elección y se aplica en
            // `onStreamStopped`. Arrancar la nueva antes de que la anterior
            // suelte la cámara es la forma más rápida de quedarse sin ninguna.
            const QVariant choice = cameraCombo_->currentData();
            if (!choice.isValid() || choice.toInt() == kSourceOpenedFile) {
                return;  // el propio fichero abierto: no hay nada que cambiar
            }
            pendingSourceChoice_ = choice.toInt();
            statusBar()->showMessage(tr("Cambiando de fuente…"));
            onStartStopClicked();  // detiene; la nueva arranca al terminar
            return;
        }
        const QVariant choice = cameraCombo_->currentData();
        const bool opensAFile = choice.isValid() && (choice.toInt() == kSourceOpenImage ||
                                                     choice.toInt() == kSourceOpenVideo);
        startStopButton_->setText(opensAFile ? tr("Abrir…") : tr("Iniciar"));
    });

    // Congelar. Va junto al de arrancar porque es la misma decisión —«qué estoy
    // mirando»— y porque es lo que se pulsa justo después de ver pasar la pieza
    // buena.
    freezeButton_ = new QPushButton(tr("Capturar foto"), central);
    freezeButton_->setEnabled(false);
    freezeButton_->setToolTip(
        tr("Congela el frame actual y trabaja sobre esa foto: con el vídeo en vivo la\n"
           "pieza tiembla y la detección late, así que dibujar una herramienta encima es\n"
           "puntería. Sobre una foto se traza, se calibra y se mide con calma.\n\n"
           "La cámara no se cierra: vuelves al vídeo con el mismo botón."));
    connect(freezeButton_, &QPushButton::clicked, this, &MainWindow::toggleFrozenPhoto);
    cameraLayout->addWidget(freezeButton_);

    cameraLayout->addWidget(separator());

    // UN solo control para la zona, con menú, en vez de dos botones.
    //
    // Había dos, y cada uno cambiaba de texto según el estado: «Zona de
    // detección» pasaba a «Quitar zona», y «Zona libre» a «Quitar zona libre».
    // En la barra se leía «Zona de detección | Quitar zona libre», que es un
    // botón diciendo lo que dibuja al lado de otro diciendo lo que borra — dos
    // verbos distintos para la misma decisión. Para saber qué había puesto
    // había que leer los dos y deducirlo.
    //
    // Ahora el botón dice SIEMPRE lo mismo («Zona») y el menú ofrece las tres
    // acciones por su nombre, con la activa marcada. El estado se lee de un
    // vistazo en vez de deducirse de dos etiquetas que se mueven.
    zoneButton_ = new QToolButton(central);
    // El rótulo alterna entre «Zona» y «Zona fija» según cuál esté en uso.
    zoneButton_->setObjectName(QStringLiteral("zoneButton"));
    zoneButton_->setText(tr("Zona"));
    zoneButton_->setIcon(inspection::regionIcon());
    zoneButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    zoneButton_->setPopupMode(QToolButton::InstantPopup);
    zoneButton_->setToolTip(
        tr("Dónde busca el programa la pieza. Fuera de la zona, las sombras, los\n"
           "reflejos y las piezas de al lado dejan de estorbar."));
    auto* zoneMenu = new QMenu(zoneButton_);
    rectZoneAction_ = zoneMenu->addAction(tr("Dibujar zona rectangular"));
    rectZoneAction_->setIcon(inspection::regionIcon());
    rectZoneAction_->setToolTip(
        tr("Arrastra un rectángulo sobre el vídeo: el contorno solo se buscará ahí."));
    freeZoneAction_ = zoneMenu->addAction(tr("Dibujar zona libre"));
    freeZoneAction_->setIcon(inspection::freeZoneIcon());
    freeZoneAction_->setToolTip(
        tr("La misma zona sin la obligación de que sea un rectángulo: rodea el área\n"
           "arrastrando, o marca las esquinas a clics y cierra sobre la primera.\n"
           "Para lo que un rectángulo no puede separar — el borde del útil pegado a\n"
           "la pieza, la pieza de al lado en diagonal."));
    zoneMenu->addSeparator();
    clearZoneAction_ = zoneMenu->addAction(tr("Quitar la zona"));
    zoneButton_->setMenu(zoneMenu);
    cameraLayout->addWidget(zoneButton_);

    // Pincel para corregir el borde detectado.
    edgeBrushButton_ = new QToolButton(central);
    edgeBrushButton_->setObjectName(QStringLiteral("edgeBrushButton"));
    edgeBrushButton_->setText(tr("Corregir borde"));
    edgeBrushButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    edgeBrushButton_->setPopupMode(QToolButton::InstantPopup);
    auto* brushMenu = new QMenu(edgeBrushButton_);
    brushAddAction_ = brushMenu->addAction(tr("Pincel: añadir a la pieza"));
    brushAddAction_->setObjectName(QStringLiteral("brushAddAction"));
    brushAddAction_->setCheckable(true);
    brushRemoveAction_ = brushMenu->addAction(tr("Pincel: quitar de la pieza"));
    brushRemoveAction_->setObjectName(QStringLiteral("brushRemoveAction"));
    brushRemoveAction_->setCheckable(true);
    brushMenu->addSeparator();

    // RODEAR, que es otra cosa que pintar.
    //
    // Petición de uso: «añadir pieza dibujando un contorno manualmente, y que
    // detecte o intente detectar la pieza (igual para quitarlo), por si en un
    // lote no la detecta, o detecta algo que no debe».
    //
    // El pincel ya servía para las dos cosas, y a mano: una pieza entera son
    // decenas de pinceladas, y lo que queda es una silueta dibujada a pulso —de
    // la que no se pueden sacar cotas—. Rodear es un gesto y el borde lo busca
    // el programa dentro del trazo.
    outlineAddAction_ = brushMenu->addAction(tr("Marcar una pieza rodeándola…"));
    outlineAddAction_->setObjectName(QStringLiteral("outlineAddAction"));
    outlineAddAction_->setCheckable(true);
    outlineAddAction_->setToolTip(
        tr("Rodea con el ratón una pieza que la detección no ve. Dentro del trazo\n"
           "se vuelve a buscar el borde con el fondo que haya ahí, así que la\n"
           "pieza se mide de verdad y no con el pulso de tu mano.\n\n"
           "Si ahí dentro no hay nada que detectar, se dice: la pieza se marca\n"
           "igual —vale para contarla— pero sus cotas serían las del trazo."));
    outlineDropAction_ = brushMenu->addAction(tr("Descartar lo que no es una pieza…"));
    outlineDropAction_->setObjectName(QStringLiteral("outlineDropAction"));
    outlineDropAction_->setCheckable(true);
    outlineDropAction_->setToolTip(
        tr("Rodea una mancha que la detección cuenta como pieza y no lo es: una\n"
           "sombra, un reflejo, un rótulo impreso en la mesa.\n\n"
           "Todo lo que quede dentro del trazo pasa a ser fondo."));
    brushMenu->addSeparator();

    // EL TAMAÑO, A LA VISTA.
    //
    // Antes solo se podia cambiar con la rueda del raton, y la rueda no la
    // encuentra quien no sabe ya que esta ahi. Peor: el unico sitio donde se
    // veia el tamaño era el anillo bajo el cursor, y ese anillo se dejaba de
    // dibujar justo al terminar la primera pincelada. El resultado era un ajuste
    // que existia, no se veia y ademas se reiniciaba solo.
    auto* sizeRow = new QWidget(brushMenu);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(12, 4, 12, 4);
    sizeLayout->addWidget(new QLabel(tr("Tamaño:"), sizeRow));
    brushSizeSlider_ = new QSlider(Qt::Horizontal, sizeRow);
    brushSizeSlider_->setObjectName(QStringLiteral("brushSizeSlider"));
    brushSizeSlider_->setRange(2, 120);  // los mismos topes que el lienzo
    brushSizeSlider_->setMinimumWidth(150);
    brushSizeSlider_->setToolTip(
        tr("Radio del pincel, en píxeles de la imagen.\n"
           "La rueda del ratón sobre la imagen hace lo mismo, más rápido."));
    sizeLayout->addWidget(brushSizeSlider_);
    brushSizeLabel_ = new QLabel(sizeRow);
    brushSizeLabel_->setMinimumWidth(52);
    sizeLayout->addWidget(brushSizeLabel_);
    auto* sizeAction = new QWidgetAction(brushMenu);
    sizeAction->setDefaultWidget(sizeRow);
    brushMenu->addAction(sizeAction);
    brushMenu->addSeparator();

    // LAS TRES AYUDAS.
    //
    // Tres y no una porque son tres problemas distintos, y quien pide «que el
    // pincel ayude» no siempre quiere las tres a la vez: una arregla la mano que
    // dibuja, otra restringe lo que se puede dibujar, y la tercera arregla el
    // resultado. Cada una se puede apagar por su cuenta.
    auto* assistTitle = brushMenu->addAction(tr("Ayuda del pincel"));
    assistTitle->setEnabled(false);
    brushSteadyAction_ = brushMenu->addAction(tr("   Pulso estable"));
    brushSteadyAction_->setObjectName(QStringLiteral("brushSteadyAction"));
    brushSteadyAction_->setCheckable(true);
    brushSteadyAction_->setToolTip(
        tr("El pincel persigue al ratón en vez de seguirlo al píxel.\n"
           "Filtra el temblor de la mano; la intención llega igual.\n"
           "Medido sobre un trazo con temblor: de 3,6 px de desviación a 1,4."));
    brushStraightAction_ = brushMenu->addAction(tr("   Trazo recto"));
    brushStraightAction_->setObjectName(QStringLiteral("brushStraightAction"));
    brushStraightAction_->setCheckable(true);
    brushStraightAction_->setToolTip(
        tr("La pincelada va en línea recta del principio al final, y el rodeo\n"
           "que dé la mano por el camino no cuenta.\n"
           "Mantener Mayús mientras se pinta hace lo CONTRARIO de lo que diga\n"
           "este interruptor, para no tener que venir a cambiarlo por un trazo."));
    brushSnapAction_ = brushMenu->addAction(tr("   Ceñir al borde"));
    brushSnapAction_->setObjectName(QStringLiteral("brushSnapAction"));
    brushSnapAction_->setCheckable(true);
    brushSnapAction_->setToolTip(
        tr("El resultado sigue el contraste real de la imagen en vez de tener\n"
           "el ancho del pincel: menos uniforme y más pegado a la pieza.\n\n"
           "Se queda con la mitad de la pincelada que se parece al punto donde\n"
           "EMPEZASTE el trazo, así que empieza encima de lo que quieres marcar.\n"
           "Donde no hay contraste que seguir, pinta como el pincel de siempre."));
    brushMenu->addSeparator();
    // UN solo deshacer, no dos.
    //
    // La aplicación ya tiene Ctrl+Z para las herramientas dibujadas. Darle al
    // pincel su propio atajo obligaría a saber cuál de los dos deshaceres está
    // uno usando, y a acertar — que es peor que no tener deshacer.
    //
    // La regla es la que espera cualquiera con un pincel en la mano: mientras
    // el pincel está activo, Ctrl+Z deshace la pincelada; con el pincel
    // apagado, Ctrl+Z sigue siendo el de las herramientas, como siempre. El
    // reparto lo hace `onUndo`, y los nombres del menú lo dicen para que no
    // haya que descubrirlo probando.
    brushUndoAction_ = brushMenu->addAction(
        tr("Deshacer la última pincelada	Ctrl+Z con el pincel activo"));
    brushUndoAction_->setEnabled(false);
    brushRedoAction_ = brushMenu->addAction(
        tr("Rehacer la pincelada	Ctrl+Y con el pincel activo"));
    brushRedoAction_->setEnabled(false);
    brushMenu->addSeparator();
    // La segunda mitad de corregir el borde: la corrección no sólo arregla ESTA
    // imagen, también dice dónde se equivoca la detección y con qué signo.
    brushTuneAction_ = brushMenu->addAction(tr("Afinar la detección con esta corrección…"));
    brushTuneAction_->setObjectName(QStringLiteral("brushTuneAction"));
    brushTuneAction_->setEnabled(false);  // hasta que haya algo que aprender
    brushClearAction_ = brushMenu->addAction(tr("Quitar las correcciones"));
    edgeBrushButton_->setMenu(brushMenu);
    cameraLayout->addWidget(edgeBrushButton_);

    connect(brushAddAction_, &QAction::triggered, this, [this](bool on) {
        brushRemoveAction_->setChecked(false);
        video_->setEdgeBrush(on ? inspection::EditorCanvas::EdgeBrush::AddPiece
                                : inspection::EditorCanvas::EdgeBrush::Off);
        statusBar()->showMessage(
            on ? tr("Pinta sobre lo que la detección se dejó fuera y forma parte de la pieza.")
               : tr("Pincel apagado."));
    });
    connect(brushRemoveAction_, &QAction::triggered, this, [this](bool on) {
        brushAddAction_->setChecked(false);
        video_->setEdgeBrush(on ? inspection::EditorCanvas::EdgeBrush::RemovePiece
                                : inspection::EditorCanvas::EdgeBrush::Off);
        statusBar()->showMessage(
            on ? tr("Pinta sobre lo que la detección metió y no es la pieza: sombras, "
                    "reflejos, la pieza de al lado.")
               : tr("Pincel apagado."));
    });
    // Rodear: los dos modos son exclusivos entre sí y con el pincel, porque el
    // gesto es el mismo arrastre y sólo puede significar una cosa.
    connect(outlineAddAction_, &QAction::triggered, this, [this](bool on) {
        outlineDropAction_->setChecked(false);
        brushAddAction_->setChecked(false);
        brushRemoveAction_->setChecked(false);
        video_->setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
        video_->setOutlinePickMode(on ? inspection::EditorCanvas::TracePurpose::MarkPiece
                                     : inspection::EditorCanvas::TracePurpose::WorkZone);
        statusBar()->showMessage(
            on ? tr("Rodea la pieza que falta: arrastra para trazarla a pulso, o marca "
                    "esquinas a clics y cierra sobre la primera.")
               : tr("Marcar piezas: apagado."));
    });
    connect(outlineDropAction_, &QAction::triggered, this, [this](bool on) {
        outlineAddAction_->setChecked(false);
        brushAddAction_->setChecked(false);
        brushRemoveAction_->setChecked(false);
        video_->setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
        video_->setOutlinePickMode(on ? inspection::EditorCanvas::TracePurpose::DropPiece
                                     : inspection::EditorCanvas::TracePurpose::WorkZone);
        statusBar()->showMessage(
            on ? tr("Rodea lo que no es una pieza: todo lo que quede dentro pasa a ser "
                    "fondo.")
               : tr("Descartar manchas: apagado."));
    });
    connect(video_, &inspection::EditorCanvas::pieceOutlined, this,
            &MainWindow::onPieceOutlined);
    connect(brushClearAction_, &QAction::triggered, this, [this] {
        video_->clearEdgeCorrection();
        statusBar()->showMessage(tr("Correcciones del borde quitadas."));
    });
    connect(brushTuneAction_, &QAction::triggered, this, &MainWindow::onTuneDetectionFromEdge);
    connect(brushUndoAction_, &QAction::triggered, this, [this] {
        if (!video_->undoEdgeCorrection()) {
            statusBar()->showMessage(tr("No hay ninguna pincelada que deshacer."));
        }
    });
    connect(brushRedoAction_, &QAction::triggered, this, [this] {
        if (!video_->redoEdgeCorrection()) {
            statusBar()->showMessage(tr("No hay ninguna pincelada que rehacer."));
        }
    });
    cameraLayout->addStretch(0);
    rootLayout->addLayout(cameraLayout);

    // --- Fila 2: pieza y flujo ---
    auto* pieceLayout = new QHBoxLayout();
    pieceLayout->addWidget(new QLabel(tr("Pieza:"), central));
    pieceCombo_ = new QComboBox(central);
    pieceCombo_->setMinimumWidth(140);
    // Acotado, por lo mismo que el de la fuente: estirado hasta el final
    // separaba la pieza de las acciones que se le aplican.
    pieceCombo_->setMaximumWidth(260);
    pieceLayout->addWidget(pieceCombo_);

    // Indicador del modo de medición (M3): junto al combo de pieza, que es
    // donde se decide. El operador nunca debe dudar en qué modo está.
    modeChip_ = new QLabel(central);
    modeChip_->setObjectName(QStringLiteral("modeChip"));
    modeChip_->setAlignment(Qt::AlignCenter);
    pieceLayout->addWidget(modeChip_);

    // Cuántas piezas se están viendo. Estaba SÓLO dentro de Configurar ▸ Piezas,
    // y ahí no lo ve quien está trabajando: con seis piezas en el encuadre la
    // ventana no decía nada y se medía la mayor en silencio.
    piecesChip_ = new QLabel(central);
    // Nombre estable: las pruebas lo buscaban por su texto, y en cuanto apareció
    // otra etiqueta que también dice «pieza» empezaron a leer la equivocada.
    piecesChip_->setObjectName(QStringLiteral("piecesChip"));
    piecesChip_->setAlignment(Qt::AlignCenter);
    piecesChip_->setVisible(false);  // sin recuento no ocupa sitio
    pieceLayout->addWidget(piecesChip_);

    // El navegador de piezas: solo aparece cuando hay mas de una, porque con una
    // sola no hay nada entre lo que elegir y un control apagado permanente es
    // ruido en una barra que ya esta llena.
    pieceNav_ = new QWidget(central);
    auto* navLayout = new QHBoxLayout(pieceNav_);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(2);
    piecePrevButton_ = new QToolButton(pieceNav_);
    piecePrevButton_->setObjectName(QStringLiteral("piecePrevButton"));
    piecePrevButton_->setText(QStringLiteral("\u2039"));
    // Una ayuda de partida: la de verdad la escribe `updatePieceNavigator`
    // cuando ya sabe cuántas piezas hay, y hasta entonces estas flechas eran
    // dos símbolos sin una palabra.
    piecePrevButton_->setToolTip(tr("Pieza anterior del encuadre, en orden de lectura."));
    piecePrevButton_->setAutoRaise(true);
    piecePrevButton_->setFocusPolicy(Qt::NoFocus);
    navLayout->addWidget(piecePrevButton_);
    pieceNavLabel_ = new QLabel(pieceNav_);
    pieceNavLabel_->setObjectName(QStringLiteral("pieceNavLabel"));
    pieceNavLabel_->setAlignment(Qt::AlignCenter);
    navLayout->addWidget(pieceNavLabel_);
    pieceNextButton_ = new QToolButton(pieceNav_);
    pieceNextButton_->setObjectName(QStringLiteral("pieceNextButton"));
    pieceNextButton_->setText(QStringLiteral("\u203a"));
    pieceNextButton_->setToolTip(tr("Pieza siguiente del encuadre, en orden de lectura."));
    pieceNextButton_->setAutoRaise(true);
    pieceNextButton_->setFocusPolicy(Qt::NoFocus);
    navLayout->addWidget(pieceNextButton_);
    pieceNav_->setVisible(false);
    pieceLayout->addWidget(pieceNav_);
    connect(piecePrevButton_, &QToolButton::clicked, this, [this] { stepFocusedPiece(-1); });
    connect(pieceNextButton_, &QToolButton::clicked, this, [this] { stepFocusedPiece(1); });

    // Que hay una correccion a mano puesta.
    //
    // Hace falta PORQUE el trazo se retira: sin este aviso, una correccion
    // activa seria estado invisible, y el operador podria estar mirando un
    // contorno que no sale de la deteccion sin tener forma de saberlo.
    edgeChip_ = new QLabel(central);
    edgeChip_->setObjectName(QStringLiteral("edgeChip"));
    edgeChip_->setAlignment(Qt::AlignCenter);
    edgeChip_->setVisible(false);
    pieceLayout->addWidget(edgeChip_);

    pieceLayout->addWidget(new QLabel(tr("Plantilla:"), central));
    templateCombo_ = new QComboBox(central);
    templateCombo_->setMinimumWidth(110);
    templateCombo_->setToolTip(
        tr("Una pieza puede tener varias plantillas de herramientas; se inspecciona "
           "con la activa."));
    pieceLayout->addWidget(templateCombo_);
    newTemplateButton_ = new QPushButton(tr("+"), central);
    newTemplateButton_->setToolTip(tr("Crear una plantilla nueva para esta pieza"));
    newTemplateButton_->setMaximumWidth(32);
    pieceLayout->addWidget(newTemplateButton_);
    manageTemplatesButton_ = new QPushButton(tr("Gestionar…"), central);
    manageTemplatesButton_->setToolTip(
        tr("Renombrar, duplicar o eliminar plantillas de esta pieza"));
    connect(manageTemplatesButton_, &QPushButton::clicked, this,
            &MainWindow::onManageTemplatesClicked);
    pieceLayout->addWidget(manageTemplatesButton_);

    // Aquí cambia la pregunta: hasta este punto la fila dice QUÉ se mide —la
    // pieza y su plantilla—, y a partir de aquí QUÉ SE HACE con ello. Sin la
    // línea, las siete cosas se leían como una lista sin relación.
    pieceLayout->addWidget(separator());

    registerLiveButton_ = new QPushButton(tr("Registrar y activar"), central);
    registerLiveButton_->setToolTip(
        tr("Captura automáticamente %1 referencias de la pieza en el video, guarda las "
           "herramientas dibujadas y arranca la auto-inspección")
            .arg(kCaptureTarget));
    pieceLayout->addWidget(registerLiveButton_);

    autoInspectButton_ = new QPushButton(tr("Auto-inspección"), central);
    autoInspectButton_->setObjectName(QStringLiteral("autoInspectButton"));
    autoInspectButton_->setCheckable(true);
    autoInspectButton_->setToolTip(
        tr("Inspecciona continuamente el video contra la pieza seleccionada"));
    pieceLayout->addWidget(autoInspectButton_);

    inspectButton_ = new QPushButton(tr("Inspeccionar"), central);
    inspectButton_->setObjectName(QStringLiteral("inspectButton"));
    inspectButton_->setToolTip(tr("Inspección única con reporte detallado"));
    // LA acción de esta pantalla, y la única destacada. Con trece botones del
    // mismo peso, el que se pulsa cien veces al día parecía tan importante como
    // «Gestionar…», que se abre una vez al mes. Un solo elemento distinto llama
    // la atención; dos o tres destacados no destacan ninguno.
    //
    // OJO CON ESTA LÍNEA: está aquí por el MARCO, no por el Enter.
    //
    // Esto es un `QMainWindow` y no un `QDialog`, y la documentación de Qt es
    // explícita: «the default button behavior is provided only in dialogs». Así
    // que la propiedad NO hace que Enter inspeccione, y quien la lea esperando
    // eso se equivoca. Para inspeccionar con el teclado está la tecla `I`, que
    // el menú ya enseña.
    //
    // Y no se quita, aunque el nombre engañe: está medido que es lo que pinta el
    // realce. Comparando el mismo botón con y sin la propiedad,
    // `tests/test_default_on_main_window.cpp` da **1645 de 1680 píxeles
    // distintos, el 97,9 %**. Quitarla dejaría el botón principal igual que los
    // otros doce.
    //
    // La negrita de abajo es el realce que NO depende de estar en un diálogo, y
    // esa es la que vigila la prueba.
    inspectButton_->setDefault(true);
    QFont emphasis = inspectButton_->font();
    emphasis.setBold(true);
    inspectButton_->setFont(emphasis);
    pieceLayout->addWidget(inspectButton_);

    // Medir la pieza NO es inspeccionarla, y por eso es un botón aparte aunque
    // estén juntos: inspeccionar compara contra una referencia y da un
    // veredicto; esto solo contesta cuánto mide lo que hay delante. Se puede
    // usar sin pieza registrada, sin plantilla y sin calibrar — dando píxeles y
    // diciéndolo.
    measurePieceButton_ = new QPushButton(tr("Medir pieza"), central);
    measurePieceButton_->setToolTip(
        tr("Mide la pieza entera a partir de su contorno y enseña todas las cotas:\n"
           "qué figura es, perímetro, área, envolvente, agujeros, y las cotas que su\n"
           "forma tenga — diámetro y redondez si es redonda, cada lado y cada ángulo\n"
           "si es un polígono, los dos diámetros si es una arandela.\n\n"
           "No hace falta pieza registrada ni plantilla. Sin calibrar da píxeles y lo\n"
           "dice. Desde el informe puedes copiarlo, exportarlo a CSV o convertir las\n"
           "cotas en herramientas vigiladas."));
    pieceLayout->addWidget(measurePieceButton_);
    pieceLayout->addStretch(0);
    rootLayout->addLayout(pieceLayout);

    // --- Fila 3: lo que actúa sobre la PIEZA y la PLANTILLA ---
    //
    // El dibujo se fue al dock de la derecha (P5) y con él lo que actúa sobre
    // la herramienta seleccionada. Aquí se queda lo demás, y el reparto no es
    // por hacer sitio: es por significado. «Rasgo distintivo», «Fijar escala» y
    // «Guardar plantilla» no son herramientas de dibujo — meterlas en el dock
    // sería ordenar por tamaño en vez de por lo que hace cada cosa.
    auto* toolsLayout = new QHBoxLayout();

    anchorButton_ = new QPushButton(tr("Rasgo distintivo"), central);
    anchorButton_->setIcon(inspection::anchorIcon());
    anchorButton_->setCheckable(true);
    anchorButton_->setToolTip(
        tr("Marca un punto visualmente único de la pieza (un agujero, una marca, una\n"
           "esquina oscura). Con él la orientación queda fija aunque la pieza sea\n"
           "simétrica: se detecta igual en cualquier rotación, incluso girada 180°."));
    toolsLayout->addWidget(anchorButton_);

    calibrateFromToolButton_ = new QPushButton(tr("Fijar escala con esta medida…"), central);
    calibrateFromToolButton_->setEnabled(false);
    calibrateFromToolButton_->setToolTip(
        tr("La forma más fácil de calibrar: traza una herramienta sobre algo de tamaño\n"
           "conocido (una regla, una moneda), selecciónala y escribe cuánto mide de\n"
           "verdad. La escala px→mm sale de esa medida y todas las cotas quedan reales."));
    toolsLayout->addWidget(calibrateFromToolButton_);

    saveTemplateButton_ = new QPushButton(tr("Guardar plantilla (Ctrl+S)"), central);
    saveTemplateButton_->setToolTip(
        tr("Guarda las herramientas dibujadas en vivo en la plantilla activa de la\n"
           "pieza, sin tener que volver a registrarla. Si no hay pieza seleccionada\n"
           "te pide crear una."));
    connect(saveTemplateButton_, &QPushButton::clicked, this,
            &MainWindow::onSaveTemplateClicked);
    toolsLayout->addWidget(saveTemplateButton_);

    toolsLayout->addStretch(1);
    auto* shortcutsButton = new QPushButton(tr("Atajos (F1)"), central);
    shortcutsButton->setToolTip(tr("Guía de atajos de teclado — también puedes cambiarlos"));
    connect(shortcutsButton, &QPushButton::clicked, this, &MainWindow::onShowShortcuts);
    toolsLayout->addWidget(shortcutsButton);
    rootLayout->addLayout(toolsLayout);

    // Guía del primer arranque (I3). Va donde el veredicto y no en un diálogo
    // a propósito: un asistente modal se cierra sin leer y encima tapa la
    // ventana que hay que mirar para hacer el primer paso.
    setupBanner_ = new QWidget(central);
    {
        auto* row = new QHBoxLayout(setupBanner_);
        row->setContentsMargins(8, 4, 4, 4);
        setupHintLabel_ = new QLabel(setupBanner_);
        setupHintLabel_->setWordWrap(true);
        row->addWidget(setupHintLabel_, 1);
        auto* dismiss = new QPushButton(tr("Entendido"), setupBanner_);
        dismiss->setToolTip(tr("No volver a mostrarlo. Los indicadores de la barra de "
                               "abajo siguen diciendo el estado en todo momento."));
        row->addWidget(dismiss);
        connect(dismiss, &QPushButton::clicked, this, &MainWindow::dismissSetupGuide);
    }
    setupBanner_->setStyleSheet(theme::bandStyle(theme::kProseOnBand, theme::kBandField));
    setupBanner_->setVisible(false);
    rootLayout->addWidget(setupBanner_);

    // Banner de veredicto para la auto-inspección.
    verdictBanner_ = new QLabel(central);
    verdictBanner_->setAlignment(Qt::AlignCenter);
    verdictBanner_->setMinimumHeight(36);
    verdictBanner_->setVisible(false);
    rootLayout->addWidget(verdictBanner_);

    // Lectura continua de la pieza respecto al tablero (T3): solo visible con
    // el tablero encendido, junto al banner y nunca en un diálogo.
    boardReadoutLabel_ = new QLabel(central);
    boardReadoutLabel_->setAlignment(Qt::AlignCenter);
    boardReadoutLabel_->setStyleSheet(
        theme::bandStyle(theme::kInkOnBand, theme::kBandField));
    boardReadoutLabel_->setVisible(false);
    rootLayout->addWidget(boardReadoutLabel_);

    // Video (canvas de edición) como área central de la ventana.
    video_ = new inspection::EditorCanvas(central);
    video_->setTools(&liveTools_);
    // AQUÍ, y no arriba con el resto del pincel.
    //
    // El botón y su menú se construyen mucho antes que el lienzo, y este
    // `connect` estaba con ellos — sobre `video_` todavía NULO. Qt no puede
    // conectar nada a un puntero nulo: avisa por consola y sigue. El resultado
    // era un pincel que pintaba en pantalla, marcaba verde y rojo, y cuya
    // corrección NO LLEGABA A NINGUNA PARTE: ni se reanalizaba, ni salía el
    // mensaje de «+N px, −M px», ni se movía el contorno.
    //
    // Las lambdas del menú sí funcionaban porque se ejecutan después, con
    // `video_` ya creado; sólo esta línea se evaluaba en el momento equivocado.
    // Por eso el fallo parecía «el pincel no hace su función» y no «falta una
    // conexión».
    // TODO ESTO VIVE AQUI Y NO JUNTO AL MENU, y no es orden estetico.
    //
    // `video_` se construye unas lineas mas arriba. Conectar o llamar al lienzo
    // desde donde se arma el menu del pincel seria hacerlo sobre un puntero
    // nulo: `connect` sobre nulo no conecta nada y solo avisa por consola —el
    // fallo que ya costo cuatro rondas con `edgeCorrected`— y una llamada como
    // `video_->brushRadius()` directamente tumba el programa al arrancar.
    connect(brushSizeSlider_, &QSlider::valueChanged, this,
            [this](int value) { applyBrushRadius(value, false); });
    // La rueda del raton sobre la imagen y el deslizador son el MISMO ajuste, y
    // tienen que enseñar el mismo numero. Antes el lienzo emitia este aviso y no
    // lo escuchaba nadie: se podia cambiar el tamaño sin que nada lo dijera.
    connect(video_, &inspection::EditorCanvas::brushRadiusChanged, this,
            [this](int radiusPx) { applyBrushRadius(radiusPx, true); });
    // Que ha hecho la ultima pincelada. Una ayuda que unas veces actua y otras
    // no, sin decir cual de las dos ha pasado, se vive como que el programa va a
    // rachas.
    connect(video_, &inspection::EditorCanvas::edgeStrokeFinished, this,
            [this](bool snapped, double contrast, int kept, int band) {
                if (!video_->brushSnap() || band <= 0) {
                    return;
                }
                if (snapped) {
                    statusBar()->showMessage(
                        tr("Pincelada ceñida al borde: se queda con %1 de %2 px "
                           "(contraste %3).")
                            .arg(kept)
                            .arg(band)
                            .arg(contrast, 0, 'f', 0));
                } else {
                    statusBar()->showMessage(
                        tr("Sin borde que seguir bajo la pincelada (contraste %1, hace "
                           "falta 12): se marcó entera, como el pincel de siempre.")
                            .arg(contrast, 0, 'f', 0));
                }
            });
    const auto rememberAssist = [this](const char* key, QAction* action,
                                       void (inspection::EditorCanvas::*apply)(bool)) {
        connect(action, &QAction::toggled, this, [this, key, apply](bool on) {
            (video_->*apply)(on);
            if (repos_.settings != nullptr) {
                repos_.settings->setInt(key, on ? 1 : 0);
            }
        });
    };
    rememberAssist("brush_steady", brushSteadyAction_,
                   &inspection::EditorCanvas::setBrushSteady);
    rememberAssist("brush_straight", brushStraightAction_,
                   &inspection::EditorCanvas::setBrushStraight);
    rememberAssist("brush_snap", brushSnapAction_, &inspection::EditorCanvas::setBrushSnap);

    // RECUPERAR NO PUEDE DEPENDER DE UNA SEÑAL QUE SOLO SALTA AL CAMBIAR.
    //
    // Antes esto era `action->setChecked(guardado)` y punto, confiando en que
    // `toggled` llevara el valor al lienzo. Una QAction empieza SIN MARCAR, así
    // que con un «apagado» guardado, `setChecked(false)` no emitía nada y el
    // lienzo se quedaba con su valor de fábrica.
    //
    // «Pulso estable» viene de fábrica ENCENDIDO, o sea que el operador lo
    // apagaba, reiniciaba, y el menú se lo enseñaba apagado mientras el pincel
    // lo seguía aplicando. No es que se olvide el ajuste: es que la pantalla
    // afirma una cosa y el programa hace otra, y no hay forma de descubrirlo
    // salvo notando que el trazo no obedece.
    //
    // Ahora el valor se empuja al lienzo SIEMPRE, haya cambiado la casilla o no.
    const auto restoreAssist = [this](const char* key, QAction* action,
                                      void (inspection::EditorCanvas::*apply)(bool),
                                      bool factory) {
        const auto saved = repos_.settings->getInt(key, factory ? 1 : 0);
        const bool wanted = saved.isOk() ? saved.value() != 0 : factory;
        action->setChecked(wanted);
        (video_->*apply)(wanted);
    };

    // Lo guardado, o lo que trae el lienzo de fabrica. Los valores por defecto
    // viven en UN solo sitio —el lienzo— para que restablecer los ajustes
    // devuelva exactamente lo que hace una instalacion nueva.
    if (repos_.settings != nullptr) {
        const auto saved = repos_.settings->getInt("brush_radius", video_->brushRadius());
        applyBrushRadius(saved.isOk() ? saved.value() : video_->brushRadius(), true);
        restoreAssist("brush_steady", brushSteadyAction_,
                      &inspection::EditorCanvas::setBrushSteady, video_->brushSteady());
        restoreAssist("brush_straight", brushStraightAction_,
                      &inspection::EditorCanvas::setBrushStraight, video_->brushStraight());
        restoreAssist("brush_snap", brushSnapAction_,
                      &inspection::EditorCanvas::setBrushSnap, video_->brushSnap());
    } else {
        applyBrushRadius(video_->brushRadius(), true);
        brushSteadyAction_->setChecked(video_->brushSteady());
        brushStraightAction_->setChecked(video_->brushStraight());
        brushSnapAction_->setChecked(video_->brushSnap());
    }

    connect(video_, &inspection::EditorCanvas::edgeCorrected, this,
            &MainWindow::onEdgeCorrected);
    rootLayout->addWidget(video_, 1);
    buildVideoBar(central, rootLayout);

    setCentralWidget(central);

    // Panel de comparación "registrada vs actual" en un dock reubicable (S3):
    // el operador lo puede mover, flotar o cerrar, y su posición se guarda.
    auto* compareWidget = new QWidget(this);
    auto* compareLayout = new QVBoxLayout(compareWidget);
    auto makeThumb = [compareWidget]() {
        auto* label = new QLabel(compareWidget);
        label->setFixedSize(170, 170);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(
            theme::placeholderStyle());
        label->setText(QStringLiteral("—"));
        return label;
    };
    compareLayout->addWidget(new QLabel(tr("Pieza registrada:"), compareWidget));
    refThumbLabel_ = makeThumb();
    compareLayout->addWidget(refThumbLabel_);
    compareLayout->addWidget(new QLabel(tr("Pieza actual:"), compareWidget));
    currentThumbLabel_ = makeThumb();
    compareLayout->addWidget(currentThumbLabel_);

    // Rotar la vista de la pieza a gusto del usuario (gira la orientación de
    // la pieza seleccionada; persiste y aplica en registro e inspección).
    auto* rotateLayout = new QHBoxLayout();
    auto* rotateLeft = new QPushButton(QStringLiteral("⟲ 90°"), compareWidget);
    auto* rotateRight = new QPushButton(QStringLiteral("⟳ 90°"), compareWidget);
    const QString rotateTip =
        tr("Gira cómo se ve la pieza (su recorte normalizado). Con una pieza\n"
           "seleccionada el giro se guarda con ella.");
    rotateLeft->setToolTip(rotateTip);
    rotateRight->setToolTip(rotateTip);
    rotateLayout->addWidget(rotateLeft);
    rotateLayout->addWidget(rotateRight);
    compareLayout->addLayout(rotateLayout);
    connect(rotateLeft, &QPushButton::clicked, this, [this] { rotatePieceView(-90.0); });
    connect(rotateRight, &QPushButton::clicked, this, [this] { rotatePieceView(90.0); });
    similarityLabel_ = new QLabel(compareWidget);
    similarityLabel_->setWordWrap(true);
    compareLayout->addWidget(similarityLabel_);
    compareLayout->addStretch(1);

    // --- Dock «Herramientas» (P5) ---
    //
    // La paleta y lo que actúa sobre la herramienta seleccionada: «Borrar» y el
    // parámetro de muestreo. Su sitio natural es junto a la herramienta, no
    // suelto en una barra donde ya no cabía nada.
    {
        auto* toolsPanel = new QWidget(this);
        auto* panelLayout = new QVBoxLayout(toolsPanel);
        panelLayout->setContentsMargins(0, 0, 0, 0);

        toolPalette_ = new inspection::ToolPalette(toolsPanel);
        panelLayout->addWidget(toolPalette_);

        // Parámetro de muestreo de la herramienta seleccionada, sin abrir el
        // editor: banda del Caliper, rayos del Círculo, escaneos del Borde,
        // área mínima del Blob.
        auto* paramRow = new QHBoxLayout();
        liveParamLabel_ = new QLabel(tr("Puntos:"), toolsPanel);
        paramRow->addWidget(liveParamLabel_);
        liveParamSpin_ = new QSpinBox(toolsPanel);
        liveParamSpin_->setRange(1, 1000);
        liveParamSpin_->setEnabled(false);
        liveParamSpin_->setToolTip(
            tr("Cantidad de puntos de muestreo de la herramienta seleccionada:\n"
               "Calibre: grosor de banda (px) · Círculo: rayos · Borde liso: escaneos\n"
               "· Blob: área mínima (px²)"));
        paramRow->addWidget(liveParamSpin_, 1);
        panelLayout->addLayout(paramRow);

        // «Borrar» ya no vive aquí: se mudó DENTRO de la paleta, junto a
        // Mover/Elegir, que es donde se elige la herramienta sobre la que actúa.
        // Tenerlo al final del panel obligaba a un viaje de ida y vuelta con el
        // ratón para el gesto más encadenado que hay: elegir y quitar.
        panelLayout->addStretch(1);

        toolsDock_ = new QDockWidget(tr("Herramientas"), this);
        // El nombre TIENE que ser estable: `saveState`/`restoreState` guardan la
        // disposición por `objectName`, así que cambiarlo perdería la
        // colocación que el operador dejó.
        toolsDock_->setObjectName(QStringLiteral("toolsDock"));
        toolsDock_->setWidget(toolsPanel);
        addDockWidget(Qt::RightDockWidgetArea, toolsDock_);
    }

    compareDock_ = new QDockWidget(tr("Comparación registrada / actual"), this);
    compareDock_->setObjectName(QStringLiteral("compareDock"));
    compareDock_->setWidget(compareWidget);
    addDockWidget(Qt::RightDockWidgetArea, compareDock_);

    mosaic_ = new PieceMosaic(this);
    mosaicDock_ = new QDockWidget(tr("Piezas del encuadre"), this);
    mosaicDock_->setObjectName(QStringLiteral("mosaicDock"));
    mosaicDock_->setWidget(mosaic_);
    addDockWidget(Qt::RightDockWidgetArea, mosaicDock_);
    // Arranca cerrado: con una sola pieza no tiene nada que enseñar, y un panel
    // vacío ocupando sitio desde el primer arranque enseña a cerrarlo y a no
    // volver a abrirlo.
    mosaicDock_->setVisible(false);
    // Al reabrirlo se vuelve a analizar. Sin esto, con una imagen fija cargada
    // —que no genera análisis nuevos— el panel reaparecería con lo que hubiera
    // dentro la última vez, o vacío, y el operador concluiría que no funciona.
    connect(mosaicDock_, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown) {
            reanalyseCurrentFrame();
        }
    });
    connect(mosaic_, &PieceMosaic::pieceChosen, this, [this](int number) {
        // Pulsar una baldosa es ELEGIRLA: pasa a ser la que miden las
        // herramientas y la que el vídeo remarca. Es el mismo enfoque que mueven
        // las flechas del selector, no un estado aparte del panel.
        focusedPiece_ = number;
        updatePieceNavigator();
        reanalyseCurrentFrame();
    });

    // Controles de vista (Z3): mínimo / − / porcentaje / + / máximo, siempre a
    // mano en la barra inferior para quien no use atajos ni rueda.
    auto* zoomBar = new QWidget(this);
    auto* zoomLayout = new QHBoxLayout(zoomBar);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(2);
    auto addZoomButton = [this, zoomBar, zoomLayout](const QString& text, const QString& tip,
                                                     auto slot) {
        auto* button = new QToolButton(zoomBar);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);  // no robar el foco al lienzo
        connect(button, &QToolButton::clicked, this, slot);
        zoomLayout->addWidget(button);
        return button;
    };
    zoomMinButton_ = addZoomButton(QStringLiteral("⤢"), tr("Zoom mínimo: ajustar a la ventana"),
                                   [this] { video_->zoomToMin(); });
    zoomOutButton_ = addZoomButton(QStringLiteral("−"), tr("Alejar (Ctrl+-)"),
                                   [this] { video_->zoomOut(); });
    zoomLabel_ = new QLabel(zoomBar);
    zoomLabel_->setMinimumWidth(52);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setToolTip(tr("Zoom actual. Rueda = acercar/alejar hacia el cursor,\n"
                              "botón central o Ctrl + arrastrar = mover la vista,\n"
                              "doble clic = ajustar a la ventana."));
    zoomLayout->addWidget(zoomLabel_);
    zoomInButton_ = addZoomButton(QStringLiteral("+"), tr("Acercar (Ctrl++)"),
                                  [this] { video_->zoomIn(); });
    zoomMaxButton_ = addZoomButton(QStringLiteral("⛶"), tr("Zoom máximo (20×)"),
                                   [this] { video_->zoomToMax(); });
    statusBar()->addPermanentWidget(zoomBar);
    connect(video_, &inspection::EditorCanvas::viewChanged, this, &MainWindow::updateZoomIndicator);
    updateZoomIndicator();

    // Tira de estado de la estación (I1). Los cuatro datos que deciden si una
    // medida vale estaban repartidos por las pestañas de «Configurar»: para
    // saber si estabas midiendo en condiciones había que abrirlo y recorrerlas,
    // que es justo lo que nadie hace antes de medir.
    //
    // Son botones planos y no etiquetas porque cada uno LLEVA a la pestaña que
    // lo arregla: enseñar un problema sin decir dónde se toca es media ayuda.
    for (int i = 0; i < 4; ++i) {
        auto* light = new QPushButton(this);
        light->setFlat(true);
        light->setCursor(Qt::PointingHandCursor);
        statusBar()->addPermanentWidget(light);
        stationLights_.push_back(light);
    }

    calibLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(calibLabel_);
    statsLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statsLabel_);

    // Indicadores de estado con punto verde/rojo (S4): cámara, BD y modelo ONNX.
    camIndicator_ = new QLabel(this);
    dbIndicator_ = new QLabel(this);
    modelIndicator_ = new QLabel(this);
    statusBar()->addPermanentWidget(camIndicator_);
    statusBar()->addPermanentWidget(dbIndicator_);
    statusBar()->addPermanentWidget(modelIndicator_);
    updateStatusIndicators();

    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(&enumerationWatcher_, &QFutureWatcher<std::vector<camera::CameraInfo>>::finished,
            this, &MainWindow::onCamerasEnumerated);
    connect(&analysisWatcher_, &QFutureWatcher<AnalysisOverlay>::finished, this,
            &MainWindow::onAnalysisFinished);

    cameraFrames_ =
        connect(&controller_, &camera::CameraController::frameReady, this, &MainWindow::onFrame);
    connect(&controller_, &camera::CameraController::statsUpdated, this, &MainWindow::onStats);
    connect(&controller_, &camera::CameraController::cameraError, this,
            &MainWindow::onCameraError);
    connect(&controller_, &camera::CameraController::stopped, this, &MainWindow::onStreamStopped);
    connect(&controller_, &camera::CameraController::controlsProbed, this,
            &MainWindow::onControlsProbed);
    connect(&controller_, &camera::CameraController::resolutionsProbed, this,
            &MainWindow::onResolutionsProbed);
    connect(&controller_, &camera::CameraController::exposureChosen, this,
            [this](double exposure, const std::vector<camera::ExposureFpsSample>& sweep) {
                // Se deja en el log lo que se PROBÓ y no solo lo que salió: si
                // algún día una cámara elige mal, la tabla dice por qué.
                core::logInfo("Exposición elegida por medida: " +
                              std::to_string(exposure) + " (de " +
                              std::to_string(sweep.size()) + " probadas)");
                autoExposureOn_ = false;
                updateCalibrationLabel();
            });
    connect(&controller_, &camera::CameraController::profileRejected, this,
            [this](const QString& reason) {
                // El perfil se deshizo: la cámara vuelve a automático, así que
                // las medidas NO son repetibles y hay que decirlo donde se
                // miran — que es la etiqueta de la escala, no un log.
                core::logWarning("Perfil rechazado: " + reason.toStdString());
                autoExposureOn_ = true;
                updateCalibrationLabel();
            });

    connect(toolPalette_, &inspection::ToolPalette::toolChosen, this,
            &MainWindow::onToolModeChanged);
    connect(video_, &inspection::EditorCanvas::toolCreated, this,
            &MainWindow::onLiveToolCreated);
    connect(video_, &inspection::EditorCanvas::toolModified, this,
            &MainWindow::onLiveToolModified);
    connect(video_, &inspection::EditorCanvas::selectionChanged, this,
            &MainWindow::onLiveSelectionChanged);
    connect(liveParamSpin_, &QSpinBox::valueChanged, this, &MainWindow::onLiveParamChanged);
    connect(calibrateFromToolButton_, &QPushButton::clicked, this,
            &MainWindow::onCalibrateFromToolClicked);
    connect(toolPalette_, &inspection::ToolPalette::deleteRequested, this,
            &MainWindow::onDeleteToolClicked);
    connect(toolPalette_, &inspection::ToolPalette::deleteAllRequested, this,
            &MainWindow::onDeleteAllToolsClicked);
    connect(anchorButton_, &QPushButton::toggled, this, &MainWindow::onAnchorButtonToggled);
    connect(video_, &inspection::EditorCanvas::pointPicked, this,
            &MainWindow::onAnchorPicked);
    connect(pieceCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onPieceSelectionChanged);
    connect(templateCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onTemplateChanged);
    connect(newTemplateButton_, &QPushButton::clicked, this,
            &MainWindow::onNewTemplateClicked);
    connect(video_, &inspection::EditorCanvas::contextMenuRequested, this,
            &MainWindow::onCanvasContextMenu);

    connect(registerLiveButton_, &QPushButton::clicked, this,
            &MainWindow::onRegisterLiveClicked);
    connect(&captureTimer_, &QTimer::timeout, this, &MainWindow::onCaptureTick);
    connect(&captureWatcher_,
            &QFutureWatcher<
                core::Result<engine::RegistrationSession::SampleFeedback>>::finished,
            this, &MainWindow::onCaptureProcessed);
    connect(autoInspectButton_, &QPushButton::toggled, this, &MainWindow::onAutoToggled);
    connect(&autoTimer_, &QTimer::timeout, this, &MainWindow::onAutoTick);

    connect(rectZoneAction_, &QAction::triggered, this,
            [this] { onRoiButtonToggled(true); });
    connect(clearZoneAction_, &QAction::triggered, this, &MainWindow::onClearZoneClicked);
    connect(video_, &inspection::EditorCanvas::regionPicked, this,
            &MainWindow::onRegionPicked);
    connect(measurePieceButton_, &QPushButton::clicked, this,
            &MainWindow::onMeasurePieceClicked);
    connect(freeZoneAction_, &QAction::triggered, this,
            [this] { onFreeZoneButtonToggled(true); });
    connect(video_, &inspection::EditorCanvas::freeZonePicked, this,
            &MainWindow::onFreeZonePicked);
    connect(video_, &inspection::EditorCanvas::freeZoneCancelled, this,
            &MainWindow::onFreeZoneCancelled);
    connect(inspectButton_, &QPushButton::clicked, this, &MainWindow::onInspectClicked);
    connect(&inspectionWatcher_,
            &QFutureWatcher<core::Result<engine::InspectionEngine::Outcome>>::finished, this,
            &MainWindow::onInspectionFinished);

    // Dos segundos después del último movimiento. Suficiente para que un
    // arrastre entero cueste una sola escritura, y corto para que un cierre
    // brusco no se lleve por delante lo que se acaba de colocar.
    layoutSaveTimer_.setSingleShot(true);
    layoutSaveTimer_.setInterval(2000);
    connect(&layoutSaveTimer_, &QTimer::timeout, this, &MainWindow::persistWindowLayout);

    captureTimer_.setInterval(350);
    autoTimer_.setInterval(autoIntervalMs_);  // se reajusta al cargar Preferencias

    // Calibración de escala persistida.
    if (repos_.settings != nullptr) {
        calibration_.mmPerPixel =
            repos_.settings->getDouble("calib_mm_per_px", 0.0).value();
        calibration_.cameraDistanceMm =
            repos_.settings->getDouble("calib_camera_dist_mm", 0.0).value();
        calibration_.horizontalFovDeg =
            repos_.settings->getDouble("calib_fov_deg", 60.0).value();
        calibration_.calibratedWidth = repos_.settings->getInt("calib_width", 0).value();
        calibration_.calibratedHeight = repos_.settings->getInt("calib_height", 0).value();
        calibratedCameraKey_ = QString::fromStdString(
            repos_.settings->getString("calib_camera", std::string()).value());
    }
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);

    // Preferencias persistidas (O1): intervalo de auto-inspección y kSigma.
    if (repos_.settings != nullptr) {
        autoIntervalMs_ =
            std::clamp(repos_.settings->getInt("pref_auto_interval_ms", 1000).value(),
                       200, 10000);
        kSigma_ = std::clamp(repos_.settings->getDouble("pref_ksigma", 3.0).value(), 0.5, 6.0);
        // Pestaña del panel Configurar (C1). Sin acotar por arriba: el diálogo
        // ignora un índice que no exista, que es lo que pasará si una versión
        // futura tiene menos pestañas que la que guardó el número.
        configureTab_ = std::max(0, repos_.settings->getInt("config_last_tab", 0).value());
        measureStages_ = repos_.settings->getInt("measure_stages", 0).value() != 0;
        pipelineConfig_.minAreaFraction = std::clamp(
            repos_.settings->getDouble("det_min_area", 0.005).value(), 0.0001, 0.5);
        pipelineConfig_.maxAreaFraction = std::clamp(
            repos_.settings->getDouble("det_max_area", 0.9).value(), 0.1, 1.0);
        // Por defecto, IMAGEN ENTERA. Estuvo en «automática» y hubo que
        // revertirlo: el argumento para ponerla —«la automática no puede
        // cambiar ninguna respuesta»— era FALSO, y lo demostró usar la
        // aplicación.
        //
        // El recorte automático rodea a UNA pieza, la mayor, con su margen. Se
        // suelta cuando alguien «está contando», pero eso exige que el operador
        // haya declarado antes que espera varias — y no puede saber que tiene
        // que declararlo hasta que ya ha visto el problema. Con varias piezas
        // en la mesa y nada declarado, las demás quedaban fuera por
        // construcción y la aplicación decía que solo había una.
        //
        // Una optimización que cambia una respuesta no es una optimización, es
        // un fallo. Esa frase ya estaba escrita en `effectiveWorkingZone`; lo
        // que faltaba era aplicármela al elegir el valor por defecto.
        zoneMode_ = vision::workingZoneModeFromKey(
            repos_.settings->getString("work_zone_mode", "off").value().c_str());
    }
    autoTimer_.setInterval(autoIntervalMs_);
    if (repos_.engine != nullptr) {
        repos_.engine->setKSigma(kSigma_);
    }

    // Ajustes de detección persistidos (umbral, polaridad, kernels y zona).
    if (repos_.settings != nullptr) {
        auto& seg = pipelineConfig_.segmentation;
        seg.manualThreshold = repos_.settings->getInt("det_threshold", -1).value();
        seg.polarity = static_cast<vision::SegmentationPolarity>(
            std::clamp(repos_.settings->getInt("det_polarity", 0).value(), 0, 2));
        seg.blurKernel = repos_.settings->getInt("det_blur", 5).value();
        seg.morphKernel = repos_.settings->getInt("det_morph", 5).value();
        // La separación de piezas que se tocan también se recuerda: es una
        // propiedad de CÓMO están colocadas las piezas en el puesto, no algo
        // que se decida cada vez.
        seg.splitTouchingPieces = repos_.settings->getInt("det_split_touching", 0).value() != 0;
        // Se guarda el NÚMERO, no un sí/no: el día que el nivel de aflojado sea
        // ajustable, lo que ya está guardado sigue queriendo decir lo mismo.
        seg.recoverHighlightsBy = repos_.settings->getInt("det_recover_glare", 0).value();
        // LA CLAVE DE COLOR DE FONDO ES UNA PROPIEDAD DEL PUESTO.
        //
        // El color de la mesa no cambia entre inspecciones, así que preguntarlo
        // cada vez sería preguntar por algo que ya se sabe. Se guarda el modo y
        // el color por separado: quien lo tenga en «lo busca solo» y un día pase
        // a «lo digo yo» se encuentra el último color que eligió, no un blanco.
        seg.backgroundKey = static_cast<vision::SegmentationOptions::BackgroundKey>(
            std::clamp(repos_.settings->getInt("det_background_key", 0).value(), 0, 2));
        seg.background = cv::Vec3b(
            static_cast<unsigned char>(
                std::clamp(repos_.settings->getInt("det_background_b", 255).value(), 0, 255)),
            static_cast<unsigned char>(
                std::clamp(repos_.settings->getInt("det_background_g", 255).value(), 0, 255)),
            static_cast<unsigned char>(
                std::clamp(repos_.settings->getInt("det_background_r", 255).value(), 0, 255)));
        pipelineConfig_.roi = cv::Rect(repos_.settings->getInt("det_roi_x", 0).value(),
                                       repos_.settings->getInt("det_roi_y", 0).value(),
                                       repos_.settings->getInt("det_roi_w", 0).value(),
                                       repos_.settings->getInt("det_roi_h", 0).value());
        // Modo «fija» sin zona guardada es un estado imposible de alcanzar hoy,
        // pero sí de heredar de una versión anterior. Sin esto el programa diría
        // que trabaja en una zona y estaría mirando la imagen entera.
        //
        // Aquí NO vale `modeAfterFixedZoneChanged`: esa función es para cuando
        // el operador acaba de dibujar, y forzaría «fija» al abrir. Si guardó
        // una zona y luego se pasó a automática, el modo guardado es el que
        // manda; lo único que se corrige es la incoherencia a la baja.
        if (zoneMode_ == vision::WorkingZoneMode::Fixed &&
            pipelineConfig_.roi.area() <= 0) {
            zoneMode_ = vision::WorkingZoneMode::Off;
        }
        pipelineConfig_.roiPolygon = decodeZonePolygon(
            repos_.settings->getString(kSettingFreeZone, std::string()).value());
        // Y lo mismo para la libre, por el mismo motivo: el modo guardado puede
        // apuntar a un dibujo que ya no está.
        if (zoneMode_ == vision::WorkingZoneMode::Free &&
            pipelineConfig_.roiPolygon.size() < 3) {
            zoneMode_ = vision::WorkingZoneMode::Off;
        }
        pixelReferenceSize_ = QSize(repos_.settings->getInt("det_zone_ref_w", 0).value(),
                                   repos_.settings->getInt("det_zone_ref_h", 0).value());
        pipelineConfig_.autoOrient = repos_.settings->getInt("track_rotation", 0).value() != 0;
        pipelineConfig_.subpixelEdges =
            repos_.settings->getInt("det_subpixel", 0).value() != 0;
        arucoLiveScale_ = repos_.settings->getInt("aruco_live", 0).value() != 0;
        markerSizeMm_ = repos_.settings->getDouble("aruco_marker_mm", 30.0).value();
    }
    updateRoiButton();

    // Controles de la cámara guardados (O2): se reaplican al abrirla. Solo se
    // recuerdan los que el operador tocó alguna vez.
    if (repos_.settings != nullptr) {
        for (const camera::CameraProperty property : camera::allCameraProperties()) {
            const std::string key(camera::propertyKey(property));
            if (auto stored = repos_.settings->getDouble(key, -1e9);
                stored.isOk() && stored.value() > -1e9) {
                savedCameraControls_.push_back({property, stored.value()});
            }
        }
        setupGuided_ = repos_.settings->getInt("setup_guided", 0).value() != 0;
        savedResolution_.width = repos_.settings->getInt("cam_width", 0).value();
        savedResolution_.height = repos_.settings->getInt("cam_height", 0).value();
    }

    // Tablero de referencia (T2): visibilidad y origen elegidos por el operador.
    if (repos_.settings != nullptr) {
        boardVisible_ = repos_.settings->getInt("board_visible", 0).value() != 0;
        boardConfig_.origin = vision::originFromKey(
            repos_.settings->getString("board_origin", std::string("bounds")).value());
        boardConfig_.followPieceAngle = repos_.settings->getInt("board_follow", 0).value() != 0;
        boardConfig_.fixedPoint = {
            static_cast<float>(repos_.settings->getDouble("board_fixed_x", 0.0).value()),
            static_cast<float>(repos_.settings->getDouble("board_fixed_y", 0.0).value())};
        boardConfig_.manualOffset = {
            static_cast<float>(repos_.settings->getDouble("board_offset_x", 0.0).value()),
            static_cast<float>(repos_.settings->getDouble("board_offset_y", 0.0).value())};
    }
    if (repos_.settings != nullptr) {
        rulerVisible_ = repos_.settings->getInt("ruler_visible", 0).value() != 0;
        // El realce se recuerda: quien inspecciona piezas negras las inspecciona
        // todos los días, y volver a encenderlo cada mañana es un impuesto.
        // El modelo de la lente que quedara guardado. Se carga SIEMPRE; que se
        // aplique o no es otra cosa, y va en su propio ajuste.
        if (auto stored = repos_.settings->getString("lens_model", ""); stored.isOk()) {
            if (auto model = vision::parseCalibration(stored.value()); model.has_value()) {
                lensCorrector_ = vision::LensCorrector(*model);
            }
        }
        const bool lensOn = repos_.settings->getInt("lens_enabled", 0).value() != 0;
        lensCorrectionOn_ = lensOn && lensCorrector_.isReady();
        if (lensCorrectionAction_ != nullptr) {
            lensCorrectionAction_->setEnabled(lensCorrector_.isReady());
            const QSignalBlocker block(lensCorrectionAction_);
            lensCorrectionAction_->setChecked(lensCorrectionOn_);
        }
        const bool enhance = repos_.settings->getInt("view_enhance", 0).value() != 0;
        if (viewEnhanceAction_ != nullptr) {
            viewEnhanceAction_->setChecked(enhance);
        }
        video_->setViewEnhance(enhance);
    }
    video_->setRulerVisible(rulerVisible_);
    video_->setBoardVisible(boardVisible_);
    video_->setBoardConfig(boardConfig_);
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }
    updateModeChip();  // el indicador arranca con el modo por defecto (M3)
    updateBoardReadout();

    buildCaptureDock();  // antes de restaurar la disposición, o no se colocaría
    // LOS ATAJOS, ANTES QUE LOS MENÚS, y el orden es el arreglo entero.
    //
    // Los atajos son `QAction` colgadas de la ventana. Para que una entrada de
    // menú ENSEÑE su tecla tiene que ser esa misma acción, no una gemela: dos
    // acciones con la misma secuencia en la misma ventana es
    // `ambiguousActivate`, y Qt no dispara ninguna de forma fiable. Este
    // proyecto ya se comió ese fallo con Ctrl+1 y Ctrl+2.
    //
    // Construir el menú primero obligaba a que la entrada se creara sola, y por
    // eso ninguna de las 58 enseñaba nada. `buildShortcuts` no depende de nada
    // de lo que hay debajo: solo crea acciones y lee las teclas guardadas.
    buildShortcuts();
    buildMenuBar();  // crea las acciones de menú (incluidas unidad y contorno)
    // El menú se construye DESPUÉS de la primera actualización de estado, así
    // que su acción de auto-inspección se quedaba sin el motivo que sí tenía el
    // botón: uno apagado con explicación y el otro vivo. Se pone al día aquí.
    updateAutoInspectAvailability();

    // Unidad de medida elegida por el operador (persistida).
    if (repos_.settings != nullptr) {
        // Se busca la acción POR SU VALOR, no por su posición en la lista. Eran
        // lo mismo mientras las dos listas coincidieran, y basta con insertar
        // una unidad en medio para que dejen de coincidir: quien tuviera
        // «píxeles» guardado se encontraría midiendo en otra cosa.
        const int unit = repos_.settings->getInt("length_unit", 0).value();
        for (auto* action : unitGroup_->actions()) {
            if (action->data().toInt() == unit) {
                action->setChecked(true);
                break;
            }
        }
    }
    video_->setLengthUnit(currentUnit());

    // Restaurar tamaño, posición, pantalla, maximizada y disposición de
    // paneles: la ventana se abre donde el operador la dejó (S3).
    restoreWindowLayout();

    // Un dock NUEVO sobre un estado guardado VIEJO: `restoreState` no sabe nada
    // de él —se guardó antes de que existiera— y lo deja donde le parece, que a
    // veces es oculto. Quien ya usaba el programa abriría la versión nueva sin
    // paleta y sin forma de adivinar que le falta un panel.
    //
    // Se comprueba DESPUÉS de restaurar y se coloca a mano si hace falta. Es el
    // mismo rigor que con las migraciones de esquema: no basta con que funcione
    // en un perfil limpio.
    if (toolsDock_ != nullptr && toolsDock_->isHidden()) {
        addDockWidget(Qt::RightDockWidgetArea, toolsDock_);
        toolsDock_->show();
        core::logInfo("El dock de herramientas no estaba en la disposición guardada: "
                      "se coloca a la derecha");
    }
    // La tira de capturas es un dock NUEVO, así que cae exactamente en el caso
    // que describe el párrafo de arriba: ninguna disposición guardada hasta hoy
    // sabe de ella. Sin esto, quien ya usaba el programa actualizaría y no la
    // vería nunca — y no tendría forma de adivinar que le falta un panel.
    if (captureDock_ != nullptr && captureDock_->isHidden()) {
        addDockWidget(Qt::LeftDockWidgetArea, captureDock_);
        captureDock_->show();
        core::logInfo("La tira de capturas no estaba en la disposición guardada: "
                      "se coloca a la izquierda");
    }

    refreshCameras();

    // Se vuelve a la pieza y a la plantilla con las que se estaba trabajando.
    // Sin esto, el combo caía siempre en la primera de la lista y en
    // «principal»: quien tiene veinte piezas registradas empezaba cada turno
    // buscando la suya.
    std::int64_t lastPiece = -1;
    QString lastTemplate;
    if (repos_.settings != nullptr) {
        lastPiece = repos_.settings->getInt("last_piece_id", -1).value();
        lastTemplate = QString::fromStdString(
            repos_.settings->getString("last_template", std::string()).value());
    }
    // Si la pieza se borró desde otra sesión, `loadPieceList` cae sola en la
    // primera: recordar una elección no puede impedir arrancar.
    loadPieceList(lastPiece);
    if (!lastTemplate.isEmpty()) {
        loadTemplateList(lastTemplate);
    }
    // Y la fuente elegida la última vez. Se PRESELECCIONA y nada más: la
    // cámara guardada tampoco arranca sola, y un programa que al abrirse se
    // pone a leer un fichero hace algo que nadie le ha pedido.
    if (repos_.settings != nullptr) {
        const auto kind = camera::sourceKindFromKey(
            repos_.settings->getString("last_source_kind", "camera").value().c_str());
        lastSourcePath_ = QString::fromStdString(
            repos_.settings->getString("last_source_file", std::string()).value());
        const int wanted = kind == camera::SourceKind::Image  ? kSourceOpenImage
                           : kind == camera::SourceKind::Video ? kSourceOpenVideo
                                                               : 0;
        if (wanted < 0) {
            if (const int index = cameraCombo_->findData(QVariant(wanted)); index >= 0) {
                cameraCombo_->setCurrentIndex(index);
            }
        }
    }

    // Al arrancar con una pieza ya seleccionada, su modo y su tablero mandan
    // sobre el ajuste global (M2); loadPieceList puede no disparar la señal.
    loadMeasurementForSelectedPiece();
    loadDetectionProfileForSelectedPiece();
}

inspection::LengthUnit MainWindow::currentUnit() const {
    const QAction* checked = unitGroup_->checkedAction();
    return static_cast<inspection::LengthUnit>(checked != nullptr ? checked->data().toInt()
                                                                  : 0);
}

std::string MainWindow::activeTemplate() const {
    const QString name = templateCombo_->currentText();
    return name.isEmpty() ? std::string("principal") : name.toStdString();
}

void MainWindow::onUnitChanged() {
    const inspection::LengthUnit unit = currentUnit();
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("length_unit", static_cast<int>(unit));
    }
    video_->setLengthUnit(unit);
    // Elegir mm/cm sin escala no hace nada visible: avisar una vez.
    if (unit != inspection::LengthUnit::Auto && unit != inspection::LengthUnit::Pixels &&
        !calibration_.valid()) {
        statusBar()->showMessage(
            tr("Para ver medidas en mm/cm primero calibra la escala "
               "(Medida ▸ Calibrar escala (mm)…)."));
    }
}

// Barra de menú: agrupa las acciones de baja frecuencia que antes saturaban
// las filas de botones. Las combos y botones de uso constante siguen visibles.
// QUÉ HACE CADA ENTRADA, dicho donde el operador la lee.
//
// Sale de una queja directa: «no son intuitivos ni coherentes; debería decirle
// al usuario qué hace cada cosa». Medido: 25 de las 40 entradas de menú no
// explicaban nada.
//
// Y algo peor — Qt NO ENSEÑA las ayudas de los menús salvo que se pida con
// `setToolTipsVisible`, y nadie lo había pedido. Así que las quince que SÍ
// estaban escritas tampoco se veían. Escribir explicaciones que el programa
// esconde es peor que no escribirlas: cuesta lo mismo y no ayuda a nadie.
//
// Las explicaciones van juntas y no repartidas por los sitios de construcción
// a propósito: así se leen todas de una vez y se ve si dos entradas se pisan o
// si una dice lo contrario que otra, que es de donde sale la sensación de
// incoherencia.
void MainWindow::explainMenus() {
    const std::pair<QString, QString> ayudas[] = {
        {tr("Exportar configuración…"),
         tr("Guarda en un fichero la puesta a punto de esta PC: cámara, detección,\nescala, tablero y preferencias.\n\nSirve para clonar un puesto que ya funciona en otra máquina de la línea\nsin repetir los ajustes a mano. NO se lleva las piezas ni su historial.")},
        {tr("Importar configuración…"),
         tr("Carga la puesta a punto guardada de otra PC. Sustituye los ajustes\nactuales; las piezas registradas y su historial se quedan como están.\n\nRevisa la escala después: si esta cámara está a otra altura, hay que\nvolver a calibrarla.")},
        {tr("Restablecer configuración de fábrica…"),
         tr("Devuelve todos los ajustes a como venían de fábrica. Pide confirmación.\n\nNo borra piezas, herramientas ni historial: solo los ajustes.")},
        {tr("Buscar cámaras de nuevo"),
         tr("Vuelve a preguntar al sistema qué cámaras hay conectadas.\n\nÚsalo si has enchufado una cámara con el programa ya abierto.")},
        {tr("Calibrar escala (mm)…"),
         tr("Le enseñas al programa cuánto mide un píxel, marcando con dos clics una\ndistancia que conoces (una regla, una moneda) y escribiendo cuánto mide.\n\nSin esto todas las medidas salen en píxeles. Hay que rehacerla si cambia\nla altura de la cámara o la resolución.")},
        {tr("Calibrar la lente…"),
         tr("Corrige la deformación del objetivo con fotos de un tablero de ajedrez.\n\nHace falta cuando la misma pieza mide distinto en el centro que en una\nesquina: sin corregir, la diferencia llega al 18 %.")},
        {tr("Unidad de medida"),
         tr("En qué unidad se enseñan las medidas: milímetros, centímetros, píxeles o\nautomática (mm o cm según el tamaño).\n\nPara ver milímetros hace falta haber calibrado la escala antes.")},
        {tr("Medir pieza"),
         tr("Mide la pieza que hay ahora delante con las herramientas dibujadas y\nenseña el resultado, sin guardarlo en el historial.\n\nEs la prueba de antes de inspeccionar: sirve para ver si las\nherramientas están donde tienen que estar.")},
        {tr("Modo de medición de la pieza…"),
         tr("Elige si esta pieza se juzga por sus medidas reales o por su posición\nrespecto al cero del tablero.\n\nVa con la pieza, no con la máquina.")},
        {tr("Automática (mm/cm)"),
         tr("Enseña milímetros en las medidas pequeñas y centímetros en las grandes,\npara no leer «1250,0 mm».")},
        {tr("Milímetros"),
         tr("Todas las medidas en milímetros. Necesita la escala calibrada.")},
        {tr("Centímetros"),
         tr("Todas las medidas en centímetros. Necesita la escala calibrada.")},
        {tr("Píxeles"),
         tr("Todas las medidas en píxeles de la imagen. Es lo que hay sin calibrar, y\nsirve para trabajar cuando no importa el tamaño real.")},
        {tr("Pulgadas"),
         tr("Todas las medidas en pulgadas. Necesita la escala calibrada.\n\n"
            "Se escriben con tres decimales: una pulgada son 25,4 mm, así que con\n"
            "dos la resolución sería de un cuarto de milímetro.")},
        {tr("Registrar con asistente…"),
         tr("Da de alta una pieza nueva paso a paso: capturas varias buenas y el\nprograma aprende cómo tiene que ser.\n\nCon eso puede avisar de piezas raras aunque no midas nada.")},
        {tr("Registrar otro acabado de esta pieza…"),
         tr("Añade un acabado admisible A LA MISMA pieza: otro proveedor, otro lote,\notro brillo.\n\nNo la registres otra vez con el asistente: eso crea una pieza distinta.\nY mezclar dos acabados en la misma referencia no da falsos NG, deja\nCIEGA la referencia — un defecto que se detectaba deja de detectarse.")},
        {tr("Gestionar piezas…"),
         tr("Renombrar, duplicar o borrar piezas registradas, y ver cuántas\nherramientas e inspecciones tiene cada una.")},
        {tr("Gestionar plantillas…"),
         tr("Las plantillas son juegos de herramientas de la misma pieza: una por\ncara, o una rápida y otra completa.\n\nAquí se crean, se renombran y se borran.")},
        {tr("Guardar plantilla"),
         tr("Guarda las herramientas que hay dibujadas ahora como plantilla de esta\npieza, para recuperarlas tal cual.")},
        {tr("Inspeccionar"),
         tr("Mide la pieza, da el veredicto OK/NG y lo GUARDA en el historial con su\nfoto.\n\nEs lo que diferencia una inspección de una prueba: queda registrada.")},
        {tr("Editor de plantilla…"),
         tr("Abre la pieza registrada a tamaño completo para dibujar sus herramientas\ncon calma, sin la cámara en marcha.")},
        {tr("Ver historial…"),
         tr("Todas las inspecciones guardadas de esta pieza, con su foto, su veredicto\ny por qué.\n\nDesde ahí se saca el informe del turno.")},
        {tr("Panel de herramientas"),
         tr("Enseña u oculta el panel lateral con las herramientas dibujadas y sus\ntolerancias.")},
        {tr("Panel de comparación"),
         tr("Enseña u oculta el panel que pone lado a lado la pieza registrada y la\nque hay ahora delante.")},
        {tr("Origen del tablero"),
         tr("Dónde está el punto cero desde el que se miden las posiciones: el centro\nde la pieza, un punto fijo de la imagen o el rasgo que marques.\n\nSolo afecta a las herramientas de Posición.")},
        {tr("Atajos de teclado…"),
         tr("La lista de teclas: zoom, paso a paso, cambiar de pieza, medir.")},
    };
    for (auto* menu : menuBar()->findChildren<QMenu*>()) {
        // Sin esto no se ve NINGUNA, ni las que ya estaban escritas.
        menu->setToolTipsVisible(true);
        for (auto* action : menu->actions()) {
            for (const auto& [nombre, ayuda] : ayudas) {
                if (action->text() == nombre) {
                    action->setToolTip(ayuda);
                }
            }
        }
    }
}

void MainWindow::buildMenuBar() {
    // Archivo: clonar la puesta a punto a otra PC de la línea (O4).
    auto* fileMenu = menuBar()->addMenu(tr("&Archivo"));
    fileMenu->addAction(tr("Exportar configuración…"), this,
                        &MainWindow::onExportConfigClicked);
    fileMenu->addAction(tr("Importar configuración…"), this,
                        &MainWindow::onImportConfigClicked);
    fileMenu->addSeparator();
    // Separada de las otras dos por lo que hace, no por estética: exportar e
    // importar copian ajustes de una máquina a otra; esta los borra.
    fileMenu->addAction(tr("Restablecer configuración de fábrica…"), this,
                        &MainWindow::onResetConfigClicked);

    auto* cameraMenu = menuBar()->addMenu(tr("&Fuente"));
    refreshAction_ = cameraMenu->addAction(tr("Buscar cámaras de nuevo"), this,
                                           &MainWindow::refreshCameras);
    cameraMenu->addSeparator();
    // Una sola entrada: los ajustes estaban repartidos en cuatro menús y para
    // cambiar el enfoque y el umbral había que saber en cuál vivía cada uno.
    configureAction_ = cameraMenu->addAction(tr("Configurar…"), this,
                                             &MainWindow::onConfigureClicked);
    configureAction_->setObjectName(QStringLiteral("configureAction"));
    configureAction_->setToolTip(
        tr("Cámara e imagen, detección, escala, preferencias y atajos, todo en el\n"
           "mismo sitio. Se abre sin bloquear el vídeo: lo que ajustes se ve al\n"
           "momento sobre la pieza."));
    // --- Medida ---
    //
    // Menú nuevo, y no por gusto de tener uno más: para preparar una medición en
    // milímetros había que visitar DOS menús que no hablan de medir. «Calibrar
    // escala» vivía en *Fuente*, junto a «Buscar cámaras», y «Unidad de medida»
    // en *Ver*, junto a «Mostrar contorno» — como si elegir milímetros o píxeles
    // fuera una cuestión de aspecto, cuando cambia el número que se apunta en el
    // parte.
    //
    // Lo que las une es la pregunta que contestan: con qué se mide. Quien busca
    // cualquiera de las dos va al mismo sitio.
    auto* measureMenu = menuBar()->addMenu(tr("&Medida"));
    if (auto* calibrate = shortcutAction(QStringLiteral("calibrate"),
                                        tr("Calibrar escala (mm)…"))) {
        measureMenu->addAction(calibrate);
    } else {
        measureMenu->addAction(tr("Calibrar escala (mm)…"), this,
                               &MainWindow::onCalibrateClicked);
    }
    measureMenu->addSeparator();
    measureMenu->addAction(tr("Calibrar la lente…"), this,
                           &MainWindow::onCalibrateLensClicked);
    lensCorrectionAction_ = measureMenu->addAction(tr("Corregir la distorsión de la lente"));
    lensCorrectionAction_->setCheckable(true);
    lensCorrectionAction_->setEnabled(false);  // hasta que haya un modelo
    lensCorrectionAction_->setToolTip(
        tr("Endereza lo que curva la lente, antes de medir.\n"
           "\n"
           "OJO: esto SÍ cambia las medidas, y es lo que se pretende. Una pieza ya\n"
           "registrada tiene sus tolerancias ajustadas contra el borde de antes,\n"
           "así que al encender esto hay que volver a mirarlas.\n"
           "\n"
           "Medido con una lente de gama de consumo: la misma pieza salía un 18,5 %\n"
           "más pequeña en una esquina que en el centro."));
    connect(lensCorrectionAction_, &QAction::toggled, this, [this](bool on) {
        lensCorrectionOn_ = on && lensCorrector_.isReady();
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("lens_enabled", lensCorrectionOn_ ? 1 : 0);
        }
        statusBar()->showMessage(
            lensCorrectionOn_
                ? tr("Distorsión de la lente corregida. Las medidas han cambiado: vuelve "
                     "a comprobar las tolerancias de las piezas registradas.")
                : tr("Corrección de la lente apagada."));
        reanalyseCurrentFrame();
    });
    measureMenu->addSeparator();
    auto* arucoAction = measureMenu->addAction(tr("Escala por marcador ArUco (en vivo)"));
    arucoAction->setCheckable(true);
    arucoAction->setChecked(arucoLiveScale_);
    arucoAction->setToolTip(
        tr("Pon un marcador ArUco (diccionario 4x4) de tamaño conocido junto a la\n"
           "pieza: la escala px→mm se recalcula en cada frame y se ajusta sola si\n"
           "acercas o alejas la cámara (marcador en el mismo plano)."));
    connect(arucoAction, &QAction::toggled, this, [this](bool on) {
        if (on) {
            bool ok = false;
            const double mm = QInputDialog::getDouble(
                this, tr("Marcador ArUco"), tr("Lado real del marcador (mm):"),
                markerSizeMm_, 1.0, 10000.0, 1, &ok);
            if (!ok) {
                // Revertir sin re-disparar la señal.
                QSignalBlocker blocker(sender());
                qobject_cast<QAction*>(sender())->setChecked(false);
                return;
            }
            markerSizeMm_ = mm;
        }
        arucoLiveScale_ = on;
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("aruco_live", on ? 1 : 0);
            repos_.settings->setDouble("aruco_marker_mm", markerSizeMm_);
        }
        statusBar()->showMessage(
            on ? tr("Escala por marcador ArUco activa (lado %1 mm).").arg(markerSizeMm_, 0, 'f', 1)
               : tr("Escala por marcador ArUco desactivada."));
        reanalyseCurrentFrame();
    });


    auto* unitMenu = measureMenu->addMenu(tr("Unidad de medida"));
    unitGroup_ = new QActionGroup(this);
    // El número es el valor del enum `LengthUnit`, no la posición en la lista.
    // Se guarda tal cual en los ajustes, así que tiene que seguir significando
    // lo mismo aunque la lista se reordene.
    const std::pair<QString, int> units[] = {
        {tr("Automática (mm/cm)"), 0}, {tr("Milímetros"), 1},
        {tr("Centímetros"), 2}, {tr("Píxeles"), 3}, {tr("Pulgadas"), 4}};
    for (const auto& [label, value] : units) {
        auto* action = unitMenu->addAction(label);
        action->setCheckable(true);
        action->setData(value);
        unitGroup_->addAction(action);
        if (value == 0) {
            action->setChecked(true);
        }
    }
    measureMenu->addSeparator();
    // La acción de la barra, también aquí: un botón que solo existe en la barra
    // no lo encuentra quien navega con el teclado, y a los menús se va justo
    // cuando no se reconoce el icono.
    if (auto* measure = shortcutAction(QStringLiteral("measure_piece"),
                                       tr("Medir pieza"))) {
        measureMenu->addAction(measure);
    } else {
        measureMenu->addAction(tr("Medir pieza"), this,
                               &MainWindow::onMeasurePieceClicked);
    }
    measureMenu->addAction(tr("Modo de medición de la pieza…"), this,
                           &MainWindow::onMeasurementModeClicked);

    auto* pieceMenu = menuBar()->addMenu(tr("&Pieza"));
    pieceMenu->addAction(tr("Registrar con asistente…"), this,
                                                 &MainWindow::onRegisterWizardClicked);
    // OTRO ACABADO DE LA MISMA PIEZA, y no una pieza nueva.
    //
    // Va justo debajo de «Registrar con asistente…» porque es la confusión que
    // hay que evitar: quien tiene delante la misma pieza con otro acabado
    // acabaría registrándola otra vez, y eso crea una pieza distinta con sus
    // herramientas y su historial aparte.
    pieceMenu->addAction(tr("Registrar otro acabado de esta pieza…"), this,
                         &MainWindow::onRegisterVariantClicked);
    pieceMenu->addAction(tr("Gestionar piezas…"), this,
                                               &MainWindow::onManagePiecesClicked);
    pieceMenu->addSeparator();
    // Las plantillas son de la pieza, así que sus acciones viven aquí y no solo
    // en la barra.
    pieceMenu->addAction(tr("Gestionar plantillas…"), this,
                         &MainWindow::onManageTemplatesClicked);
    if (auto* save = shortcutAction(QStringLiteral("save_template"),
                                    tr("Guardar plantilla"))) {
        pieceMenu->addAction(save);
    } else {
        pieceMenu->addAction(tr("Guardar plantilla"), this,
                             &MainWindow::onSaveTemplateClicked);
    }

    auto* inspectionMenu = menuBar()->addMenu(tr("&Inspección"));
    if (auto* inspect = shortcutAction(QStringLiteral("inspect_once"),
                                       tr("Inspeccionar"))) {
        inspectionMenu->addAction(inspect);
    } else {
        inspectionMenu->addAction(tr("Inspeccionar"), this, &MainWindow::onInspectClicked);
    }
    autoInspectAction_ = inspectionMenu->addAction(tr("Auto-inspección"));
    autoInspectAction_->setCheckable(true);
    // Espejo del botón de la barra, en los dos sentidos: si el menú dijera una
    // cosa y el botón otra, el operador no sabría cuál se cree.
    connect(autoInspectAction_, &QAction::toggled, this, [this](bool on) {
        if (autoInspectButton_ != nullptr && autoInspectButton_->isChecked() != on) {
            autoInspectButton_->setChecked(on);
        }
    });
    inspectionMenu->addSeparator();
    if (auto* editor = shortcutAction(QStringLiteral("template_editor"),
                                      tr("Editor de plantilla…"))) {
        inspectionMenu->addAction(editor);
    } else {
        inspectionMenu->addAction(tr("Editor de plantilla…"), this,
                                  &MainWindow::onOpenEditorClicked);
    }
    inspectionMenu->addAction(tr("Ver historial…"), this,
                              &MainWindow::onShowHistoryClicked);

    auto* viewMenu = menuBar()->addMenu(tr("&Ver"));
    showContourAction_ = viewMenu->addAction(tr("Mostrar contorno"));
    showContourAction_->setObjectName(QStringLiteral("showContourAction"));
    showContourAction_->setCheckable(true);
    // Era la ÚNICA capa del menú Ver que no se recordaba: el tablero, la regla
    // y el resto sí. Quien lo apagaba para inspeccionar con la pieza congelada
    // se lo encontraba encendido en cada arranque.
    //
    // Y el menú lo recordaba, pero el LIENZO no se enteraba, por dos motivos
    // encadenados: `setChecked(false)` sobre una acción que ya está en `false`
    // no emite `toggled`, y además el `connect` que lleva el valor al lienzo se
    // hacía DESPUÉS, así que aunque emitiera no había nadie escuchando. Como el
    // lienzo trae el contorno visible de fábrica, el menú decía «oculto» y el
    // contorno se seguía pintando encima del vídeo.
    //
    // Se arregla igual que las ayudas del pincel: el valor se empuja al lienzo
    // SIEMPRE. Recuperar un ajuste no puede depender de una señal que solo
    // salta cuando algo cambia.
    const bool contourVisible = repos_.settings == nullptr ||
                                repos_.settings->getInt("show_contour", 1).value() != 0;
    showContourAction_->setChecked(contourVisible);
    video_->setLiveContourVisible(contourVisible);
    showContourAction_->setToolTip(
        tr("Al ocultarlo, las herramientas se congelan en su sitio (la pieza se "
           "inspecciona fija, sin que nada se mueva)."));
    connect(showContourAction_, &QAction::toggled, video_,
            &inspection::EditorCanvas::setLiveContourVisible);
    connect(showContourAction_, &QAction::toggled, this, [this](bool on) {
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("show_contour", on ? 1 : 0);
        }
        reanalyseCurrentFrame();
    });

    // Un panel que se cierra sin forma de recuperarlo es una herramienta
    // perdida, así que el dock tiene su entrada en el menú igual que el de
    // comparación.
    if (toolsDock_ != nullptr) {
        auto* toggle = toolsDock_->toggleViewAction();
        toggle->setText(tr("Panel de herramientas"));
        viewMenu->addAction(toggle);
    }

    // Mostrar/ocultar el panel de comparación reubicable (S3).
    if (compareDock_ != nullptr) {
        auto* toggle = compareDock_->toggleViewAction();
        toggle->setText(tr("Panel de comparación"));
        viewMenu->addAction(toggle);
    }

    // El mosaico se ofrece solo la primera vez que hay varias piezas. Si el
    // operador lo cierra, no se le vuelve a abrir — por eso hace falta esta
    // entrada: sin ella, cerrarlo una vez sería cerrarlo para siempre.
    if (mosaicDock_ != nullptr) {
        auto* toggle = mosaicDock_->toggleViewAction();
        toggle->setText(tr("Piezas del encuadre (mosaico)"));
        toggle->setToolTip(
            tr("Enseña cada pieza recortada y numerada, todas al mismo tamaño. "
               "Pulsa una para medir esa."));
        viewMenu->addAction(toggle);
    }

    trackRotationAction_ = viewMenu->addAction(tr("Seguir rotación de la pieza"));
    trackRotationAction_->setCheckable(true);
    trackRotationAction_->setChecked(pipelineConfig_.autoOrient);
    trackRotationAction_->setToolTip(
        tr("Por defecto la pieza se muestra vertical (más estable). Actívalo solo si "
           "la pieza llega girada y quieres que las herramientas la sigan al rotar."));
    connect(trackRotationAction_, &QAction::toggled, this, [this](bool on) {
        pipelineConfig_.autoOrient = on;
        persistPipelineConfig();
        statusBar()->showMessage(on ? tr("Siguiendo la rotación de la pieza.")
                                    : tr("Pieza mostrada vertical (orientación fija)."));
    });

    // Tablero de referencia centrado (T2): overlay + elección de origen.
    viewMenu->addSeparator();
    boardAction_ = viewMenu->addAction(tr("Tablero de referencia (centro = 0)"));
    boardAction_->setCheckable(true);
    boardAction_->setChecked(boardVisible_);
    boardAction_->setToolTip(
        tr("Dibuja ejes y grilla con el CERO en el origen elegido, para medir\n"
           "la posición de la pieza (desviación en X/Y y ángulo) en vez de solo\n"
           "distancias sueltas. +X a la derecha, +Y hacia arriba."));
    connect(boardAction_, &QAction::toggled, this, [this](bool on) {
        boardVisible_ = on;
        video_->setBoardVisible(on);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("board_visible", on ? 1 : 0);
        }
        updateBoardReadout();
        statusBar()->showMessage(on ? tr("Tablero de referencia activo (centro = 0).")
                                    : tr("Tablero de referencia oculto."));
    });

    rulerAction_ = viewMenu->addAction(tr("Regla graduada"));
    rulerAction_->setCheckable(true);
    rulerAction_->setChecked(rulerVisible_);
    rulerAction_->setToolTip(
        tr("Reglas en los bordes con marcas y números en la unidad activa, barra de\n"
           "escala y marca de la posición del cursor. Sirve para leer una medida de\n"
           "un vistazo sin dibujar una herramienta."));
    connect(rulerAction_, &QAction::toggled, this, [this](bool on) {
        rulerVisible_ = on;
        video_->setRulerVisible(on);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("ruler_visible", on ? 1 : 0);
        }
    });

    // REALZAR PARA VER, que no es lo mismo que subir el brillo de la camara.
    //
    // La diferencia esta escrita en el propio texto de ayuda porque es la que
    // importa: los controles de «Camara e imagen» cambian el fotograma que se
    // ANALIZA —y con el, el umbral, la polaridad y todas las cotas—. Esto solo
    // cambia lo que se pinta. Un operador que no vea la pieza va a tocar lo
    // primero que encuentre, y conviene que lo primero que encuentre sea lo que
    // no le mueve las medidas.
    viewEnhanceAction_ = viewMenu->addAction(tr("Realzar la imagen para verla"));
    viewEnhanceAction_->setObjectName(QStringLiteral("viewEnhanceAction"));
    viewEnhanceAction_->setCheckable(true);
    viewEnhanceAction_->setToolTip(
        tr("Estira el contraste de lo que se ve en pantalla: una pieza oscura\n"
           "sobre fondo oscuro pasa a distinguirse.\n\n"
           "SOLO cambia lo que se pinta. Las medidas salen del fotograma tal\n"
           "como llega de la cámara, así que realzar no mueve ninguna cota.\n"
           "Si lo que quieres es arreglar la iluminación de verdad, eso está en\n"
           "Fuente ▸ Configurar…, pestaña Cámara e imagen — y eso sí cambia lo\n"
           "que se mide."));
    connect(viewEnhanceAction_, &QAction::toggled, this, [this](bool on) {
        video_->setViewEnhance(on);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("view_enhance", on ? 1 : 0);
        }
        if (!on) {
            statusBar()->showMessage(tr("Realce de vista apagado."));
        } else if (video_->viewEnhanceActive()) {
            statusBar()->showMessage(
                tr("Realce de vista activo. Solo cambia lo que se ve: las medidas "
                   "salen del fotograma original."));
        } else {
            // Decirlo, en vez de dejar al operador dudando de si el interruptor
            // hace algo.
            statusBar()->showMessage(
                tr("Realce de vista activo, pero esta imagen ya usa todo el rango: "
                   "no hay nada que estirar."));
        }
    });

    auto* boardMenu = viewMenu->addMenu(tr("Origen del tablero"));
    boardOriginGroup_ = new QActionGroup(this);
    const struct {
        QString label;
        QString tip;
        vision::BoardOrigin origin;
    } origins[] = {
        {tr("Automático: centro del contorno"),
         tr("Centra el cero en el centro geométrico de la pieza — el que se ve\n"
            "centrado. Es la opción recomendada para centrar automáticamente."),
         vision::BoardOrigin::PieceBounds},
        {tr("Automático: centro de masa"),
         tr("Centro de masa del contorno. En piezas asimétricas (una L, por\n"
            "ejemplo) queda visiblemente desplazado respecto al centro que se ve."),
         vision::BoardOrigin::PieceCenter},
        {tr("Automático: centro de la imagen"),
         tr("El cero queda fijo en pantalla: mide cuánto se desvía la pieza del centro\n"
            "del campo de visión (útil para centrarla en un soporte)."),
         vision::BoardOrigin::ImageCenter},
        {tr("Manual: punto fijado a mano…"),
         tr("Marca un punto de la imagen con el ratón y todo se mide respecto a él."),
         vision::BoardOrigin::FixedPoint},
    };
    for (const auto& entry : origins) {
        auto* action = boardMenu->addAction(entry.label);
        action->setCheckable(true);
        action->setToolTip(entry.tip);
        action->setData(static_cast<int>(entry.origin));
        action->setChecked(entry.origin == boardConfig_.origin);
        boardOriginGroup_->addAction(action);
    }
    connect(boardOriginGroup_, &QActionGroup::triggered, this, &MainWindow::onBoardOriginChanged);

    boardFollowAction_ = boardMenu->addAction(tr("Ejes girados con la pieza"));
    boardFollowAction_->setCheckable(true);
    boardFollowAction_->setChecked(boardConfig_.followPieceAngle);
    boardFollowAction_->setToolTip(
        tr("Activado: los ejes acompañan el giro de la pieza (se mide en su marco).\n"
           "Desactivado: los ejes quedan alineados con la imagen (marco de la máquina)."));
    connect(boardFollowAction_, &QAction::toggled, this, [this](bool on) {
        boardConfig_.followPieceAngle = on;
        video_->setBoardConfig(boardConfig_);
        persistBoardConfig();
        updateBoardReadout();
    });

    connect(unitGroup_, &QActionGroup::triggered, this, &MainWindow::onUnitChanged);

    auto* helpMenu = menuBar()->addMenu(tr("A&yuda"));
    if (auto* guide = shortcutAction(QStringLiteral("shortcuts_help"),
                                     tr("Atajos de teclado…"))) {
        helpMenu->addAction(guide);
    } else {
        helpMenu->addAction(tr("Atajos de teclado…"), this, &MainWindow::onShowShortcuts);
    }
    // Lo último: las explicaciones se ponen cuando ya existen todas las
    // entradas, y así vale con un solo sitio en vez de veinticinco.
    explainMenus();
}

void MainWindow::persistPipelineConfig() {
    if (repos_.settings == nullptr) {
        return;
    }
    const auto& seg = pipelineConfig_.segmentation;
    repos_.settings->setInt("det_threshold", seg.manualThreshold);
    repos_.settings->setInt("det_polarity", static_cast<int>(seg.polarity));
    repos_.settings->setInt("det_blur", seg.blurKernel);
    repos_.settings->setInt("det_morph", seg.morphKernel);
    repos_.settings->setInt("det_split_touching", seg.splitTouchingPieces ? 1 : 0);
    repos_.settings->setInt("det_recover_glare", seg.recoverHighlightsBy);
    repos_.settings->setInt("det_background_key", static_cast<int>(seg.backgroundKey));
    repos_.settings->setInt("det_background_b", seg.background[0]);
    repos_.settings->setInt("det_background_g", seg.background[1]);
    repos_.settings->setInt("det_background_r", seg.background[2]);
    repos_.settings->setInt("det_roi_x", pipelineConfig_.roi.x);
    repos_.settings->setInt("det_roi_y", pipelineConfig_.roi.y);
    repos_.settings->setInt("det_roi_w", pipelineConfig_.roi.width);
    repos_.settings->setInt("det_roi_h", pipelineConfig_.roi.height);
    repos_.settings->setString(kSettingFreeZone,
                               encodeZonePolygon(pipelineConfig_.roiPolygon));
    // Y a qué resolución están expresados los píxeles de arriba.
    persistPixelReference();
    repos_.settings->setDouble("det_min_area", pipelineConfig_.minAreaFraction);
    repos_.settings->setDouble("det_max_area", pipelineConfig_.maxAreaFraction);
    repos_.settings->setInt("track_rotation", pipelineConfig_.autoOrient ? 1 : 0);
    repos_.settings->setInt("det_subpixel", pipelineConfig_.subpixelEdges ? 1 : 0);
}

// A qué resolución están expresados los ajustes que van en PÍXELES: la zona de
// trabajo y el cero del tablero.
//
// Sin esto, esos ajustes se guardaban en píxeles a secas y se volvían a aplicar
// tal cual. Una zona dibujada sobre 1920×1080 y reabierta con una fuente de
// 640×480 señala otro sitio: recortada contra el frame se queda en un trozo que
// nadie eligió, o desaparece entera. En silencio, que es lo peor.
//
// Va en su propia función porque lo llaman los DOS que guardan píxeles. Estaba
// sólo en el de la zona, y quien tuviera puesto un cero de tablero y ninguna
// zona no guardaba referencia alguna: el arreglo no podía ni dispararse.
//
// `isValid()` no sirve para comprobarlo: en Qt, `QSize(0, 0).isValid()` es true
// —sólo exige que no sean negativos—, así que un ajuste ausente, que se lee
// como cero, pasaba por bueno.
//
// Y sin condición sobre lo que ya hubiera: al persistir, los ajustes están
// SIEMPRE en coordenadas del frame actual —o acaban de ponerse, o acaban de
// reajustarse a él—, así que la referencia es el frame actual y punto.
void MainWindow::persistPixelReference() {
    if (repos_.settings == nullptr) {
        return;
    }
    if (!lastFrame_.isNull()) {
        pixelReferenceSize_ = lastFrame_.size();
    }
    repos_.settings->setInt("det_zone_ref_w", pixelReferenceSize_.width());
    repos_.settings->setInt("det_zone_ref_h", pixelReferenceSize_.height());
}

void MainWindow::updateRoiButton() {
    updateWorkingZoneOverlay();
    if (zoneButton_ == nullptr) {
        return;
    }
    const bool hasRect = pipelineConfig_.roi.area() > 0;
    const bool hasFree = pipelineConfig_.roiPolygon.size() >= 3;

    // Las dos acciones de dibujar NO son marcables, y eso costó un fallo.
    //
    // Marcarlas parecía buena idea —enseñar cuál está activa— pero una acción
    // marcable se marca AL PULSARLA, y pulsar «Dibujar zona rectangular» solo
    // empieza el gesto: todavía no hay zona. El menú quedaba afirmando que
    // había una, y si el operador no llegaba a arrastrar, seguía mintiendo.
    //
    // El estado se lee donde no puede desincronizarse: en el texto del propio
    // botón, que se pone desde la configuración y no desde el clic.
    rectZoneAction_->setCheckable(false);
    freeZoneAction_->setCheckable(false);
    clearZoneAction_->setEnabled(hasRect || hasFree);
    // Deshabilitado CON MOTIVO: un «Quitar» vivo sin nada que quitar enseña a
    // desconfiar de los menús.
    clearZoneAction_->setToolTip(hasRect || hasFree
                                     ? tr("Vuelve a analizar la imagen entera.")
                                     : tr("No hay ninguna zona dibujada."));
    // El botón dice qué zona está EN USO, no cuál hay guardada, y la diferencia
    // es justo el fallo que tenía: con una libre guardada y una rectangular
    // dibujada después seguía diciendo «Zona libre» mientras actuaba la otra; y
    // con el modo en automática decía que había zona cuando no se aplicaba
    // ninguna.
    //
    // Quien manda es el MODO, que es lo mismo que decide qué se recorta de
    // verdad. Leerlo de otro sitio es como se llega a que la barra afirme una
    // cosa y el análisis haga otra.
    switch (zoneMode_) {
        case vision::WorkingZoneMode::Fixed:
            zoneButton_->setText(hasRect ? tr("Zona fija") : tr("Zona"));
            break;
        case vision::WorkingZoneMode::Free:
            zoneButton_->setText(hasFree ? tr("Zona libre") : tr("Zona"));
            break;
        case vision::WorkingZoneMode::Automatic:
            // La automática no la dibuja el operador: sigue a la pieza sola. Se
            // nombra para que no parezca que no hay ninguna.
            zoneButton_->setText(tr("Zona auto"));
            break;
        case vision::WorkingZoneMode::Off:
            zoneButton_->setText(tr("Zona"));
            break;
    }
}

void MainWindow::declareExpectedPieces(int expected) {
    expectedPieces_ = std::max(0, expected);
    pipelineConfig_.expectedPieces = expectedPieces_;
    updatePiecesChip();
    reanalyseCurrentFrame();
}

bool MainWindow::countingPieces() const {
    // La pieza declara que espera varias: el recuento es parte del veredicto y
    // se cuenta siempre.
    if (expectedPieces_ > 1) {
        return true;
    }
    // UNA PIEZA DECLARADA A MANO ES UNA PIEZA, Y SE DEJA DE ENUMERAR.
    //
    // Esta línea sale de una queja de uso: «por defecto le tengo una pieza, e
    // intenta detectar más de una». Era exacto, y de dos maneras a la vez: la
    // regla del final de esta función manda contar por defecto, y con el
    // recuento en marcha cualquier sombra o reflejo que pase el filtro de área
    // sale como una segunda pieza; y como el número esperado también era 1, esa
    // sombra daba directamente NG «esperaba 1, veo 2».
    //
    // Quien pone «manual, una pieza» está diciendo que lo que hay en la mesa es
    // una pieza. Se mide la mayor y se acabó.
    //
    // Esto NO deshace la razón por la que contar venía puesto —seis piezas
    // delante y la aplicación midiendo la mayor sin decir que había otras
    // cinco—: ese caso es el modo AUTOMÁTICO, que ahora es un botón con su
    // nombre en la pestaña Piezas en vez de un cero mágico escondido dentro de
    // un campo numérico.
    if (expectedPieces_ == 1 &&
        !(configureDialog_ != nullptr && configureDialog_->showingPieceCount())) {
        return false;
    }
    // O el operador está mirando la pestaña Piezas. Antes bastaba con que el
    // panel Configurar estuviera abierto, y eso era demasiado: contar cuesta
    // una segmentación multi-pieza y además suelta el recorte automático, así
    // que abrir la pestaña de la Cámara pagaba las dos cosas para nadie. Peor
    // aún en la de Rendimiento, que es donde se enciende la zona automática:
    // el operador la encendía y la veía apagada, por su propia culpa de estar
    // mirándola.
    if (configureDialog_ != nullptr && configureDialog_->showingPieceCount()) {
        return true;
    }
    // Y, por defecto, SIEMPRE que el recorte automático no esté en juego.
    //
    // La regla de antes dejaba fuera el caso normal: seis piezas delante del
    // operador, ajustes de fábrica, y la aplicación midiendo la mayor sin decir
    // en ningún sitio que había otras cinco. «Solo detecta una» era literal.
    //
    // La justificación era que contar cuesta una segmentación multi-pieza.
    // Medido sobre 1920x1080 con seis piezas: 7,5 ms quedarse con la mayor,
    // 11,2 ms contarlas todas. Son 3,7 ms de 33 que dura un frame a 30 fps —no
    // es un coste, es ruido.
    //
    // Lo que sí es real es la otra mitad: contar SUELTA el recorte automático
    // (rodea a la pieza mayor, así que contar dentro daría siempre uno). Por eso
    // ahí se respeta la regla anterior y sólo se cuenta si alguien lo pide.
    return zoneMode_ != vision::WorkingZoneMode::Automatic;
}

cv::Rect MainWindow::effectiveWorkingZone() const {
    return vision::effectiveWorkingZone(zoneMode_, pipelineConfig_.roi, autoRoi_.roi(),
                                        countingPieces(), pipelineConfig_.roiPolygon);
}

vision::PipelineConfig MainWindow::inspectionConfig() const {
    vision::PipelineConfig config = pipelineConfig_;
    config.roiPolygon =
        vision::effectiveWorkingPolygon(zoneMode_, pipelineConfig_.roiPolygon);
    return config;
}

core::Result<vision::PieceAnalysis> MainWindow::analyseMeasuredPiece(
    const cv::Mat& image) const {
    // Con el navegador en cero se toma el camino de UNA pieza, que es el mismo
    // de siempre. No es una optimización de adorno: `analyzeFrames` analiza
    // TODAS las manchas que pasan el filtro de área, y este camino corre
    // también cuando solo hay una pieza en la mesa.
    if (focusedPiece_ < 1) {
        return vision::analyzeFrame(image, inspectionConfig());
    }
    auto all = vision::analyzeFrames(image, inspectionConfig());
    if (!all.isOk()) {
        return core::Result<vision::PieceAnalysis>::err(all.error().message);
    }
    if (all.value().empty()) {
        return core::Result<vision::PieceAnalysis>::err("No se detectó ninguna pieza");
    }
    const std::size_t chosen = vision::measuredPieceIndex(all.value(), focusedPiece_);
    return core::Result<vision::PieceAnalysis>::ok(std::move(all.value()[chosen]));
}

void MainWindow::setWorkingZoneMode(vision::WorkingZoneMode mode) {
    if (mode == zoneMode_) {
        return;
    }
    zoneMode_ = mode;
    // Al cambiar de modo se olvida lo seguido hasta ahora: reanudar con un
    // recorte viejo mediría dentro de una ventana que ya no corresponde.
    autoRoi_.reset();
    if (repos_.settings != nullptr) {
        repos_.settings->setString("work_zone_mode",
                                   vision::workingZoneModeKey(mode));
    }
    // El modo puede cambiar sin tocar el panel (al dibujar o quitar la zona),
    // así que si está abierto se le pone al día. `showMode` no reemite.
    if (configureDialog_ != nullptr) {
        if (auto* page = configureDialog_->performancePage(); page != nullptr) {
            page->showMode(zoneMode_, pipelineConfig_.roi.area() > 0,
                           pipelineConfig_.roiPolygon.size() >= 3);
        }
    }
    updateWorkingZoneOverlay();
}

// La zona que se está procesando, dibujada sobre el vídeo. Sin esto, el
// operador no tiene forma de saber por dónde está mirando el programa cuando
// algo falla.
void MainWindow::updateWorkingZoneOverlay() {
    const auto polygon =
        vision::effectiveWorkingPolygon(zoneMode_, pipelineConfig_.roiPolygon);
    const cv::Rect zone = effectiveWorkingZone();
    // Con la zona libre en uso se pinta el polígono y NO su envolvente: el
    // recuadro es solo cómo se recorta por dentro, y dibujarlo diría que se
    // analiza un rectángulo que el operador rechazó a propósito.
    video_->setFreeZone(!polygon.empty(), polygon);
    video_->setDetectionRegion(polygon.empty() && zone.area() > 0, zone);
}

void MainWindow::applyPiecesPage(PiecesPage* page) {
    if (page == nullptr) {
        return;
    }
    // POR LA MISMA PUERTA QUE TODO LO DEMÁS.
    //
    // Antes esto solo asignaba `expectedPieces_` y se olvidaba de la mitad: no
    // tocaba la configuración del pipeline ni pedía reanalizar, así que cambiar
    // el número en la ventana no cambiaba NADA hasta el siguiente fotograma que
    // llegara por otro motivo — y con una imagen parada, nunca.
    //
    // Lo peor es por qué no saltó: `declareExpectedPieces` sí lo hacía bien, y
    // era la que usaban las pruebas. Un camino de prueba que funciona mientras
    // el camino de verdad no, y las dos con el mismo nombre en la cabeza de
    // quien las escribió. Ahora hay uno solo.
    declareExpectedPieces(page->expectedPieces());
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.pieces == nullptr) {
        statusBar()->showMessage(
            tr("Selecciona una pieza para guardar cuántas se esperan: el número va "
               "con el trabajo, no con la máquina."));
        return;
    }
    // Se lee y se reescribe la medición entera: es una sola fila y así no hay
    // dos caminos distintos para tocar las columnas de la pieza.
    auto measurement = repos_.pieces->loadMeasurement(pieceId);
    if (!measurement.isOk()) {
        core::logWarning("No se pudo leer la medición de la pieza: " +
                         measurement.error().message);
        return;
    }
    measurement.value().expectedPieces = expectedPieces_;
    showMosaic_ = page->showMosaic();
    measurement.value().showMosaic = page->showMosaic();
    if (auto saved = repos_.pieces->saveMeasurement(pieceId, measurement.value());
        !saved.isOk()) {
        core::logWarning("No se pudieron guardar las piezas esperadas: " +
                         saved.error().message);
    }
}

// Vuelca la página de detección del panel Configurar en la configuración viva.
void MainWindow::applyDetectionPage(DetectionPage* page) {
    if (page == nullptr) {
        return;
    }
    pipelineConfig_.segmentation = page->options();
    pipelineConfig_.minAreaFraction = page->minAreaFraction();
    pipelineConfig_.maxAreaFraction = page->maxAreaFraction();

    // Encender o apagar el afinado subpíxel NO es un ajuste más: cambia dónde
    // está el borde, y con él el área, el perímetro y todas las cotas de la
    // pieza a la vez. Quien tenga tolerancias ajustadas contra el borde de antes
    // vería una pieza buena salir NG por un cambio de definición y no por un
    // defecto.
    //
    // Así que se avisa, y solo cuando CAMBIA. Repetir el aviso cada vez que se
    // aceptan los ajustes de detección lo convertiría en algo que se cierra sin
    // leer, que es peor que no avisar.
    const bool subpixelBefore = pipelineConfig_.subpixelEdges;
    pipelineConfig_.subpixelEdges = page->subpixelEdges();
    if (pipelineConfig_.subpixelEdges != subpixelBefore) {
        QMessageBox::information(
            this, tr("Ha cambiado la definición del borde"),
            pipelineConfig_.subpixelEdges
                ? tr("El borde pasa a afinarse a subpíxel: en vez de caer donde lo puso el "
                     "umbral, cada punto se coloca donde el brillo cruza la mitad entre el "
                     "nivel de dentro y el de fuera.\n\n"
                     "Las medidas de la pieza —área, perímetro y todas las cotas— cambian "
                     "un poco a partir de ahora, porque el borde ya no está en el mismo "
                     "sitio.\n\n"
                     "Si tienes tolerancias ajustadas, REVÍSALAS: una pieza buena podría "
                     "salir NG por este cambio y no por un defecto.\n\n"
                     "Y hay una contrapartida que conviene saber: gana EXACTITUD y pierde "
                     "un poco de REPETIBILIDAD. Cada punto del borde se coloca mejor, pero "
                     "recoge el ruido de su propio sitio en vez de quedarse pegado a la "
                     "rejilla de píxeles, y el perímetro suma todos esos puntos.\n\n"
                     "Medido sobre seis piezas reales, moviendo la imagen fracciones de "
                     "píxel: el perímetro se mueve algo más en cinco de las seis (por "
                     "ejemplo de 0,59 %% a 1,02 %%). Las tolerancias se juzgan con la "
                     "repetibilidad, así que si tu cota es de perímetro, mira si te "
                     "compensa.")
                : tr("El borde vuelve a ser el que marca el umbral.\n\n"
                     "Las medidas cambian un poco respecto a las de ahora mismo. Si "
                     "ajustaste tolerancias con el afinado encendido, revísalas."));
    }
    persistPipelineConfig();

    // El perfil elegido se guarda CON LA PIEZA: cada pieza puede necesitar una
    // iluminación distinta y así no hay que reajustar al cambiar de una a otra.
    currentProfileId_ = page->selectedProfileId();
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.detectionProfiles != nullptr) {
        if (auto saved = repos_.detectionProfiles->assignToPiece(pieceId, currentProfileId_);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el perfil de la pieza: " +
                             saved.error().message);
        }
    }
    statusBar()->showMessage(
        currentProfileId_ > 0
            ? tr("Ajustes de detección aplicados y guardados en el perfil de la pieza.")
            : tr("Ajustes de detección aplicados: el contorno en vivo ya los usa."));
    reanalyseCurrentFrame();
}

// Perfil de detección de la pieza seleccionada (O3): si tiene uno, sus ajustes
// mandan sobre los globales; si no, todo sigue como antes.
// LA PÁGINA DE DETECCIÓN, SI CONFIGURAR ESTÁ ABIERTO.
//
// Va aquí y no en `loadMeasurementForSelectedPiece` por el ORDEN: aquella corre
// antes y deja `pipelineConfig_` y `currentProfileId_` todavía con lo de la
// pieza anterior. Poner al día la página desde allí la llenaría con lo viejo,
// que es exactamente el fallo que se vino a quitar.
void MainWindow::refreshDetectionPageForSelectedPiece() {
    if (configureDialog_ == nullptr) {
        return;
    }
    if (auto* page = configureDialog_->detectionPage(); page != nullptr) {
        page->reloadFor(pipelineConfig_.segmentation, currentProfileId_,
                        pipelineConfig_.minAreaFraction, pipelineConfig_.maxAreaFraction,
                        pipelineConfig_.subpixelEdges);
    }
}

void MainWindow::loadDetectionProfileForSelectedPiece() {
    currentProfileId_ = 0;
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.detectionProfiles == nullptr) {
        refreshDetectionPageForSelectedPiece();
        return;
    }
    auto assigned = repos_.detectionProfiles->profileForPiece(pieceId);
    if (!assigned.isOk() || assigned.value() <= 0) {
        // Sin perfil asignado también hay que poner al día la página: se
        // trabaja con los ajustes sueltos, y los de la pieza nueva no tienen
        // por qué ser los que se quedó enseñando de la anterior.
        refreshDetectionPageForSelectedPiece();
        return;
    }
    auto profile = repos_.detectionProfiles->load(assigned.value());
    if (!profile.isOk()) {
        refreshDetectionPageForSelectedPiece();
        return;  // perfil borrado a mano: se sigue con los ajustes globales
    }
    currentProfileId_ = profile.value().id;
    pipelineConfig_.segmentation = profile.value().options;
    refreshDetectionPageForSelectedPiece();
    statusBar()->showMessage(tr("Detección: perfil '%1' de esta pieza.")
                                 .arg(QString::fromStdString(profile.value().name)));
    reanalyseCurrentFrame();
}

// Quitar la zona que haya, sea la que sea. Va aparte porque ahora es una acción
// con su propio nombre en el menú, y no «volver a pulsar el botón de dibujar»,
// que obligaba a que la etiqueta cambiara de verbo para avisar.
void MainWindow::onClearZoneClicked() {
    const bool hadRect = pipelineConfig_.roi.area() > 0;
    const bool hadFree = pipelineConfig_.roiPolygon.size() >= 3;
    if (!hadRect && !hadFree) {
        return;
    }
    pipelineConfig_.roi = cv::Rect();
    pipelineConfig_.roiPolygon.clear();
    if (hadRect) {
        setWorkingZoneMode(vision::modeAfterFixedZoneChanged(zoneMode_, false));
    }
    if (hadFree) {
        setWorkingZoneMode(vision::modeAfterFreeZoneChanged(zoneMode_, false));
    }
    persistPipelineConfig();
    updateRoiButton();
    // Cambiar la zona sin volver a medir deja el contorno de la zona anterior:
    // sobre una foto no llega ningún frame nuevo que fuerce el recálculo.
    reanalyseCurrentFrame();
    statusBar()->showMessage(tr("Zona quitada: se vuelve a analizar la imagen entera."));
}

void MainWindow::onRoiButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setRegionPickMode(false);
        return;
    }
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("Inicia la cámara para dibujar la zona de detección."));
        return;
    }
    video_->setRegionPickMode(true);
    statusBar()->showMessage(
        tr("Arrastra un rectángulo sobre el video: la detección se limitará a esa zona."));
}

void MainWindow::onRegionPicked(const cv::Rect& imageRect) {
    pipelineConfig_.roi = imageRect;
    // Dibujar la zona la PONE EN USO (la regla vive en `vision::auto_roi`).
    setWorkingZoneMode(vision::modeAfterFixedZoneChanged(zoneMode_, true));
    persistPipelineConfig();
    updateRoiButton();
    reanalyseCurrentFrame();
    statusBar()->showMessage(
        tr("Zona de detección activa: el contorno solo se busca dentro del recuadro."));
}

namespace {

// TODO LO QUE LA MISMA FIGURA PUEDE MEDIR, no solo lo que el operador eligió.
//
// Cinco clases de herramienta llevan un selector de medida —Región, Ranura,
// Chaflán, Acuerdo y Extremos—, y al dibujarlas se escoge UNA. Las demás salen
// de la misma figura, con el mismo trazo y el mismo fixture: lo único que
// cambia es el campo del enum. Que estuvieran escondidas obligaba a dibujar una
// segunda región encima de la primera para ver su perímetro.
//
// Se ejecutan de verdad en vez de dejar la fila en blanco porque una lista de
// medidas sin sus valores no ayuda a decidir cuál vigilar, que es justo para lo
// que está.
//
// Devuelve vacío para las herramientas de una sola medida —un calibre mide una
// distancia y nada más—, y entonces la pestaña no las despliega.
std::vector<PieceReportDialog::DrawnTool::OtherMeasure> everyMeasureOfThatFigure(
    const inspection::ToolConfig& config, const cv::Mat& image,
    const vision::Fixture& fixture, const vision::BoardFrame& board,
    double mmPerPixel, inspection::LengthUnit unit) {
    const auto parsed = inspection::geometryFromJson(config.type, config.geometryJson);
    if (!parsed.isOk()) {
        return {};
    }
    const inspection::ToolGeometry& geometry = parsed.value();
    const auto choices = inspection::measureChoicesOf(geometry);
    if (choices.options.size() < 2) {
        return {};
    }

    // TODAS DE UNA VEZ, incluida la que ya mide: `runTools` recorre la imagen
    // una sola vez por lote, así que pedirlas juntas cuesta menos que una
    // llamada por medida — y la que ya mide se vuelve a ejecutar aquí a
    // propósito, para que las seis filas salgan del mismo instante y no haya
    // una copiada de otro sitio que pueda no cuadrar.
    std::vector<inspection::ToolConfig> siblings;
    siblings.reserve(choices.options.size());
    for (const auto& option : choices.options) {
        inspection::ToolGeometry copy = geometry;
        if (!inspection::setMeasureChoice(copy, option.value)) {
            continue;
        }
        inspection::ToolConfig sibling = config;
        sibling.enabled = true;
        sibling.geometryJson = inspection::toJson(copy);
        // Nombre propio para poder emparejar el resultado: si las seis se
        // llamaran igual, cualquiera podría pasar por cualquiera.
        sibling.name = config.name + " · " + option.label;
        siblings.push_back(std::move(sibling));
    }

    const auto results = inspection::runTools(image, fixture, siblings, mmPerPixel, unit,
                                              cv::Mat(), &board);

    std::vector<PieceReportDialog::DrawnTool::OtherMeasure> out;
    out.reserve(choices.options.size());
    for (std::size_t i = 0; i < choices.options.size() && i < siblings.size(); ++i) {
        PieceReportDialog::DrawnTool::OtherMeasure other;
        other.label = choices.options[i].label;
        other.value = choices.options[i].value;
        other.isTheOneItMeasures = choices.options[i].value == choices.current;
        other.text = "—";
        for (const auto& result : results) {
            if (result.name == siblings[i].name) {
                other.text = inspection::formatMeasure(result, mmPerPixel, unit, true);
                break;
            }
        }
        out.push_back(std::move(other));
    }
    return out;
}

}  // namespace

void MainWindow::onMeasurePieceClicked() {
    const QImage frame = frameOrFile();
    if (frame.isNull()) {
        statusBar()->showMessage(
            tr("No hay imagen que medir: inicia una fuente o abre una imagen."));
        return;
    }

    // Se mide con la MISMA configuración con la que se inspecciona —zona
    // incluida— para que el informe hable de lo mismo que el veredicto. Si
    // midiera el frame entero mientras la detección trabaja dentro de una zona,
    // los dos números serían de piezas distintas.
    const cv::Mat image = camera::qImageToMat(frame);
    // LA PIEZA QUE EL OPERADOR ESTÁ MIRANDO, no la mayor del encuadre.
    //
    // Esto llamaba a `analyzeFrame`, que devuelve siempre la mayor. Con el
    // navegador puesto en la pieza 3, el vídeo dibujaba las cotas sobre la 3, el
    // rótulo decía «Midiendo la pieza 3 de 5» — y este botón abría el informe de
    // otra pieza. Dos partes de la misma pantalla hablando de cosas distintas, y
    // ninguna de las dos avisando.
    //
    // Es también la queja literal: con varias piezas «toma una medición para
    // todas las piezas, en lugar de una medición independiente por pieza». Aquí
    // no había forma de pedir la de una en concreto.
    const auto analysis = analyseMeasuredPiece(image);
    if (!analysis.isOk()) {
        statusBar()->showMessage(tr("No se puede medir: %1")
                                     .arg(QString::fromStdString(analysis.error().message)));
        return;
    }

    // Con los agujeros de vuelta: la máscara que devuelve el análisis viene
    // rellena, y sin esto una arandela se mediría como un disco.
    const cv::Mat mask = vision::pieceMaskWithHoles(image, analysis.value().mask,
                                                    inspectionConfig().segmentation);
    // Con el TAMAÑO DEL ENCUADRE: sin él, el informe no puede saber si la pieza
    // está cortada por el borde, y entonces publicaría sus cotas como medidas
    // cuando son límites inferiores.
    auto report = inspection::measureWholePiece(image, mask,
                                                analysis.value().fixture,
                                                calibration_.mmPerPixel, currentUnit(),
                                                image.size());
    // CUÁNTO SE MOVERÍA ESTO SI LA LUZ CAMBIARA UN POCO.
    //
    // Queja del taller: «la manera en que toma los contornos varía mucho por su
    // sombra y la luz de enfrente, y estar midiendo mal». Es cierto y se puede
    // poner número: se barre el corte de gris unos niveles a cada lado y se mira
    // cuánto se mueve el ancho.
    //
    // Va AQUÍ y no en el vídeo porque cuesta un análisis por nivel. En este
    // botón, que ya cuesta dos, se paga sin que se note; por fotograma sería
    // pagar nueve veces para decir casi siempre que no pasa nada.
    //
    // Y solo se dice cuando pasa: ocho de once fotos del banco se quedan por
    // debajo del 0,5 % y no ven este aviso nunca. Uno que saliera siempre se
    // aprendería a ignorar.
    if (const auto stability = vision::measureStability(image, inspectionConfig());
        stability.measured &&
        stability.swingFraction >= vision::kMeasurementMovesWithTheLight) {
        report.warnings.push_back(stability.summary);
    }
    if (!report.ok) {
        statusBar()->showMessage(QString::fromStdString(report.problem));
        return;
    }

    // LAS HERRAMIENTAS DEL OPERADOR, medidas sobre ESTA misma pieza.
    //
    // Hasta ahora este botón no las enseñaba: dibujabas cinco cotas, pulsabas
    // «Medir pieza» y veías hechos del contorno y propuestas automáticas, pero
    // ninguna de las tuyas. Para verlas había que inspeccionar — que además
    // guarda en el historial, o sea dos decisiones distintas en un botón.
    //
    // Se miden con el MISMO fixture con el que se acaba de medir la pieza, para
    // que las dos pestañas hablen de la misma imagen y del mismo instante.
    std::vector<inspection::ToolConfig> configs;
    configs.reserve(liveTools_.size());
    for (const auto& tool : liveTools_) {
        auto config = tool.config;
        config.geometryJson = inspection::toJson(tool.geometry);
        configs.push_back(std::move(config));
    }
    std::vector<PieceReportDialog::DrawnTool> drawn;
    // El mismo orden que `drawn`, que NO es el de `configs`: las filas se
    // construyen recorriendo resultados. El diálogo devuelve índices sobre lo
    // que se le dio, así que hay que poder volver desde ahí.
    std::vector<inspection::ToolConfig> drawnOrder;
    if (!configs.empty()) {
        const vision::BoardFrame board = vision::resolveBoardFrame(
            boardConfig_, analysis.value().fixture, true, image.size());
        // TODAS, también las desmarcadas: la pestaña tiene que poder enseñar
        // qué mediría una cota apagada, que es justo lo que hace falta para
        // decidir si volver a encenderla.
        auto all = configs;
        for (auto& config : all) {
            config.enabled = true;
        }
        const auto results =
            inspection::runTools(image, analysis.value().fixture, all,
                                 calibration_.mmPerPixel, currentUnit(), cv::Mat(), &board);
        drawn.reserve(results.size());
        for (const auto& result : results) {
            for (const auto& config : configs) {
                if (config.id != result.toolId || config.name != result.name) {
                    continue;
                }
                PieceReportDialog::DrawnTool entry;
                entry.config = config;
                entry.result = result;
                entry.text = inspection::formatMeasure(result, calibration_.mmPerPixel,
                                                       currentUnit(), true);
                entry.alsoMeasures =
                    everyMeasureOfThatFigure(config, image, analysis.value().fixture, board,
                                             calibration_.mmPerPixel, currentUnit());
                drawnOrder.push_back(config);
                drawn.push_back(std::move(entry));
                break;
            }
        }
    }

    PieceReportDialog dialog(report, currentSourceLabel(), repos_.settings, this,
                             std::move(drawn));
    const int answer = dialog.exec();
    // LOS INTERRUPTORES SE GUARDAN AUNQUE SE CIERRE SIN «vigilar».
    //
    // Apagar una cota y vigilar unas propuestas son decisiones independientes:
    // atar la primera a que se pulse el botón de la segunda perdería el cambio
    // que el operador acaba de hacer, sin decirle nada.
    if (const auto changed = dialog.toolsWithChangedState(); !changed.empty()) {
        int saved = 0;
        for (const auto& config : changed) {
            for (auto& tool : liveTools_) {
                if (tool.config.id == config.id && tool.config.name == config.name) {
                    tool.config.enabled = config.enabled;
                    ++saved;
                }
            }
            // `save` hace UPDATE cuando la herramienta ya tiene id, y la
            // columna `enabled` va dentro: no hace falta un método aparte.
            if (repos_.tools != nullptr && config.id >= 0 && selectedPieceId() >= 0) {
                if (auto ok = repos_.tools->save(selectedPieceId(), config,
                                                 activeTemplate());
                    !ok.isOk()) {
                    core::logWarning("No se pudo guardar el interruptor de la "
                                     "herramienta: " + ok.error().message);
                }
            }
        }
        video_->update();
        statusBar()->showMessage(
            tr("%1 cota(s) cambiaron de estado: las desmarcadas dejan de medirse y de "
               "pesar en el veredicto.")
                .arg(saved));
        reanalyseCurrentFrame();
    }
    // LAS MEDIDAS HERMANAS MARCADAS EN EL SEGUNDO NIVEL.
    //
    // Se atienden aunque se cierre sin «vigilar», por lo mismo que los
    // interruptores: es una decisión que el operador ya tomó dentro de la
    // pestaña, y descartarla porque salió por otra puerta la perdería en
    // silencio.
    if (const auto extra = dialog.measuresToAdd(); !extra.empty()) {
        int born = 0;
        for (const auto& want : extra) {
            if (want.fromTool < 0 ||
                want.fromTool >= static_cast<int>(drawnOrder.size())) {
                continue;
            }
            const inspection::ToolConfig& from =
                drawnOrder[static_cast<std::size_t>(want.fromTool)];
            auto parsed = inspection::geometryFromJson(from.type, from.geometryJson);
            if (!parsed.isOk()) {
                continue;
            }
            inspection::ToolGeometry geometry = parsed.value();
            if (!inspection::setMeasureChoice(geometry, want.measureValue)) {
                continue;
            }
            const std::string newName = from.name + " · " + want.label;
            // NO DOS VECES LA MISMA. El nombre es determinista, así que volver a
            // marcarla en una segunda consulta crearía una cota gemela con otro
            // id — que es exactamente lo que hacía «Vigilar estas cotas» antes de
            // que se arreglara.
            bool alreadyThere = false;
            for (const auto& existing : liveTools_) {
                if (existing.config.name == newName) {
                    alreadyThere = true;
                    break;
                }
            }
            if (alreadyThere) {
                continue;
            }
            inspection::EditedTool tool;
            tool.geometry = geometry;
            tool.config = from;
            tool.config.id = -1;  // nace sin guardar: la plantilla le dará el suyo
            tool.config.name = newName;
            // SIN TOLERANCIA, que es lo que la pestaña prometía: nace midiendo y
            // sin juzgar hasta que alguien le declare la banda. Heredar la del
            // padre sería peor que no poner ninguna — un perímetro dentro de la
            // banda de un área es una conformidad inventada.
            tool.config.toleranceMin = 0.0;
            tool.config.toleranceMax = 1e9;
            tool.config.enabled = true;
            liveTools_.push_back(std::move(tool));
            ++born;
        }
        if (born > 0) {
            commitUndoState();
            video_->clearResults();
            video_->setSelectedIndex(static_cast<int>(liveTools_.size()) - 1);
            statusBar()->showMessage(
                tr("%n medida(s) añadidas sobre las figuras que ya tenías. Nacen sin "
                   "tolerancia: decláresela para que puedan no cumplir.",
                   nullptr, born));
            reanalyseCurrentFrame();
        }
    }
    if (answer != QDialog::Accepted) {
        return;
    }
    const auto watch = dialog.toWatch();
    if (watch.empty()) {
        return;
    }
    for (const auto& proposal : watch) {
        inspection::EditedTool tool;
        tool.geometry = proposal.geometry;
        tool.config = proposal.config;
        tool.config.id = -1;  // sin guardar todavía: la plantilla decide su id
        liveTools_.push_back(std::move(tool));
    }
    // Todas de una vez y UN solo estado de deshacer: quitar veinte herramientas
    // con veinte Ctrl+Z sería peor que no haberlas puesto.
    commitUndoState();
    video_->clearResults();
    video_->setSelectedIndex(static_cast<int>(liveTools_.size()) - 1);
    statusBar()->showMessage(
        tr("%n cota(s) añadidas como herramientas. Guarda la plantilla para "
           "conservarlas.",
           nullptr, static_cast<int>(watch.size())));
}

void MainWindow::onFreeZoneButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setFreeZonePickMode(false);
        return;
    }
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("Inicia la fuente para dibujar la zona libre."));
        return;
    }
    video_->setFreeZonePickMode(true);
    statusBar()->showMessage(
        tr("Rodea la zona arrastrando el ratón, o marca las esquinas a clics y cierra "
           "sobre la primera. Botón derecho: deshacer el último vértice."));
}

void MainWindow::onFreeZonePicked(const std::vector<cv::Point>& imagePolygon) {
    pipelineConfig_.roiPolygon = imagePolygon;
    setWorkingZoneMode(vision::modeAfterFreeZoneChanged(zoneMode_, true));
    persistPipelineConfig();
    updateRoiButton();
    reanalyseCurrentFrame();
    // El número de vértices no es decoración: es lo que permite notar que un
    // trazo de doce esquinas se guardó con cuatro, o al revés.
    statusBar()->showMessage(tr("Zona libre activa (%1 vértices): se analiza solo lo de "
                                "dentro, y lo de fuera se oscurece.")
                                 .arg(imagePolygon.size()));
}

void MainWindow::onFreeZoneCancelled() {
    updateRoiButton();
    statusBar()->showMessage(tr("Zona libre cancelada: sigue la de antes."));
}

// Acciones con atajo configurable: el valor por defecto puede sobreescribirse
// desde la guía (F1) y persiste en Settings ("key_<id>").
QAction* MainWindow::shortcutAction(const QString& id, const QString& menuText) {
    // LA MISMA ACCIÓN, NO UNA GEMELA.
    //
    // Ninguna de las 58 entradas de menú enseñaba su atajo, y el arreglo obvio
    // —`setShortcut` en la entrada— es el equivocado: los atajos ya son
    // `QAction` invisibles colgadas de la ventana, así que poner la misma tecla
    // en la entrada del menú da DOS acciones con la misma secuencia en la misma
    // ventana. Eso es `ambiguousActivate`: Qt no dispara ninguna de forma
    // fiable. Este proyecto ya se comió ese fallo con Ctrl+1 y Ctrl+2.
    //
    // Así que la entrada del menú no se crea: se cuelga la que ya existe. Una
    // sola acción, un solo atajo, y el menú lo enseña solo porque Qt pinta la
    // secuencia de la acción que le den.
    //
    // Se le cambia el texto al del menú —«Calibrar escala (mm)…» en vez de
    // «Calibrar mm…»— y eso no toca la guía de atajos, que enseña
    // `ShortcutSpec::description` y no el texto de la acción. Son dos sitios
    // con dos públicos: el menú se lee de pasada, la guía se lee buscando.
    for (const auto& spec : shortcuts_) {
        if (spec.id == id && spec.action != nullptr) {
            if (!menuText.isEmpty()) {
                spec.action->setText(menuText);
            }
            return spec.action;
        }
    }
    // Sin la acción no se deja el menú cojo: el llamante pone la entrada de
    // siempre. Devolver nulo y que alguien lo cuelgue sería una entrada vacía.
    return nullptr;
}

void MainWindow::buildShortcuts() {
    auto addShortcut = [this](const QString& id, const QString& description,
                              const QKeySequence& defaultKey, auto slot) {
        auto* action = new QAction(description, this);
        QKeySequence key = defaultKey;
        if (repos_.settings != nullptr) {
            const auto saved =
                repos_.settings->getString(("key_" + id).toStdString(), std::string());
            if (saved.isOk() && !saved.value().empty()) {
                key = QKeySequence(QString::fromStdString(saved.value()));
            }
        }
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        shortcuts_.push_back({id, description, defaultKey, action});
    };

    addShortcut("undo", tr("Deshacer (herramientas dibujadas)"), QKeySequence::Undo,
                &MainWindow::onUndo);
    addShortcut("redo", tr("Rehacer"), QKeySequence::Redo, &MainWindow::onRedo);
    addShortcut("delete_tool", tr("Borrar la herramienta seleccionada"),
                QKeySequence(Qt::Key_Delete), &MainWindow::onDeleteToolClicked);
    addShortcut("select_mode", tr("Modo Mover/Elegir (cancela dibujo y rasgo)"),
                QKeySequence(Qt::Key_Escape), [this] {
                    anchorButton_->setChecked(false);
                    toolPalette_->activate(std::nullopt);
                });

    // Atajos por FAMILIA + dígito, generados de las propias familias. Antes
    // había una tabla escrita a mano con un dígito por herramienta, y se quedó
    // corta: con catorce herramientas y diez dígitos, Arco, Eje, Rosca y
    // Engranaje no tenían tecla.
    int familyNumber = 0;
    for (const inspection::ToolCategory category : inspection::allToolCategories()) {
        if (inspection::toolsInCategory(category).empty()) {
            continue;
        }
        ++familyNumber;
        addShortcut(QStringLiteral("tool_family_%1").arg(familyNumber),
                    tr("Familia: %1").arg(QString::fromUtf8(
                        inspection::categoryLabel(category))),
                    QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + familyNumber)),
                    [this, category] { toolPalette_->activateCategory(category); });
    }
    for (int slot = 1; slot <= 9; ++slot) {
        addShortcut(QStringLiteral("tool_slot_%1").arg(slot),
                    tr("Herramienta %1 de la familia activa").arg(slot),
                    QKeySequence(static_cast<Qt::Key>(Qt::Key_0 + slot)),
                    [this, slot] { toolPalette_->activateInCurrentCategory(slot - 1); });
    }

    addShortcut("camera_toggle", tr("Iniciar/Detener cámara"), QKeySequence(Qt::Key_V),
                [this] {
                    if (startStopButton_->isEnabled()) {
                        onStartStopClicked();
                    }
                });
    addShortcut("register_live", tr("Registrar y activar"), QKeySequence(Qt::Key_R),
                &MainWindow::onRegisterLiveClicked);
    addShortcut("auto_inspect", tr("Auto-inspección (alternar)"), QKeySequence(Qt::Key_A),
                [this] { autoInspectButton_->toggle(); });
    addShortcut("inspect_once", tr("Inspeccionar una vez"), QKeySequence(Qt::Key_I),
                &MainWindow::onInspectClicked);
    addShortcut("template_editor", tr("Abrir Plantilla…"), QKeySequence(Qt::Key_P),
                &MainWindow::onOpenEditorClicked);
    addShortcut("calibrate", tr("Calibrar mm…"), QKeySequence(Qt::Key_C),
                &MainWindow::onCalibrateClicked);
    addShortcut("anchor", tr("Marcar rasgo distintivo"), QKeySequence(Qt::Key_D),
                [this] { anchorButton_->toggle(); });
    addShortcut("duplicate_tool", tr("Duplicar la herramienta seleccionada"),
                QKeySequence(Qt::CTRL | Qt::Key_D), &MainWindow::onDuplicateToolClicked);
    addShortcut("save_template", tr("Guardar la plantilla (herramientas en vivo)"),
                QKeySequence::Save, &MainWindow::onSaveTemplateClicked);
    // MEDIR PIEZA TIENE TECLA, y la tiene por una razón concreta.
    //
    // El taller pidió «otra ventana en donde al momento de realizar las
    // mediciones te mostrara los cálculos, como área, perímetro, etc.». Esa
    // ventana existe desde hace tiempo: es este botón, y da perímetro, área,
    // agujeros, circularidad, diámetros y las cotas dibujadas, con copiar y
    // exportar.
    //
    // Que alguien pida algo que ya está es la mejor prueba posible de que no se
    // encuentra. Y no se encuentra porque es uno de trece botones del mismo
    // peso: destacarlo también sería quitarle el sitio al de inspeccionar, que
    // es el único destacado a propósito.
    //
    // Así que se hace descubrible por donde se descubren las cosas: el menú, que
    // desde hace poco enseña la tecla de cada entrada. La M es la letra natural
    // y estaba libre — A, C, D, I, P, R y V ya tienen dueño.
    addShortcut("measure_piece", tr("Medir la pieza (área, perímetro, cotas)"),
                QKeySequence(Qt::Key_M), &MainWindow::onMeasurePieceClicked);
    addShortcut("shortcuts_help", tr("Guía de atajos"), QKeySequence(Qt::Key_F1),
                &MainWindow::onShowShortcuts);

    // Vista (Z3). ZoomIn cubre Ctrl++ y Ctrl+= (la misma tecla sin Shift).
    addShortcut("zoom_in", tr("Acercar la vista"), QKeySequence::ZoomIn,
                [this] { video_->zoomIn(); });
    addShortcut("zoom_out", tr("Alejar la vista"), QKeySequence::ZoomOut,
                [this] { video_->zoomOut(); });
    addShortcut("zoom_fit", tr("Ajustar la vista a la ventana (zoom mínimo)"),
                QKeySequence(Qt::CTRL | Qt::Key_0), [this] { video_->zoomToMin(); });
    // CTRL+1 Y CTRL+2 YA ESTABAN COGIDOS.
    //
    // Las cinco familias de herramientas se reparten Ctrl+1 … Ctrl+5 más arriba,
    // y aquí se volvían a pedir Ctrl+1 para «vista al 100 %» y Ctrl+2 para
    // «zoom máximo». Dos acciones con la misma secuencia en la misma ventana no
    // se reparten el turno: Qt emite `ambiguousActivate` y NO dispara ninguna de
    // forma fiable. Los dos atajos estaban documentados en F1 y ninguno de los
    // cuatro hacía lo que decía.
    //
    // Se mueven a Ctrl+Alt, que no choca con nada, y se dejan al lado de
    // Ctrl+0 —que sí está libre— en vez de repartirlos por otras teclas: los
    // tres son «encuadre», y encuadrar con tres modificadores distintos sería
    // otra forma de lo mismo.
    addShortcut("zoom_actual", tr("Vista al 100 % (píxeles reales)"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1),
                [this] { video_->zoomToActualPixels(); });
    addShortcut("zoom_max", tr("Zoom máximo"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2),
                [this] { video_->zoomToMax(); });
}

// Porcentaje visible y estado de los botones de la barra de zoom.
void MainWindow::updateZoomIndicator() {
    const double scale = video_->displayScale();
    const bool hasImage = scale > 0.0;
    zoomLabel_->setText(hasImage ? tr("%1 %").arg(qRound(scale * 100.0))
                                 : QStringLiteral("—"));
    zoomLabel_->setEnabled(hasImage);
    zoomMinButton_->setEnabled(hasImage && !video_->atMinZoom());
    zoomOutButton_->setEnabled(hasImage && !video_->atMinZoom());
    zoomInButton_->setEnabled(hasImage && !video_->atMaxZoom());
    zoomMaxButton_->setEnabled(hasImage && !video_->atMaxZoom());
}

void MainWindow::onShowShortcuts() {
    ShortcutsDialog dialog(&shortcuts_, repos_.settings, this);
    keepDialogSize(dialog, repos_.settings, "shortcuts", 560, 620);
    dialog.exec();
}

// --- Deshacer / rehacer sobre las herramientas dibujadas ---

void MainWindow::commitUndoState() {
    undoStack_.push(stableTools_);
    stableTools_ = liveTools_;
    templateDirty_ = true;  // toda mutación de herramientas deja la plantilla sucia
}

void MainWindow::restoreTools(std::vector<inspection::EditedTool> tools) {
    liveTools_ = std::move(tools);
    stableTools_ = liveTools_;
    templateDirty_ = true;  // deshacer/rehacer también cambia el estado guardado
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    video_->update();
}

void MainWindow::onUndo() {
    // Con el pincel en la mano, deshacer es deshacer la pincelada. Es lo que
    // espera quien lo está usando, y evita tener dos atajos que hay que acertar.
    if (video_ != nullptr && video_->edgeBrush() != inspection::EditorCanvas::EdgeBrush::Off &&
        video_->undoEdgeCorrection()) {
        statusBar()->showMessage(tr("Pincelada deshecha."));
        return;
    }
    if (auto previous = undoStack_.undo(liveTools_)) {
        restoreTools(std::move(*previous));
        statusBar()->showMessage(tr("Deshecho."));
    }
}

void MainWindow::onRedo() {
    if (video_ != nullptr && video_->edgeBrush() != inspection::EditorCanvas::EdgeBrush::Off &&
        video_->redoEdgeCorrection()) {
        statusBar()->showMessage(tr("Pincelada rehecha."));
        return;
    }
    if (auto next = undoStack_.redo(liveTools_)) {
        restoreTools(std::move(*next));
        statusBar()->showMessage(tr("Rehecho."));
    }
}

// Si la auto-inspección se puede encender AHORA, y si no, por qué no.
//
// Antes esto se comprobaba DESPUÉS de pulsar y se contestaba con un
// `QMessageBox` modal: el operador encendía el conmutador, le saltaba un diálogo
// diciendo que no, y el conmutador se apagaba solo. Tres pasos para enterarse de
// algo que se podía ver antes de tocar nada.
//
// Y el modal tenía un segundo coste, que apareció al escribir su test: sin
// pantalla bloquea para siempre, así que el banco se colgó cinco minutos hasta
// que hubo que matar el proceso. Un control que no se puede probar es un control
// que nadie va a probar.
//
// Ahora está apagado con su motivo en el tooltip, que es lo que este proyecto ya
// hace en los botones de borrar: se lee antes de pulsar, y se puede comprobar.
void MainWindow::updateAutoInspectAvailability() {
    if (autoInspectButton_ == nullptr) {
        return;
    }
    QStringList missing;
    if (repos_.engine == nullptr) {
        missing << tr("no hay motor de inspección");
    }
    if (selectedPieceId() < 0) {
        missing << tr("no hay ninguna pieza registrada seleccionada");
    }
    if (!streaming_) {
        missing << tr("no hay ninguna fuente en marcha");
    }
    // Mientras está en marcha no se deshabilita: apagar el conmutador tiene que
    // seguir siendo posible aunque la fuente se haya caído, o la auto-inspección
    // quedaría encendida sin forma de pararla.
    const bool usable = missing.isEmpty() || autoInspecting_;
    const QString why =
        missing.isEmpty()
            ? tr("Inspecciona continuamente el vídeo contra la pieza seleccionada.")
            : tr("No se puede empezar todavía: %1.").arg(missing.join(tr(", ")));
    autoInspectButton_->setEnabled(usable);
    autoInspectButton_->setToolTip(why);
    if (autoInspectAction_ != nullptr) {
        autoInspectAction_->setEnabled(usable);
        autoInspectAction_->setToolTip(why);
    }
}

void MainWindow::updateStatusIndicators() {
    updateAutoInspectAvailability();
    updateEdgeBrushAvailability();
    // Punto de color + leyenda por indicador (rich text: sin assets externos).
    auto set = [](QLabel* label, const QString& caption, bool ok, const QString& okText,
                  const QString& badText) {
        const QString color =
            ok ? QString(theme::kGood) : QString(theme::kBad);
        label->setText(QStringLiteral("<span style='color:%1'>&#9679;</span> %2")
                           .arg(color, caption));
        label->setToolTip(ok ? okText : badText);
    };

    // El indicador dice QUÉ fuente está viva, no solo que hay una. «Cám» en
    // verde mientras se analiza una fotografía sería exacto en el color y falso
    // en la palabra, y el color por sí solo nunca debe cargar con el
    // significado.
    switch (sourceKind_) {
        case camera::SourceKind::Camera:
            set(camIndicator_, tr("Cám"), streaming_, tr("Cámara: transmitiendo"),
                tr("Cámara: detenida"));
            break;
        case camera::SourceKind::Photo:
            set(camIndicator_, tr("Foto"), streaming_,
                tr("Fuente: una foto congelada de esta cámara. La escala calibrada sigue "
                   "valiendo."),
                tr("Sin fuente"));
            break;
        case camera::SourceKind::Image:
            set(camIndicator_, tr("Img"), streaming_,
                tr("Fuente: una imagen de archivo. Todo se mide igual que en vivo."),
                tr("Sin fuente"));
            break;
        case camera::SourceKind::Video:
            set(camIndicator_, tr("Víd"), streaming_,
                tr("Fuente: un vídeo de archivo, en bucle."), tr("Sin fuente"));
            break;
    }
    set(dbIndicator_, tr("BD"), repos_.pieces != nullptr,
        tr("Base de datos: conectada"),
        tr("Base de datos: no disponible (sin persistencia)"));
    set(modelIndicator_, tr("ONNX"), static_cast<bool>(repos_.embedFn),
        tr("Modelo de embeddings: cargado"),
        tr("Modelo ONNX: no disponible (inspección solo con herramientas)"));
}

void MainWindow::updateCalibrationLabel() {
    // La tira se refresca aquí y no aparte: este método ya se llama en todos
    // los sitios donde cambia cualquiera de los cuatro datos que enseña
    // —calibrar, cambiar de cámara, tocar un automático, mover la zona—, así
    // que engancharse a él es engancharse a todos de una vez.
    updateStationStatus();
    updateSetupGuide();
    // La escala por ArUco gestiona su propia etiqueta por frame (es dinámica).
    if (arucoLiveScale_) {
        return;
    }
    if (!calibration_.valid()) {
        calibLabel_->setText(tr("Sin calibrar (medidas en px)"));
        return;
    }
    // D1: la calibración se hizo a una resolución y cámara concretas. Si el
    // frame actual no coincide, la escala en px ya no es fiable: se avisa en vez
    // de mostrar milímetros silenciosamente equivocados.
    const bool resMismatch =
        !lastFrame_.isNull() &&
        !calibration_.matchesResolution(lastFrame_.width(), lastFrame_.height());
    const bool camMismatch = !calibratedCameraKey_.isEmpty() &&
                             !currentCameraKey_.isEmpty() &&
                             calibratedCameraKey_ != currentCameraKey_;
    if (resMismatch || camMismatch) {
        // «Otra cámara» dejó de ser cierto en cuanto una imagen o un vídeo
        // pueden ser la fuente, y el motivo es lo único que le dice al operador
        // si tiene que recalibrar o si puede fiarse. La escala en px/mm depende
        // de la óptica y de la distancia al plano, y un fichero no garantiza
        // ninguna de las dos: al abrirlo, la escala de la estación deja de
        // valer, aunque la imagen tenga el mismo tamaño.
        QString why;
        if (resMismatch) {
            why = tr("otra resolución");
        } else if (sourceKind_ != camera::SourceKind::Camera &&
                   sourceKind_ != camera::SourceKind::Photo) {
            why = tr("la escala se calibró con otra fuente y un fichero no dice a qué "
                     "distancia se tomó");
        } else {
            why = tr("otra cámara");
        }
        calibLabel_->setText(tr("⚠ Calibración obsoleta (%1) — recalibra con C").arg(why));
        return;
    }
    // Escala calibrada + automático encendido es la combinación que da números
    // creíbles y falsos, y el sitio donde hay que decirlo es este: junto a la
    // escala que el operador se está creyendo, no en una pestaña que no abrirá.
    const std::string warning =
        camera::automaticsWarning(true, autoExposureOn_, autoFocusOn_);
    if (!warning.empty()) {
        calibLabel_->setText(tr("⚠ Escala: %1 mm/px — %2")
                                 .arg(calibration_.mmPerPixel, 0, 'f', 4)
                                 .arg(QString::fromStdString(warning)));
        return;
    }
    calibLabel_->setText(tr("Escala: %1 mm/px · cámara ~%2 mm")
                             .arg(calibration_.mmPerPixel, 0, 'f', 4)
                             .arg(calibration_.cameraDistanceMm, 0, 'f', 0));
}

// Sella la cámara actual y persiste toda la calibración (incluida la resolución
// que el llamante ya fijó en calibration_). Base común de los dos flujos de
// calibración (diálogo y "fijar con esta medida").
void MainWindow::persistCalibration() {
    calibratedCameraKey_ = currentCameraKey_;
    if (repos_.settings == nullptr) {
        return;
    }
    repos_.settings->setDouble("calib_mm_per_px", calibration_.mmPerPixel);
    repos_.settings->setDouble("calib_camera_dist_mm", calibration_.cameraDistanceMm);
    repos_.settings->setDouble("calib_fov_deg", calibration_.horizontalFovDeg);
    repos_.settings->setInt("calib_width", calibration_.calibratedWidth);
    repos_.settings->setInt("calib_height", calibration_.calibratedHeight);
    repos_.settings->setString("calib_camera", calibratedCameraKey_.toStdString());
}

void MainWindow::onCalibrateClicked() {
    const QImage snapshot = frameOrFile();
    if (snapshot.isNull()) {
        return;
    }
    // LA REFERENCIA QUE SE ESCRIBIÓ LA VEZ ANTERIOR.
    //
    // Es lo único que hay que teclear cada vez que se calibra, y era lo único
    // que no se recordaba: el campo volvía a 100 mm aunque la regla del puesto
    // midiera 6 pulgadas. La distancia de cámara y el FOV sí se recuperaban, lo
    // que lo hacía aún más difícil de entender.
    ScaleEntry last;
    if (repos_.settings != nullptr) {
        const double saved = repos_.settings->getDouble("scale_known_length", 0.0).value();
        if (saved > 0.0) {
            last.knownLength = saved;
        }
        last.unitIndex = repos_.settings->getInt("scale_known_unit", 0).value();
    }
    CalibrationDialog dialog(snapshot, calibration_, last, currentUnit(), this);
    keepDialogSize(dialog, repos_.settings, "calibration", 1000, 640);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (repos_.settings != nullptr) {
        const ScaleEntry entered = dialog.lastEntry();
        repos_.settings->setDouble("scale_known_length", entered.knownLength);
        repos_.settings->setInt("scale_known_unit", entered.unitIndex);
    }
    calibration_ = dialog.calibration();
    calibration_.calibratedWidth = snapshot.width();
    calibration_.calibratedHeight = snapshot.height();
    persistCalibration();
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);
    statusBar()->showMessage(
        tr("Escala calibrada: las medidas ahora se muestran también en mm."));
}

MainWindow::~MainWindow() {
    autoTimer_.stop();
    captureTimer_.stop();
    controller_.stop();
    enumerationWatcher_.waitForFinished();
    analysisWatcher_.waitForFinished();
    inspectionWatcher_.waitForFinished();
    captureWatcher_.waitForFinished();
}

// --- Cámara y análisis -----------------------------------------------------

void MainWindow::refreshCameras() {
    if (enumerationWatcher_.isRunning()) {
        return;
    }
    setControlsEnabled(false);
    cameraCombo_->clear();
    cameraCombo_->addItem(tr("Buscando cámaras…"));
    statusBar()->showMessage(tr("Buscando cámaras conectadas…"));

    enumerationWatcher_.setFuture(
        QtConcurrent::run([] { return camera::CameraEnumerator::enumerate(); }));
}

void MainWindow::onCamerasEnumerated() {
    cameras_ = enumerationWatcher_.result();

    // CON LAS SEÑALES BLOQUEADAS mientras se repuebla, y esto costó caro.
    //
    // `clear()` seguido de `addItem()` mueve el índice de -1 a 0, y Qt emite
    // `currentIndexChanged`. Desde que se puede cambiar de fuente en marcha,
    // esa señal se lee como «han elegido la cámara 0»: paraba el fichero que
    // estuviera abierto y arrancaba la cámara.
    //
    // El resultado, para quien lo sufre: abres una imagen o un vídeo nada más
    // arrancar y, cuando termina la enumeración de cámaras —que va en segundo
    // plano y tarda lo suyo—, tu fichero se cierra solo. Igual al pulsar
    // «Actualizar». Nadie eligió nada; lo eligió un índice al moverse.
    //
    // Es el mismo fallo que el del bucle del diálogo de fichero, en otro sitio:
    // repoblar una lista NO es una elección del operador.
    const QSignalBlocker repopulating(cameraCombo_);
    const QVariant previous = cameraCombo_->currentData();
    cameraCombo_->clear();

    for (std::size_t i = 0; i < cameras_.size(); ++i) {
        // La resolución solo se conoce tras conectar (la enumeración ya no abre
        // el dispositivo), así que se omite mientras sea 0.
        QString label = QString::fromStdString(cameras_[i].name);
        if (cameras_[i].width > 0 && cameras_[i].height > 0) {
            label += QStringLiteral(" (%1x%2)").arg(cameras_[i].width).arg(cameras_[i].height);
        }
        // El índice va en el DATO, no en la posición del combo. Este proyecto
        // ya pagó una vez el precio de señalar cosas por su posición: en cuanto
        // se añaden «Abrir imagen…» y «Abrir vídeo…» al final, cualquier código
        // que asumiera «índice del combo == índice en cameras_» apunta a otra
        // cosa sin avisar.
        cameraCombo_->addItem(label, QVariant(static_cast<int>(i)));
    }

    // Los ficheros son fuentes como la cámara, y van SIEMPRE, haya cámaras o
    // no. Antes, sin cámara, la aplicación entera se quedaba inservible: ni se
    // podía ajustar la detección, ni dibujar herramientas, ni probar una
    // plantilla. Con una imagen se puede hacer todo eso.
    if (!cameras_.empty()) {
        cameraCombo_->insertSeparator(cameraCombo_->count());
    }
    cameraCombo_->addItem(tr("Abrir imagen…"), QVariant(kSourceOpenImage));
    cameraCombo_->addItem(tr("Abrir vídeo…"), QVariant(kSourceOpenVideo));

    if (cameras_.empty()) {
        statusBar()->showMessage(
            tr("No se detectó ninguna cámara. Puedes conectar una y actualizar, o abrir una "
               "imagen o un vídeo para trabajar sin ella."));
        refreshAction_->setEnabled(true);
        startStopButton_->setEnabled(true);
        return;
    }

    // Si había un fichero abierto, su entrada se acaba de borrar con la lista:
    // se repone y se vuelve a seleccionar. Sin esto el desplegable diría
    // «cámara 0» mientras en pantalla se ve la imagen abierta.
    if (streaming_ && fileSource_ != nullptr) {
        cameraCombo_->insertItem(0, fileSource_->describe(), QVariant(kSourceOpenedFile));
        cameraCombo_->setCurrentIndex(0);
        statusBar()->showMessage(tr("%n cámara(s) detectada(s); sigue abierto «%1».", nullptr,
                                    static_cast<int>(cameras_.size()))
                                     .arg(fileSource_->describe()));
        setControlsEnabled(true);
        return;
    }
    // Y si la selección de antes sigue existiendo, se respeta: la enumeración
    // no es motivo para mover lo que el operador tenía elegido.
    if (previous.isValid()) {
        for (int i = 0; i < cameraCombo_->count(); ++i) {
            if (cameraCombo_->itemData(i) == previous) {
                cameraCombo_->setCurrentIndex(i);
                break;
            }
        }
    }

    // Restaurar la última cámara elegida por el usuario (si sigue conectada).
    if (repos_.settings != nullptr && !previous.isValid()) {
        const auto saved = repos_.settings->getInt(kSettingCameraIndex, -1);
        if (saved.isOk() && saved.value() >= 0) {
            for (std::size_t i = 0; i < cameras_.size(); ++i) {
                if (cameras_[i].index == saved.value()) {
                    cameraCombo_->setCurrentIndex(static_cast<int>(i));
                    break;
                }
            }
        }
    }

    statusBar()->showMessage(tr("%n cámara(s) detectada(s)", nullptr,
                                static_cast<int>(cameras_.size())));
    setControlsEnabled(true);
}

void MainWindow::onStartStopClicked() {
    if (streaming_) {
        // stop() une el hilo de captura; la UI se restablece en onStreamStopped.
        startStopButton_->setEnabled(false);
        if (fileSource_ != nullptr) {
            fileSource_->stop();
        } else {
            controller_.stop();
        }
        return;
    }

    // Qué fuente se eligió, preguntado por el DATO del elemento. Un separador no
    // tiene dato, y por eso no hace nada en vez de arrancar lo que hubiera
    // debajo.
    const QVariant choice = cameraCombo_->currentData();
    if (!choice.isValid()) {
        return;
    }
    if (choice.toInt() == kSourceOpenImage || choice.toInt() == kSourceOpenVideo) {
        startFileSource(choice.toInt() == kSourceOpenImage ? camera::SourceKind::Image
                                                           : camera::SourceKind::Video);
        return;
    }
    const int comboIndex = choice.toInt();
    if (comboIndex < 0 || comboIndex >= static_cast<int>(cameras_.size())) {
        return;
    }

    if (repos_.settings != nullptr) {
        if (auto saved =
                repos_.settings->setInt(kSettingCameraIndex, cameras_[comboIndex].index);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar la cámara elegida: " + saved.error().message);
        }
    }

    streaming_ = true;
    // Identidad de la cámara en uso, para detectar si la calibración guardada
    // corresponde a otra cámara (D1).
    currentCameraKey_ = QString::fromStdString(cameras_[comboIndex].name);
    loadCachedResolutions();  // lista sondeada antes para ESTA cámara
    startStopButton_->setText(tr("Detener"));
    freezeButton_->setEnabled(true);
    // El desplegable NO se apaga: cambiar de fuente se decide mirando lo que
    // hay. Apagarlo dejaba «Abrir imagen…» inalcanzable con la cámara en
    // marcha, sin decir por qué — el operador veía la opción y no podía
    // llegar a ella.
    cameraCombo_->setEnabled(true);
    refreshAction_->setEnabled(false);
    statusBar()->showMessage(tr("Transmitiendo desde %1")
                                 .arg(QString::fromStdString(cameras_[comboIndex].name)));
    updateCalibrationLabel();  // reevalúa obsolescencia con la cámara nueva
    updateStatusIndicators();  // cámara ahora en verde (S4)
    // Los controles guardados se reaplican al abrir: la línea conserva su
    // exposición y su enfoque entre sesiones (O2).
    controller_.start(cameras_[comboIndex], savedCameraControls_);
    if (savedResolution_.valid()) {
        // La resolución elegida se reaplica al abrir, igual que los controles.
        controller_.requestResolution(savedResolution_);
    }
}

QString MainWindow::currentSourceLabel() const {
    switch (sourceKind_) {
        case camera::SourceKind::Camera: return tr("Frame actual de la cámara");
        case camera::SourceKind::Photo: return tr("La foto capturada");
        case camera::SourceKind::Image: return tr("La imagen abierta");
        case camera::SourceKind::Video: return tr("Frame actual del vídeo");
    }
    return tr("Frame actual");
}

void MainWindow::toggleFrozenPhoto() {
    if (sourceKind_ == camera::SourceKind::Photo) {
        // Soltar la foto: se para la fuente y se vuelve a escuchar la cámara,
        // que nunca dejó de transmitir. Volver cuesta cero.
        if (fileSource_ != nullptr) {
            fileSource_->stop();
            fileSource_.release()->deleteLater();
        }
        sourceKind_ = camera::SourceKind::Camera;
        cameraFrames_ = connect(&controller_, &camera::CameraController::frameReady, this,
                                &MainWindow::onFrame);
        freezeButton_->setText(tr("Capturar foto"));
        statusBar()->showMessage(tr("De vuelta al vídeo en vivo."));
        updateStatusIndicators();
        updateCalibrationLabel();
        return;
    }
    if (sourceKind_ != camera::SourceKind::Camera || lastFrame_.isNull()) {
        statusBar()->showMessage(
            tr("Solo se puede capturar una foto del vídeo en vivo de la cámara."));
        return;
    }

    // Se deja de escuchar a la cámara, pero NO se la para: resondear controles y
    // relanzar el perfil de exposición al volver costaría segundos y cambiaría
    // la imagen, que es justo lo que no se quiere de una foto.
    disconnect(cameraFrames_);
    const QString label =
        tr("Foto %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    // La foto se queda EN LA TIRA además de congelarse. Antes cada captura
    // tiraba la anterior, así que no había forma de reunir varias para
    // comparar, guardar un historial ni alimentar el aprendizaje.
    captureTray_.add(lastFrame_, currentSourceLabel());
    refreshCaptureList();
    fileSource_ = std::make_unique<camera::StillImageSource>(lastFrame_, label,
                                                             camera::SourceKind::Photo);
    connect(fileSource_.get(), &camera::FrameSource::frameReady, this, &MainWindow::onFrame);
    connect(fileSource_.get(), &camera::FrameSource::statsUpdated, this, &MainWindow::onStats);
    connect(fileSource_.get(), &camera::FrameSource::sourceError, this,
            &MainWindow::onCameraError);
    sourceKind_ = camera::SourceKind::Photo;
    // `currentCameraKey_` NO se toca: la foto salió de esta misma cámara, con
    // esta óptica y a esta distancia, así que la calibración sigue valiendo. Si
    // se tocara, congelar dispararía el aviso de «calibración obsoleta» y en dos
    // días el operador dejaría de leer ese aviso también cuando importa.
    freezeButton_->setText(tr("Volver al vídeo"));
    statusBar()->showMessage(tr("Trabajando sobre %1. La cámara sigue conectada.").arg(label));
    updateStatusIndicators();
    fileSource_->start();
}

bool MainWindow::startFileSource(camera::SourceKind kind) {
    const bool wantsImage = kind == camera::SourceKind::Image;
    // Se vuelve a la última carpeta usada. Quien está revisando casos abre diez
    // ficheros de la misma carpeta, y volver a navegar cada vez es una fricción
    // tonta que se paga en cada abrir.
    QString startDir;
    if (repos_.settings != nullptr) {
        if (const auto saved = repos_.settings->getString(kSettingLastSourceDir, "");
            saved.isOk()) {
            startDir = QString::fromStdString(saved.value());
        }
    }
    const QString path = QFileDialog::getOpenFileName(
        this, wantsImage ? tr("Abrir imagen") : tr("Abrir vídeo"), startDir,
        wantsImage ? tr("Imágenes (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Todos (*)")
                   : tr("Vídeos (*.mp4 *.avi *.mkv *.mov *.wmv);;Todos (*)"));
    if (path.isEmpty()) {
        // Cancelar no es un error: la ventana se queda exactamente como estaba.
        return false;
    }
    if (repos_.settings != nullptr) {
        if (auto saved = repos_.settings->setString(kSettingLastSourceDir,
                                                    QFileInfo(path).absolutePath().toStdString());
            !saved.isOk()) {
            core::logWarning("No se pudo guardar la carpeta de la fuente: " +
                             saved.error().message);
        }
    }
    return startFileSourceAtPath(kind, path);
}

// Abrir un fichero CONCRETO, sin diálogo. Separado de `startFileSource` porque
// elegir el fichero y montarlo son dos cosas distintas, y sólo la primera
// necesita a una persona delante: con el diálogo dentro, todo lo que pasa
// después —que llegue el frame, que se mida, que corregir el borde mueva el
// contorno— sólo se podía comprobar a mano, y así es como se colaron los
// últimos tres fallos.
bool MainWindow::startFileSourceAtPath(camera::SourceKind kind, const QString& path) {
    const bool wantsImage = kind == camera::SourceKind::Image;
    if (wantsImage) {
        fileSource_ = std::make_unique<camera::StillImageSource>(path);
    } else {
        fileSource_ = std::make_unique<camera::VideoFileSource>(path);
    }
    // Las mismas ranuras que la cámara. Ahí está todo el asunto: a partir de
    // aquí la ventana no sabe —ni necesita saber— de dónde vino el frame.
    connect(fileSource_.get(), &camera::FrameSource::frameReady, this, &MainWindow::onFrame);
    connect(fileSource_.get(), &camera::FrameSource::statsUpdated, this, &MainWindow::onStats);
    connect(fileSource_.get(), &camera::FrameSource::sourceError, this,
            &MainWindow::onCameraError);
    connect(fileSource_.get(), &camera::FrameSource::stopped, this,
            &MainWindow::onStreamStopped);

    if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get())) {
        connect(video, &camera::VideoFileSource::positionChanged, this,
                &MainWindow::onVideoPosition);
        playPauseButton_->setText(tr("Pausa"));
    }
    showVideoBar(kind == camera::SourceKind::Video);

    sourceKind_ = kind;
    lastSourcePath_ = path;
    streaming_ = true;
    // La identidad de la fuente sirve para el aviso de calibración obsoleta: la
    // escala en px/mm depende de la óptica y de la distancia al plano, y pasar
    // de una cámara a un fichero (o entre ficheros) cambia las dos.
    currentCameraKey_ = fileSource_->describe();
    // Y el desplegable pasa a decir QUÉ está abierto. Dejarlo en «Abrir
    // imagen…» convertía la única pista sobre con qué se está trabajando en una
    // etiqueta que no dice nada.
    //
    // Con las señales BLOQUEADAS, y esto costó un bucle infinito. Insertar en la
    // posición 0 desplaza al elemento seleccionado —«Abrir imagen…»— de la
    // posición N a la N+1, y Qt emite `currentIndexChanged` porque el ÍNDICE ha
    // cambiado, aunque el elemento elegido sea exactamente el mismo.
    //
    // Desde que se puede cambiar de fuente en marcha, esa señal se lee como
    // «han elegido abrir una imagen»: paraba la fuente recién arrancada y
    // volvía a abrir el diálogo de fichero. El operador veía la carpeta
    // cerrarse y abrirse una y otra vez, sin llegar a cargar nada.
    //
    // Bloquear aquí es lo correcto y no un parche: esta selección no es una
    // elección del operador, es la consecuencia de la que acaba de hacer.
    {
        QSignalBlocker blocker(cameraCombo_);
        cameraCombo_->insertItem(0, fileSource_->describe(), QVariant(kSourceOpenedFile));
        cameraCombo_->setCurrentIndex(0);
    }
    startStopButton_->setText(tr("Cerrar"));
    // El desplegable NO se apaga: cambiar de fuente se decide mirando lo que
    // hay. Apagarlo dejaba «Abrir imagen…» inalcanzable con la cámara en
    // marcha, sin decir por qué — el operador veía la opción y no podía
    // llegar a ella.
    cameraCombo_->setEnabled(true);
    refreshAction_->setEnabled(false);
    statusBar()->showMessage(wantsImage ? tr("Analizando la imagen %1").arg(fileSource_->describe())
                                        : tr("Reproduciendo %1").arg(fileSource_->describe()));
    updateCalibrationLabel();
    updateStatusIndicators();
    fileSource_->start();
    return true;
}


// Barra de transporte del vídeo: reproducir/pausar, un paso, y dónde va.
//
// Un vídeo sin esto no sirve para lo que se abre un vídeo — encontrar EL frame
// en el que la pieza se ve bien y trabajar sobre él. Antes reproducía en bucle
// sin más, así que para volver a un frame había que esperar a que el bucle
// pasara otra vez por ahí.
void MainWindow::buildVideoBar(QWidget* parent, QVBoxLayout* root) {
    videoBar_ = new QWidget(parent);
    auto* row = new QHBoxLayout(videoBar_);
    row->setContentsMargins(0, 0, 0, 0);

    playPauseButton_ = new QToolButton(videoBar_);
    // Por nombre y no por su rótulo: es el único botón que CAMBIA de texto
    // —«Pausa» y «Seguir»— así que buscarlo por lo que dice obliga a probar
    // los dos, y una prueba que ya no encuentra ninguno falla lejos de aquí.
    playPauseButton_->setObjectName(QStringLiteral("playPauseButton"));
    playPauseButton_->setText(tr("Pausa"));
    playPauseButton_->setToolTip(tr("Pausar o seguir. Con el vídeo parado se puede dibujar\n"
                                    "una herramienta sin que la pieza tiemble."));
    row->addWidget(playPauseButton_);

    stepButton_ = new QToolButton(videoBar_);
    stepButton_->setText(tr("▶|"));
    stepButton_->setToolTip(tr("Avanzar un frame y quedarse ahí. Con la barra no se puede\n"
                               "elegir el frame: en un vídeo largo, un píxel son varios."));
    row->addWidget(stepButton_);

    videoSlider_ = new QSlider(Qt::Horizontal, videoBar_);
    videoSlider_->setObjectName(QStringLiteral("videoSlider"));
    videoSlider_->setRange(0, 1000);
    videoSlider_->setToolTip(tr("Dónde va el vídeo. Arrastra para buscar."));
    row->addWidget(videoSlider_, 1);

    videoTimeLabel_ = new QLabel(videoBar_);
    videoTimeLabel_->setMinimumWidth(110);
    videoTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(videoTimeLabel_);

    root->addWidget(videoBar_);
    videoBar_->setVisible(false);

    connect(playPauseButton_, &QToolButton::clicked, this, [this] {
        auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get());
        if (video == nullptr) {
            return;
        }
        const bool pausing = !video->isPaused();
        video->setPaused(pausing);
        playPauseButton_->setText(pausing ? tr("Seguir") : tr("Pausa"));
        // Pausar habilita el pincel y seguir lo apaga: un vídeo detenido en un
        // frame es tan quieto como una foto, y en marcha no lo es.
        updateEdgeBrushAvailability();
    });
    connect(stepButton_, &QToolButton::clicked, this, [this] {
        if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get())) {
            video->stepOneFrame();
            playPauseButton_->setText(tr("Seguir"));  // el paso deja en pausa
            updateEdgeBrushAvailability();
        }
    });
    // Mientras se arrastra, la barra deja de seguir al vídeo: si no, el pulgar
    // daría saltos bajo el dedo cada vez que llega una posición nueva.
    connect(videoSlider_, &QSlider::sliderPressed, this,
            [this] { videoSliderHeld_ = true; });
    connect(videoSlider_, &QSlider::sliderReleased, this, [this] {
        videoSliderHeld_ = false;
        if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get())) {
            video->seekToFraction(videoSlider_->value() / 1000.0);
        }
    });
}

void MainWindow::showVideoBar(bool visible) {
    if (videoBar_ != nullptr) {
        videoBar_->setVisible(visible);
    }
}

void MainWindow::onVideoPosition(qint64 frame, qint64 total, double fps) {
    if (videoSlider_ == nullptr || videoTimeLabel_ == nullptr) {
        return;
    }
    // Sin total no se puede colocar el pulgar, y colocarlo donde sea sería
    // inventarse dónde va el vídeo. La barra se apaga y el rótulo lo dice.
    const bool placeable = total > 1;
    videoSlider_->setEnabled(placeable);
    if (placeable && !videoSliderHeld_) {
        QSignalBlocker blocker(videoSlider_);
        videoSlider_->setValue(static_cast<int>(1000.0 * frame / (total - 1)));
    }
    const auto asTime = [fps](qint64 f) {
        const double seconds = fps > 0.0 ? f / fps : 0.0;
        return QStringLiteral("%1:%2")
            .arg(static_cast<int>(seconds) / 60, 2, 10, QLatin1Char('0'))
            .arg(static_cast<int>(seconds) % 60, 2, 10, QLatin1Char('0'));
    };
    videoTimeLabel_->setText(placeable ? tr("%1 / %2").arg(asTime(frame), asTime(total - 1))
                                       : tr("frame %1").arg(frame));
}


// La tira de capturas, a la izquierda.
//
// «Capturar foto» congelaba el frame y ahí se quedaba: tomar la siguiente
// tiraba la anterior. Eso vale para medir UNA pieza y no vale para lo que se
// pide de un montón de fotos —historial, comparar unas con otras, alimentar el
// aprendizaje— porque las tres necesitan que coexistan.
//
// A la izquierda y no a la derecha: la derecha ya es de las herramientas, y se
// lee de izquierda a derecha — primero lo que has recogido, después sobre qué
// trabajas.
void MainWindow::buildCaptureDock() {
    captureDock_ = new QDockWidget(tr("Capturas"), this);
    captureDock_->setObjectName(QStringLiteral("captureDock"));
    auto* panel = new QWidget(captureDock_);
    auto* column = new QVBoxLayout(panel);

    captureCountLabel_ = new QLabel(panel);
    captureCountLabel_->setWordWrap(true);
    column->addWidget(captureCountLabel_);

    captureList_ = new QListWidget(panel);
    captureList_->setViewMode(QListView::IconMode);
    captureList_->setIconSize(QSize(112, 84));
    captureList_->setResizeMode(QListView::Adjust);
    captureList_->setMovement(QListView::Static);
    captureList_->setSpacing(4);
    captureList_->setToolTip(
        tr("Las fotos tomadas en esta sesión. Haz clic en una para trabajar sobre\n"
           "ella; con Supr se quita de la tira."));

    // SUPR, AQUÍ, QUITA LA FOTO — Y ANTES NO LO HACÍA.
    //
    // La ayuda de arriba lo prometía desde el principio y era falso:
    // `CaptureTray::removeAt` estaba escrita y no la llamaba NADIE. Mientras
    // tanto, Supr es un atajo de ventana atado a borrar la herramienta
    // seleccionada, y `QListWidget` no se queda con esa tecla, así que ganaba el
    // atajo: pulsar Supr con el foco en la tira borraba una cota de la plantilla
    // Y la quitaba de la base de datos.
    //
    // O sea que la propia ayuda enseñaba a pulsar la tecla que destruye trabajo
    // guardado, en silencio y mirando a otro panel.
    //
    // Se resuelve con un atajo de ámbito WIDGET: mientras el foco esté en la
    // tira, Supr es suyo; en cuanto el foco sale, vuelve a ser el de la ventana.
    auto* dropCapture = new QAction(tr("Quitar la foto de la tira"), captureList_);
    dropCapture->setShortcut(QKeySequence::Delete);
    dropCapture->setShortcutContext(Qt::WidgetShortcut);
    connect(dropCapture, &QAction::triggered, this, [this] {
        const int row = captureList_->currentRow();
        if (row < 0 || row >= captureTray_.count()) {
            return;
        }
        captureTray_.removeAt(row);
        refreshCaptureList();
        updateLearnFromCaptureAvailability();
        statusBar()->showMessage(
            tr("Foto quitada de la tira. Quedan %n.", nullptr, captureTray_.count()));
    });
    captureList_->addAction(dropCapture);
    column->addWidget(captureList_, 1);

    auto* buttons = new QHBoxLayout();
    auto* save = new QPushButton(tr("Guardar todas…"), panel);
    save->setToolTip(
        tr("Escribe las capturas en una carpeta, en PNG y con el nombre de la pieza\n"
           "y la fecha por delante, para que la carpeta se ordene sola por tiempo.\n\n"
           "En PNG y no JPEG a propósito: estas fotos son para volver a medir sobre\n"
           "ellas, y el JPEG inventa bordes donde no los hay."));
    buttons->addWidget(save);
    auto* clear = new QPushButton(tr("Vaciar"), panel);
    clear->setToolTip(tr("Quita todas las capturas de la tira. No borra lo ya guardado."));
    buttons->addWidget(clear);
    column->addLayout(buttons);

    // Aprender de una foto: la última pieza que le faltaba a la tira.
    //
    // Hasta ahora las capturas eran fotos y nada más. La visión del proyecto
    // dice «actualizar la referencia estadística tras cada pieza buena, nunca
    // reentrenar», y eso sólo se podía hacer desde el diálogo de una inspección
    // recién corrida: las fotos que uno guarda durante la puesta a punto —que
    // son precisamente las buenas, elegidas a mano— no servían para nada.
    learnFromCaptureButton_ = new QPushButton(tr("Aprender de esta foto"), panel);
    learnFromCaptureButton_->setObjectName(QStringLiteral("learnFromCaptureButton"));
    learnFromCaptureButton_->setEnabled(false);
    column->addWidget(learnFromCaptureButton_);
    connect(learnFromCaptureButton_, &QPushButton::clicked, this,
            &MainWindow::onLearnFromCaptureClicked);

    captureDock_->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, captureDock_);

    connect(save, &QPushButton::clicked, this, &MainWindow::onSaveCapturesClicked);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (captureTray_.empty()) {
            return;
        }
        // Se pregunta: vaciar es lo único aquí que no se puede deshacer.
        const auto answer = QMessageBox::question(
            this, tr("Vaciar la tira"),
            tr("Se quitarán las %n captura(s) de la tira. Las que ya hayas guardado en "
               "disco no se tocan.", nullptr, captureTray_.count()));
        if (answer == QMessageBox::Yes) {
            captureTray_.clear();
            refreshCaptureList();
        }
    });
    connect(captureList_, &QListWidget::currentRowChanged, this,
            &MainWindow::onCaptureChosen);
    refreshCaptureList();
}

// Cuándo se puede aprender de la foto elegida, y si no se puede, POR QUÉ.
//
// Un botón apagado sin explicación se lee como que la aplicación está rota; y
// aquí hay tres motivos distintos para estarlo, que piden tres arreglos
// distintos por parte del operador.
void MainWindow::updateLearnFromCaptureAvailability() {
    if (learnFromCaptureButton_ == nullptr) {
        return;
    }
    const int row = captureList_ != nullptr ? captureList_->currentRow() : -1;
    const bool hasCapture = row >= 0 && row < captureTray_.count();
    const std::int64_t pieceId = selectedPieceId();
    const bool hasEngine = repos_.engine != nullptr && static_cast<bool>(repos_.embedFn);

    const bool usable = hasCapture && pieceId >= 0 && hasEngine;
    learnFromCaptureButton_->setEnabled(usable);
    learnFromCaptureButton_->setToolTip(
        usable ? tr("Añade esta foto a la referencia de la pieza como un ejemplar BUENO.\n\n"
                    "La referencia no se reentrena: se le suma esta muestra y se guarda una "
                    "versión nueva, conservando las anteriores. Antes de añadirla se "
                    "inspecciona, y si sale NG se avisa — enseñarle una pieza mala a la "
                    "referencia es la forma más rápida de que deje de detectar nada.")
        : !hasCapture ? tr("Elige antes una foto de la tira.")
        : pieceId < 0 ? tr("Elige antes qué pieza es: la referencia que se actualiza es la "
                           "suya.")
                      : tr("Sin el modelo ONNX no hay apariencia que aprender. Con las "
                           "herramientas de medida se sigue inspeccionando, pero la "
                           "referencia por apariencia necesita el modelo."));
}

// Aprender de una captura elegida a mano.
//
// EXPLÍCITO Y POR FOTO, nunca automático, y la decisión es deliberada: una
// referencia contaminada con piezas malas no falla ruidosamente, falla dejando
// pasar defectos. Es el peor modo de fallo de toda la aplicación, porque nadie
// lo nota hasta que llega una reclamación. Así que aprender es siempre un acto
// del operador sobre una foto concreta que él ha mirado.
//
// Y antes de sumarla se INSPECCIONA. Si el programa la considera mala, se dice
// —con el motivo— y se pregunta. El operador puede tener razón (la referencia
// era demasiado estrecha) o puede haberse equivocado de foto; lo que no puede
// es decidirlo sin la información.
void MainWindow::onLearnFromCaptureClicked() {
    const int row = captureList_ != nullptr ? captureList_->currentRow() : -1;
    if (row < 0 || row >= captureTray_.count() || repos_.engine == nullptr) {
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        statusBar()->showMessage(tr("Elige antes qué pieza es."));
        return;
    }

    const cv::Mat frame = camera::qImageToMat(captureTray_.at(row).image);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto outcome = repos_.engine->inspect(frame, pieceId);
    QApplication::restoreOverrideCursor();
    if (!outcome.isOk()) {
        QMessageBox::warning(this, tr("Aprender de esta foto"),
                             tr("No se pudo inspeccionar la foto, así que tampoco añadirla "
                                "a la referencia.\n\n%1")
                                 .arg(QString::fromStdString(outcome.error().message)));
        return;
    }
    if (outcome.value().embedding.empty()) {
        QMessageBox::warning(this, tr("Aprender de esta foto"),
                             tr("De esta foto no salió ninguna huella de apariencia: sin "
                                "modelo cargado o sin pieza detectada en ella."));
        return;
    }

    const auto& verdict = outcome.value().verdict;
    if (!verdict.ok) {
        const auto answer = QMessageBox::question(
            this, tr("Esta foto sale NG"),
            tr("El programa considera MALA esta pieza:\n\n%1\n\n"
               "Añadirla a la referencia mueve lo que se considera normal hacia esa "
               "pieza, y a partir de ahí defectos parecidos empezarán a pasar como "
               "buenos.\n\n"
               "Tiene sentido hacerlo si la referencia se quedó demasiado estrecha y esta "
               "pieza es buena de verdad. ¿La añado?")
                .arg(QString::fromStdString(verdict.summary)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            statusBar()->showMessage(tr("No se añadió: la referencia sigue como estaba."));
            return;
        }
    }

    const auto version = repos_.engine->updateReference(pieceId, outcome.value().embedding);
    if (!version.isOk()) {
        QMessageBox::warning(this, tr("Aprender de esta foto"),
                             QString::fromStdString(version.error().message));
        return;
    }
    // Se dice QUÉ cambió, y con el parecido de la foto añadida: sin eso,
    // «referencia actualizada» es indistinguible de no haber hecho nada.
    const double similarity = verdict.embedding.similarity;
    statusBar()->showMessage(
        tr("Aprendido: la referencia de la pieza pasa a la versión %1 (las anteriores se "
           "conservan). Esta foto se parecía a la referencia un %2 %.")
            .arg(version.value())
            .arg(100.0 * similarity, 0, 'f', 1));
}

void MainWindow::refreshCaptureList() {
    if (captureList_ == nullptr) {
        return;
    }
    QSignalBlocker blocker(captureList_);
    captureList_->clear();
    for (int i = 0; i < captureTray_.count(); ++i) {
        const Capture& capture = captureTray_.at(i);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(capture.image)),
                                         capture.taken.toString(QStringLiteral("HH:mm:ss")));
        // De dónde salió: sin esto, dos fotos de dos montajes distintos son
        // indistinguibles una semana después, que es cuando se miran.
        item->setToolTip(tr("%1 — %2")
                             .arg(capture.taken.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")),
                                  capture.source));
        captureList_->addItem(item);
    }
    captureCountLabel_->setText(
        captureTray_.empty()
            ? tr("Sin capturas. Pulsa «Capturar foto» y se irán juntando aquí.")
            : tr("%n captura(s) en esta sesión.", nullptr, captureTray_.count()));
    // La disponibilidad cambia con la lista: sin recalcularla aquí el botón
    // se quedaría como estuviera, que es el fallo que ya costó el pincel.
    updateLearnFromCaptureAvailability();
}

void MainWindow::onCaptureChosen(int row) {
    if (row < 0 || row >= captureTray_.count()) {
        return;
    }
    // Se trabaja sobre ella como sobre cualquier foto: la fuente pasa a ser esa
    // imagen. Así todo lo que ya funciona —medir, dibujar, inspeccionar— vale
    // igual sin un camino nuevo que mantener.
    const Capture& capture = captureTray_.at(row);
    fileSource_ = std::make_unique<camera::StillImageSource>(
        capture.image, capture.source, camera::SourceKind::Photo);
    connect(fileSource_.get(), &camera::FrameSource::frameReady, this, &MainWindow::onFrame);
    connect(fileSource_.get(), &camera::FrameSource::stopped, this,
            &MainWindow::onStreamStopped);
    sourceKind_ = camera::SourceKind::Photo;
    streaming_ = true;
    showVideoBar(false);
    fileSource_->start();
    statusBar()->showMessage(tr("Trabajando sobre la captura de las %1.")
                                 .arg(capture.taken.toString(QStringLiteral("HH:mm:ss"))));
    updateLearnFromCaptureAvailability();
}

void MainWindow::onSaveCapturesClicked() {
    if (captureTray_.empty()) {
        statusBar()->showMessage(tr("No hay capturas que guardar."));
        return;
    }
    QString startDir;
    if (repos_.settings != nullptr) {
        startDir = QString::fromStdString(
            repos_.settings->getString("last_capture_dir", std::string()).value());
    }
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Guardar las capturas en…"), startDir);
    if (folder.isEmpty()) {
        return;  // cancelar no es un error
    }
    if (repos_.settings != nullptr) {
        repos_.settings->setString("last_capture_dir", folder.toStdString());
    }

    const QString piece = pieceCombo_ != nullptr && !pieceCombo_->currentText().isEmpty()
                              ? pieceCombo_->currentText()
                              : tr("pieza");
    const auto saved = captureTray_.saveAll(folder, piece);
    if (!saved.isOk()) {
        QMessageBox::warning(this, tr("No se pudieron guardar"),
                             QString::fromStdString(saved.error().message));
        return;
    }
    statusBar()->showMessage(tr("%n captura(s) guardadas en %1.", nullptr, saved.value())
                                 .arg(folder));
}


// El pincel solo se ofrece con una imagen QUIETA.
//
// En vídeo en vivo el contorno se recalcula en cada frame, así que un borde
// corregido a mano sería mentira en cuanto la pieza se moviera un píxel. Y
// apagado con su motivo, no muerto y en silencio: un control que no responde sin
// explicación se lee como que la aplicación está rota.
// El tamaño del pincel, en los tres sitios donde se ve, sin que ninguno mande
// sobre los otros.
//
// `fromCanvas` evita el ida y vuelta: si el cambio viene de la rueda del raton,
// mover el deslizador volveria a llamar al lienzo. Se bloquean las señales del
// deslizador en vez de comparar valores, que es lo unico que funciona cuando el
// valor se acota por el camino.
void MainWindow::applyBrushRadius(int radiusPx, bool fromCanvas) {
    if (brushSizeSlider_ == nullptr) {
        return;
    }
    const int wanted = std::clamp(radiusPx, brushSizeSlider_->minimum(),
                                  brushSizeSlider_->maximum());
    {
        const QSignalBlocker block(brushSizeSlider_);
        brushSizeSlider_->setValue(wanted);
    }
    if (!fromCanvas && video_ != nullptr) {
        video_->setBrushRadius(wanted);
    }
    if (brushSizeLabel_ != nullptr) {
        // El DIAMETRO, no el radio: es lo que se ve pintado, y lo que uno compara
        // con el hueco que quiere rellenar.
        brushSizeLabel_->setText(tr("%1 px").arg(wanted * 2));
    }
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("brush_radius", wanted);
    }
}

// El asistente de calibracion de la lente.
//
// Come de la camara en vivo mientras esta abierto: `onFrame` le va pasando los
// fotogramas SIN corregir, que son los que hay que medir. Corregirlos antes
// seria pedirle a la lente que se mida a si misma ya enderezada.
void MainWindow::onCalibrateLensClicked() {
    if (lensDialog_ != nullptr) {
        lensDialog_->raise();
        lensDialog_->activateWindow();
        return;
    }
    auto* dialog = new LensCalibrationDialog(this);
    lensDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QObject::destroyed, this, [this] { lensDialog_ = nullptr; });
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        if (dialog->result().has_value()) {
            applyLensCalibration(*dialog->result(), true);
        }
    });
    // Si ya hay una imagen parada —una foto o un fichero en pausa—, el asistente
    // tiene que verla igual: sin esto solo serviria con la camara en marcha.
    if (!lastFrame_.isNull()) {
        dialog->offerFrame(lastFrame_);
    }
    dialog->show();
}

// Guardar el modelo y, si se pide, encenderlo.
//
// Se GUARDA aunque no se encienda: calibrar cuesta un rato con un tablero en la
// mano, y perder ese trabajo por no querer mover las cotas hoy seria un castigo
// absurdo.
void MainWindow::applyLensCalibration(const vision::LensCalibration& calibration,
                                      bool enable) {
    lensCorrector_ = vision::LensCorrector(calibration);
    if (repos_.settings != nullptr) {
        repos_.settings->setString("lens_model", vision::serializeCalibration(calibration));
    }
    const double worst = vision::worstDisplacementPx(calibration);
    if (lensCorrectionAction_ != nullptr) {
        lensCorrectionAction_->setEnabled(lensCorrector_.isReady());
        // Se dice en PIXELES y no en coeficientes: «tu lente desplaza hasta 34 px
        // en las esquinas» se entiende, y «k1 = -0,2478» no.
        if (vision::distortionIsNegligible(calibration)) {
            statusBar()->showMessage(
                tr("Lente calibrada: desplaza %1 px como mucho, así que corregirla no "
                   "cambiaría ninguna medida. Se guarda por si cambias de cámara.")
                    .arg(worst, 0, 'f', 1));
            enable = false;
        } else {
            statusBar()->showMessage(
                tr("Lente calibrada con %1 tomas: desplaza hasta %2 px en el borde "
                   "(error de ajuste %3 px).")
                    .arg(calibration.views)
                    .arg(worst, 0, 'f', 1)
                    .arg(calibration.reprojectionError, 0, 'f', 3));
        }
        const QSignalBlocker block(lensCorrectionAction_);
        lensCorrectionAction_->setChecked(enable && lensCorrector_.isReady());
    }
    lensCorrectionOn_ = enable && lensCorrector_.isReady();
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("lens_enabled", lensCorrectionOn_ ? 1 : 0);
    }
    reanalyseCurrentFrame();
}

void MainWindow::updateEdgeBrushAvailability() {
    if (edgeBrushButton_ == nullptr) {
        return;
    }
    // Una imagen QUIETA: una foto, un fichero de imagen, o un vídeo EN PAUSA.
    //
    // El vídeo pausado entra, y al principio no estaba: se dejó fuera por
    // pensar «vídeo = se mueve», pero un vídeo detenido en un frame es tan
    // quieto como una foto — y es justo donde hace falta corregir, porque es el
    // frame que uno ha elegido tras buscarlo con la barra.
    //
    // Lo que sigue fuera es el vídeo EN MARCHA y la cámara en vivo, y ahí la
    // razón se mantiene: el contorno se recalcula en cada frame, así que un
    // borde corregido a mano sería mentira en cuanto la pieza se moviera.
    const auto* video = dynamic_cast<const camera::VideoFileSource*>(fileSource_.get());
    const bool pausedVideo = video != nullptr && video->isPaused();
    const bool still = sourceKind_ == camera::SourceKind::Photo ||
                       sourceKind_ == camera::SourceKind::Image || pausedVideo;
    const bool usable = still && !lastFrame_.isNull();

    // Sin cambios, no se toca nada: esto se consulta en cada frame y reescribir
    // el tooltip sesenta veces por segundo es trabajo tirado.
    if (edgeBrushButton_->isEnabled() == usable && !edgeBrushButton_->toolTip().isEmpty()) {
        return;
    }
    edgeBrushButton_->setEnabled(usable);
    edgeBrushButton_->setToolTip(
        usable
            ? tr("Corrige a mano dónde está el borde de la pieza cuando la detección se\n"
                 "equivoca: una sombra que se come un lado, un reflejo que la parte.\n\n"
                 "Verde lo que añades, rojo lo que quitas. La rueda del ratón cambia el\n"
                 "tamaño del pincel. La corrección vale para esta imagen: no cambia cómo\n"
                 "se detectan las demás.")
        : sourceKind_ == camera::SourceKind::Video
            ? tr("Pausa el vídeo para corregir el borde.\n\n"
                 "Con el vídeo en marcha el contorno se recalcula en cada frame, así que\n"
                 "una corrección a mano dejaría de valer al frame siguiente.")
            : tr("Solo con una imagen quieta: una foto, un fichero abierto o un vídeo en\n"
                 "pausa.\n\n"
                 "En vídeo en vivo el contorno se recalcula en cada frame, así que un borde\n"
                 "corregido a mano dejaría de valer en cuanto la pieza se moviera. Captura\n"
                 "una foto y corrígela ahí."));
    if (!usable) {
        // Al dejar de poder usarse, el pincel se apaga solo: dejarlo encendido
        // haría que el siguiente clic sobre la imagen pintara sin que nadie lo
        // pidiera.
        if (brushAddAction_ != nullptr && brushRemoveAction_ != nullptr) {
            QSignalBlocker a(brushAddAction_);
            QSignalBlocker b(brushRemoveAction_);
            brushAddAction_->setChecked(false);
            brushRemoveAction_->setChecked(false);
        }
        video_->setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
    }
}

// MARCAR UNA PIEZA RODEÁNDOLA, Y DESCARTAR LO QUE NO LO ES.
//
// El trazo no se toma como si fuera la pieza, y ese es el punto entero: un
// contorno dibujado a pulso no se puede medir, así que el diámetro que saliera
// de ahí sería el pulso del operador con aspecto de cota. El trazo dice DÓNDE
// MIRAR y el borde lo busca `pieceInsideOutline` dentro, con el fondo que haya
// ahí — que es el mismo truco de la zona de trabajo, y por eso funciona con las
// piezas que el umbral global se deja fuera.
//
// Se aplica por `applyCorrectionArea` para que entre en la MISMA pila de
// deshacer que las pinceladas: dos formas de corregir el borde, una deshacible
// y otra no, se aprende perdiendo trabajo.
void MainWindow::onPieceOutlined(const std::vector<cv::Point>& polygon, bool add) {
    // El modo se apaga solo al terminar el trazo, como la zona libre: quien
    // quiera rodear otra vuelve a pulsar. Dejarlo encendido convierte el
    // siguiente arrastre —para mover, para dibujar— en una pieza inventada.
    if (outlineAddAction_ != nullptr) {
        outlineAddAction_->setChecked(false);
    }
    if (outlineDropAction_ != nullptr) {
        outlineDropAction_->setChecked(false);
    }
    if (lastFrame_.isNull() || polygon.size() < 3) {
        statusBar()->showMessage(tr("Ese trazo no encierra ninguna zona."));
        return;
    }

    const cv::Mat frame = camera::qImageToMat(lastFrame_);
    if (!add) {
        cv::Mat area(frame.size(), CV_8UC1, cv::Scalar(0));
        cv::fillPoly(area, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
        video_->applyCorrectionArea(area, false);
        statusBar()->showMessage(
            tr("Descartado: %1 px pasan a ser fondo. Ctrl+Z lo deshace.")
                .arg(cv::countNonZero(area)));
        return;
    }

    const auto found = vision::pieceInsideOutline(frame, polygon, pipelineConfig_.segmentation);
    if (found.mask.empty()) {
        statusBar()->showMessage(QString::fromStdString(found.why));
        return;
    }
    video_->applyCorrectionArea(found.mask, true);
    // El motivo va entero a la barra de estado, y con él la diferencia que
    // importa: si el borde salió de la imagen, la pieza se puede medir; si salió
    // del trazo, no. Callarlo dejaría al operador aceptando cotas de su pulso.
    statusBar()->showMessage(QString::fromStdString(found.why));
}

void MainWindow::onEdgeCorrected(const cv::Mat& forcePiece, const cv::Mat& forceBackground) {
    // La corrección viene ya clonada del lienzo, pero se vuelve a clonar aquí
    // por lo mismo: estas máscaras viajan a un hilo de trabajo, y compartir un
    // búfer que otro hilo puede reasignar es la clase de fallo que se manifiesta
    // como una aplicación que se cierra sola sin decir nada.
    pipelineConfig_.forcePiece = forcePiece.clone();
    pipelineConfig_.forceBackground = forceBackground.clone();

    // Y se comprueba que la corrección CORRESPONDE a la imagen que se está
    // analizando. Si no, `applyMaskCorrection` la ignora en silencio —hace bien,
    // aplicarla desplazada sería peor— pero el operador vería su pincelada
    // pintada en pantalla y el contorno sin moverse, sin ninguna explicación.
    if (!lastFrame_.isNull() && !pipelineConfig_.forcePiece.empty() &&
        (pipelineConfig_.forcePiece.cols != lastFrame_.width() ||
         pipelineConfig_.forcePiece.rows != lastFrame_.height())) {
        const int hadCols = pipelineConfig_.forcePiece.cols;
        const int hadRows = pipelineConfig_.forcePiece.rows;
        // Y NO SE GUARDA, que es lo que faltaba.
        //
        // Estas dos líneas se asignaban ARRIBA, antes de comprobar si la
        // corrección sirve, así que al salir por aquí quedaba una máscara del
        // tamaño equivocado. `applyMaskCorrection` la ignora —hace bien,
        // aplicarla desplazada borraría un trozo cualquiera de la pieza— pero la
        // ignora EN SILENCIO.
        //
        // El resultado era el peor de los tres posibles: el operador veía la
        // pastilla «Borde corregido» encendida, el contorno sin moverse, y la
        // única explicación en un mensaje de la barra de estado que se va solo.
        // Desde fuera eso es «pasé el pincel y sigue remarcando la zona».
        pipelineConfig_.forcePiece = cv::Mat();
        pipelineConfig_.forceBackground = cv::Mat();
        updateEdgeCorrectionChip();
        statusBar()->showMessage(
            tr("La corrección es de una imagen de %1×%2 y ahora se ve una de %3×%4: no se "
               "puede aplicar. Vuelve a corregir sobre esta.")
                .arg(hadCols)
                .arg(hadRows)
                .arg(lastFrame_.width())
                .arg(lastFrame_.height()));
        return;
    }

    // Se reanaliza en el acto: el sentido de corregir es VER el borde nuevo.
    //
    // Y se apunta que, en cuanto ese analisis llegue, hay que RETIRAR el trazo.
    // No antes: entre soltar el pincel y ver el contorno nuevo hay unas decimas,
    // y quitar la mancha en ese hueco dejaria un momento en el que no se ve ni
    // lo pintado ni el resultado, que se lee como que no ha pasado nada.
    hideCorrectionWhenAnalysed_ = true;
    reanalyseCurrentFrame();
    const int added = forcePiece.empty() ? 0 : cv::countNonZero(forcePiece);
    const int removed = forceBackground.empty() ? 0 : cv::countNonZero(forceBackground);
    // Sin corrección no hay nada que aprender, y afinar con la nada devolvería
    // los ajustes de ahora presentados como un hallazgo.
    if (brushTuneAction_ != nullptr) {
        brushTuneAction_->setEnabled(added > 0 || removed > 0);
    }
    statusBar()->showMessage(
        added == 0 && removed == 0
            ? tr("Sin correcciones: el borde es el que detecta el programa.")
            : tr("Borde corregido a mano: +%1 px, −%2 px. Ctrl+Z deshace la pincelada; en "
                 "«Corregir borde» puedes afinar la detección con ella.")
                  .arg(added)
                  .arg(removed));
    updateEdgeCorrectionChip();
}

// Afinar la detección a partir de una corrección a mano.
//
// Corregir el borde arregla ESTA imagen. Pero la corrección es, literalmente,
// la respuesta correcta: dice qué es pieza y qué no en un caso que la detección
// falló. Con la respuesta correcta delante se puede buscar qué ajuste la habría
// dado solo — y si existe, dejar de corregir a mano una imagen tras otra.
//
// Se hace a petición y no tras cada pincelada: sobre un frame de 1920x1080 la
// búsqueda cuesta unos 650 ms medidos, y meterlos en cada trazo convertiría el
// pincel en algo intratable.
void MainWindow::onTuneDetectionFromEdge() {
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("No hay imagen sobre la que afinar."));
        return;
    }
    const cv::Mat image = camera::qImageToMat(lastFrame_);
    auto detected = vision::segmentPiece(image, pipelineConfig_.segmentation);
    if (!detected.isOk()) {
        statusBar()->showMessage(
            tr("No se pudo segmentar la imagen para compararla con tu corrección."));
        return;
    }

    // La verdad según el operador: lo que detecta el programa, con la
    // corrección aplicada encima. Mismo orden que el análisis —primero añadir,
    // después quitar— para que lo que se busca sea EXACTAMENTE lo que se ve.
    cv::Mat truth = detected.value();
    vision::applyMaskCorrection(truth, pipelineConfig_, cv::Rect(), false);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto suggestion =
        vision::suggestSegmentation(image, truth, pipelineConfig_.segmentation);
    QApplication::restoreOverrideCursor();

    if (!suggestion.found) {
        statusBar()->showMessage(tr("No se pudo evaluar ningún ajuste sobre esta imagen."));
        return;
    }

    const auto percent = [](double value) { return QString::number(100.0 * value, 'f', 1); };

    if (!suggestion.worthApplying()) {
        // Y se dice CON LAS CIFRAS. «No hay nada que cambiar» sin números es
        // indistinguible de «no lo he mirado».
        QMessageBox::information(
            this, tr("Afinar la detección"),
            tr("Con estos ajustes no se gana nada.\n\n"
               "Los de ahora reproducen tu corrección en un %1 %, y el mejor ajuste que "
               "he encontrado llega al %2 %. La diferencia no justifica cambiarlos.\n\n"
               "Si el borde te sigue saliendo mal, el problema no está en el umbral: "
               "mira la iluminación, el enfoque o la zona de trabajo.")
                .arg(percent(suggestion.agreementNow))
                .arg(percent(suggestion.agreementSuggested)));
        return;
    }

    const auto& proposed = suggestion.options;
    const QString polarity = proposed.polarity == vision::SegmentationPolarity::DarkPiece
                                 ? tr("pieza oscura sobre fondo claro")
                                 : proposed.polarity == vision::SegmentationPolarity::LightPiece
                                       ? tr("pieza clara sobre fondo oscuro")
                                       : tr("automática");
    const auto answer = QMessageBox::question(
        this, tr("Afinar la detección"),
        tr("Hay un ajuste que habría detectado este borde SOLO, sin corregirlo a mano.\n\n"
           "Ahora: coincide con tu corrección en un %1 %.\n"
           "Propuesto: %2 %, con umbral %3 y polaridad «%4».\n\n"
           "Se aplica a todas las piezas que se midan de aquí en adelante, no sólo a "
           "ésta. ¿Lo aplico?")
            .arg(percent(suggestion.agreementNow))
            .arg(percent(suggestion.agreementSuggested))
            .arg(proposed.manualThreshold)
            .arg(polarity),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        statusBar()->showMessage(tr("Ajustes sin tocar: la corrección sigue valiendo para "
                                    "esta imagen."));
        return;
    }

    pipelineConfig_.segmentation = proposed;
    persistPipelineConfig();
    // Y se quita la corrección: si los ajustes nuevos dan el mismo borde, la
    // corrección ya no pinta nada, y dejarla puesta escondería si el ajuste
    // funciona de verdad o si lo que se ve sigue siendo la pincelada.
    video_->clearEdgeCorrection();
    reanalyseCurrentFrame();
    statusBar()->showMessage(tr("Detección afinada: umbral %1, coincidencia %2 %. La "
                                "corrección a mano se ha retirado — lo que ves ahora sale "
                                "de los ajustes.")
                                 .arg(proposed.manualThreshold)
                                 .arg(percent(suggestion.agreementSuggested)));
}

void MainWindow::onFrame(const QImage& rawFrame) {
    // LA LENTE SE ENDEREZA LO PRIMERO, antes de que nadie vea el fotograma.
    //
    // A diferencia del realce de vista —que solo toca lo que se pinta— esto
    // tiene que llegar TAMBIEN al analisis: es una correccion geometrica, y la
    // pieza que se mide y la que se ve tienen que ser la misma. Corregir solo
    // una de las dos dejaria al operador señalando un borde que no esta donde
    // el programa cree.
    QImage frame = rawFrame;
    if (lensCorrectionOn_ && lensCorrector_.isReady()) {
        const cv::Mat straight = lensCorrector_.apply(camera::qImageToMat(rawFrame));
        if (!straight.empty()) {
            frame = camera::matToQImage(straight).copy();
        }
    }
    // El asistente de calibracion, si esta abierto, come de la camara en vivo.
    if (lensDialog_ != nullptr) {
        lensDialog_->offerFrame(rawFrame);  // SIN corregir: es lo que hay que medir
    }
    video_->setFrame(frame);
    // Si cambia la resolución del frame, reevaluar si la calibración sigue
    // siendo válida (D1); barato porque solo ocurre al cambiar de fuente.
    const QSize previousSize = lastFrame_.size();
    const bool sizeChanged = previousSize != frame.size();
    lastFrame_ = frame;
    // La disponibilidad del pincel depende de que HAYA un frame, y el primero
    // llega después de que se monte la fuente: sin recalcularla aquí el botón
    // se quedaba apagado para siempre.
    //
    // Y DESPUÉS de asignar `lastFrame_`, no antes. Puesta antes iba siempre un
    // frame por detrás: con una imagen abierta el contorno ya estaba en
    // pantalla y el pincel seguía muerto hasta el frame siguiente —un cuarto de
    // segundo con la herramienta apagada sin motivo visible, y para siempre en
    // cualquier fuente que entregue un solo frame.
    updateEdgeBrushAvailability();
    // Primer frame de la sesión: no hay resolución anterior con la que comparar,
    // pero sí la que quedó guardada. Es el único momento en que se puede notar
    // que los ajustes en píxeles vienen de una fuente distinta a la de ahora.
    //
    // La condición mira TODO lo que se guarda en píxeles, no sólo la zona. El
    // cero del tablero también es un punto en coordenadas de imagen, y quien
    // tenga puesto un cero y ninguna zona sufría exactamente el mismo fallo:
    // el origen de todas las medidas de Posición, corrido y sin avisar.
    const bool hasPixelSettings = pipelineConfig_.roi.area() > 0 ||
                                  pipelineConfig_.roiPolygon.size() >= 3 ||
                                  boardConfig_.origin == vision::BoardOrigin::FixedPoint;
    if (previousSize.isEmpty() && !pixelReferenceSize_.isEmpty() &&
        pixelReferenceSize_ != frame.size() && hasPixelSettings) {
        rescalePixelSettings(pixelReferenceSize_, frame.size());
    }
    if (sizeChanged) {
        // El lienzo ya ha olvidado la corrección del borde —sus pasos guardaban
        // coordenadas de la imagen anterior—, así que aquí se suelta la copia
        // que usa el análisis. Se hace desde este lado y no emitiendo desde el
        // lienzo: `setFrame` corre en mitad de la llegada de un frame, y
        // reentrar ahí en el análisis mediría el frame viejo.
        pipelineConfig_.forcePiece = cv::Mat();
        pipelineConfig_.forceBackground = cv::Mat();
        updateEdgeCorrectionChip();
        // Se reacciona al tamaño REAL del frame, no a lo que se pidió: la
        // cámara puede dar otra resolución distinta de la solicitada.
        if (previousSize.isValid() && !previousSize.isEmpty()) {
            rescalePixelSettings(previousSize, frame.size());
        }
        updateCalibrationLabel();
    }
    if (streaming_) {
        // Un frame que llega mientras el anterior sigue esperando es un frame
        // que NADIE va a analizar: se pisa aquí mismo. Contarlo es toda la
        // diferencia entre «va fluido» y «va fluido y mide uno de cada cuatro».
        //
        // Solo cuenta si el análisis hacía falta: con el contorno oculto no se
        // analiza a propósito, y llamar «descartados» a esos frames sería
        // contar como avería lo que el operador ha pedido.
        frames_.frameArrived(analysisNeeded(), !pendingAnalysisFrame_.isNull());
        // El análisis corre siempre: da el fixture que ancla el dibujo en vivo.
        pendingAnalysisFrame_ = frame;
        maybeStartAnalysis();
        updateRateReadout();
    }
}

void MainWindow::onAnalysisFinished() {
    const AnalysisOverlay overlay = analysisWatcher_.result();
    frames_.analysisFinished();
    if (overlay.timed) {
        stageStats_.add(overlay.timings);
    }
    // Zona de trabajo automática (C3): el seguimiento se alimenta SIEMPRE, esté
    // o no abierto el panel, porque es lo que decide el recorte del próximo
    // frame. Si el modo no es automático, el tracker se mantiene en reposo.
    // Solo se alimenta si el frame SE ANALIZÓ. Con la pose congelada (contorno
    // oculto) no se segmenta nada, así que no hay contorno: decirle al
    // seguimiento «no hay pieza» sería afirmar algo que no se ha mirado, y a los
    // dos frames se rendía con un «se dejó de ver la pieza» que era mentira —
    // la pieza estaba ahí, lo que estaba apagado era el contorno.
    if (zoneMode_ == vision::WorkingZoneMode::Automatic && overlay.analysed) {
        const QRectF bounds = overlay.contour.boundingRect();
        autoRoi_.update(overlay.valid && !overlay.contour.isEmpty(),
                        cv::Rect(static_cast<int>(bounds.x()), static_cast<int>(bounds.y()),
                                 static_cast<int>(bounds.width()),
                                 static_cast<int>(bounds.height())),
                        cv::Size(overlay.frameSize.width(), overlay.frameSize.height()));
        // La zona automática cambia con cada frame, así que su dibujo tiene que
        // repintarse aquí. Sin esto solo se refrescaba al cambiar de modo: el
        // recorte se movía de verdad y el operador veía un rectángulo quieto —
        // o ninguno, si no había cambiado de modo desde que arrancó.
        updateWorkingZoneOverlay();
    }
    if (configureDialog_ != nullptr) {
        if (auto* page = configureDialog_->performancePage(); page != nullptr) {
            page->setZoneStatus(effectiveWorkingZone(),
                                cv::Size(overlay.frameSize.width(),
                                         overlay.frameSize.height()),
                                autoRoi_.lastGiveUp());
            page->setStageStats(stageStats_);
        }
    }

    if (overlay.piecesFound >= 0) {
        lastPiecesSeen_ = overlay.piecesFound;
        lastPiecesTooSmall_ = overlay.piecesTooSmall;
        // El recuento con el que trabaja todo lo demas es el de las piezas que
        // se estan TRATANDO como tales: son las que se dibujan, las que se
        // numeran y entre las que navega el selector.
        lastPieceCount_ = overlay.piecesUsed >= 0 ? overlay.piecesUsed : overlay.piecesFound;
        lastMeasuredPiece_ = overlay.measuredPiece;
        // Si la elección se salió del encuadre —cambiaron las piezas de sitio, o
        // desapareció una— el análisis ya ha medido la mayor en su lugar. Aquí se
        // deja constancia de que la elección ya no vale, para que el indicador no
        // siga diciendo que se está midiendo una pieza que no existe.
        if (focusedPiece_ > lastPieceCount_) {
            focusedPiece_ = 0;
            statusBar()->showMessage(
                tr("Ya no hay tantas piezas en el encuadre: se vuelve a medir la mayor."));
        }
    }
    if (configureDialog_ != nullptr) {
        if (auto* pieces = configureDialog_->piecesPage(); pieces != nullptr) {
            // A la pagina Piezas se le dan las MANCHAS y no las usadas: el boton
            // «usar lo que se ve ahora» tiene que poder subir el numero cuando
            // de verdad hay mas piezas de las declaradas. Con las usadas siempre
            // coincidiria con lo declarado y el boton no serviria para corregir
            // nada.
            pieces->setDetectedCount(lastPiecesSeen_);
        }
    }
    updatePiecesChip();

    // Asistente de enfoque (C2): solo se alimenta si el panel está abierto por
    // esa pestaña; medir para nadie sería trabajo tirado.
    if (configureDialog_ != nullptr) {
        if (auto* page = configureDialog_->cameraPage(); page != nullptr) {
            page->setSharpness(overlay.sharpness, overlay.sharpnessOnPiece);
        }
        // Y lo que la escena dice de sí misma, para la pestaña Detección. Va
        // aquí y no en `onFrame` por lo mismo que la nitidez: hacerlo por
        // fotograma para un panel que casi nunca está abierto es trabajo tirado.
        //
        // Quien está en esa pestaña está ahí porque la detección no le funciona,
        // así que es el momento de decirle si su escena es de las que ningún
        // umbral por nivel puede resolver.
        //
        // Y ADEMÁS SE FRENA EN EL TIEMPO. Leer la escena costaba «un desenfoque y
        // dos comparaciones» cuando solo miraba niveles; desde que también mide
        // si el corte recorta la pieza, segmenta la imagen dos veces y cuesta
        // 13,1 ms medidos —el 39 % del presupuesto de un fotograma a 30 Hz—.
        // Eso deja la vista a tirones justo mientras el operador mueve la luz
        // para ver el efecto.
        //
        // Una vez por segundo basta: lo que se está leyendo es la ILUMINACIÓN,
        // que cambia en segundos, no en fotogramas. Refrescarlo treinta veces por
        // segundo no daría ni un dato más.
        constexpr int kSceneReadingEveryMs = 1000;
        if (auto* detection = configureDialog_->detectionPage();
            detection != nullptr && !lastFrame_.isNull() &&
            (!sceneReadingClock_.isValid() ||
             sceneReadingClock_.elapsed() >= kSceneReadingEveryMs)) {
            sceneReadingClock_.restart();
            const cv::Mat frame = camera::qImageToMat(lastFrame_);
            detection->setSceneReading(vision::readScene(frame));
            detection->setBackgroundColour(vision::estimateBackgroundColour(frame));
        }
    }
    if (streaming_) {
        const QString status = overlay.valid
                                   ? tr("Pieza: %1°").arg(overlay.angleDeg, 0, 'f', 1)
                                   : overlay.error;
        if (overlay.valid) {
            // El fixture llega ya estabilizado desde el worker (banda muerta,
            // suavizado y continuidad anti-giro de 180°).
            liveFixture_ = vision::Fixture{{static_cast<float>(overlay.centroid.x()),
                                            static_cast<float>(overlay.centroid.y())},
                                           overlay.angleDeg};
            video_->setPieceBoundsCenter(
                true, {static_cast<float>(overlay.boundsCenter.x()),
                       static_cast<float>(overlay.boundsCenter.y())});
            video_->setLivePiece(true, overlay.contour, overlay.centroid,
                                 overlay.angleDeg, status);
            video_->setLivePieceOutlines(overlay.pieceContours, overlay.measuredPiece,
                                         focusedPiece_ > 0);
            showPiecesInMosaic(overlay);
            currentThumbLabel_->setPixmap(QPixmap::fromImage(overlay.normalized)
                                              .scaled(currentThumbLabel_->size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
        } else {
            liveFixture_.reset();
            video_->setLivePiece(false, overlay.contour, overlay.centroid,
                                 overlay.angleDeg, status);
        }
        // Escala por marcador ArUco: si se detectó este frame, actualiza la
        // escala en vivo (etiquetas y barra) sin persistir (es dinámica).
        if (overlay.liveMmPerPixel > 0.0) {
            calibration_.mmPerPixel = overlay.liveMmPerPixel;
            video_->setMmPerPixel(overlay.liveMmPerPixel);
            // Indicador de calidad (D5): buena / regular / pobre según cuán
            // perpendicular esté la cámara al plano del marcador.
            const double q = overlay.liveScaleQuality;
            const QString quality = q >= 0.9   ? tr("buena")
                                    : q >= 0.75 ? tr("regular — endereza la cámara")
                                                : tr("pobre — cámara muy inclinada");
            calibLabel_->setText(tr("Escala (ArUco): %1 mm/px · calidad %2 (%3%)")
                                     .arg(overlay.liveMmPerPixel, 0, 'f', 4)
                                     .arg(quality)
                                     .arg(q * 100.0, 0, 'f', 0));
        } else if (arucoLiveScale_) {
            calibLabel_->setText(tr("Escala (ArUco): marcador no visible"));
        }
        // Medidas en vivo de las herramientas dibujadas (px o mm calibrados).
        video_->setResults(overlay.toolResults);
        lastToolResults_ = overlay.toolResults;
        // Y el lienzo enseña las cotas de ESA pieza. Sin esto sólo salían las de
        // la primera en orden de lectura, así que enfocar la tercera dejaba la
        // pieza remarcada y las cifras encima de otra.
        video_->setFocusedPiece(overlay.measuredPiece >= 1 ? overlay.measuredPiece - 1 : 0);
        updateBoardReadout();  // desviación y giro respecto al tablero (T3)
        // El contorno corregido ya esta en pantalla: el trazo ha hecho su
        // trabajo y se retira. La correccion sigue en vigor, y el aviso de al
        // lado del modo de medicion lo dice.
        if (hideCorrectionWhenAnalysed_) {
            hideCorrectionWhenAnalysed_ = false;
            video_->setEdgeCorrectionVisible(false);
            updateEdgeCorrectionChip();
        }
        maybeStartAnalysis();
    }
}

// El análisis (segmentación + herramientas) solo hace falta si hay algo que
// mostrar o medir: contorno visible, herramientas dibujadas, auto-inspección
// o una herramienta de dibujo seleccionada (para anclar el próximo trazo).
// Así, apagar "Mostrar contorno" con la escena vacía ahorra CPU de verdad.
bool MainWindow::analysisNeeded() const {
    return streaming_ &&
           (showContourAction_->isChecked() || !liveTools_.empty() || autoInspecting_ ||
            toolPalette_->currentTool().has_value());
}

// «Vuelve a medir ESTA imagen», que no es lo mismo que `maybeStartAnalysis`.
//
// `maybeStartAnalysis` arranca el análisis del frame que esté ESPERANDO, y en
// una foto o en un vídeo en pausa no llega ninguno más: el pendiente se
// consumió en el primer análisis y el hueco se quedó vacío para siempre. Pedir
// un reanálisis sin reponerlo no hacía nada en absoluto.
//
// Ese era el motivo de fondo de que corregir el borde no moviera el contorno
// —y, con él, de que cambiar la detección, la zona o la escala tampoco se
// notara sobre una imagen quieta: los ajustes se guardaban, pero nadie volvía
// a medir con ellos.
//
// Se repone desde `lastFrame_` solo si no hay uno pendiente: si lo hay, es más
// reciente. Y si hay un análisis en vuelo, `maybeStartAnalysis` se retira y lo
// recoge al terminar, que para eso deja el frame puesto.
void MainWindow::reanalyseCurrentFrame() {
    if (lastFrame_.isNull()) {
        return;
    }
    if (pendingAnalysisFrame_.isNull()) {
        pendingAnalysisFrame_ = lastFrame_;
    }
    maybeStartAnalysis();
}

// Como máximo un análisis en vuelo; si la visión va más lenta que la cámara,
// se procesan solo los frames más recientes (se descartan los intermedios).
void MainWindow::maybeStartAnalysis() {
    if (analysisWatcher_.isRunning() || pendingAnalysisFrame_.isNull() || !analysisNeeded()) {
        return;
    }
    const QImage frame = pendingAnalysisFrame_;
    analysedFrame_ = frame;
    pendingAnalysisFrame_ = QImage();
    const auto anchor = currentAnchor_;

    // Copia de las herramientas dibujadas para medirlas sobre este frame.
    std::vector<inspection::ToolConfig> configs;
    configs.reserve(liveTools_.size());
    for (const auto& tool : liveTools_) {
        auto config = tool.config;
        config.geometryJson = inspection::toJson(tool.geometry);
        configs.push_back(std::move(config));
    }

    const bool freeze = !showContourAction_->isChecked();
    const double markerMm = arucoLiveScale_ ? markerSizeMm_ : 0.0;
    // La zona con la que se analiza sale del modo elegido; `pipelineConfig_.roi`
    // sigue guardando la zona que dibujó el operador y no se toca.
    //
    // El orden importa: `effectiveWorkingZone` ya sabe que se está contando y
    // por eso suelta el recorte automático. Antes no lo sabía, y el recuento
    // salía SIEMPRE 1 —el recorte rodea a la pieza mayor y las demás quedan
    // fuera por construcción—, con seis piezas delante del operador.
    vision::PipelineConfig working = pipelineConfig_;
    working.roi = effectiveWorkingZone();
    working.roiPolygon =
        vision::effectiveWorkingPolygon(zoneMode_, pipelineConfig_.roiPolygon);
    const bool countPieces = countingPieces();
    analysisWatcher_.setFuture(QtConcurrent::run(
        [frame, anchor, offset = currentOrientationOffset_, configs = std::move(configs),
         pipeline = working, previous = liveFixture_,
         mm = calibration_.mmPerPixel, unit = currentUnit(), freeze, markerMm,
         board = boardConfig_, countPieces, wanted = focusedPiece_,
         measureStages = measureStages_] {
            return buildOverlay(frame, anchor, offset, configs, pipeline, previous, mm, unit,
                                freeze, markerMm, board, countPieces, wanted, measureStages);
        }));
}

void MainWindow::onStats(double fps, int width, int height) {
    currentResolution_ = {width, height};
    lastCaptureFps_ = fps;
    updateRateReadout();
}

void MainWindow::updateSetupGuide() {
    if (setupBanner_ == nullptr || setupHintLabel_ == nullptr) {
        return;
    }
    SetupState state;
    state.cameraRunning = streaming_;
    state.calibrated = calibration_.valid();
    state.anyPieceRegistered = pieceCombo_ != nullptr && pieceCombo_->count() > 0;
    state.alreadyGuided = setupGuided_;

    state.canFocus = camera::capabilitiesOf(sourceKind_).focusable;
    const QString hint = setupHint(nextSetupStep(state), state.canFocus);
    setupHintLabel_->setText(hint);
    setupBanner_->setVisible(!hint.isEmpty());
}

void MainWindow::dismissSetupGuide() {
    setupGuided_ = true;
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("setup_guided", 1);
    }
    updateSetupGuide();
}

void MainWindow::updateStationStatus() {
    if (stationLights_.empty()) {
        return;
    }
    StationState state;
    state.calibrated = calibration_.valid();
    state.calibrationStale =
        state.calibrated &&
        ((!lastFrame_.isNull() &&
          !calibration_.matchesResolution(lastFrame_.width(), lastFrame_.height())) ||
         (!calibratedCameraKey_.isEmpty() && !currentCameraKey_.isEmpty() &&
          calibratedCameraKey_ != currentCameraKey_));
    state.autoExposureOn = autoExposureOn_;
    state.autoFocusOn = autoFocusOn_;
    state.streaming = streaming_;
    state.zoneActive = effectiveWorkingZone().area() > 0;
    // Si la cámara no deja tocar un control, no es culpa del operador y no se
    // pinta como si lo fuera. Lo dijo el sondeo al abrir.
    for (const auto& control : cameraControls_) {
        if (control.property == camera::CameraProperty::Exposure) {
            state.exposureAdjustable = control.supported;
        }
        if (control.property == camera::CameraProperty::Focus) {
            state.focusAdjustable = control.supported;
        }
    }

    const auto indicators = stationStatus(state);
    for (std::size_t i = 0; i < stationLights_.size() && i < indicators.size(); ++i) {
        const auto& indicator = indicators[i];
        auto* light = stationLights_[i];
        light->setText(indicator.label);
        light->setToolTip(indicator.reason);
        // Aquí el color es del TEXTO, no de un fondo, así que van los tokens
        // de superficie clara. Antes llevaba un verde (#2e7d32) y un rojo
        // (#c62828) propios, distintos de los del resto de la aplicación: el
        // operador que aprende que «lo rojo no cumple» tiene que poder fiarse
        // del mismo rojo en todas partes. Y de paso suben de contraste, porque
        // aquellos rondaban el mínimo.
        const char* colour = theme::kInkOff;
        switch (indicator.light) {
            case StationLight::Good: colour = theme::kGood; break;
            case StationLight::Neutral: colour = theme::kInkOff; break;
            case StationLight::Warning: colour = theme::kWarn; break;
            case StationLight::Bad: colour = theme::kBad; break;
        }
        light->setStyleSheet(
            QStringLiteral("QPushButton { border: none; padding: 0 6px; color: %1; }")
                .arg(QString::fromUtf8(colour)));
        light->disconnect();
        if (indicator.target != ConfigureTarget::None) {
            const ConfigureTarget target = indicator.target;
            connect(light, &QPushButton::clicked, this, [this, target] {
                onConfigureClicked();
                if (configureDialog_ != nullptr) {
                    configureDialog_->showPage(target);
                }
            });
        }
    }
}

void MainWindow::updateRateReadout() {
    if (statsLabel_ == nullptr) {
        return;
    }
    // Con el contorno oculto no hay análisis que medir, así que se pide la
    // forma corta con un −1 en vez de enseñar un cero que parecería una avería.
    const bool analysing = streaming_ && analysisNeeded();

    // Los fps de captura de una IMAGEN no significan nada: se reemite al ritmo
    // que se inventa la aplicación. Enseñar «0.0 fps» sería responder a una
    // pregunta que nadie hizo, y encima con cara de avería. Lo que sí importa
    // es el tamaño —de él depende la calibración— y a qué ritmo se está
    // analizando.
    if (streaming_ && !camera::capabilitiesOf(sourceKind_).meaningfulCaptureFps) {
        QString text = QStringLiteral("%1x%2 — imagen")
                           .arg(currentResolution_.width)
                           .arg(currentResolution_.height);
        if (analysing) {
            text += tr(" · analiza %1").arg(frames_.analysisFps(), 0, 'f', 1);
        }
        statsLabel_->setText(text);
        return;
    }

    statsLabel_->setText(formatRates(currentResolution_.width, currentResolution_.height,
                                     lastCaptureFps_,
                                     analysing ? frames_.analysisFps() : 0.0,
                                     analysing ? frames_.droppedFps() : -1.0));
}

void MainWindow::onCameraError(const QString& message) {
    core::logError("Error de cámara: " + message.toStdString());
    statusBar()->showMessage(tr("Error: %1").arg(message));
}

void MainWindow::onStreamStopped() {
    streaming_ = false;
    if (fileSource_ != nullptr) {
        // `deleteLater` y no `reset()`: esta ranura puede estar corriendo
        // DENTRO de la emisión de `stopped()` de la propia fuente, y destruirla
        // ahí sería tirar el suelo mientras se está de pie encima.
        fileSource_.release()->deleteLater();
    }
    // QUÉ ERA LO QUE SE ESTABA USANDO, antes de olvidarlo.
    //
    // Hace falta unas líneas más abajo: al quitar la entrada del fichero, la
    // selección del desplegable cae en lo que quede en su sitio —la cámara
    // integrada— y eso no lo ha elegido nadie. Queja del taller: «usar imagen,
    // luego cerrarla, y que se ponga cámara integrada arruina la experiencia».
    const camera::SourceKind closedKind = sourceKind_;
    sourceKind_ = camera::SourceKind::Camera;
    freezeButton_->setText(tr("Capturar foto"));
    freezeButton_->setEnabled(false);
    // Se quita la entrada del fichero que estaba abierto. Por su DATO y no por
    // su índice: entre abrir y cerrar puede haberse reenumerado la lista.
    bool hadFileOpen = false;
    for (int i = cameraCombo_->count() - 1; i >= 0; --i) {
        if (cameraCombo_->itemData(i).isValid() &&
            cameraCombo_->itemData(i).toInt() == kSourceOpenedFile) {
            cameraCombo_->removeItem(i);
            hadFileOpen = true;
        }
    }
    // Y LA SELECCIÓN SE QUEDA EN EL MISMO TIPO QUE SE ACABA DE CERRAR.
    //
    // Quitar un elemento de un QComboBox deja la selección en el que ocupe ese
    // sitio, que aquí es la primera cámara. Nadie lo eligió: es la consecuencia
    // de borrar la entrada, y desde fuera se vive como que el programa cambia
    // de fuente solo.
    //
    // Lo que sigue a cerrar una imagen es abrir otra, casi siempre la de al
    // lado en la misma carpeta. Así que el desplegable se queda en «Abrir
    // imagen…» y basta con darle a Iniciar.
    //
    // Con las señales BLOQUEADAS: elegir en este desplegable abre el diálogo de
    // fichero, y esto no es una elección del operador. Igual que al restaurar
    // la fuente al arrancar, se PRESELECCIONA y nada más — un programa que al
    // cerrar un fichero se pone a abrir otro hace algo que nadie ha pedido.
    if (hadFileOpen) {
        const int wanted = closedKind == camera::SourceKind::Video ? kSourceOpenVideo
                                                                  : kSourceOpenImage;
        if (const int index = cameraCombo_->findData(QVariant(wanted)); index >= 0) {
            QSignalBlocker blocker(cameraCombo_);
            cameraCombo_->setCurrentIndex(index);
        }
    }
    autoInspectButton_->setChecked(false);
    stopLiveCapture();
    startStopButton_->setText(tr("Iniciar"));
    // Siempre habilitado: aunque no haya ninguna cámara, se puede abrir una
    // imagen o un vídeo.
    startStopButton_->setEnabled(true);
    cameraCombo_->setEnabled(true);
    refreshAction_->setEnabled(true);
    showVideoBar(false);
    statsLabel_->clear();
    pendingAnalysisFrame_ = QImage();
    lastFrame_ = QImage();
    video_->clearLive();
    liveFixture_.reset();
    cameraControls_.clear();
    // El panel Configurar es no modal: si sigue abierto tras detener la cámara,
    // los deslizadores de su página de cámara no harían nada. Se cierra en vez
    // de mentir; al volver a abrirlo se reconstruye con lo que haya.
    if (configureDialog_ != nullptr) {
        configureDialog_->close();
    }

    // La fuente que se pidió mientras la anterior seguía en marcha.
    //
    // Se arranca DIFERIDA y no aquí mismo, y la diferencia importa: esta ranura
    // puede estar corriendo dentro de la emisión de `stopped()` de la fuente que
    // acaba de morir, y abrir un diálogo de fichero MODAL ahí dentro es parar el
    // desmontaje a la mitad y quedarse esperando. `singleShot(0)` lo saca al
    // bucle de eventos, con el apagado ya terminado.
    if (pendingSourceChoice_.has_value()) {
        const int wanted = *pendingSourceChoice_;
        pendingSourceChoice_.reset();
        if (const int index = cameraCombo_->findData(QVariant(wanted)); index >= 0) {
            QSignalBlocker blocker(cameraCombo_);
            cameraCombo_->setCurrentIndex(index);
        }
        QTimer::singleShot(0, this, &MainWindow::onStartStopClicked);
    }
    updateBoardReadout();      // "sin pieza detectada" al cortar la transmisión
    updateStatusIndicators();  // cámara vuelve a rojo (S4)
}

void MainWindow::setControlsEnabled(bool enabled) {
    cameraCombo_->setEnabled(enabled);
    refreshAction_->setEnabled(enabled);
    // Sin cámaras el botón sigue vivo: el desplegable siempre ofrece abrir una
    // imagen o un vídeo.
    startStopButton_->setEnabled(enabled);
}

// --- Herramientas dibujadas sobre el video ---------------------------------

void MainWindow::onToolModeChanged(std::optional<inspection::ToolType> chosen) {
    if (!chosen.has_value()) {
        video_->setCreateType(std::nullopt);
        statusBar()->showMessage(tr("Modo mover: clic para seleccionar, arrastra para mover."));
        return;
    }
    const auto type = *chosen;
    video_->setCreateType(type);
    // Elegir herramienta exige el fixture: reactiva el análisis si estaba
    // pausado por tener el contorno oculto y la escena vacía.
    reanalyseCurrentFrame();
    // La primera línea de la descripción como guía inmediata.
    const QString description = QString::fromUtf8(inspection::toolTypeDescription(type));
    statusBar()->showMessage(description.section(QLatin1Char('\n'), 0, 1));
}

void MainWindow::onLiveToolCreated(const inspection::ToolGeometry& geometry) {
    inspection::EditedTool tool;
    tool.geometry = geometry;
    tool.config.type = inspection::typeOf(geometry);
    ++toolNameCounter_;
    tool.config.name = (typeLabel(tool.config.type) +
                        QStringLiteral(" %1").arg(toolNameCounter_))
                           .toStdString();
    tool.config.geometryJson = inspection::toJson(geometry);
    tool.config.toleranceMin = 0.0;
    tool.config.toleranceMax = 100000.0;

    // Medir la pieza actual de inmediato y sugerir tolerancias alrededor de
    // ese valor: la pieza buena define su propio rango de aceptación.
    QString hint;
    if (liveFixture_.has_value() && !lastFrame_.isNull()) {
        // Mismo tablero que está dibujado: si la herramienta es de Posición, la
        // tolerancia sugerida se calcula respecto al cero que el operador ve.
        const vision::BoardFrame board = video_->boardFrame();
        const auto result =
            inspection::runTool(camera::qImageToMat(lastFrame_), *liveFixture_, tool.config,
                                calibration_.mmPerPixel, currentUnit(), cv::Mat(), &board);
        if (result.isOk() && !result.value().detail.empty() &&
            (result.value().ok || result.value().measured > 0.0)) {
            inspection::suggestTolerances(tool.config.type, result.value().measured,
                                          tool.config.toleranceMin,
                                          tool.config.toleranceMax);
            // La unidad la decide `formatMeasure`, no esta pantalla.
            const QString measure = QString::fromStdString(inspection::formatMeasure(
                result.value(), calibration_.mmPerPixel, currentUnit()));
            hint = tr("%1 — midió %2; tolerancias sugeridas [%3, %4]")
                       .arg(QString::fromStdString(tool.config.name), measure)
                       .arg(tool.config.toleranceMin, 0, 'f', 1)
                       .arg(tool.config.toleranceMax, 0, 'f', 1);
        } else {
            hint = tr("%1 creada, pero no midió en este frame (%2) — ajusta su posición")
                       .arg(QString::fromStdString(tool.config.name),
                            QString::fromStdString(result.isOk() ? result.value().detail
                                                                 : result.error().message));
        }
    }
    liveTools_.push_back(std::move(tool));
    commitUndoState();

    video_->clearResults();
    const int newIndex = static_cast<int>(liveTools_.size()) - 1;
    video_->setSelectedIndex(newIndex);
    onLiveSelectionChanged(newIndex);  // sincroniza el spin de "Puntos"
    statusBar()->showMessage(hint.isEmpty()
                                 ? tr("%1 creada").arg(QString::fromStdString(
                                       liveTools_.back().config.name))
                                 : hint);
}

// Un movimiento terminó (arrastre en el canvas): estado nuevo, undoable.
void MainWindow::onLiveToolModified() {
    video_->clearResults();
    commitUndoState();
}

void MainWindow::onDeleteToolClicked() {
    auto indices = video_->selectedIndices();
    if (indices.empty()) {
        return;
    }
    // De mayor a menor para que los índices no se corran al borrar.
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (const int index : indices) {
        if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
            continue;
        }
        const auto& tool = liveTools_[static_cast<std::size_t>(index)];
        if (tool.config.id >= 0 && repos_.tools != nullptr) {
            // Si se deshace el borrado, el guardado reinsertará la fila.
            if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
                statusBar()->showMessage(QString::fromStdString(removed.error().message));
                continue;
            }
        }
        liveTools_.erase(liveTools_.begin() + index);
    }
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
}

void MainWindow::onDeleteAllToolsClicked() {
    const int total = static_cast<int>(liveTools_.size());

    // SIN PIEZA ABIERTA TAMBIÉN HAY QUE PODER BORRAR.
    //
    // Queja de uso: «la herramienta de borrar todo no detecta nada o no me deja
    // usarla, hasta que selecciono una pieza». Era exacto, y con una ironía
    // dentro: este botón se iba EN SILENCIO cuando `liveTools_` estaba vacío, y
    // `liveTools_` solo se llena al seleccionar pieza. O sea que la salida
    // «borrar las de todas las piezas» —que se añadió justo para no tener que ir
    // pieza por pieza— vivía dentro de un diálogo que no se abría nunca si no
    // habías entrado en una.
    //
    // Ahora el recuento de TODO el programa se hace antes de decidir si hay algo
    // que hacer, y no después.
    repositories::ToolRepository::ToolTally everywhere;
    if (repos_.tools != nullptr) {
        if (auto tally = repos_.tools->tallyAll(); tally.isOk()) {
            everywhere = tally.value();
        }
    }

    const DeleteScope scope = decideDeleteScope(total, everywhere.tools);
    if (scope.nothingAnywhere) {
        // Y SI NO HAY NADA EN NINGUNA PARTE, SE DICE. Un botón que no hace nada
        // y no explica por qué se lee como un botón roto: el operador vuelve a
        // pulsarlo, y luego busca qué ha hecho mal.
        statusBar()->showMessage(
            tr("No hay ninguna herramienta que borrar, ni en esta pieza ni en las demás."));
        return;
    }

    // Se pregunta, y la pregunta DICE CUÁNTAS. «¿Seguro?» a secas no informa de
    // nada: quien lleva media hora dibujando necesita ver el número para
    // reconocer si es el trabajo que cree o el de otra pieza que abrió sin
    // darse cuenta. Es la misma regla que ya sigue el botón de insertar
    // propuestas, que dice cuántas va a añadir.
    //
    // Y se dice que hay vuelta atrás: el miedo a un botón destructivo viene de
    // no saber si se puede deshacer, y aquí se puede.
    // ¿HAY HERRAMIENTAS EN OTRAS PIEZAS?
    //
    // De esto salió una queja de uso: «el botón de borrar todas las herramientas
    // no debería de ocupar seleccionar las piezas de una en una». Y era verdad:
    // el botón borra las de la pieza ABIERTA, así que vaciar el trabajo entero
    // obligaba a ir al combo, cambiar de pieza, confirmar, y repetir.
    //
    // No se añade un botón nuevo. Se añade una SEGUNDA SALIDA al diálogo que ya
    // existe, y solo cuando de verdad hay algo en otras piezas: un botón para
    // borrarlo todo, visible siempre, sería un botón peligroso a la vista de
    // alguien que casi nunca lo necesita.
    // Hay trabajo fuera de esta pieza si el total del programa supera al de
    // aquí. Con la pieza sin abrir (`total` = 0) eso es cierto en cuanto haya
    // una sola herramienta guardada, que es justo el caso que antes se perdía.
    const bool othersHaveTools = scope.offerEverywhere;

    // EL TÍTULO DICE DE QUÉ SE HABLA. Sin pieza abierta no hay «esta pieza», y
    // un diálogo que anuncia «se van a borrar las 0 herramientas de esta pieza»
    // parece un error del programa en vez de una pregunta.
    QMessageBox box(
        QMessageBox::Warning, tr("Borrar todas las herramientas"),
        total > 0
            ? tr("Se van a borrar las %n herramienta(s) de esta pieza.", nullptr, total)
            : tr("No hay ninguna pieza abierta, pero el programa guarda %n "
                 "herramienta(s).", nullptr, everywhere.tools),
        QMessageBox::NoButton, this);
    if (othersHaveTools) {
        // LA VERDAD SOBRE EL DESHACER, que es lo delicado de esta opción.
        //
        // Ctrl+Z guarda el estado de las herramientas de la pieza ABIERTA. Puede
        // devolver las de esa y no tiene forma de devolver las de las demás,
        // porque nunca las tuvo en memoria. Un «se puede deshacer» que solo
        // funciona a medias es peor que no prometer nada, así que las dos
        // salidas dicen exactamente lo que se puede recuperar de cada una.
        box.setInformativeText(
            total > 0
                ? tr("Borrar las de esta pieza se puede deshacer con Ctrl+Z.\n\n"
                     "En el programa hay %1 herramientas repartidas en %2 piezas. "
                     "Borrarlas TODAS de una vez NO se puede deshacer.")
                      .arg(everywhere.tools)
                      .arg(everywhere.pieces)
                // Sin pieza abierta NO se menciona Ctrl+Z, y no es un olvido:
                // la pila de deshacer guarda las herramientas de la pieza
                // abierta, y aquí no hay ninguna. Prometer una vuelta atrás que
                // no existe es peor que avisar de que no la hay.
                : tr("Están repartidas en %1 piezas. Borrarlas NO se puede "
                     "deshacer.")
                      .arg(everywhere.pieces));
    } else {
        box.setInformativeText(tr("Se puede deshacer con Ctrl+Z."));
    }
    // La salida «esta pieza» solo existe si hay una pieza con algo dentro.
    QPushButton* confirm = nullptr;
    if (scope.offerThisPiece) {
        confirm = box.addButton(tr("Borrar las %n de esta pieza", nullptr, total),
                                QMessageBox::DestructiveRole);
    }
    QPushButton* confirmAll = nullptr;
    if (othersHaveTools) {
        confirmAll = box.addButton(
            tr("Borrar las %1 de las %2 piezas").arg(everywhere.tools).arg(everywhere.pieces),
            QMessageBox::DestructiveRole);
    }
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);  // el defecto NUNCA es el destructivo
    box.exec();
    if (confirmAll != nullptr && box.clickedButton() == confirmAll) {
        auto removed = repos_.tools->removeAllTools();
        if (!removed.isOk()) {
            statusBar()->showMessage(QString::fromStdString(removed.error().message));
            return;
        }
        liveTools_.clear();
        // Se limpia también la pila de deshacer: dejarla con un estado anterior
        // haría que Ctrl+Z devolviera las herramientas de ESTA pieza y ninguna de
        // las demás, que es justo el medio deshacer que se acaba de prometer que
        // no habría.
        undoStack_.clear();
        commitUndoState();
        video_->setSelectedIndex(-1);
        onLiveSelectionChanged(-1);
        video_->clearResults();
        statusBar()->showMessage(
            tr("%1 herramientas borradas de %2 piezas. Esto no se puede deshacer.")
                .arg(removed.value())
                .arg(everywhere.pieces));
        return;
    }
    if (confirm == nullptr || box.clickedButton() != confirm) {
        return;
    }

    for (const auto& tool : liveTools_) {
        if (tool.config.id >= 0 && repos_.tools != nullptr) {
            if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
                statusBar()->showMessage(QString::fromStdString(removed.error().message));
            }
        }
    }
    liveTools_.clear();
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    statusBar()->showMessage(
        tr("%n herramienta(s) borrada(s). Ctrl+Z las devuelve.", nullptr, total));
}

// Marcar el rasgo distintivo: el siguiente clic sobre la pieza en el video
// define el punto; se guarda de inmediato si hay una pieza seleccionada.
void MainWindow::onAnchorButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setPickMode(false);
        return;
    }

    // Si la pieza ya tiene rasgo, ofrecer quitarlo o reemplazarlo.
    if (currentAnchor_.has_value()) {
        QMessageBox box(QMessageBox::Question, tr("Rasgo distintivo"),
                        tr("Esta pieza ya tiene un rasgo distintivo. ¿Qué quieres hacer?"),
                        QMessageBox::NoButton, this);
        auto* removeBtn = box.addButton(tr("Quitar rasgo"), QMessageBox::DestructiveRole);
        auto* replaceBtn = box.addButton(tr("Marcar otro"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        anchorButton_->setChecked(false);
        if (box.clickedButton() == removeBtn) {
            currentAnchor_.reset();
            video_->setAnchorMarker(false);
            if (const std::int64_t pieceId = selectedPieceId();
                pieceId >= 0 && repos_.pieces != nullptr) {
                repos_.pieces->clearAnchor(pieceId);
            }
            statusBar()->showMessage(tr("Rasgo distintivo eliminado."));
            return;
        }
        if (box.clickedButton() != replaceBtn) {
            return;
        }
        // Reemplazar: continúa al modo de selección abajo.
    }

    if (!streaming_ || !liveFixture_.has_value()) {
        statusBar()->showMessage(
            tr("Para marcar el rasgo necesitas video en vivo con la pieza detectada."));
        anchorButton_->setChecked(false);
        return;
    }
    video_->setPickMode(true);
    statusBar()->showMessage(
        tr("Haz clic sobre un punto único de la pieza (agujero, marca, esquina oscura)…"));
}

// Lectura continua "cuánto está descentrada y girada la pieza" respecto al
// tablero (T3). Con el origen en la propia pieza la desviación es cero por
// definición, así que ahí solo tiene sentido el giro: se dice explícitamente en
// vez de mostrar un 0,0 que parecería un fallo.
void MainWindow::updateBoardReadout() {
    if (boardReadoutLabel_ == nullptr) {
        return;
    }
    if (!boardVisible_) {
        boardReadoutLabel_->setVisible(false);
        return;
    }
    boardReadoutLabel_->setVisible(true);
    if (!liveFixture_.has_value()) {
        boardReadoutLabel_->setText(tr("Tablero — sin pieza detectada"));
        return;
    }

    const vision::BoardFrame frame = video_->boardFrame();
    const vision::BoardReading reading = vision::readPiece(frame, *liveFixture_);
    const double offsetDeg = vision::pieceAngleOffset(frame, *liveFixture_);

    // Misma unidad que el resto de la UI: mm/cm si hay escala, px si no.
    const inspection::LengthUnit unit = currentUnit();
    const double mmPerPixel = calibration_.mmPerPixel;
    const auto len = [unit, mmPerPixel](double px) {
        if (mmPerPixel > 0.0 && unit != inspection::LengthUnit::Pixels) {
            const double mm = px * mmPerPixel;
            return (unit == inspection::LengthUnit::Centimeters)
                       ? QStringLiteral("%1 cm").arg(mm / 10.0, 0, 'f', 2)
                       : QStringLiteral("%1 mm").arg(mm, 0, 'f', 1);
        }
        return QStringLiteral("%1 px").arg(px, 0, 'f', 0);
    };
    const auto signedLen = [&len](double px) {
        return (px > 0.0 ? QStringLiteral("+") : QString()) + len(px);
    };

    // Reglas activas (M4): se muestran junto a la lectura para que el operador
    // pueda colocar la pieza ANTES de inspeccionar, y la banda se pone en rojo
    // cuando la posición actual daría NG.
    QString limits;
    bool outOfTolerance = false;
    if (measurementMode_ == domain::MeasurementMode::Special) {
        if (maxOffsetPx_ > 0.0) {
            limits += tr("  ·  máx %1").arg(len(maxOffsetPx_));
            outOfTolerance = outOfTolerance || reading.radius > maxOffsetPx_;
        }
        if (maxAngleDeg_ > 0.0) {
            limits += tr("  ·  máx %1°").arg(maxAngleDeg_, 0, 'f', 1);
            outOfTolerance = outOfTolerance || std::abs(offsetDeg) > maxAngleDeg_;
        }
    }
    // setStyleSheet reanaliza el CSS y repule el widget: llamarlo en cada
    // análisis (unas 30 veces por segundo) costaba CPU y hacía parpadear la
    // banda. Solo se cambia cuando cambia de estado.
    if (outOfTolerance != boardReadoutAlarm_) {
        boardReadoutAlarm_ = outOfTolerance;
        // El mismo estilo que al crearla, y de la misma fuente: estaba tecleado
        // dos veces, y así es como una copia acaba con otra cifra.
        boardReadoutLabel_->setStyleSheet(
            outOfTolerance
                ? theme::bandStyle(theme::kInkOnBandAlarm, theme::kBandAlarm, true)
                : theme::bandStyle(theme::kInkOnBand, theme::kBandField));
    }

    const bool zeroOnPiece = boardConfig_.origin == vision::BoardOrigin::PieceCenter ||
                             boardConfig_.origin == vision::BoardOrigin::PieceBounds;
    if (zeroOnPiece && boardConfig_.manualOffset.x == 0.0F &&
        boardConfig_.manualOffset.y == 0.0F) {
        // El cero está sobre la pieza: su desviación es 0 por definición.
        boardReadoutLabel_->setText(
            tr("Tablero — el cero viaja con la pieza · giro %1°%2")
                .arg(offsetDeg, 0, 'f', 1)
                .arg(limits));
        return;
    }
    boardReadoutLabel_->setText(tr("Tablero — dx %1 · dy %2 · radio %3 · giro %4°%5")
                                    .arg(signedLen(reading.dx), signedLen(reading.dy),
                                         len(reading.radius))
                                    .arg(offsetDeg, 0, 'f', 1)
                                    .arg(limits));
}

// Aplica a toda la UI el modo y el tablero de una pieza. Decisión del usuario
// (2026-07-27): manda la pieza — en modo Especial el tablero se enciende con su
// configuración y en modo Real se apaga.
void MainWindow::applyMeasurement(const repositories::PieceMeasurement& measurement) {
    measurementMode_ = measurement.mode;
    boardConfig_ = measurement.board;
    maxOffsetPx_ = measurement.maxOffsetPx;
    maxAngleDeg_ = measurement.maxAngleDeg;
    boardVisible_ = measurement.mode == domain::MeasurementMode::Special;

    video_->setBoardConfig(boardConfig_);
    video_->setBoardVisible(boardVisible_);
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }

    // Menús al día sin re-disparar sus señales (evita guardados en cascada).
    if (boardAction_ != nullptr) {
        QSignalBlocker blocker(boardAction_);
        boardAction_->setChecked(boardVisible_);
    }
    if (boardFollowAction_ != nullptr) {
        QSignalBlocker blocker(boardFollowAction_);
        boardFollowAction_->setChecked(boardConfig_.followPieceAngle);
    }
    if (boardOriginGroup_ != nullptr) {
        for (auto* action : boardOriginGroup_->actions()) {
            if (action->data().toInt() == static_cast<int>(boardConfig_.origin)) {
                QSignalBlocker blocker(action);
                action->setChecked(true);
            }
        }
    }
    updateModeChip();
    updateBoardReadout();
}

// Etiqueta del modo activo (M3). Cambia de color para distinguirse de un
// vistazo y el tooltip explica qué implica el modo y dónde se cambia.
// El recuento de piezas, donde se trabaja.
//
// Se destaca cuando hay MÁS de una, porque es el único caso en que cambia lo
// que el operador debe hacer: con varias en el encuadre, las herramientas miden
// la mayor y las demás quedan sin medir. Con una sola, el aviso sería ruido.
void MainWindow::updatePiecesChip() {
    if (piecesChip_ == nullptr) {
        return;
    }
    if (lastPieceCount_ < 0 || !countingPieces()) {
        piecesChip_->setVisible(false);
        // Y el selector de pieza con el. Sin esta linea se quedaba a la vista
        // diciendo «pieza 1/2» despues de que el operador declarara que hay una
        // sola: estado viejo en pantalla, que es peor que no enseñar nada.
        updatePieceNavigator();
        return;
    }
    const bool several = lastPieceCount_ > 1;
    const bool someLeftOut = lastPiecesSeen_ > lastPieceCount_;
    piecesChip_->setVisible(true);
    // Sin `%n`: el plural de Qt sólo se resuelve con un traductor cargado, y sin
    // él esto se queda en «6 pieza(s)» de forma permanente en pantalla.
    // Cuando sobran manchas se dicen LAS DOS cifras. Enseñar solo las usadas
    // haria desaparecer del informe una sombra de mas sin dejar rastro; enseñar
    // solo las vistas contradiria al selector, que numera las usadas.
    piecesChip_->setText(
        someLeftOut ? tr(" %1 de %2 ").arg(lastPieceCount_).arg(lastPiecesSeen_)
                    : (several ? tr(" %1 piezas ").arg(lastPieceCount_) : tr(" 1 pieza ")));
    piecesChip_->setStyleSheet(
        (several || someLeftOut)
            ? theme::noticeStyle(theme::kWarn, theme::kWarnField) +
                  QStringLiteral(" border-radius:8px; padding:1px 6px; font-weight:bold;")
            : QStringLiteral("color:%1; background:%2; border-radius:8px;"
                             " padding:1px 6px;")
                  .arg(QString(theme::kInkMuted), QString(theme::kSurfaceSunken)));
    // LO QUE SE CAYÓ POR PEQUEÑO, DICHO.
    //
    // Estas manchas se descartan ANTES de contarse, así que no aparecen en
    // ninguna de las dos cifras de arriba. Sin esta frase el operador ve «1
    // pieza» sobre una foto con dieciséis y no tiene ni el número ni idea de qué
    // tocar. Medido sobre la foto de catálogo `arandelas-2`: el área mínima de
    // fábrica deja UNA de dieciséis arandelas.
    //
    // Va con el ajuste que lo arregla dentro del texto, porque un aviso que dice
    // que algo pasa y no dice dónde se toca obliga a buscarlo.
    const QString tooSmall =
        lastPiecesTooSmall_ > 0
            ? tr("\n\nAdemás, %1 mancha(s) más se quedaron fuera por no llegar al "
                 "área mínima. Si son piezas tuyas, baja «Área mínima» en "
                 "Configurar ▸ Detección.")
                  .arg(lastPiecesTooSmall_)
            : QString();

    if (someLeftOut) {
        piecesChip_->setToolTip(
            tr("Se ven %1 manchas y has declarado %2 piezas: se trabaja con las %2 "
               "mayores y el resto no se mide.")
                .arg(lastPiecesSeen_)
                .arg(lastPieceCount_) +
            tooSmall);
        updatePieceNavigator();
        return;
    }
    piecesChip_->setToolTip(
        (several ? tr("Se ven %1 piezas en el encuadre.\n\n"
                     "Las herramientas miden UNA: la que dice el selector de al lado. "
                     "Las demás se cuentan y se pueden mirar una a una con las flechas.")
                      .arg(lastPieceCount_)
                : tr("Se ve una sola pieza en el encuadre.")) +
        tooSmall);
    updatePieceNavigator();
}

// Cuál de las piezas se está midiendo, y cómo pasar a otra.
//
// El número va en ORDEN DE LECTURA —filas de arriba abajo, izquierda a derecha—
// que es el mismo con el que salen de la detección, y por eso «la tercera»
// significa lo mismo en la pantalla, en el informe y en la mesa.
// EL MOSAICO COME DE LO MISMO QUE EL VÍDEO.
//
// Los contornos ya los calculó el análisis; volver a segmentar para pintar un
// panel sería pagar dos veces por la misma respuesta, y con el riesgo de que
// las dos no coincidieran — que es peor que no tener panel: el operador
// elegiría la pieza 3 del mosaico y se le mediría otra.
//
// El recorte sale de `analysedFrame_` y no de `lastFrame_` por lo mismo: el
// análisis va por detrás del vídeo y los contornos son de aquel frame.
void MainWindow::showPiecesInMosaic(const AnalysisOverlay& overlay) {
    if (mosaic_ == nullptr || mosaicDock_ == nullptr) {
        return;
    }
    // Con una sola pieza el panel no añade nada: el vídeo ya la enseña entera y
    // más grande. Se deja lo último que hubo en vez de vaciarlo, porque vaciar
    // en cada fotograma sin varias piezas haría parpadear el panel abierto.
    if (overlay.pieceContours.size() <= 1) {
        return;
    }
    if (!mosaicOffered_) {
        mosaicOffered_ = true;
        mosaicDock_->setVisible(true);
    }
    // CON EL PANEL CERRADO NO SE PINTA NADA.
    //
    // Reconstruirlo cuesta 12,3 ms con la bandeja de cien tuercas (medido sobre
    // la imagen real). Eso entra de sobra en un fotograma —techo de 81 por
    // segundo— pero solo si alguien lo está mirando: gastarlo en cada análisis
    // con el panel cerrado es tirar un tercio del presupuesto de fotograma para
    // pintar algo que nadie ve.
    if (!mosaicDock_->isVisible()) {
        return;
    }
    mosaic_->setPieces(analysedFrame_, overlay.pieceContours, overlay.measuredPiece);
}

void MainWindow::showMosaicPanel(bool on) {
    if (mosaicDock_ == nullptr) {
        return;
    }
    // Pedirlo a mano cuenta como haberlo decidido: ya no se le vuelve a ofrecer
    // solo la próxima vez que aparezcan varias piezas. Ofrecerle un panel a
    // quien acaba de apagarlo es no haberle escuchado.
    mosaicOffered_ = true;
    mosaicDock_->setVisible(on);
}

void MainWindow::updatePieceNavigator() {
    if (pieceNav_ == nullptr) {
        return;
    }
    const bool several = lastPieceCount_ > 1 && countingPieces();
    pieceNav_->setVisible(several);
    if (!several) {
        return;
    }
    const int shown = lastMeasuredPiece_ > 0 ? lastMeasuredPiece_ : 1;
    // Se dice cuándo la elección es del programa y cuándo es del operador. Sin
    // eso, «3 / 6» no distingue «he elegido la 3» de «te ha tocado la 3».
    pieceNavLabel_->setText(focusedPiece_ == 0
                                ? tr(" pieza %1/%2 (la mayor) ").arg(shown).arg(lastPieceCount_)
                                : tr(" pieza %1/%2 ").arg(shown).arg(lastPieceCount_));
    // En reposo mide la mayor —lo decide la aplicación— y elegida la señaló el
    // operador. El azul de «elegida» era aquí #7fd1ff y en el indicador de modo
    // #7fd6ff: dos azules para lo mismo, que es exactamente lo que la paleta
    // viene a impedir.
    pieceNavLabel_->setStyleSheet(focusedPiece_ == 0 ? theme::chipRestStyle()
                                                     : theme::chipChosenStyle());
    const QString tip =
        tr("Qué pieza del encuadre están midiendo las herramientas.\n\n"
           "Numeradas en orden de lectura: por filas de arriba abajo, y dentro de\n"
           "cada fila de izquierda a derecha. Las flechas pasan de una a otra para\n"
           "ver cómo sale cada una.\n\n"
           "Sin elegir ninguna se mide la mayor, que es lo de siempre. Pasa de la\n"
           "última a «la mayor» para volver a ese modo.");
    pieceNavLabel_->setToolTip(tip);
    piecePrevButton_->setToolTip(tip);
    pieceNextButton_->setToolTip(tip);
}

// Pasar a la pieza siguiente o a la anterior.
//
// El recorrido incluye el estado «la mayor» como si fuera una posición más, al
// final: así se sale del modo manual con el mismo gesto con el que se entró, y
// no hace falta descubrir otro control para volver.
void MainWindow::stepFocusedPiece(int delta) {
    if (lastPieceCount_ <= 1) {
        return;
    }
    const int positions = lastPieceCount_ + 1;  // 1..N, más «la mayor» en el 0
    // UN RECORRIDO LLANO: 0 (la mayor), 1, 2, ... N, y vuelta a empezar.
    //
    // La primera versión intentaba ser lista: avanzar desde «la mayor» llevaba a
    // la siguiente de la que se estaba midiendo, para que el salto fuera al
    // vecino de lo que el operador tiene delante. La prueba lo tumbó, y con
    // razón — al volver a «la mayor» se recalculaba lo mismo, así que se quedaba
    // rebotando entre esas dos posiciones y las demás piezas eran INALCANZABLES
    // avanzando. Con tres piezas, dos no se podían mirar.
    //
    // Un recorrido predecible en el que todas las posiciones salen antes o
    // después vale más que uno que acierta el atajo y pierde piezas.
    focusedPiece_ = ((focusedPiece_ + delta) % positions + positions) % positions;
    if (focusedPiece_ == 0) {
        statusBar()->showMessage(tr("Midiendo la pieza mayor del encuadre."));
    } else {
        statusBar()->showMessage(
            tr("Midiendo la pieza %1 de %2, en orden de lectura.")
                .arg(focusedPiece_)
                .arg(lastPieceCount_));
    }
    updatePieceNavigator();
    reanalyseCurrentFrame();
}

// El aviso de que el borde lleva una correccion a mano.
//
// Existe porque el trazo se retira una vez aplicado: sin el, una correccion
// activa seria estado invisible. Dice cuantos pixeles y como quitarla.
void MainWindow::updateEdgeCorrectionChip() {
    if (edgeChip_ == nullptr || video_ == nullptr) {
        return;
    }
    const int corrected = video_->correctedPixelCount();
    if (corrected <= 0) {
        edgeChip_->setVisible(false);
        if (brushUndoAction_ != nullptr) {
            brushUndoAction_->setEnabled(video_->canUndoEdgeCorrection());
        }
        if (brushRedoAction_ != nullptr) {
            brushRedoAction_->setEnabled(video_->canRedoEdgeCorrection());
        }
        return;
    }
    // ¿SE ESTÁ APLICANDO DE VERDAD?
    //
    // La pastilla miraba solo lo que tiene el LIENZO pintado, y eso no es lo
    // mismo que lo que usa el análisis. Con una corrección que no encaja con el
    // frame actual, la pastilla decía «Borde corregido» mientras el contorno
    // salía sin corregir — y el operador no tenía forma de saber cuál de las dos
    // cosas creerse.
    //
    // Una etiqueta que afirma algo que no está pasando es peor que no tener
    // etiqueta: la primera se cree.
    const bool applied = !pipelineConfig_.forcePiece.empty() &&
                         (lastFrame_.isNull() ||
                          (pipelineConfig_.forcePiece.cols == lastFrame_.width() &&
                           pipelineConfig_.forcePiece.rows == lastFrame_.height()));
    edgeChip_->setVisible(true);
    if (!applied) {
        edgeChip_->setText(tr(" Borde corregido — sin aplicar "));
        edgeChip_->setStyleSheet(theme::noticeStyle(theme::kWarn, theme::kWarnField) +
                                 QStringLiteral(" border-radius:8px; padding:1px 6px;"));
        edgeChip_->setToolTip(
            tr("Hay %1 px pintados a mano, pero no se están aplicando: la corrección "
               "es de una imagen de otro tamaño.\n\n"
               "Vuelve a corregir sobre la imagen que tienes delante.")
                .arg(corrected));
        if (brushUndoAction_ != nullptr) {
            brushUndoAction_->setEnabled(video_->canUndoEdgeCorrection());
        }
        if (brushRedoAction_ != nullptr) {
            brushRedoAction_->setEnabled(video_->canRedoEdgeCorrection());
        }
        return;
    }
    edgeChip_->setText(tr(" Borde corregido "));
    edgeChip_->setStyleSheet(theme::chipChosenStyle(theme::kChipEdited));
    edgeChip_->setToolTip(tr("%1 px del borde están puestos a mano.\n\n"
                             "El trazo ya no se pinta: lo que ves es el contorno que sale de "
                             "la corrección, no la pincelada. Con el pincel activo, Ctrl+Z deshace "
                             "la última; "
                             "en \u00abCorregir borde\u00bb puedes quitarlas todas o afinar la "
                             "detección con ellas.")
                              .arg(corrected));
    if (brushUndoAction_ != nullptr) {
        brushUndoAction_->setEnabled(video_->canUndoEdgeCorrection());
    }
    if (brushRedoAction_ != nullptr) {
        brushRedoAction_->setEnabled(video_->canRedoEdgeCorrection());
    }
}

void MainWindow::updateModeChip() {
    if (modeChip_ == nullptr) {
        return;
    }
    const bool special = measurementMode_ == domain::MeasurementMode::Special;
    modeChip_->setText(special ? tr(" Especial (tablero) ") : tr(" Posición real "));
    modeChip_->setStyleSheet(special ? theme::chipChosenStyle()
                                     : theme::chipRestStyle());
    modeChip_->setToolTip(QString::fromUtf8(domain::modeDescription(measurementMode_)) +
                          tr("\n\nSe cambia en Pieza ▸ Modo de medición…"));
}

// Herramientas de Posición dibujadas ahora mismo. Sus tolerancias se sugieren
// respecto al cero del tablero, así que cambiar el origen las deja midiendo
// otra cosa: hay que avisar en vez de invalidarlas en silencio (revisión de
// diseño previa a M4).
int MainWindow::positionToolCount() const {
    int count = 0;
    for (const auto& tool : liveTools_) {
        if (!tool.deleted && tool.config.type == inspection::ToolType::Position) {
            ++count;
        }
    }
    return count;
}

void MainWindow::warnIfPositionToolsAffected(vision::BoardOrigin previousOrigin) {
    const int count = positionToolCount();
    if (count == 0 || previousOrigin == boardConfig_.origin) {
        return;
    }
    if (positionWarningShown_) {
        // Ya se avisó en esta sesión: repetir el diálogo cada vez que se toca el
        // origen es molesto y deja de leerse. Basta la barra de estado.
        statusBar()->showMessage(
            tr("Cambió el cero: revisa las %n herramienta(s) de Posición.", nullptr, count));
        return;
    }
    positionWarningShown_ = true;
    QMessageBox::information(
        this, tr("El cero del tablero ha cambiado"),
        tr("Hay %n herramienta(s) de Posición dibujada(s). Sus tolerancias se "
           "calcularon respecto al cero anterior, así que ahora miden otra cosa: "
           "revísalas (o vuelve a crearlas) antes de dar por buena la inspección.\n\n"
           "(Este aviso no se repetirá en esta sesión.)",
           nullptr, count));
}

void MainWindow::loadMeasurementForSelectedPiece() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.pieces == nullptr) {
        return;  // sin pieza: se conserva el ajuste de la sesión
    }
    if (auto loaded = repos_.pieces->loadMeasurement(pieceId); loaded.isOk()) {
        applyMeasurement(loaded.value());
        // Las piezas esperadas viajan con la pieza (C5): al cambiar de trabajo
        // se recupera su recuento, y el de la anterior no se arrastra.
        expectedPieces_ = loaded.value().expectedPieces;
        // Y si esta pieza se mira en mosaico. Va con el trabajo: quien pasa de
        // una bandeja a una pieza suelta no tiene por qué acordarse del panel.
        showMosaic_ = loaded.value().showMosaic;
        showMosaicPanel(showMosaic_);
        // Y AL PIPELINE, que es quien decide con cuantas manchas se trabaja.
        // Sin esta linea, cambiar de pieza recuperaba su recuento en la ventana
        // y dejaba a la deteccion con el de la pieza anterior.
        pipelineConfig_.expectedPieces = expectedPieces_;
        lastPieceCount_ = -1;
        lastPiecesSeen_ = -1;
        // Y LA VENTANA DE CONFIGURAR, SI ESTÁ ABIERTA, TAMBIÉN.
        //
        // Es única y vive fuera del selector de piezas, así que se puede cambiar
        // de trabajo con ella abierta. Lo que había dentro era entonces de la
        // pieza ANTERIOR — y «piezas esperadas» y «ver en mosaico» se guardan
        // con la pieza, así que aceptar escribía los ajustes de la bandeja
        // encima de la pieza suelta recién seleccionada. Sin avisar, y sin
        // forma de notarlo hasta que esa pieza empieza a dar NG de recuento.
        //
        // No es que se pierda un ajuste: es que se le copia a un trabajo que no
        // es el suyo, que es peor.
        if (configureDialog_ != nullptr) {
            if (auto* page = configureDialog_->piecesPage(); page != nullptr) {
                page->setExpectedPieces(expectedPieces_);
                page->setShowMosaic(showMosaic_);
            }
        }
        // Y la eleccion de pieza no se arrastra de un trabajo a otro: «la
        // tercera» de la bandeja anterior no significa nada en esta.
        focusedPiece_ = 0;
    }
}

void MainWindow::onMeasurementModeClicked() {
    repositories::PieceMeasurement current;
    current.mode = measurementMode_;
    current.board = boardConfig_;
    current.maxOffsetPx = maxOffsetPx_;
    current.maxAngleDeg = maxAngleDeg_;

    const std::int64_t pieceId = selectedPieceId();
    MeasurementModeDialog dialog(current, pieceCombo_->currentText(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const vision::BoardOrigin previousOrigin = boardConfig_.origin;
    applyMeasurement(dialog.measurement());
    persistBoardConfig();  // guarda en la pieza (si hay) y en el ajuste global
    warnIfPositionToolsAffected(previousOrigin);

    const QString modeName = QString::fromUtf8(domain::modeLabel(measurementMode_));
    statusBar()->showMessage(
        pieceId < 0
            ? tr("Modo de medición de la sesión: %1 (aún sin pieza seleccionada).")
                  .arg(modeName)
            : tr("La pieza %1 medirá en modo %2.")
                  .arg(pieceCombo_->currentText(), modeName));
}

void MainWindow::onBoardOriginChanged(QAction* action) {
    if (action == nullptr) {
        return;
    }
    const vision::BoardOrigin previousOrigin = boardConfig_.origin;
    boardConfig_.origin = static_cast<vision::BoardOrigin>(action->data().toInt());
    video_->setBoardConfig(boardConfig_);
    persistBoardConfig();
    updateBoardReadout();
    warnIfPositionToolsAffected(previousOrigin);
    if (boardConfig_.origin == vision::BoardOrigin::FixedPoint) {
        // El punto se marca con el ratón; el canvas sale del modo al primer clic.
        boardPointPick_ = true;
        video_->setPickMode(true);
        statusBar()->showMessage(tr("Haz clic en el punto que será el cero del tablero."));
        return;
    }
    switch (boardConfig_.origin) {
        case vision::BoardOrigin::PieceBounds:
            statusBar()->showMessage(
                tr("Centrado automático en el centro del contorno de la pieza."));
            break;
        case vision::BoardOrigin::PieceCenter:
            statusBar()->showMessage(
                tr("Centrado automático en el centro de masa (puede no verse centrado en "
                   "piezas asimétricas)."));
            break;
        case vision::BoardOrigin::ImageCenter:
            statusBar()->showMessage(
                tr("Tablero centrado en la imagen: el cero queda fijo en pantalla."));
            break;
        case vision::BoardOrigin::FixedPoint:
            break;  // gestionado arriba
    }
}

vision::BoardConfig MainWindow::defaultBoardConfig() const {
    vision::BoardConfig config;
    if (repos_.settings == nullptr) {
        return config;
    }
    config.origin = vision::originFromKey(
        repos_.settings->getString("board_origin", std::string("bounds")).value());
    config.followPieceAngle = repos_.settings->getInt("board_follow", 0).value() != 0;
    config.fixedPoint = {
        static_cast<float>(repos_.settings->getDouble("board_fixed_x", 0.0).value()),
        static_cast<float>(repos_.settings->getDouble("board_fixed_y", 0.0).value())};
    config.manualOffset = {
        static_cast<float>(repos_.settings->getDouble("board_offset_x", 0.0).value()),
        static_cast<float>(repos_.settings->getDouble("board_offset_y", 0.0).value())};
    return config;
}

void MainWindow::seedMeasurementForNewPiece(std::int64_t pieceId) {
    if (pieceId < 0 || repos_.pieces == nullptr) {
        return;
    }
    // Se lee y se reescribe la fila entera, como en todas partes: construir un
    // `PieceMeasurement` desde cero pondría a su valor por defecto todo lo que
    // esta función no toca.
    auto measurement = repos_.pieces->loadMeasurement(pieceId);
    if (!measurement.isOk()) {
        core::logWarning("No se pudo leer la medición de la pieza nueva: " +
                         measurement.error().message);
        return;
    }
    measurement.value().board = defaultBoardConfig();
    if (auto saved = repos_.pieces->saveMeasurement(pieceId, measurement.value());
        !saved.isOk()) {
        core::logWarning("No se pudo sembrar el tablero de la pieza nueva: " +
                         saved.error().message);
    }
}

void MainWindow::persistBoardConfig() {
    // El cero del tablero es un punto en coordenadas de imagen, así que su
    // resolución de referencia se guarda igual que la de la zona.
    persistPixelReference();
    // El motor de inspección juzga las herramientas de Posición con este mismo
    // tablero: si no se le pasa, el veredicto no coincidiría con lo que se ve.
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }
    // La regla, una sola y escrita: **el ajuste global es solo la plantilla
    // para piezas nuevas**. Con una pieza seleccionada mandan sus columnas y
    // ahí va todo cambio; sin pieza, se guarda en `Settings` como valor por
    // defecto de la próxima.
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.pieces != nullptr) {
        // Leer, modificar, escribir. `saveMeasurement` escribe la FILA ENTERA:
        // construir un `PieceMeasurement` nuevo aquí ponía a su valor por
        // defecto todo lo que esta función no toca — y así, cambiar el origen
        // del tablero borraba en silencio las piezas esperadas de la pieza.
        auto measurement = repos_.pieces->loadMeasurement(pieceId);
        if (!measurement.isOk()) {
            core::logWarning("No se pudo leer la medición de la pieza: " +
                             measurement.error().message);
            return;
        }
        measurement.value().mode = measurementMode_;
        measurement.value().board = boardConfig_;
        measurement.value().maxOffsetPx = maxOffsetPx_;
        measurement.value().maxAngleDeg = maxAngleDeg_;
        if (auto saved = repos_.pieces->saveMeasurement(pieceId, measurement.value());
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el modo de medición: " +
                             saved.error().message);
        }
        // Con pieza seleccionada NO se toca el global: es la plantilla de las
        // piezas nuevas, y pisarla con los ajustes de esta haría que la
        // siguiente naciera con el tablero de la anterior.
        return;
    }
    if (repos_.settings == nullptr) {
        return;
    }
    repos_.settings->setString("board_origin",
                               std::string(vision::originKey(boardConfig_.origin)));
    repos_.settings->setInt("board_follow", boardConfig_.followPieceAngle ? 1 : 0);
    repos_.settings->setDouble("board_fixed_x", boardConfig_.fixedPoint.x);
    repos_.settings->setDouble("board_fixed_y", boardConfig_.fixedPoint.y);
    repos_.settings->setDouble("board_offset_x", boardConfig_.manualOffset.x);
    repos_.settings->setDouble("board_offset_y", boardConfig_.manualOffset.y);
}

void MainWindow::onAnchorPicked(const cv::Point2f& imagePoint) {
    // El mismo gesto de "elegir un punto" sirve para fijar el cero del tablero.
    if (boardPointPick_) {
        boardPointPick_ = false;
        boardConfig_.fixedPoint = imagePoint;
        video_->setBoardConfig(boardConfig_);
        persistBoardConfig();
        updateBoardReadout();
        if (!boardVisible_ && boardAction_ != nullptr) {
            boardAction_->setChecked(true);  // mostrar lo que se acaba de fijar
        }
        statusBar()->showMessage(tr("Cero del tablero fijado en (%1, %2) px.")
                                     .arg(qRound(imagePoint.x))
                                     .arg(qRound(imagePoint.y)));
        return;
    }
    anchorButton_->setChecked(false);
    if (!liveFixture_.has_value() || lastFrame_.isNull()) {
        return;
    }

    vision::OrientationAnchor anchor;
    anchor.piecePoint = vision::toPieceCoords(*liveFixture_, imagePoint);
    anchor.intensity = vision::sampleIntensity(camera::qImageToMat(lastFrame_), imagePoint);
    currentAnchor_ = anchor;
    video_->setAnchorMarker(true, anchor.piecePoint);

    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto saved = repos_.pieces->saveAnchor(pieceId, anchor); saved.isOk()) {
            statusBar()->showMessage(
                tr("Rasgo distintivo guardado: la pieza se detectará en cualquier rotación."));
        } else {
            statusBar()->showMessage(QString::fromStdString(saved.error().message));
        }
    } else {
        statusBar()->showMessage(
            tr("Rasgo marcado — se guardará con la pieza al registrar."));
    }
}

// Sincroniza el spin de "Puntos" con la herramienta seleccionada en el video.
void MainWindow::onLiveSelectionChanged(int index) {
    liveParamSpin_->setEnabled(false);
    liveParamLabel_->setText(tr("Puntos:"));
    const bool valid = index >= 0 && index < static_cast<int>(liveTools_.size());
    // Los dos botones de borrar saben si tienen algo que hacer. Se pasa el
    // recuento de SELECCIONADAS y no un booleano porque el marco de selección
    // múltiple puede llevarse varias, y el tooltip lo dice.
    toolPalette_->setDeletable(static_cast<int>(video_->selectedIndices().size()),
                               static_cast<int>(liveTools_.size()));
    // Calibrar con la medida: solo tiene sentido en herramientas de longitud.
    // Calibrar con la medida requiere una LONGITUD; Blob (conteo) y
    // Línea-Línea (grados) no sirven.
    calibrateFromToolButton_->setEnabled(
        valid && liveTools_[static_cast<std::size_t>(index)].config.type !=
                     inspection::ToolType::Blob &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::LineToLine &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::Angle &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::PolyBlob &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::Position);
    if (!valid) {
        return;
    }
    QSignalBlocker blocker(liveParamSpin_);
    std::visit(
        [this](const auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, inspection::CaliperGeometry>) {
                liveParamLabel_->setText(tr("Banda (px):"));
                liveParamSpin_->setValue(static_cast<int>(g.bandWidth));
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::CircleGeometry>) {
                liveParamLabel_->setText(tr("Rayos:"));
                liveParamSpin_->setValue(g.rayCount);
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::EdgeFlawGeometry>) {
                liveParamLabel_->setText(tr("Escaneos:"));
                liveParamSpin_->setValue(g.scanCount);
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::BlobGeometry>) {
                liveParamLabel_->setText(tr("Área mín:"));
                liveParamSpin_->setValue(static_cast<int>(g.minArea));
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::PositionGeometry>) {
                liveParamLabel_->setText(tr("Eje:"));
                liveParamSpin_->setToolTip(
                    tr("Eje sobre el que se juzga la desviación:\n"
                       "1 = radial (distancia al cero)\n"
                       "2 = solo X\n"
                       "3 = solo Y"));
                liveParamSpin_->setValue(static_cast<int>(g.axis) + 1);
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::RegionGeometry>) {
                // El editor tiene un desplegable con los nombres; aquí, donde
                // solo hay este spin, se numeran. Los números salen de la misma
                // lista, así que no pueden desordenarse respecto al editor.
                QString tip = tr("Qué mide esta Región:");
                const auto& measures = inspection::allRegionMeasures();
                for (std::size_t i = 0; i < measures.size(); ++i) {
                    tip += QStringLiteral("\n%1 = %2")
                               .arg(i + 1)
                               .arg(QString::fromUtf8(
                                   inspection::regionMeasureLabel(measures[i])));
                }
                liveParamLabel_->setText(tr("Medida:"));
                liveParamSpin_->setToolTip(tip);
                liveParamSpin_->setValue(static_cast<int>(g.measure) + 1);
                liveParamSpin_->setEnabled(true);
            }
            // Punto-Línea no tiene parámetro de muestreo editable.
        },
        liveTools_[static_cast<std::size_t>(index)].geometry);
}

void MainWindow::onLiveParamChanged(int value) {
    const int index = video_->selectedIndex();
    if (!liveParamSpin_->isEnabled() || index < 0 ||
        index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    std::visit(
        [value](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, inspection::CaliperGeometry>) {
                g.bandWidth = static_cast<float>(value);
            } else if constexpr (std::is_same_v<T, inspection::CircleGeometry>) {
                g.rayCount = value;
            } else if constexpr (std::is_same_v<T, inspection::EdgeFlawGeometry>) {
                g.scanCount = value;
            } else if constexpr (std::is_same_v<T, inspection::BlobGeometry>) {
                g.minArea = static_cast<float>(value);
            } else if constexpr (std::is_same_v<T, inspection::PositionGeometry>) {
                g.axis = (value == 2)   ? inspection::PositionAxis::X
                         : (value == 3) ? inspection::PositionAxis::Y
                                        : inspection::PositionAxis::Radial;
            } else if constexpr (std::is_same_v<T, inspection::RegionGeometry>) {
                const auto& measures = inspection::allRegionMeasures();
                const int index = std::clamp(value, 1,
                                             static_cast<int>(measures.size())) - 1;
                g.measure = measures[static_cast<std::size_t>(index)];
            }
        },
        liveTools_[static_cast<std::size_t>(index)].geometry);
    commitUndoState();
    video_->update();
}

// Calibración fácil: la herramienta seleccionada se mide ahora mismo en
// píxeles y el usuario dice cuánto mide de verdad → escala px→mm.
void MainWindow::onCalibrateFromToolClicked() {
    const int index = video_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    if (!liveFixture_.has_value() || lastFrame_.isNull()) {
        statusBar()->showMessage(
            tr("Necesito ver la pieza para medir la herramienta y calibrar."));
        return;
    }

    auto& tool = liveTools_[static_cast<std::size_t>(index)];
    tool.config.geometryJson = inspection::toJson(tool.geometry);
    const auto result = inspection::runTool(camera::qImageToMat(lastFrame_), *liveFixture_,
                                            tool.config);
    if (!result.isOk() || result.value().measured <= 0.0) {
        statusBar()->showMessage(
            tr("La herramienta no midió nada aquí; ajústala sobre la pieza y reintenta."));
        return;
    }
    const double measuredPx = result.value().measured;

    bool ok = false;
    const double knownMm = QInputDialog::getDouble(
        this, tr("Fijar escala con la medida"),
        tr("'%1' mide ahora %2 px.\n¿Cuánto mide de verdad? (mm)")
            .arg(QString::fromStdString(tool.config.name))
            .arg(measuredPx, 0, 'f', 1),
        10.0, 0.01, 100000.0, 2, &ok);
    if (!ok || knownMm <= 0.0) {
        return;
    }

    calibration_ = domain::calibrationFromKnownLength(
        measuredPx, knownMm, lastFrame_.width(), calibration_.horizontalFovDeg);
    calibration_.calibratedWidth = lastFrame_.width();
    calibration_.calibratedHeight = lastFrame_.height();
    persistCalibration();
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);
    statusBar()->showMessage(
        tr("Escala fijada: %1 mm/px (%2 px = %3 mm). Todas las medidas ya están en mm.")
            .arg(calibration_.mmPerPixel, 0, 'f', 4)
            .arg(measuredPx, 0, 'f', 1)
            .arg(knownMm, 0, 'f', 2));
}

void MainWindow::rotatePieceView(double deltaDeg) {
    currentOrientationOffset_ = wrapAngleDeg(currentOrientationOffset_ + deltaDeg);
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto saved =
                repos_.pieces->saveOrientationOffset(pieceId, currentOrientationOffset_);
            !saved.isOk()) {
            statusBar()->showMessage(QString::fromStdString(saved.error().message));
            return;
        }
        statusBar()->showMessage(
            tr("Orientación de la pieza girada a %1° (guardada).")
                .arg(currentOrientationOffset_, 0, 'f', 0));
    } else {
        statusBar()->showMessage(
            tr("Vista girada a %1° — se guardará con la pieza al registrar.")
                .arg(currentOrientationOffset_, 0, 'f', 0));
    }
}

void MainWindow::onManagePiecesClicked() {
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("BD no disponible"),
                             tr("La gestión de piezas necesita la base de datos."));
        return;
    }
    const std::int64_t previous = selectedPieceId();
    PieceManagerDialog dialog(repos_.pieces, repos_.tools, this);
    keepDialogSize(dialog, repos_.settings, "pieces", 560, 480);
    dialog.exec();
    if (dialog.changed()) {
        autoInspectButton_->setChecked(false);
        loadPieceList(previous);
        onPieceSelectionChanged(pieceCombo_->currentIndex());
    }
}

// Repuebla el combo de plantillas de la pieza actual. Siempre incluye
// "principal" aunque aún no tenga herramientas.
void MainWindow::loadTemplateList(const QString& selectName) {
    QSignalBlocker blocker(templateCombo_);
    const QString previous = selectName.isEmpty() ? templateCombo_->currentText()
                                                  : selectName;
    templateCombo_->clear();

    std::vector<std::string> names{"principal"};
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.tools != nullptr) {
        if (auto listed = repos_.tools->listTemplates(pieceId); listed.isOk()) {
            for (const auto& name : listed.value()) {
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
    }
    for (const auto& name : names) {
        templateCombo_->addItem(QString::fromStdString(name));
    }
    const int idx = templateCombo_->findText(previous);
    templateCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void MainWindow::onTemplateChanged(int index) {
    Q_UNUSED(index);
    // Igual que al cambiar de pieza: no perder cambios sin guardar en silencio.
    if (!confirmSaveBeforeLeaving()) {
        QSignalBlocker blocker(templateCombo_);
        const int idx = templateCombo_->findText(loadedTemplate_);
        if (idx >= 0) {
            templateCombo_->setCurrentIndex(idx);
        }
        return;
    }
    autoInspectButton_->setChecked(false);
    loadToolsForSelectedPiece();
}

// La cámara acaba de decir qué controles soporta (O2): se habilita el menú y
// se recuerda el estado para poblar el diálogo.
// Al cambiar la resolución, todo lo que el operador definió en PÍXELES DE
// IMAGEN dejaría de señalar el mismo sitio: la zona de detección y el cero
// fijado del tablero. Se reescalan proporcionalmente en vez de dejarlos
// desplazados en silencio. Las herramientas no hacen falta: viven en
// coordenadas de pieza.
void MainWindow::rescalePixelSettings(const QSize& from, const QSize& to) {
    const cv::Size before(from.width(), from.height());
    const cv::Size after(to.width(), to.height());
    QStringList adjusted;
    // A partir de aquí la zona vive en las coordenadas NUEVAS: si no se anotara,
    // el próximo arranque volvería a reajustarla desde la resolución vieja y la
    // movería una segunda vez.
    pixelReferenceSize_ = to;

    if (pipelineConfig_.roi.area() > 0) {
        pipelineConfig_.roi = vision::rescaleRect(pipelineConfig_.roi, before, after);
        persistPipelineConfig();
        updateRoiButton();
        adjusted << tr("la zona de detección");
    }
    if (pipelineConfig_.roiPolygon.size() >= 3) {
        // Vértice a vértice, por lo mismo que el rectángulo: una zona dibujada
        // sobre 640×480 señala otro sitio en 1920×1080, y una zona que se
        // desplaza sola es peor que ninguna.
        for (auto& vertex : pipelineConfig_.roiPolygon) {
            const cv::Point2f moved = vision::rescalePoint(
                cv::Point2f(static_cast<float>(vertex.x), static_cast<float>(vertex.y)),
                before, after);
            vertex = cv::Point(cvRound(moved.x), cvRound(moved.y));
        }
        persistPipelineConfig();
        updateRoiButton();
        adjusted << tr("la zona libre");
    }
    if (boardConfig_.origin == vision::BoardOrigin::FixedPoint) {
        boardConfig_.fixedPoint = vision::rescalePoint(boardConfig_.fixedPoint, before, after);
        video_->setBoardConfig(boardConfig_);
        persistBoardConfig();
        adjusted << tr("el cero del tablero");
    }

    const QString sizes = tr("%1×%2 → %3×%4")
                              .arg(from.width())
                              .arg(from.height())
                              .arg(to.width())
                              .arg(to.height());
    statusBar()->showMessage(
        adjusted.isEmpty()
            ? tr("Resolución %1.").arg(sizes)
            : tr("Resolución %1: se reajustó %2.").arg(sizes, adjusted.join(tr(" y "))));
}

// La lista de resoluciones se recuerda POR CÁMARA en Settings: sondearla cuesta
// unos 15 s con una webcam real y detiene el vídeo, así que se paga una vez.
void MainWindow::onResolutionsProbed(
    const std::vector<camera::CameraResolution>& available,
    const camera::CameraResolution& current) {
    knownResolutions_ = available;
    currentResolution_ = current;
    if (repos_.settings == nullptr || currentCameraKey_.isEmpty()) {
        return;
    }
    QStringList encoded;
    for (const auto& resolution : available) {
        encoded << QStringLiteral("%1x%2").arg(resolution.width).arg(resolution.height);
    }
    repos_.settings->setString(resolutionCacheKey(), encoded.join(QLatin1Char(';')).toStdString());
}

std::string MainWindow::resolutionCacheKey() const {
    return "cam_res_" + currentCameraKey_.toStdString();
}

void MainWindow::loadCachedResolutions() {
    knownResolutions_.clear();
    if (repos_.settings == nullptr || currentCameraKey_.isEmpty()) {
        return;
    }
    const auto stored = repos_.settings->getString(resolutionCacheKey(), std::string());
    if (!stored.isOk() || stored.value().empty()) {
        return;
    }
    for (const QString& item :
         QString::fromStdString(stored.value()).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList parts = item.split(QLatin1Char('x'));
        if (parts.size() != 2) {
            continue;
        }
        camera::CameraResolution resolution{parts[0].toInt(), parts[1].toInt()};
        if (resolution.valid()) {
            knownResolutions_.push_back(resolution);
        }
    }
}

void MainWindow::onControlsProbed(const std::vector<camera::CameraControlState>& controls) {
    cameraControls_ = controls;
    bool anySupported = false;
    for (const auto& control : controls) {
        anySupported = anySupported || control.supported;
    }
    if (!anySupported) {
        core::logInfo("La cámara no expone ningún control ajustable");
    }

    // De dónde parte cada automático. `probeControls` da 0 cuando la cámara no
    // informa, que es lo más honesto que se puede suponer, pero el estado real
    // lo van fijando el perfil y el barrido justo debajo.
    for (const auto& state : controls) {
        if (state.property == camera::CameraProperty::AutoExposure) {
            autoExposureOn_ = state.value > 0.5;
        }
        if (state.property == camera::CameraProperty::AutoFocus) {
            autoFocusOn_ = state.value > 0.5;
        }
    }

    // Perfil de medición (C1). Va aquí y no antes de abrir porque depende del
    // SONDEO: qué controles acepta esta cámara y en qué valor los tiene. Antes
    // de sondear no se sabe ni una cosa ni la otra.
    //
    // No se persiste a propósito. Guardarlo lo convertiría en «lo que el
    // operador eligió» y a partir de ahí el perfil no volvería a aplicarse ni
    // se distinguiría de un ajuste suyo. Así, `cam_*` en Settings sigue
    // significando exactamente lo que significaba: lo que el operador tocó.
    const auto defaults = camera::measurementDefaults(controls, savedCameraControls_);
    if (!defaults.empty()) {
        for (const auto& value : defaults) {
            core::logInfo(std::string("Perfil de medición: ") +
                          std::string(camera::propertyKey(value.property)) + " = " +
                          std::to_string(value.value));
        }
        controller_.requestControls(defaults);
        for (const auto& value : defaults) {
            if (value.property == camera::CameraProperty::AutoExposure) {
                autoExposureOn_ = value.value > 0.5;
            }
            if (value.property == camera::CameraProperty::AutoFocus) {
                autoFocusOn_ = value.value > 0.5;
            }
        }
        updateCalibrationLabel();
    }

    // Y la exposición se ELIGE midiendo, que es la parte que no se puede hacer
    // desde aquí: hay que leer frames entre cambio y cambio. Solo en una cámara
    // que el operador no haya configurado — si la tocó, manda él.
    const bool operatorSetExposure =
        std::any_of(savedCameraControls_.begin(), savedCameraControls_.end(),
                    [](const camera::CameraControlValue& value) {
                        return value.property == camera::CameraProperty::Exposure;
                    });
    for (const auto& state : controls) {
        if (state.property == camera::CameraProperty::Exposure && state.supported &&
            !operatorSetExposure) {
            controller_.requestExposureSweep(state.min, state.max);
        }
    }

    // «Configurar» no se deshabilita nunca: aunque la cámara no exponga nada,
    // ahí siguen estando la detección, la escala y las preferencias. La página
    // de cámara es la que dice que no hay nada que ajustar.
}

// La página de cámara aplica sola (mover y mirar); aquí solo se persiste lo
// que el operador deja puesto, para reaplicarlo en el próximo arranque.
void MainWindow::wireCameraPage(CameraImagePage* page) {
    if (page == nullptr) {
        return;
    }
    connect(page, &CameraImagePage::measurementProfileRequested, this, [this] {
        // Olvidar lo guardado es la mitad que importa: el perfil se salta a
        // propósito toda propiedad que el operador haya tocado, así que sin
        // borrarlas volvería a saltárselas y el botón no haría nada.
        for (const auto property : camera::allCameraProperties()) {
            if (repos_.settings != nullptr) {
                repos_.settings->remove(std::string(camera::propertyKey(property)));
            }
        }
        savedCameraControls_.clear();
        core::logInfo("Ajustes de cámara olvidados a petición del operador");

        const auto defaults = camera::measurementDefaults(cameraControls_, {});
        if (!defaults.empty()) {
            controller_.requestControls(defaults);
        }
        for (const auto& state : cameraControls_) {
            if (state.property == camera::CameraProperty::Exposure && state.supported) {
                controller_.requestExposureSweep(state.min, state.max);
            }
        }
    });
    connect(page, &CameraImagePage::resolutionChosen, this,
            [this](const camera::CameraResolution& resolution) {
                savedResolution_ = resolution;
                if (repos_.settings != nullptr) {
                    repos_.settings->setInt("cam_width", resolution.width);
                    repos_.settings->setInt("cam_height", resolution.height);
                }
            });
    connect(page, &CameraImagePage::controlChanged, this,
            [this](const camera::CameraControlValue& control) {
                // Si el operador vuelve a encender un automático, el aviso tiene
                // que aparecer: no es menos peligroso por haberlo pedido él.
                if (control.property == camera::CameraProperty::AutoExposure) {
                    autoExposureOn_ = control.value > 0.5;
                    updateCalibrationLabel();
                }
                if (control.property == camera::CameraProperty::AutoFocus) {
                    autoFocusOn_ = control.value > 0.5;
                    updateCalibrationLabel();
                }
                // Se recuerda el último valor de cada propiedad para reaplicarlo
                // en el próximo arranque.
                for (auto& saved : savedCameraControls_) {
                    if (saved.property == control.property) {
                        saved.value = control.value;
                        if (repos_.settings != nullptr) {
                            repos_.settings->setDouble(
                                std::string(camera::propertyKey(control.property)),
                                control.value);
                        }
                        return;
                    }
                }
                savedCameraControls_.push_back(control);
                if (repos_.settings != nullptr) {
                    repos_.settings->setDouble(
                        std::string(camera::propertyKey(control.property)), control.value);
                }
            });
}

// Exportar/importar la configuración de la máquina (O4): calibración, ajustes
// y perfiles de detección, atajos y preferencias. No incluye piezas ni
// plantillas a propósito (esas se comparten con el export de plantillas).
void MainWindow::onResetConfigClicked() {
    if (repos_.settings == nullptr) {
        statusBar()->showMessage(tr("No hay ajustes guardados que restablecer."));
        return;
    }

    // Se pregunta ANTES, y la pregunta dice las dos cosas que hacen falta para
    // contestarla: qué se lleva por delante y qué NO toca. Un «¿Está seguro?» a
    // secas no se puede contestar — el operador no sabe si va a perder sus
    // piezas registradas.
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(tr("Restablecer configuración de fábrica"));
    confirm.setText(tr("<b>Se olvidarán todos los ajustes de esta máquina.</b>"));
    confirm.setInformativeText(
        tr("Se restablecen: la calibración de escala, los ajustes y perfiles de "
           "detección, la zona de trabajo, las preferencias, los atajos de teclado, "
           "los controles de cámara guardados, las capas de la vista y el tamaño de "
           "las ventanas.\n\n"
           "NO se toca: las piezas registradas, sus plantillas de herramientas ni el "
           "historial de inspecciones.\n\n"
           "Esto no se puede deshacer. Si quieres conservar la puesta a punto, "
           "cancela y usa antes «Exportar configuración…»."));
    auto* reset = confirm.addButton(tr("Restablecer"), QMessageBox::DestructiveRole);
    confirm.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    // El botón por defecto es Cancelar: en un diálogo destructivo, la tecla
    // Intro no puede ser la que borra.
    confirm.setDefaultButton(qobject_cast<QPushButton*>(confirm.buttons().last()));
    confirm.exec();
    if (confirm.clickedButton() != reset) {
        return;
    }

    const auto forgotten = repos_.settings->forget();
    if (!forgotten.isOk()) {
        QMessageBox::warning(this, tr("No se pudo restablecer"),
                             QString::fromStdString(forgotten.error().message));
        return;
    }

    // Los ajustes ya están olvidados; lo que queda en memoria es de la sesión
    // que se acaba de restablecer. Se devuelven a fábrica los que se ven en el
    // acto, para que la ventana no siga enseñando lo que ya no existe.
    pipelineConfig_ = vision::PipelineConfig{};
    zoneMode_ = vision::WorkingZoneMode::Automatic;
    autoRoi_.reset();
    calibration_ = domain::ScaleCalibration{};
    calibratedCameraKey_.clear();
    boardConfig_ = vision::BoardConfig{};
    boardVisible_ = false;
    rulerVisible_ = false;
    measureStages_ = false;
    stageStats_.clear();
    video_->setMmPerPixel(0.0);
    video_->setBoardConfig(boardConfig_);
    video_->setBoardVisible(boardVisible_);
    video_->setRulerVisible(rulerVisible_);
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }
    updateCalibrationLabel();
    updateRoiButton();
    updateStatusIndicators();

    // Y se dice qué queda por aplicarse. Lo que se lee una sola vez al arrancar
    // —los atajos, la disposición de la ventana— no puede rehacerse sin volver
    // a abrir, y callárselo dejaría al operador creyendo que el restablecido no
    // funcionó.
    QMessageBox::information(
        this, tr("Configuración restablecida"),
        tr("Se han olvidado %n ajuste(s).\n\n"
           "La detección, la calibración, la zona y las capas de la vista ya están "
           "como de fábrica. Los atajos de teclado y la disposición de las ventanas "
           "se aplican al volver a abrir el programa.",
           nullptr, forgotten.value()));
    statusBar()->showMessage(tr("Configuración de fábrica restablecida."));
    reanalyseCurrentFrame();
}

void MainWindow::onExportConfigClicked() {
    if (repos_.settings == nullptr || repos_.detectionProfiles == nullptr) {
        QMessageBox::warning(this, tr("Sin base de datos"),
                             tr("No hay configuración que exportar sin base de datos."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar configuración"), QStringLiteral("pc_inspector_config.json"),
        tr("Configuración (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    auto result = repositories::exportConfig(path.toStdString(), *repos_.settings,
                                             *repos_.detectionProfiles);
    if (!result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo exportar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    statusBar()->showMessage(tr("Configuración exportada: %1 ajustes y %2 perfil(es).")
                                 .arg(result.value().settings)
                                 .arg(result.value().profiles));
}

void MainWindow::onImportConfigClicked() {
    if (repos_.settings == nullptr || repos_.detectionProfiles == nullptr) {
        QMessageBox::warning(this, tr("Sin base de datos"),
                             tr("No se puede importar configuración sin base de datos."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importar configuración"), QString(), tr("Configuración (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    // Sobrescribe ajustes: se avisa antes, y se recuerda que la calibración
    // depende de la cámara y la resolución de ESTA máquina.
    if (QMessageBox::question(
            this, tr("Importar configuración"),
            tr("Se sobrescribirán los ajustes actuales (calibración, detección, atajos "
               "y preferencias) y se añadirán los perfiles del archivo.\n\n"
               "La calibración de escala depende de la cámara y la resolución: si aquí "
               "usas otra, la app te avisará de que ya no es válida.\n\n"
               "¿Continuar?")) !=
        QMessageBox::Yes) {
        return;
    }
    auto result = repositories::importConfig(path.toStdString(), *repos_.settings,
                                             *repos_.detectionProfiles);
    if (!result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo importar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    QMessageBox::information(
        this, tr("Configuración importada"),
        tr("Se aplicaron %1 ajustes y %2 perfil(es).\n\nReinicia la aplicación para que "
           "todo (atajos incluidos) quede cargado.")
            .arg(result.value().settings)
            .arg(result.value().profiles));
    statusBar()->showMessage(tr("Configuración importada desde %1").arg(path));
}

void MainWindow::applyPreferencesPage(PreferencesPage* page) {
    if (page == nullptr) {
        return;
    }
    autoIntervalMs_ = page->autoIntervalMs();
    kSigma_ = page->kSigma();

    // Aplicar de inmediato.
    autoTimer_.setInterval(autoIntervalMs_);
    if (repos_.engine != nullptr) {
        repos_.engine->setKSigma(kSigma_);
    }
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("pref_auto_interval_ms", autoIntervalMs_);
        repos_.settings->setDouble("pref_ksigma", kSigma_);
    }
    statusBar()->showMessage(tr("Preferencias guardadas."));
}

void MainWindow::onConfigureClicked() {
    // Uno solo: volver a pulsar trae al frente el que ya está abierto en vez de
    // apilar paneles que se pisan entre sí.
    if (configureDialog_ != nullptr) {
        configureDialog_->raise();
        configureDialog_->activateWindow();
        return;
    }

    ConfigureDialog::Inputs inputs;
    inputs.segmentation = pipelineConfig_.segmentation;
    inputs.detectionProfileId = currentProfileId_;
    inputs.minAreaFraction = pipelineConfig_.minAreaFraction;
    inputs.subpixelEdges = pipelineConfig_.subpixelEdges;
    inputs.maxAreaFraction = pipelineConfig_.maxAreaFraction;
    inputs.profiles = repos_.detectionProfiles;
    // Las resoluciones ya sondeadas de ESTA cámara se pasan hechas: volver a
    // preguntarlas cuesta segundos y detiene el vídeo.
    inputs.controller = (streaming_ && !cameraControls_.empty()) ? &controller_ : nullptr;
    // Con una fuente de fichero no hay controles que sondear, así que la
    // pestaña de cámara cae sola en su sustituto; lo que hace falta es que ese
    // sustituto diga el motivo CORRECTO, y para eso tiene que saber qué fuente
    // hay puesta.
    inputs.sourceKind = sourceKind_;
    inputs.probedControls = cameraControls_;
    inputs.knownResolutions = knownResolutions_;
    inputs.currentResolution = currentResolution_;
    inputs.autoIntervalMs = autoIntervalMs_;
    inputs.kSigma = kSigma_;
    inputs.zoneMode = zoneMode_;
    inputs.expectedPieces = expectedPieces_;
    inputs.showMosaic = showMosaic_;
    inputs.hasFixedZone = pipelineConfig_.roi.area() > 0;
    inputs.hasFreeZone = pipelineConfig_.roiPolygon.size() >= 3;

    auto* dialog = new ConfigureDialog(std::move(inputs), this);
    keepDialogSize(*dialog, repos_.settings, "configure", 560, 520);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    configureDialog_ = dialog;
    dialog->setCurrentTab(configureTab_);

    wireCameraPage(dialog->cameraPage());
    // El modo de zona se aplica AL MOMENTO y no al pulsar Aplicar: es un
    // conmutador, no un formulario, y su efecto se ve en el vídeo.
    if (auto* performance = dialog->performancePage(); performance != nullptr) {
        performance->setStageMeasurement(measureStages_);
        connect(performance, &PerformancePage::modeChanged, this,
                &MainWindow::setWorkingZoneMode);
        connect(performance, &PerformancePage::stageMeasurementToggled, this,
                [this](bool enabled) {
                    // La ventana anterior se tira al encender: lo que se mide
                    // ahora es lo que está pasando ahora, y arrastrar muestras
                    // de una sesión anterior daría un reparto de otra escena.
                    measureStages_ = enabled;
                    stageStats_.clear();
                    // Es una preferencia de diagnóstico, y hasta ahora había
                    // que reactivarla en cada sesión: justo cuando se está
                    // persiguiendo algo que tarda, que es cuando menos apetece
                    // volver a buscarla.
                    if (repos_.settings != nullptr) {
                        repos_.settings->setInt("measure_stages", enabled ? 1 : 0);
                    }
                });
    }
    connect(dialog, &ConfigureDialog::applied, this, [this, dialog] {
        applyDetectionPage(dialog->detectionPage());
        applyPreferencesPage(dialog->preferencesPage());
        applyPiecesPage(dialog->piecesPage());
    });
    if (auto* pieces = dialog->piecesPage(); pieces != nullptr) {
        // EL BOTON QUE NO HACIA NADA.
        //
        // Decia «pone en el campo el numero de piezas que la camara esta
        // detectando», y llamaba a `setDetectedCount`, que solo refresca el texto
        // de estado de mas abajo. El campo no se movia. Un boton que promete algo
        // y no lo hace es peor que no tenerlo: quien lo pulsa se queda creyendo
        // que el numero ya esta puesto.
        // El número cambia en la pantalla al momento; guardarlo espera a
        // Aceptar. Son dos cosas distintas y mezclarlas obligaría a escribir en
        // la base por cada número intermedio.
        connect(pieces, &PiecesPage::expectedPiecesChangedLive, this,
                &MainWindow::declareExpectedPieces);
        // El mosaico se enciende o se apaga en el momento. Es una opción de VER,
        // y una opción de ver que no se ve hasta cerrar la ventana obliga a
        // abrirla dos veces para saber si era la que querías.
        connect(pieces, &PiecesPage::showMosaicChangedLive, this,
                &MainWindow::showMosaicPanel);
        connect(pieces, &PiecesPage::useDetectedRequested, this, [this, pieces] {
            // EL MISMO NÚMERO QUE DICE EL TEXTO, no otro.
            //
            // Aquí se ponía `lastPieceCount_` —las piezas USADAS— mientras el
            // texto de al lado dice `lastPiecesSeen_` —las manchas VISTAS—. Con
            // tres manchas y dos declaradas, el panel decía «se ven 3» y el
            // botón ponía 2: dos números distintos para la misma pregunta, y el
            // «3» desaparecía al pulsar.
            //
            // Y contradecía el propósito que el propio código tenía escrito:
            // «tiene que poder SUBIR el número cuando de verdad hay más piezas
            // de las declaradas». Con las usadas nunca podía subirlo, porque
            // esas ya vienen recortadas a lo declarado.
            //
            // Si la tercera mancha es una sombra, lo que hay que arreglar es la
            // detección, no el número — y para eso está el aviso de al lado.
            pieces->setDetectedCount(lastPiecesSeen_);
            pieces->setExpectedPieces(lastPiecesSeen_);
        });
    }
    if (auto* detection = dialog->detectionPage(); detection != nullptr) {
        // LA COMPROBACIÓN DE CORTE LA HACE LA VENTANA, no la página.
        //
        // La página no tiene la imagen —ni debería tenerla: es un formulario— y
        // además esto cuesta dos análisis completos, 60 ms con cien piezas. Va
        // a petición del operador y no en cada fotograma.
        connect(detection, &DetectionPage::clippingCheckRequested, this, [this, detection] {
            const QImage frame = frameOrFile();
            if (frame.isNull()) {
                statusBar()->showMessage(
                    tr("No hay imagen que mirar: arranca la cámara o abre un fichero."));
                return;
            }
            detection->setClippingCheck(
                vision::checkThresholdClipping(camera::qImageToMat(frame)));
        });
        // SEÑALAR EL FONDO EN LA IMAGEN, por lo mismo: la imagen está aquí.
        //
        // Y si no hay ninguna se cae a la rueda de colores en vez de no hacer
        // nada. Un botón que a veces no responde y no dice por qué se lee como
        // que el programa está roto.
        connect(detection, &DetectionPage::backgroundPatchRequested, this, [this, detection] {
            const QImage frame = frameOrFile();
            if (frame.isNull()) {
                statusBar()->showMessage(
                    tr("Sin imagen no se puede señalar el fondo: se elige el color a mano."));
                detection->pickBackgroundByWheel();
                return;
            }
            BackgroundPatchDialog picker(camera::qImageToMat(frame), detection->options(), this);
            if (picker.exec() == QDialog::Accepted && picker.sample().valid) {
                detection->setChosenBackground(picker.sample().colour);
            }
        });
    }
    connect(dialog, &ConfigureDialog::scaleWizardRequested, this,
            &MainWindow::onCalibrateClicked);
    connect(dialog, &ConfigureDialog::shortcutsRequested, this,
            &MainWindow::onShowShortcuts);
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (configureDialog_ == dialog) {
            configureDialog_ = nullptr;
        }
    });
    // La pestaña abierta se recuerda: quien está peleando con la iluminación
    // vuelve diez veces a la misma.
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        configureTab_ = dialog->currentTab();
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("config_last_tab", configureTab_);
        }
    });
    dialog->show();
}

void MainWindow::onShowHistoryClicked() {
    if (repos_.inspections == nullptr || repos_.pieces == nullptr) {
        QMessageBox::information(this, tr("Historial no disponible"),
                                 tr("El historial necesita la base de datos."));
        return;
    }
    HistoryDialog dialog(repos_.inspections, repos_.pieces, selectedPieceId(), this);
    keepDialogSize(dialog, repos_.settings, "history", 640, 460);
    dialog.exec();
}

void MainWindow::onManageTemplatesClicked() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.tools == nullptr) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Selecciona una pieza para gestionar sus plantillas."));
        return;
    }
    // No perder los cambios en vivo si el gestor cambia la plantilla activa.
    if (!confirmSaveBeforeLeaving()) {
        return;
    }
    TemplateManagerDialog dialog(repos_.tools, pieceId,
                                 QString::fromStdString(activeTemplate()), this);
    keepDialogSize(dialog, repos_.settings, "templates", 360, 380);
    dialog.exec();
    // Recargar el combo (pudo haber renombrados/borrados/duplicados) y activar
    // la plantilla elegida; las herramientas se recargan para la activa.
    const QString target = dialog.selectedTemplate();
    loadTemplateList(target);  // usa su propio QSignalBlocker
    if (!target.isEmpty() && templateCombo_->findText(target) < 0) {
        // Plantilla nueva y aún vacía: se añade al combo (se materializa al
        // guardar su primera herramienta), como el botón "+".
        QSignalBlocker blocker(templateCombo_);
        templateCombo_->addItem(target);
        templateCombo_->setCurrentText(target);
    }
    loadToolsForSelectedPiece();
}

void MainWindow::onNewTemplateClicked() {
    if (selectedPieceId() < 0) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Selecciona o registra una pieza primero."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Nueva plantilla"),
                              tr("Nombre de la plantilla:"), QLineEdit::Normal,
                              tr("plantilla %1").arg(templateCombo_->count() + 1), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (templateCombo_->findText(name) >= 0) {
        QMessageBox::warning(this, tr("Ya existe"),
                             tr("Ya hay una plantilla con ese nombre."));
        return;
    }
    // La plantilla nace vacía; se materializa al guardar su primera herramienta.
    templateCombo_->addItem(name);
    templateCombo_->setCurrentText(name);  // dispara onTemplateChanged
    statusBar()->showMessage(
        tr("Plantilla '%1' activa: dibuja herramientas y se guardarán en ella.").arg(name));
}

void MainWindow::onToolRightClicked(int index) {
    deleteToolAt(index);
}

// EL MENÚ DEL CLIC DERECHO SOBRE EL VÍDEO.
//
// Petición de uso: «agrega alguna función al clic derecho». Y al ir a hacerlo
// apareció algo peor que un hueco: el clic derecho sobre una cota LA BORRABA en
// el acto, sin menú y sin preguntar. En cualquier otro programa ese gesto
// significa «enséñame qué puedo hacer aquí»; aquí era el único que destruía
// trabajo, y bastaba errar el botón del ratón una vez.
//
// Tres reglas al montarlo, y las tres se notan:
//
//   - SOLO SALE LO QUE APLICA. Un menú con la mitad de las entradas en gris
//     obliga a leerlas todas para descubrir que no servían. Sobre una cota se
//     ofrecen las de la cota; sobre el vacío, las del vídeo.
//   - LO DESTRUCTIVO, AL FINAL Y SEPARADO. Borrar comparte menú con duplicar, y
//     un gesto de más con el ratón no puede costar el trabajo de media hora.
//   - CADA ENTRADA DICE SOBRE QUÉ ACTÚA. «Borrar» a secas no distingue entre la
//     cota de debajo del cursor y todas; el nombre va dentro.
// RENOMBRAR LA COTA. El nombre es lo que sale en el informe y en el parte, así
// que «Ø exterior» vale y «Círculo 7» no.
//
// Se rechaza el nombre repetido, y no por pulcritud: la ventana empareja
// herramienta y resultado POR NOMBRE en varios sitios, y dos cotas llamadas
// igual harían que una enseñara el valor de la otra.
void MainWindow::renameToolAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    auto& tool = liveTools_[static_cast<std::size_t>(index)];
    bool accepted = false;
    const QString proposed = QInputDialog::getText(
        this, tr("Renombrar la cota"),
        tr("Cómo quieres que se llame en el informe:"), QLineEdit::Normal,
        QString::fromStdString(tool.config.name), &accepted);
    if (!accepted) {
        return;
    }
    const QString wanted = proposed.trimmed();
    if (wanted.isEmpty()) {
        statusBar()->showMessage(tr("Una cota sin nombre no se puede leer en un parte."));
        return;
    }
    const std::string newName = wanted.toStdString();
    if (newName == tool.config.name) {
        return;
    }
    for (const auto& other : liveTools_) {
        if (other.config.name == newName) {
            statusBar()->showMessage(
                tr("Ya hay una cota llamada «%1». Dos con el mismo nombre acaban "
                   "enseñando el valor la una de la otra.")
                    .arg(wanted));
            return;
        }
    }
    tool.config.name = newName;
    commitUndoState();
    video_->update();
    statusBar()->showMessage(tr("Ahora se llama «%1».").arg(wanted));
}

// COPIAR LO QUE MIDE, para pegarlo en un correo o en una hoja.
//
// Se copia la ÚLTIMA lectura guardada y no una nueva: volver a ejecutar la
// herramienta ahora daría un número medido en otro instante, con la pieza ya
// movida, y el operador creería estar copiando lo que tiene delante.
void MainWindow::copyReadingAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    const auto& tool = liveTools_[static_cast<std::size_t>(index)];
    for (const auto& result : lastToolResults_) {
        if (result.name != tool.config.name) {
            continue;
        }
        const QString text =
            QStringLiteral("%1\t%2")
                .arg(QString::fromStdString(tool.config.name))
                .arg(QString::fromStdString(inspection::formatMeasure(
                    result, calibration_.mmPerPixel, currentUnit(), true)));
        QGuiApplication::clipboard()->setText(text);
        statusBar()->showMessage(tr("Copiado: %1").arg(text));
        return;
    }
    // Y SI NO HAY LECTURA, SE DICE. Copiar en silencio un portapapeles vacío
    // hace que el operador pegue lo que copió antes sin enterarse.
    statusBar()->showMessage(
        tr("«%1» todavía no ha medido nada: inspecciona o mide la pieza primero.")
            .arg(QString::fromStdString(tool.config.name)));
}

// MARCAR EL RASGO EN EL PUNTO DONDE SE PULSÓ.
//
// Es la misma operación de siempre, sin el modo: antes había que pulsar un
// botón, dejar el programa esperando, y acertar con el siguiente clic. Aquí el
// operador ya ha señalado dónde lo quiere.
void MainWindow::markAnchorAt(const cv::Point2f& imagePoint) {
    boardPointPick_ = false;  // este camino es el del rasgo, no el del cero
    video_->setPickMode(false);
    onAnchorPicked(imagePoint);
}

void MainWindow::onCanvasContextMenu(int tool, const QPoint& globalPos,
                                     const cv::Point2f& imagePoint) {
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    const bool onATool = tool >= 0 && tool < static_cast<int>(liveTools_.size());
    if (onATool) {
        const auto& hit = liveTools_[static_cast<std::size_t>(tool)];
        const QString name = QString::fromStdString(hit.config.name);

        // Un encabezado que dice sobre QUÉ va el menú. Sin él, con dos cotas
        // pegadas no hay forma de saber cuál se ha cogido hasta ejecutar algo.
        auto* header = menu.addAction(tr("%1 — %2")
                                          .arg(typeLabel(hit.config.type))
                                          .arg(name));
        header->setEnabled(false);
        menu.addSeparator();

        auto* rename = menu.addAction(tr("Renombrar…"));
        rename->setToolTip(tr("El nombre es lo que sale en el informe y en el parte:\n"
                              "«Ø exterior» se lee, «Círculo 7» no."));
        connect(rename, &QAction::triggered, this, [this, tool] { renameToolAt(tool); });

        auto* duplicate = menu.addAction(tr("Duplicar"));
        duplicate->setToolTip(tr("Una copia con la misma tolerancia, desplazada un poco\n"
                                 "para que se vea que son dos."));
        connect(duplicate, &QAction::triggered, this, [this, tool] {
            video_->setSelectedIndex(tool);
            onDuplicateToolClicked();
        });

        auto* copy = menu.addAction(tr("Copiar lo que mide"));
        copy->setToolTip(tr("El último valor medido, al portapapeles, para pegarlo\n"
                            "en un correo o en una hoja de cálculo."));
        connect(copy, &QAction::triggered, this, [this, tool] { copyReadingAt(tool); });

        // LO DESTRUCTIVO, AL FINAL Y DETRÁS DE UNA SEPARACIÓN.
        menu.addSeparator();
        auto* remove = menu.addAction(tr("Borrar «%1»").arg(name));
        remove->setToolTip(tr("Ctrl+Z la devuelve."));
        connect(remove, &QAction::triggered, this, [this, tool] { deleteToolAt(tool); });
    } else {
        auto* header = menu.addAction(tr("Aquí no hay ninguna cota"));
        header->setEnabled(false);
        menu.addSeparator();

        // MARCAR EL RASGO AQUÍ, en el punto exacto donde se ha pulsado.
        //
        // Hasta ahora marcar el rasgo distintivo era un MODO: se pulsaba un
        // botón, el programa se quedaba esperando, y el siguiente clic contaba.
        // Dos gestos y un estado invisible en medio para poner un punto. Aquí ya
        // se sabe dónde quiere ponerlo el operador: ha pulsado justo ahí.
        auto* anchor = menu.addAction(tr("Marcar aquí el rasgo distintivo"));
        anchor->setToolTip(
            tr("Fija la orientación de la pieza cuando es simétrica o puede\n"
               "llegar girada 180°. Elige un punto que solo exista en un sitio:\n"
               "un agujero, una marca, una esquina achaflanada.\n\n"
               "Solo se aplica si «seguir la rotación» está encendido."));
        anchor->setEnabled(streaming_ && liveFixture_.has_value());
        if (!anchor->isEnabled()) {
            anchor->setToolTip(tr("Necesita vídeo en vivo con la pieza detectada."));
        }
        connect(anchor, &QAction::triggered, this,
                [this, imagePoint] { markAnchorAt(imagePoint); });

        menu.addSeparator();
        auto* fit = menu.addAction(tr("Ajustar a la ventana"));
        connect(fit, &QAction::triggered, video_, &inspection::EditorCanvas::resetView);
        auto* actual = menu.addAction(tr("Píxeles reales (100 %)"));
        actual->setToolTip(tr("Un píxel de la imagen, un píxel de la pantalla: es la\n"
                              "única vista en la que lo que ves es lo que se mide."));
        connect(actual, &QAction::triggered, video_,
                &inspection::EditorCanvas::zoomToActualPixels);
    }

    menu.exec(globalPos);
}

void MainWindow::onDuplicateToolClicked() {
    const int index = video_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        statusBar()->showMessage(tr("Selecciona una herramienta para duplicar."));
        return;
    }
    // Copia con un pequeño desplazamiento y nombre nuevo; id = -1 = aún sin
    // guardar en la BD (se persiste al guardar la plantilla, como las nuevas).
    inspection::EditedTool tool = liveTools_[static_cast<std::size_t>(index)];
    tool.config.id = -1;
    inspection::translateGeometry(tool.geometry, {15.0F, 15.0F});
    ++toolNameCounter_;
    tool.config.name = (typeLabel(tool.config.type) +
                        QStringLiteral(" %1").arg(toolNameCounter_))
                           .toStdString();
    tool.config.geometryJson = inspection::toJson(tool.geometry);

    liveTools_.push_back(std::move(tool));
    commitUndoState();
    video_->clearResults();
    const int newIndex = static_cast<int>(liveTools_.size()) - 1;
    video_->setSelectedIndex(newIndex);
    onLiveSelectionChanged(newIndex);
    statusBar()->showMessage(tr("Herramienta duplicada."));
}

void MainWindow::onSaveTemplateClicked() {
    saveTemplate(selectedPieceId());
}

bool MainWindow::saveTemplate(std::int64_t pieceId) {
    if (repos_.tools == nullptr) {
        statusBar()->showMessage(tr("Base de datos no disponible: no se puede guardar."));
        return false;
    }
    if (liveTools_.empty()) {
        templateDirty_ = false;  // nada que guardar: el estado queda limpio
        statusBar()->showMessage(tr("No hay herramientas dibujadas que guardar."));
        return true;
    }

    if (pieceId < 0) {
        // Sin pieza: crear una y guardar ahí (opción elegida por el usuario).
        if (repos_.pieces == nullptr) {
            statusBar()->showMessage(
                tr("No hay pieza seleccionada ni base de datos de piezas."));
            return false;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Nueva pieza"),
            tr("No hay pieza seleccionada. Nombre de la pieza nueva:"),
            QLineEdit::Normal, tr("pieza"), &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return false;
        }
        auto created = repos_.pieces->createPiece(name.trimmed().toStdString());
        if (!created.isOk()) {
            QMessageBox::warning(this, tr("No se pudo crear la pieza"),
                                 QString::fromStdString(created.error().message));
            return false;
        }
        pieceId = created.value();
        seedMeasurementForNewPiece(pieceId);
        // Bloquear señales: si el combo dispara onPieceSelectionChanged,
        // loadToolsForSelectedPiece limpiaría liveTools_ ANTES del upsert.
        QSignalBlocker blocker(pieceCombo_);
        loadPieceList(pieceId);
    }

    persistTemplateTools(pieceId);
    return true;
}

void MainWindow::persistTemplateTools(std::int64_t pieceId) {
    // Upsert de todas las herramientas en vivo a la plantilla activa: inserta
    // las nuevas (id < 0) y actualiza las cambiadas. Los borrados en vivo ya se
    // persistieron al instante (deleteToolAt), así que esto cierra el ciclo.
    const std::string tmpl = activeTemplate();
    int saved = 0;
    int errors = 0;
    for (auto& tool : liveTools_) {
        tool.config.geometryJson = inspection::toJson(tool.geometry);
        if (auto result = repos_.tools->save(pieceId, tool.config, tmpl); result.isOk()) {
            tool.config.id = result.value();
            ++saved;
        } else {
            ++errors;
            core::logError(result.error().message);
        }
    }
    stableTools_ = liveTools_;  // el estado guardado pasa a ser el "limpio"
    templateDirty_ = false;
    loadedPieceId_ = pieceId;
    loadedTemplate_ = QString::fromStdString(tmpl);
    statusBar()->showMessage(
        errors == 0
            ? tr("Plantilla '%1' guardada (%2 herramienta(s)).")
                  .arg(QString::fromStdString(tmpl))
                  .arg(saved)
            : tr("Plantilla '%1' guardada con %2 error(es); %3 ok (ver log).")
                  .arg(QString::fromStdString(tmpl))
                  .arg(errors)
                  .arg(saved));
}

// Muestra el aviso Guardar/Descartar/Cancelar si hay cambios sin guardar.
// Devuelve true si se puede continuar (guardado o descartado), false si el
// operador cancela (o cancela la creación de pieza al guardar).
bool MainWindow::confirmSaveBeforeLeaving() {
    if (!templateDirty_) {
        return true;
    }
    QMessageBox box(QMessageBox::Question, tr("Cambios sin guardar"),
                    tr("La plantilla tiene herramientas con cambios sin guardar.\n\n"
                       "¿Qué quieres hacer?"),
                    QMessageBox::NoButton, this);
    auto* saveBtn = box.addButton(tr("Guardar"), QMessageBox::AcceptRole);
    auto* discardBtn = box.addButton(tr("Descartar"), QMessageBox::DestructiveRole);
    auto* cancelBtn = box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == cancelBtn) {
        return false;
    }
    if (box.clickedButton() == saveBtn) {
        // Guardar en la pieza a la que pertenecen las herramientas en vivo.
        if (!saveTemplate(loadedPieceId_)) {
            return false;  // el usuario canceló la creación de pieza
        }
    }
    (void)discardBtn;
    templateDirty_ = false;  // guardado o descartado: estado limpio
    return true;
}

void MainWindow::selectPieceById(std::int64_t pieceId) {
    QSignalBlocker blocker(pieceCombo_);
    for (int i = 0; i < pieceCombo_->count(); ++i) {
        if (pieceCombo_->itemData(i).toLongLong() == pieceId) {
            pieceCombo_->setCurrentIndex(i);
            return;
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmSaveBeforeLeaving()) {
        event->ignore();  // el operador canceló el cierre para no perder cambios
        return;
    }
    persistWindowLayout();
    persistLastSession();
    QMainWindow::closeEvent(event);
}

// Los tres eventos que pueden cambiar la geometría. No se guarda en el acto:
// arrastrar una ventana emite decenas de eventos por segundo y no hacen falta
// decenas de escrituras en la base de datos.
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    scheduleWindowLayoutSave();
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);
    scheduleWindowLayoutSave();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::WindowStateChange) {
        scheduleWindowLayoutSave();  // maximizar o restaurar
    }
}

void MainWindow::scheduleWindowLayoutSave() {
    if (layoutSaveTimer_.isActive()) {
        layoutSaveTimer_.stop();
    }
    layoutSaveTimer_.start();
}

void MainWindow::persistWindowLayout() {
    if (repos_.settings == nullptr) {
        return;
    }
    // `saveGeometry` lleva dentro tamaño, posición, pantalla y si estaba
    // maximizada; `saveState`, la disposición de paneles y barras. Son dos
    // cosas distintas y Qt las guarda por separado a propósito: restaurar una
    // sin la otra deja la ventana bien colocada con los paneles descuadrados,
    // o al revés.
    repos_.settings->setString("window_geometry", saveGeometry().toBase64().toStdString());
    repos_.settings->setString("window_state", saveState().toBase64().toStdString());
}

void MainWindow::restoreWindowLayout() {
    if (repos_.settings == nullptr) {
        return;
    }
    const auto restore = [this](const char* key, auto&& apply) {
        auto stored = repos_.settings->getString(key, std::string());
        if (!stored.isOk() || stored.value().empty()) {
            return;
        }
        const QByteArray base64(stored.value().data(),
                                static_cast<qsizetype>(stored.value().size()));
        apply(QByteArray::fromBase64(base64));
    };
    // La geometría PRIMERO y el estado después: `restoreState` coloca los
    // paneles dentro del tamaño que tenga la ventana, así que hacerlo al revés
    // los reparte sobre un tamaño que va a cambiar acto seguido.
    restore("window_geometry", [this](const QByteArray& data) { restoreGeometry(data); });
    restore("window_state", [this](const QByteArray& data) { restoreState(data); });
}

void MainWindow::persistLastSession() {
    if (repos_.settings == nullptr) {
        return;
    }
    // Con qué se estaba trabajando. Se recuerda la ELECCIÓN, no se reabre nada:
    // la cámara guardada tampoco arranca sola, y un programa que al abrirse se
    // pone a leer un vídeo por su cuenta hace algo que nadie le ha pedido.
    repos_.settings->setInt("last_piece_id", selectedPieceId());
    repos_.settings->setString("last_template", activeTemplate());
    repos_.settings->setString("last_source_kind",
                               std::string(camera::sourceKindKey(sourceKind_)));
    repos_.settings->setString("last_source_file", lastSourcePath_.toStdString());
}

void MainWindow::deleteToolAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    const auto& tool = liveTools_[static_cast<std::size_t>(index)];
    if (tool.config.id >= 0 && repos_.tools != nullptr) {
        if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
            statusBar()->showMessage(QString::fromStdString(removed.error().message));
            return;
        }
    }
    liveTools_.erase(liveTools_.begin() + index);
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    statusBar()->showMessage(tr("Herramienta eliminada."));
}

void MainWindow::onPieceSelectionChanged(int index) {
    Q_UNUSED(index);
    // Si hay cambios sin guardar, preguntar antes de abandonar la plantilla.
    // Al cancelar, restaurar el combo a la pieza cuyas herramientas están vivas.
    if (!confirmSaveBeforeLeaving()) {
        selectPieceById(loadedPieceId_);
        return;
    }
    autoInspectButton_->setChecked(false);
    video_->resetView();  // otra pieza, encuadre limpio (Z3)
    loadMeasurementForSelectedPiece();  // modo y tablero de ESTA pieza (M2)
    loadDetectionProfileForSelectedPiece();  // perfil de detección de la pieza (O3)
    // La referencia que se actualizaría es la de la pieza elegida.
    updateLearnFromCaptureAvailability();
    loadTemplateList();       // repuebla plantillas de la pieza
    loadToolsForSelectedPiece();

    // Rasgo distintivo y ajuste de orientación de la pieza seleccionada.
    currentAnchor_.reset();
    currentOrientationOffset_ = 0.0;
    video_->setAnchorMarker(false);
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto anchor = repos_.pieces->loadAnchor(pieceId);
            anchor.isOk() && anchor.value().has_value()) {
            currentAnchor_ = anchor.value();
            video_->setAnchorMarker(true, currentAnchor_->piecePoint);
        }
        if (auto offset = repos_.pieces->loadOrientationOffset(pieceId); offset.isOk()) {
            currentOrientationOffset_ = offset.value();
        }
    }

    // Miniatura de la pieza registrada para el panel de comparación.
    referenceThumb_ = QImage();
    refThumbLabel_->setPixmap(QPixmap());
    refThumbLabel_->setText(QStringLiteral("—"));
    similarityLabel_->clear();
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto thumb = repos_.pieces->loadThumbnail(pieceId);
            thumb.isOk() && !thumb.value().empty()) {
            referenceThumb_ = QImage::fromData(thumb.value().data(),
                                               static_cast<int>(thumb.value().size()));
            if (!referenceThumb_.isNull()) {
                refThumbLabel_->setPixmap(QPixmap::fromImage(referenceThumb_)
                                              .scaled(refThumbLabel_->size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
            }
        } else {
            refThumbLabel_->setText(tr("Sin miniatura\n(regístrala de nuevo\npara generarla)"));
        }
    }
}

void MainWindow::loadToolsForSelectedPiece() {
    liveTools_.clear();
    video_->setSelectedIndex(-1);
    video_->clearResults();

    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.tools == nullptr) {
        templateDirty_ = false;
        loadedPieceId_ = pieceId;
        loadedTemplate_ = QString::fromStdString(activeTemplate());
        video_->update();
        return;
    }
    auto listed = repos_.tools->listForPiece(pieceId, activeTemplate());
    if (!listed.isOk()) {
        core::logWarning("No se pudieron cargar las herramientas: " + listed.error().message);
        return;
    }
    for (auto& config : listed.value()) {
        auto geometry = inspection::geometryFromJson(config.type, config.geometryJson);
        if (!geometry.isOk()) {
            core::logWarning("Herramienta '" + config.name +
                             "' con geometría corrupta: " + geometry.error().message);
            continue;
        }
        inspection::EditedTool tool;
        tool.config = std::move(config);
        tool.geometry = std::move(geometry.value());
        liveTools_.push_back(std::move(tool));
    }
    // Cambiar de pieza reinicia el historial de deshacer.
    undoStack_.clear();
    stableTools_ = liveTools_;
    // Estado recién cargado de la BD = limpio; recordar a quién pertenece.
    templateDirty_ = false;
    loadedPieceId_ = pieceId;
    loadedTemplate_ = QString::fromStdString(activeTemplate());
    video_->update();
}

// --- Registro en vivo -------------------------------------------------------

void MainWindow::onRegisterLiveClicked() {
    if (!streaming_ || lastFrame_.isNull()) {
        // Se nombran las tres puertas, no solo la cámara: desde que una imagen y
        // un vídeo son fuentes, «inicia la cámara» deja fuera dos caminos que
        // funcionan igual de bien y manda a buscar hardware a quien no lo tiene.
        QMessageBox::information(
            this, tr("Sin imagen"),
            tr("Elige una fuente y ponla en marcha: una cámara, una imagen o un vídeo. "
               "También puedes registrar desde imágenes sueltas con el asistente."));
        return;
    }
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("No disponible"),
                             tr("El registro necesita la base de datos."));
        return;
    }
    // Sin modelo ONNX se puede registrar igual (G1): la pieza queda como
    // medidor puro. Se avisa una vez de lo que se pierde, no se bloquea.
    if (!repos_.embedFn && !toolsOnlyAccepted_) {
        // Se pregunta UNA vez por sesión: si el operador ya dijo que sí, repetir
        // el diálogo en cada registro solo estorba.
        if (QMessageBox::question(
                this, tr("Sin modelo de apariencia"),
                tr("El modelo ONNX no está disponible, así que las piezas se "
                   "registrarán SOLO CON HERRAMIENTAS: se medirán con las que dibujes, "
                   "pero no habrá comparación de apariencia que detecte defectos "
                   "inesperados.\n\n¿Registrar así durante esta sesión?")) !=
            QMessageBox::Yes) {
            return;
        }
        toolsOnlyAccepted_ = true;
    }

    // Pedir el nombre validando duplicados ANTES de capturar nada: si ya
    // existe se ofrece guardar como nueva versión de esa pieza o renombrar.
    pendingPieceId_ = -1;
    QString name;
    while (true) {
        name = QInputDialog::getText(this, tr("Registrar pieza"), tr("Nombre de la pieza:"),
                                     QLineEdit::Normal, name)
                   .trimmed();
        if (name.isEmpty()) {
            return;
        }
        const auto exists = repos_.pieces->nameExists(name.toStdString());
        if (!exists.isOk()) {
            QMessageBox::warning(this, tr("Error"),
                                 QString::fromStdString(exists.error().message));
            return;
        }
        if (!exists.value()) {
            break;  // nombre libre
        }

        QMessageBox question(QMessageBox::Question, tr("La pieza ya existe"),
                             tr("Ya existe una pieza llamada '%1'.\n\n¿Qué quieres hacer?")
                                 .arg(name),
                             QMessageBox::NoButton, this);
        auto* newVersion =
            question.addButton(tr("Guardar como nueva versión"), QMessageBox::AcceptRole);
        question.addButton(tr("Elegir otro nombre"), QMessageBox::ActionRole);
        auto* cancel = question.addButton(QMessageBox::Cancel);
        question.exec();
        if (question.clickedButton() == cancel) {
            return;
        }
        if (question.clickedButton() == newVersion) {
            if (auto pieces = repos_.pieces->listPieces(); pieces.isOk()) {
                for (const auto& piece : pieces.value()) {
                    if (piece.name == name.toStdString()) {
                        pendingPieceId_ = piece.id;
                        break;
                    }
                }
            }
            break;
        }
        // "Elegir otro nombre": vuelve a preguntar conservando el texto.
    }
    pendingPieceName_ = name;

    // Modo de medición de la pieza nueva (M2): se pregunta aquí, junto al
    // nombre, y se aplica ya mismo para que el operador capture viendo el
    // tablero con el que se va a medir. Cancelar aquí cancela el registro.
    {
        repositories::PieceMeasurement current;
        current.mode = measurementMode_;
        current.board = boardConfig_;
        current.maxOffsetPx = maxOffsetPx_;
        current.maxAngleDeg = maxAngleDeg_;
        MeasurementModeDialog modeDialog(current, name, this);
        if (modeDialog.exec() != QDialog::Accepted) {
            return;
        }
        pendingMeasurement_ = modeDialog.measurement();
        applyMeasurement(pendingMeasurement_);
    }

    // El rasgo distintivo marcado (si hay) fija la orientación de las 30
    // capturas de referencia y se guarda con la pieza.
    liveSession_ = std::make_shared<engine::RegistrationSession>(
        repos_.embedFn, kCaptureTarget, kCaptureMinimum, currentAnchor_, inspectionConfig(),
        currentOrientationOffset_);
    captureProgress_ = new QProgressDialog(
        tr("Capturando referencias de '%1'…\nMantén la pieza a la vista.")
            .arg(pendingPieceName_),
        tr("Cancelar"), 0, kCaptureTarget, this);
    captureProgress_->setWindowModality(Qt::WindowModal);
    captureProgress_->setMinimumDuration(0);
    captureProgress_->setValue(0);
    connect(captureProgress_, &QProgressDialog::canceled, this,
            &MainWindow::onCaptureCanceled);

    captureTimer_.start();
}

void MainWindow::onCaptureTick() {
    if (captureWatcher_.isRunning() || lastFrame_.isNull() || liveSession_ == nullptr) {
        return;
    }
    // shared_ptr capturado: la sesión sobrevive aunque el usuario cancele
    // mientras un frame sigue procesándose en el pool.
    auto session = liveSession_;
    const QImage frame = lastFrame_;
    captureWatcher_.setFuture(QtConcurrent::run([session, frame] {
        using ResultT = core::Result<engine::RegistrationSession::SampleFeedback>;
        try {
            return session->addFrame(camera::qImageToMat(frame));
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de captura: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de captura");
        }
    }));
}

void MainWindow::onCaptureProcessed() {
    if (liveSession_ == nullptr || captureProgress_ == nullptr) {
        return;  // registro cancelado mientras se procesaba un frame
    }
    const auto result = captureWatcher_.result();
    if (!result.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("Registro fallido"),
                             QString::fromStdString(result.error().message));
        return;
    }

    const auto& feedback = result.value();
    captureProgress_->setValue(feedback.count);
    if (!feedback.accepted) {
        captureProgress_->setLabelText(
            tr("Capturando referencias de '%1'…\nRechazada: %2")
                .arg(pendingPieceName_, QString::fromStdString(feedback.reason)));
    } else {
        captureProgress_->setLabelText(tr("Capturando referencias de '%1'…\n%2 de %3")
                                           .arg(pendingPieceName_)
                                           .arg(feedback.count)
                                           .arg(kCaptureTarget));
    }

    if (feedback.count >= kCaptureTarget) {
        finishLiveRegistration();
    }
}

void MainWindow::onCaptureCanceled() {
    stopLiveCapture();
    statusBar()->showMessage(tr("Registro cancelado."));
}

void MainWindow::stopLiveCapture() {
    captureTimer_.stop();
    liveSession_.reset();
    if (captureProgress_ != nullptr) {
        captureProgress_->deleteLater();
        captureProgress_ = nullptr;
    }
}

void MainWindow::finishLiveRegistration() {
    captureTimer_.stop();
    auto session = liveSession_;

    auto reference = session->finish();
    if (!reference.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("Registro incompleto"),
                             QString::fromStdString(reference.error().message));
        return;
    }

    // Pieza nueva, o nueva versión de una existente (elegido al pedir nombre).
    std::int64_t pieceId = pendingPieceId_;
    if (pieceId < 0) {
        auto created = repos_.pieces->createPiece(pendingPieceName_.toStdString());
        if (!created.isOk()) {
            stopLiveCapture();
            QMessageBox::warning(this, tr("No se pudo crear la pieza"),
                                 QString::fromStdString(created.error().message));
            return;
        }
        pieceId = created.value();
        seedMeasurementForNewPiece(pieceId);
    }

    // Modo "solo herramientas" (G1): la referencia viene vacía a propósito y NO
    // se guarda. Guardarla haría creer a la inspección que hay apariencia con
    // la que comparar, y daría NG sistemático.
    const bool toolsOnly = reference.value().mean.empty();
    int referenceVersion = 0;
    if (!toolsOnly) {
        const auto savedVersion = repos_.pieces->saveReference(pieceId, reference.value());
        if (!savedVersion.isOk()) {
            stopLiveCapture();
            QMessageBox::warning(this, tr("No se pudo guardar la referencia"),
                                 QString::fromStdString(savedVersion.error().message));
            return;
        }
        referenceVersion = savedVersion.value();
    }

    // Miniatura del recorte normalizado: alimenta el panel "Pieza registrada".
    const auto thumbnail = engine::encodeThumbnailJpeg(session->firstNormalized(), 256);
    if (!thumbnail.empty()) {
        if (auto saved = repos_.pieces->saveThumbnail(pieceId, thumbnail); !saved.isOk()) {
            core::logWarning("No se pudo guardar la miniatura: " + saved.error().message);
        }
    }

    // Modo de medición y tablero elegidos al empezar el registro (M2).
    if (auto saved = repos_.pieces->saveMeasurement(pieceId, pendingMeasurement_);
        !saved.isOk()) {
        core::logWarning("No se pudo guardar el modo de medición: " + saved.error().message);
    }

    // Rasgo distintivo elegido durante la sesión.
    if (currentAnchor_.has_value()) {
        if (auto saved = repos_.pieces->saveAnchor(pieceId, *currentAnchor_);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el rasgo distintivo: " +
                             saved.error().message);
        }
    }

    // Persistir las herramientas dibujadas sobre el video (en la plantilla
    // activa: una pieza puede tener varias plantillas).
    const std::string tmpl = activeTemplate();
    int toolErrors = 0;
    if (repos_.tools != nullptr) {
        for (auto& tool : liveTools_) {
            tool.config.geometryJson = inspection::toJson(tool.geometry);
            if (auto saved = repos_.tools->save(pieceId, tool.config, tmpl); saved.isOk()) {
                tool.config.id = saved.value();
            } else {
                ++toolErrors;
                core::logError(saved.error().message);
            }
        }
    }
    // Registro guardó las herramientas: el estado queda limpio (P2).
    stableTools_ = liveTools_;
    templateDirty_ = false;
    loadedPieceId_ = pieceId;
    loadedTemplate_ = QString::fromStdString(tmpl);

    stopLiveCapture();
    // Seleccionar la pieza sin recargar las herramientas recién guardadas,
    // pero sí refrescar la miniatura de referencia del panel.
    {
        QSignalBlocker blocker(pieceCombo_);
        loadPieceList(pieceId);
    }
    referenceThumb_ = QImage::fromData(thumbnail.data(), static_cast<int>(thumbnail.size()));
    if (!referenceThumb_.isNull()) {
        refThumbLabel_->setPixmap(QPixmap::fromImage(referenceThumb_)
                                      .scaled(refThumbLabel_->size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    }

    if (toolErrors > 0) {
        statusBar()->showMessage(
            tr("'%1' registrada, pero %2 herramienta(s) no se guardaron (ver log).")
                .arg(pendingPieceName_)
                .arg(toolErrors));
    } else if (toolsOnly) {
        statusBar()->showMessage(
            tr("'%1' registrada SOLO CON HERRAMIENTAS (%2): sin comparación de "
               "apariencia. Auto-inspección activa.")
                .arg(pendingPieceName_)
                .arg(liveTools_.size()));
    } else {
        statusBar()->showMessage(tr("'%1' registrada (referencia v%2) con %3 "
                                    "herramienta(s). Auto-inspección activa.")
                                     .arg(pendingPieceName_)
                                     .arg(referenceVersion)
                                     .arg(liveTools_.size()));
    }

    autoInspectButton_->setChecked(true);
}

// --- Auto-inspección ---------------------------------------------------------

void MainWindow::onAutoToggled(bool enabled) {
    // El menú refleja al botón, y no solo al revés: si dijeran cosas distintas
    // el operador no sabría a cuál creer. Se hace aquí y no en la conexión
    // porque este slot también revierte el botón cuando faltan condiciones, y
    // el menú tiene que revertir con él.
    if (autoInspectAction_ != nullptr && autoInspectAction_->isChecked() != enabled) {
        QSignalBlocker blocker(autoInspectAction_);
        autoInspectAction_->setChecked(enabled);
    }
    if (enabled) {
        if (repos_.engine == nullptr || selectedPieceId() < 0 || !streaming_) {
            // Red de seguridad, no el aviso: el conmutador ya está apagado con
            // su motivo, así que llegar aquí significa que algo cambió entre
            // medias. Se revierte y se dice en la barra de estado, sin un modal
            // que hay que cerrar para seguir trabajando.
            autoInspectButton_->setChecked(false);  // arrastra al menú por el slot
            statusBar()->showMessage(
                tr("La auto-inspección necesita una fuente en marcha y una pieza "
                   "seleccionada."));
            return;
        }
        autoInspecting_ = true;
        // En inspección el operador solo lee piezas: se bloquea la edición y
        // se desactivan las herramientas de dibujo.
        video_->setEditingLocked(true);
        video_->setCreateType(std::nullopt);
        toolPalette_->showSelection(std::nullopt);
        toolPalette_->setEnabled(false);
        calibrateFromToolButton_->setEnabled(false);
        // EL BANNER TIENE CUATRO ESTADOS Y DOS IBAN POR SU CUENTA.
        //
        // «Cumple» y «no cumple» ya salían de la familia de pastillas de
        // veredicto; «en marcha» y «falló la inspección» llevaban un gris `#444`
        // y un ámbar `#ffb066` tecleados aquí — un CUARTO gris y un CUARTO
        // ámbar, que es exactamente como se llegó a tener tres de cada.
        //
        // `kChipRest` es literalmente el papel que hace este estado: la
        // aplicación está trabajando y no tiene nada que decir todavía.
        verdictBanner_->setStyleSheet(
            theme::chipStyle(theme::kChipRest,
                             QStringLiteral(" font-size:16px; font-weight:bold;")));
        verdictBanner_->setText(tr("Auto-inspección en marcha…"));
        verdictBanner_->setVisible(true);
        autoTimer_.start();
    } else {
        autoInspecting_ = false;
        autoTimer_.stop();
        video_->setEditingLocked(false);
        toolPalette_->setEnabled(true);
        onLiveSelectionChanged(video_->selectedIndex());  // reactiva calibrar/puntos
        verdictBanner_->setVisible(false);
        video_->clearResults();
    }
}

void MainWindow::onAutoTick() {
    if (inspectionWatcher_.isRunning() || lastFrame_.isNull()) {
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        autoInspectButton_->setChecked(false);
        return;
    }
    inspectedFrame_ = lastFrame_;
    auto* engine = repos_.engine;
    engine->setPipelineConfig(inspectionConfig());
    engine->setMmPerPixel(calibration_.mmPerPixel);
    engine->setUnit(currentUnit());
    engine->setTemplateName(activeTemplate());
    const QImage frame = inspectedFrame_;
    inspectionWatcher_.setFuture(QtConcurrent::run([engine, frame, pieceId] {
        using ResultT = core::Result<engine::InspectionEngine::Outcome>;
        try {
            return engine->inspect(camera::qImageToMat(frame), pieceId);
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de inspección: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de inspección");
        }
    }));
}

void MainWindow::showLiveVerdict(const engine::InspectionEngine::Outcome& outcome) {
    verdictBanner_->setStyleSheet(
        theme::chipStyle(outcome.verdict.ok ? theme::kGoodChip : theme::kBadChip,
                         QStringLiteral(" font-size:16px; font-weight:bold;")));
    verdictBanner_->setText(QString::fromStdString(outcome.verdict.summary));
    // Los overlays de herramientas ya los pinta la medición en vivo de cada
    // frame; aquí solo el veredicto y la similitud.

    if (outcome.verdict.embedding.evaluated) {
        similarityLabel_->setText(tr("Similitud: %1\nUmbral: %2")
                                      .arg(outcome.verdict.embedding.similarity, 0, 'f', 4)
                                      .arg(outcome.verdict.embedding.threshold, 0, 'f', 4));
    } else {
        similarityLabel_->setText(
            QString::fromStdString(outcome.verdict.embedding.note));
    }
}

// --- Flujos con diálogo -------------------------------------------------------

// Último frame de la cámara o imagen elegida por el usuario (los flujos
// completos deben poder probarse en equipos sin cámara).
QImage MainWindow::frameOrFile() {
    if (!lastFrame_.isNull()) {
        return lastFrame_;
    }
    return openImageFile();
}

QImage MainWindow::openImageFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Elegir imagen"), QString(), tr("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return {};
    }
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Imagen inválida"), tr("No se pudo cargar la imagen."));
        return {};
    }
    return image.convertToFormat(QImage::Format_BGR888);
}

void MainWindow::loadPieceList(std::int64_t selectId) {
    pieceCombo_->clear();
    if (repos_.pieces == nullptr) {
        pieceCombo_->addItem(tr("BD no disponible"));
        pieceCombo_->setEnabled(false);
        return;
    }
    auto pieces = repos_.pieces->listPieces();
    if (!pieces.isOk()) {
        core::logWarning("No se pudieron listar las piezas: " + pieces.error().message);
        return;
    }
    for (const auto& piece : pieces.value()) {
        pieceCombo_->addItem(QString::fromStdString(piece.name),
                             QVariant::fromValue<qlonglong>(piece.id));
        if (piece.id == selectId) {
            pieceCombo_->setCurrentIndex(pieceCombo_->count() - 1);
        }
    }
    if (pieceCombo_->count() == 0) {
        pieceCombo_->addItem(tr("(sin piezas registradas)"));
    }
}

std::int64_t MainWindow::selectedPieceId() const {
    const QVariant data = pieceCombo_->currentData();
    return data.isValid() ? data.toLongLong() : -1;
}

void MainWindow::onRegisterWizardClicked() {
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("BD no disponible"),
                             tr("No se puede registrar sin base de datos."));
        return;
    }
    if (!repos_.embedFn) {
        QMessageBox::warning(
            this, tr("Modelo no disponible"),
            tr("El registro necesita el modelo de embeddings. Ejecuta run.ps1 para "
               "descargarlo y prepararlo."));
        return;
    }

    // Con la detección configurada: el asistente SEGMENTA cada captura para sacar
    // el recorte del que nace el embedding, y sin esto aprendía la pieza con los
    // valores de fábrica mientras «Registrar y activar» —que hace lo mismo— usaba
    // los del operador.
    RegistrationWizard wizard(&controller_, repos_.embedFn, repos_.pieces,
                              inspectionConfig(), this);
    keepDialogSize(wizard, repos_.settings, "registration", 900, 640);
    if (wizard.exec() == QDialog::Accepted) {
        loadPieceList(wizard.createdPieceId());
    }
}

// Registrar un acabado admisible mas de la pieza que ya esta seleccionada.
void MainWindow::onRegisterVariantClicked() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        QMessageBox::information(
            this, tr("Ninguna pieza seleccionada"),
            tr("Elige primero la pieza a la que quieres añadirle un acabado."));
        return;
    }
    if (!repos_.embedFn) {
        QMessageBox::warning(
            this, tr("Modelo no disponible"),
            tr("Registrar un acabado necesita el modelo de embeddings. Ejecuta run.ps1 "
               "para descargarlo y prepararlo."));
        return;
    }
    const QString pieceName = pieceCombo_ != nullptr ? pieceCombo_->currentText() : QString();

    RegistrationWizard wizard(&controller_, repos_.embedFn, repos_.pieces, pieceId,
                              pieceName, inspectionConfig(), this);
    keepDialogSize(wizard, repos_.settings, "registration", 900, 640);
    if (wizard.exec() != QDialog::Accepted) {
        return;
    }
    // Se dice CUANTOS acabados tiene ahora la pieza. «Guardado» a secas no deja
    // comprobar que se guardo donde uno creia, y aqui el error tipico —haberlo
    // guardado encima del anterior— es invisible sin este numero.
    if (auto variants = repos_.pieces->listVariants(pieceId); variants.isOk()) {
        QStringList names;
        for (const auto& name : variants.value()) {
            names << QString::fromStdString(name);
        }
        statusBar()->showMessage(
            tr("«%1» tiene ahora %2 acabados registrados: %3.")
                .arg(pieceName)
                .arg(names.size())
                .arg(names.join(QStringLiteral(", "))));
    }
    reanalyseCurrentFrame();
}

void MainWindow::onOpenEditorClicked() {
    // E2: con vídeo en marcha, elegir explícitamente la fuente de la imagen.
    const bool live = streaming_ && !lastFrame_.isNull();
    QImage reference;
    if (live) {
        QMessageBox box(QMessageBox::Question, tr("Editor de plantilla"),
                        tr("¿Sobre qué imagen quieres editar la plantilla?"),
                        QMessageBox::NoButton, this);
        // El botón nombra la fuente que hay de verdad. «Frame actual de la
        // cámara» era cierto cuando la cámara era lo único que había; con una
        // foto congelada, una imagen o un vídeo abierto, le está diciendo al
        // operador que va a usar algo distinto de lo que ve.
        auto* current = box.addButton(currentSourceLabel(), QMessageBox::AcceptRole);
        auto* fromFile = box.addButton(tr("Abrir archivo…"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == current) {
            reference = lastFrame_;
        } else if (box.clickedButton() == fromFile) {
            reference = openImageFile();
        } else {
            return;
        }
    } else {
        reference = frameOrFile();
    }
    if (reference.isNull()) {
        return;
    }

    // CON LA CONFIGURACIÓN QUE EL OPERADOR AJUSTÓ, no con la de fábrica.
    //
    // Esta llamada no llevaba ninguna, así que el fixture con el que se abre el
    // editor salía de una detección distinta de la que se está viendo en la
    // ventana: sin recuperación de brillos, sin separar piezas que se tocan, sin
    // clave de color de fondo y con el umbral y la polaridad de fábrica.
    //
    // Lo peor no es que el fixture saliera desplazado. Es que si esa detección de
    // fábrica FALLA —y sobre una mesa de color falla: el gris del cartón rojo cae
    // en 116 y Otsu devuelve una sola mancha del 89 % del cuadro— el editor se
    // negaba a abrir con «no se pudo analizar la imagen», mientras la ventana
    // principal enseñaba la pieza perfectamente detectada al lado.
    //
    // El editor sí recibía la configuración buena y volvía a analizar con ella;
    // el que se quedaba fuera era este primer análisis, el que decide si se abre.
    //
    // Y sobre LA PIEZA SEÑALADA: el editor se abre con este fixture, así que
    // abrirlo siempre sobre la mayor dejaba al operador dibujando cotas encima
    // de una pieza distinta de la que acababa de elegir con las flechas.
    const auto analysis = analyseMeasuredPiece(camera::qImageToMat(reference));
    if (!analysis.isOk()) {
        QMessageBox::warning(this, tr("Sin pieza detectada"),
                             tr("No se pudo analizar la imagen: %1")
                                 .arg(QString::fromStdString(analysis.error().message)));
        return;
    }

    // Con pieza seleccionada se edita su plantilla; sin piezas, una "demo".
    std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 && repos_.pieces != nullptr) {
        if (auto created = repos_.pieces->createPiece("demo"); created.isOk()) {
            pieceId = created.value();
            // Señales bloqueadas: onPieceSelectionChanged recargaría de la BD y
            // vaciaría liveTools_ antes de pasarlas al editor.
            QSignalBlocker blocker(pieceCombo_);
            loadPieceList(pieceId);
        } else if (auto pieces = repos_.pieces->listPieces(); pieces.isOk()) {
            for (const auto& piece : pieces.value()) {
                if (piece.name == "demo") {
                    pieceId = piece.id;
                    break;
                }
            }
        }
    }

    // Pasar las herramientas EN VIVO actuales (incluidas las no guardadas) para
    // que el editor muestre lo mismo que la vista en vivo (P3), y la cámara en
    // marcha para el botón "Actualizar desde cámara" (E1) — solo con vídeo.
    inspection::EditorWindow editor(reference, analysis.value().fixture, pieceId,
                                    pieceId >= 0 ? repos_.tools : nullptr, calibration_,
                                    activeTemplate(), this, &liveTools_,
                                    live ? &controller_ : nullptr, inspectionConfig());
    // Para que la receta de medición se recuerde en la pieza.
    editor.setPieceRepository(repos_.pieces);
    editor.exec();

    // Devolver las herramientas editadas a la vista en vivo (ida y vuelta), en
    // vez de recargar de la BD y perder lo no guardado.
    liveTools_ = editor.editedTools();
    undoStack_.clear();
    stableTools_ = liveTools_;
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    video_->update();
    if (editor.savedToDb()) {
        // El editor persistió: estado limpio y ligado a esta pieza/plantilla.
        templateDirty_ = false;
        loadedPieceId_ = pieceId;
        loadedTemplate_ = QString::fromStdString(activeTemplate());
    } else {
        // Hubo ediciones no guardadas: quedan como cambios pendientes (Ctrl+S).
        templateDirty_ = true;
    }
}

void MainWindow::onInspectClicked() {
    if (repos_.engine == nullptr) {
        QMessageBox::warning(this, tr("Motor no disponible"),
                             tr("La inspección necesita la base de datos."));
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Registra o selecciona una pieza primero."));
        return;
    }
    if (inspectionWatcher_.isRunning()) {
        return;
    }

    const QImage frame = frameOrFile();
    if (frame.isNull()) {
        return;
    }

    inspectedFrame_ = frame;
    inspectButton_->setEnabled(false);
    statusBar()->showMessage(tr("Inspeccionando…"));
    auto* engine = repos_.engine;
    engine->setPipelineConfig(inspectionConfig());
    engine->setMmPerPixel(calibration_.mmPerPixel);
    engine->setUnit(currentUnit());
    engine->setTemplateName(activeTemplate());
    inspectionWatcher_.setFuture(QtConcurrent::run([engine, frame, pieceId] {
        using ResultT = core::Result<engine::InspectionEngine::Outcome>;
        try {
            return engine->inspect(camera::qImageToMat(frame), pieceId);
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de inspección: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de inspección");
        }
    }));
}

void MainWindow::onInspectionFinished() {
    inspectButton_->setEnabled(true);
    const auto result = inspectionWatcher_.result();
    const bool autoMode = autoInspectButton_->isChecked();

    if (!result.isOk()) {
        if (autoMode) {
            // Una inspección que no llega a dar veredicto es un AVISO, no un
            // «no cumple»: la pieza no ha suspendido, es que no se ha podido
            // medir. Con `kBadChip` se leería como rechazo y con el gris de «en
            // marcha» no se distinguiría de estar esperando.
            verdictBanner_->setStyleSheet(
                theme::chipStyle(theme::kWarnChip,
                                 QStringLiteral(" font-size:16px; font-weight:bold;")));
            verdictBanner_->setText(QString::fromStdString(result.error().message));
        } else {
            statusBar()->showMessage(tr("Inspección fallida"));
            QMessageBox::warning(this, tr("Inspección fallida"),
                                 QString::fromStdString(result.error().message));
        }
        return;
    }

    const std::int64_t pieceId = selectedPieceId();
    if (repos_.inspections != nullptr) {
        if (auto stats = repos_.inspections->todayStats(pieceId); stats.isOk()) {
            statusBar()->showMessage(tr("Hoy: %1 inspecciones — %2 OK / %3 NG")
                                         .arg(stats.value().total)
                                         .arg(stats.value().okCount)
                                         .arg(stats.value().ngCount));
        }
    }

    if (autoMode) {
        showLiveVerdict(result.value());
        return;
    }

    InspectionResultDialog dialog(inspectedFrame_, result.value(), repos_.engine, pieceId,
                                  referenceThumb_, calibration_, this);
    keepDialogSize(dialog, repos_.settings, "result", 1000, 680);
    dialog.exec();
}

}  // namespace pci::ui
