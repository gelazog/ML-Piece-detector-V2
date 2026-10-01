#include "ui/main_window.h"
#include "ui/main_window_internal.h"

#include "camera/frame_utils.h"
#include "core/logging.h"
#include "repositories/inspection_repository.h"
#include "repositories/settings_repository.h"
#include "repositories/tool_repository.h"
#include "ui/camera_image_page.h"
#include "ui/configure_dialog.h"
#include "ui/detection_page.h"
#include "ui/dialog_geometry.h"
#include "ui/inspection_result_dialog.h"
#include "ui/lens_calibration_dialog.h"
#include "ui/measurements_panel.h"
#include "ui/performance_page.h"
#include "ui/piece_report_dialog.h"
#include "ui/pieces_page.h"
#include "ui/rate_readout.h"
#include "ui/setup_guide.h"
#include "ui/theme.h"
#include "vision/auto_roi.h"
#include "vision/contour_analysis.h"
#include "vision/edge_segmentation.h"
#include "vision/fixture_stabilizer.h"
#include "vision/pipeline.h"
#include "vision/plane_scale.h"
#include "vision/quality_metrics.h"

#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrent>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace pci::ui {

namespace {

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
            auto all = vision::analyzeFrames(image, pipeline, &overlay.piecesTooSmall,
                                             &overlay.blobAreas);
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

}  // namespace

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
    // «DIBUJARLA»: la cota que el programa no sabe colocar solo.
    //
    // Petición de uso: «y tal vez como opcional, que el usuario quiera hacerlo
    // manual». Para una Rectitud o un Chaflán no hay adivinanza honrada —hay
    // que señalar QUÉ tramo se mide— así que el informe no finge colocarla:
    // deja la herramienta elegida en la paleta, que es el paso que si no hay
    // que buscar entre treinta y dos iconos.
    if (const auto byHand = dialog.toolToDrawByHand(); byHand.has_value()) {
        toolPalette_->activate(*byHand);
        statusBar()->showMessage(
            tr("«%1» elegida en la paleta: trázala sobre la pieza.")
                .arg(QString::fromUtf8(inspection::toolTypeLabel(*byHand))));
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

// Las medidas que SE DIBUJAN sobre la pieza: todas menos las que el operador
// ha apagado con el ojo del panel.
//
// Se filtra al pintar y no al medir, y esa diferencia es la que hace que el ojo
// sea una decisión de vista: la cota apagada se sigue midiendo, sigue en la
// tabla con su veredicto y sigue contando para el OK/NG de la pieza. Filtrarla
// antes de medir sería apagar la cota, que es otra cosa y ya existe —el
// interruptor del informe—.
std::vector<inspection::ToolRunResult> MainWindow::visibleResults(
    const std::vector<inspection::ToolRunResult>& results) const {
    if (hiddenOverlays_.empty()) {
        return results;
    }
    std::vector<inspection::ToolRunResult> shown;
    shown.reserve(results.size());
    for (const auto& result : results) {
        if (std::find(hiddenOverlays_.begin(), hiddenOverlays_.end(), result.toolId) ==
            hiddenOverlays_.end()) {
            shown.push_back(result);
        }
    }
    return shown;
}

void MainWindow::updateStatusIndicators() {
    updateAutoInspectAvailability();
    updateGatedCommands();
    updateEdgeBrushAvailability();
    // Punto de color + leyenda por indicador (rich text: sin assets externos).
    //
    // ANTES, LA PALABRA ERA LA MISMA EN LOS DOS ESTADOS.
    //
    // «BD» en rojo y «BD» en verde: la única diferencia visible era el punto
    // de color, y el tooltip —que llevaba la palabra de verdad, «conectada» o
    // «no disponible»— no se ve sin pasar el ratón por encima. Eso incumple
    // WCAG 1.4.1 (la información no puede depender solo del color) y un
    // operador daltónico deutan, que es el más común, ve el mismo punto gris
    // en los dos casos.
    //
    // Ahora cada indicador lleva además una palabra corta de estado —«BD ✓» /
    // «BD ✕»— que no depende del ratón, y un nombre accesible con la frase
    // completa para un lector de pantalla.
    auto set = [](QLabel* label, const QString& caption, bool ok, const QString& okWord,
                  const QString& badWord, const QString& okText, const QString& badText) {
        const QString color = ok ? QString(theme::kGood) : QString(theme::kBad);
        const QString word = ok ? okWord : badWord;
        const QString text = ok ? okText : badText;
        label->setText(QStringLiteral("<span style='color:%1'>&#9679;</span> %2 %3")
                           .arg(color, caption, word));
        label->setToolTip(text);
        label->setAccessibleName(QStringLiteral("%1: %2").arg(caption, text));
    };

    // El indicador dice QUÉ fuente está viva, no solo que hay una. «Cám» en
    // verde mientras se analiza una fotografía sería exacto en el color y falso
    // en la palabra, y el color por sí solo nunca debe cargar con el
    // significado.
    switch (sourceKind_) {
        case camera::SourceKind::Camera:
            set(camIndicator_, tr("Cám"), streaming_, tr("✓"), tr("✕ parada"),
                tr("Cámara: transmitiendo"), tr("Cámara: detenida"));
            break;
        case camera::SourceKind::Photo:
            set(camIndicator_, tr("Foto"), streaming_, tr("✓"), tr("✕ sin fuente"),
                tr("Fuente: una foto congelada de esta cámara. La escala calibrada sigue "
                   "valiendo."),
                tr("Sin fuente"));
            break;
        case camera::SourceKind::Image:
            set(camIndicator_, tr("Img"), streaming_, tr("✓"), tr("✕ sin fuente"),
                tr("Fuente: una imagen de archivo. Todo se mide igual que en vivo."),
                tr("Sin fuente"));
            break;
        case camera::SourceKind::Video:
            set(camIndicator_, tr("Víd"), streaming_, tr("✓"), tr("✕ sin fuente"),
                tr("Fuente: un vídeo de archivo, en bucle."), tr("Sin fuente"));
            break;
    }
    set(dbIndicator_, tr("BD"), repos_.pieces != nullptr, tr("✓"), tr("✕ caída"),
        tr("Base de datos: conectada"),
        tr("Base de datos: no disponible (sin persistencia)"));
    set(modelIndicator_, tr("ONNX"), static_cast<bool>(repos_.embedFn), tr("✓"),
        tr("✕ no cargado"), tr("Modelo de embeddings: cargado"),
        tr("Modelo ONNX: no disponible (inspección solo con herramientas)"));
}

void MainWindow::onFrame(const QImage& rawFrame) {
    // Llega imagen: el problema de la fuente, si lo había, está resuelto. Es la
    // única prueba que vale; que la cámara arranque no quiere decir que dé
    // fotogramas.
    if (blockingNotice_->isReported(Blocker::Source)) {
        blockingNotice_->resolve(Blocker::Source);
    }
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

    // El disparo por paso de pieza mira la escena aquí, que es donde puede
    // haber cambiado.
    observeSceneForPassTrigger(overlay);

    if (overlay.piecesFound >= 0) {
        lastPiecesSeen_ = overlay.piecesFound;
        lastPiecesTooSmall_ = overlay.piecesTooSmall;
        // Las áreas de todas las manchas del último análisis, para que el ajuste
        // de «área mínima» pueda decir qué pasaría con otro valor. Se guardan
        // aquí y no se recalculan en el diálogo: volver a segmentar en cada
        // tecla costaría lo mismo que un frame entero, y encima podría no dar el
        // mismo resultado que lo que se está viendo.
        lastBlobAreas_ = overlay.blobAreas;
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
                                    : q >= 0.75 ? tr("regular: endereza la cámara")
                                                : tr("pobre: cámara muy inclinada");
            calibLabel_->setText(tr("Escala (ArUco): %1 mm/px · calidad %2 (%3%)")
                                     .arg(overlay.liveMmPerPixel, 0, 'f', 4)
                                     .arg(quality)
                                     .arg(q * 100.0, 0, 'f', 0));
        } else if (arucoLiveScale_) {
            calibLabel_->setText(tr("Escala (ArUco): marcador no visible"));
        }
        // SIN MARCADOR, LA ESCALA ES LA DEL ÚLTIMO FOTOGRAMA QUE LO VIO, y las
        // cotas en milímetros dejan de ser de fiar. La etiqueta de la escala lo
        // decía en letra de barra de estado; esto lo dice arriba cuando la falta
        // dura (ver `markerStreak_`) y se quita en cuanto vuelve.
        if (arucoLiveScale_) {
            if (markerStreak_.observe(overlay.liveMmPerPixel > 0.0)) {
                blockingNotice_->report(
                    Blocker::Marker,
                    tr("No se ve el marcador: las medidas en milímetros no son fiables. "
                       "Acércalo o mejora la luz."));
            } else {
                blockingNotice_->resolve(Blocker::Marker);
            }
        }
        // Medidas en vivo de las herramientas dibujadas (px o mm calibrados).
        video_->setResults(visibleResults(overlay.toolResults));
        lastToolResults_ = overlay.toolResults;
        // Y la tabla de medidas, si alguien la está mirando.
        //
        // Sólo con el panel abierto: rellenar ochenta y cuatro celdas por frame
        // para un panel cerrado es trabajo tirado, y es la misma regla que ya
        // gobierna el recuento de piezas —contar cuesta y sólo se hace cuando
        // alguien mira el número—.
        if (measurements_ != nullptr && measurementsDock_ != nullptr &&
            measurementsDock_->isVisible()) {
            measurements_->setResults(overlay.toolResults, liveToolConfigs(),
                                      calibration_.mmPerPixel, currentUnit());
            // Y el desplegable de pieza sigue a la elección de siempre, en vez
            // de llevar la suya: las flechas, el mosaico y este panel mueven la
            // MISMA cosa.
            measurements_->setChosenPiece(focusedPiece_ > 0 ? focusedPiece_ - 1 : -1);
        }
        // Y el lienzo enseña las cotas de ESA pieza. Sin esto sólo salían las de
        // la primera en orden de lectura, así que enfocar la tercera dejaba la
        // pieza remarcada y las cifras encima de otra.
        video_->setFocusedPiece(overlay.measuredPiece >= 1 ? overlay.measuredPiece - 1 : 0);
        updateBoardReadout();  // desviación y giro respecto al tablero (T3)
        showMeasuringVerdict(overlay);
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
        QString text = QStringLiteral("%1x%2 · imagen")
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

void MainWindow::onBlockerAction(Blocker blocker) {
    // Solo la fuente tiene botón. Y si ya hay otra en marcha no se toca: su
    // primer fotograma quitará el aviso.
    if (blocker != Blocker::Source || streaming_) {
        return;
    }
    // Reintentar es darle a «Iniciar» con lo que esté elegido. Si lo elegido es
    // abrir un fichero, eso abre el diálogo, que es justo lo que pide «Abrir…».
    onStartStopClicked();
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
        verdictBoard_->showVerdict(VerdictState::Working,
                                   tr("Auto-inspección: esperando la primera pieza."));
        autoTimer_.start();
    } else {
        autoInspecting_ = false;
        autoTimer_.stop();
        video_->setEditingLocked(false);
        toolPalette_->setEnabled(true);
        onLiveSelectionChanged(video_->selectedIndex());  // reactiva calibrar/puntos
        verdictBoard_->showVerdict(VerdictState::Hidden);
        video_->clearResults();
    }
}

// LA ESCENA, MIRADA UNA VEZ POR ANÁLISIS.
//
// Se observa aquí y no en el temporizador a propósito: el disparador cuenta
// milisegundos de escena QUIETA, y preguntárselo con el reloj le daría la misma
// foto varias veces —o se saltaría los cambios entre dos ticks—. La escena solo
// puede cambiar cuando llega un análisis nuevo, así que ése es el sitio.
void MainWindow::observeSceneForPassTrigger(const AnalysisOverlay& overlay) {
    if (!passTriggerOn_ || !autoInspecting_ || !overlay.analysed) {
        return;
    }
    vision::SceneSnapshot snapshot;
    snapshot.atMs = passClock_.elapsed();

    // Los contornos de TODAS si se contaron; si no, el de la pieza medida. Sin
    // este segundo camino, el disparo no funcionaría con el recuento apagado —
    // que es como está de fábrica.
    const auto& outlines = overlay.pieceContours;
    const QSize frame = overlay.frameSize;
    const auto look = [&](const QPolygonF& polygon) {
        if (polygon.isEmpty()) {
            return;
        }
        const QRectF bounds = polygon.boundingRect();
        snapshot.centres.emplace_back(static_cast<float>(bounds.center().x()),
                                      static_cast<float>(bounds.center().y()));
        // «Toca el borde» con un margen de un píxel: el contorno de una pieza
        // pegada al canto puede quedarse en x=1 por el suavizado, y tratarla
        // como entera es justo lo que se quiere evitar.
        if (frame.isValid() &&
            (bounds.left() <= 1.0 || bounds.top() <= 1.0 ||
             bounds.right() >= frame.width() - 2.0 ||
             bounds.bottom() >= frame.height() - 2.0)) {
            snapshot.someoneTouchesTheEdge = true;
        }
    };
    if (!outlines.empty()) {
        for (const auto& polygon : outlines) {
            look(polygon);
        }
    } else if (overlay.valid) {
        look(overlay.contour);
    }

    const vision::PassVerdict verdict = passTrigger_.observe(snapshot);
    if (verdict.decision == vision::PassDecision::Measure) {
        passWantsMeasure_ = true;
    }
    // El porqué, en la barra de estado y solo cuando CAMBIA. Un disparador que
    // no dispara y no dice por qué se vive como «la auto-inspección no
    // funciona», y la causa casi siempre es una de dos —la cinta no para, o la
    // pieza asoma por el borde— que llevan a hacer cosas distintas. Repetirlo en
    // cada frame, en cambio, taparía cualquier otro mensaje.
    if (verdict.why != lastPassWhy_) {
        lastPassWhy_ = verdict.why;
        statusBar()->showMessage(QString::fromStdString(verdict.why));
    }
}

void MainWindow::onAutoTick() {
    // CON EL DISPARO POR PASO DE PIEZA, EL RELOJ NO DECIDE.
    //
    // El temporizador sigue corriendo porque es lo que descubre que la
    // inspección anterior terminó, pero quien dice «ahora» es la escena: una
    // pieza entera, quieta el tiempo pedido, y el encuadre vaciado desde la
    // anterior. La bandera la pone `observeSceneForPassTrigger` cuando llega un
    // análisis, que es el único momento en el que la escena puede haber
    // cambiado.
    if (passTriggerOn_) {
        if (!passWantsMeasure_) {
            return;
        }
        passWantsMeasure_ = false;
    }
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

std::vector<inspection::ToolConfig> MainWindow::liveToolConfigs() const {
    std::vector<inspection::ToolConfig> configs;
    configs.reserve(liveTools_.size());
    for (const auto& tool : liveTools_) {
        if (!tool.deleted) {
            configs.push_back(tool.config);
        }
    }
    return configs;
}

// EL VEREDICTO DE LO QUE SE VE, SIN INSPECCIONAR.
//
// Con herramientas dibujadas, cada fotograma ya da OK o NG por cota; eso solo
// se leía en la línea del panel de medidas (cerrado casi siempre) o en el color
// de cada etiqueta sobre el vídeo. El tablero lo dice en grande con la MISMA
// cuenta del panel, `judgeMeasurements`, así que no pueden contradecirse.
//
// Sin herramientas no hay nada que juzgar y el tablero no sale: un «Sin pieza»
// permanente con la cámara encendida solo para mirar sería ruido. Durante la
// auto-inspección manda su veredicto, que es el que se guarda.
void MainWindow::showMeasuringVerdict(const AnalysisOverlay& overlay) {
    if (autoInspecting_ || !overlay.analysed) {
        return;
    }
    const bool anyTool = std::any_of(liveTools_.begin(), liveTools_.end(),
                                     [](const auto& tool) { return !tool.deleted; });
    if (!anyTool) {
        verdictBoard_->showVerdict(VerdictState::Hidden);
        return;
    }
    if (!overlay.valid) {
        verdictBoard_->showVerdict(VerdictState::NoPiece, overlay.error);
        return;
    }
    const MeasurementsVerdict verdict = judgeMeasurements(
        overlay.toolResults, liveToolConfigs(), calibration_.mmPerPixel, currentUnit());
    if (!verdict.judged) {
        verdictBoard_->showVerdict(VerdictState::Hidden);
        return;
    }
    verdictBoard_->showVerdict(verdict.good ? VerdictState::Good : VerdictState::Bad,
                               verdict.reason);
}

void MainWindow::showLiveVerdict(const engine::InspectionEngine::Outcome& outcome) {
    // EL MOTIVO NOMBRA LA COTA. El resumen del motor dice «NG: 1 herramienta(s)
    // fuera de tolerancia», que obliga a ir a buscar cuál; si lo que falla es
    // una herramienta, el motivo sale de la misma cuenta que el panel de
    // medidas —«Ø interior: se pasa 0.15mm»—. Si falla otra cosa (apariencia,
    // posición, recuento), el resumen del motor ya lo dice con palabras.
    QString reason;
    if (!outcome.verdict.ok) {
        const MeasurementsVerdict tools = judgeMeasurements(
            outcome.toolResults, liveToolConfigs(), calibration_.mmPerPixel, currentUnit());
        reason = !tools.good ? tools.reason
                             : QString::fromStdString(outcome.verdict.summary)
                                   .remove(QRegularExpression(QStringLiteral("^NG:\\s*")));
    }
    verdictBoard_->showVerdict(outcome.verdict.ok ? VerdictState::Good : VerdictState::Bad,
                               reason);
    verdictBoard_->setToolTip(QString::fromStdString(outcome.verdict.summary));
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
            verdictBoard_->showVerdict(VerdictState::Failed,
                                       QString::fromStdString(result.error().message));
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
            statusBar()->showMessage(tr("Hoy: %1 inspecciones · %2 OK / %3 NG")
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
