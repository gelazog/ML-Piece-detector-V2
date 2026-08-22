// pci_probe: corre el pipeline de inspección sobre un fichero y escribe lo que
// midió.
//
// Existe porque hasta ahora nada que dependa de VER una pieza se podía
// comprobar sin la cámara delante. Eso ya ha costado varios diseños con el banco
// en verde que fallaban con hardware real: el banco probaba figuras sintéticas
// perfectas, y nadie podía mirar lo que sale de una imagen de verdad sin abrir
// la aplicación entera y ponerse delante de una pieza.
//
// Este fichero es SOLO cableado: abrir la fuente, llamar al pipeline y volcar
// el informe. Todo lo que se puede probar sin abrir un fichero —el parseo de la
// línea de órdenes y el formato de salida— vive en `probe_options.h`, porque es
// el contrato con quien consume la herramienta.

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "domain/calibration.h"
#include "inspection_editor/auto_measure.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"
#include "vision/subpixel_edge.h"

#include "probe_options.h"

namespace {

using pci::probe::ProbeOptions;
using pci::probe::ProbeReport;

// Analiza UN frame ya en gris. Devuelve el mensaje de error del pipeline si no
// hubo pieza (cadena vacía = hubo pieza). Escribe en el informe lo medido: con
// vídeo se sobreescribe frame a frame, de modo que lo que queda es el último
// frame con pieza, que es el estado más reciente de la escena.
std::string analyzeOneFrame(const cv::Mat& gray, const ProbeOptions& options,
                            ProbeReport& report, pci::probe::ProbeStageTimes& accumulated) {
    pci::vision::PipelineConfig config;
    config.roi = report.zone;
    config.subpixelEdges = report.subpixel;

    pci::vision::StageTimings timings;
    auto analysis = pci::vision::analyzeFrame(gray, config, &timings);
    if (!analysis.isOk()) {
        return analysis.error().message;
    }
    const auto& piece = analysis.value();

    report.areaPx = piece.contour.area;
    const double frameArea = static_cast<double>(gray.cols) * static_cast<double>(gray.rows);
    report.frameFraction = frameArea > 0.0 ? piece.contour.area / frameArea : 0.0;
    report.perimeterPx = piece.contour.perimeter;
    report.centroid = piece.contour.centroid;
    report.boxWidth = piece.contour.rotatedRect.size.width;
    report.boxHeight = piece.contour.rotatedRect.size.height;
    report.boxAngleDeg = piece.contour.rotatedRect.angle;

    report.fixtureOrigin = piece.fixture.origin;
    report.fixtureAngleDeg = piece.fixture.angleDeg;
    report.anisotropy = piece.fixture.anisotropy;

    // El contorno se vuelve a sacar de la máscara con CHAIN_APPROX_NONE, y no se
    // reutiliza `piece.contour.points`, por una razón MEDIDA: el pipeline
    // devuelve el contorno simplificado, y sobre el cuadrado de
    // `aruco_4x4_id0.png` eso deja 12 puntos, todos en las cuatro esquinas, por
    // los que pasa una circunferencia casi perfecta. Clasificando esos 12, el
    // cuadrado sale «circulo de Ø 844,8 px» con 0,47 px de desviación;
    // clasificando los 2392 densos sale «poligono de 4 lados» con 1,00 px, que
    // es además lo que propone `proposeTools` (que hace exactamente esto mismo).
    //
    // Es el mismo contorno que se le pasa a la medición, a propósito: si el
    // banco clasificara uno y midiera sobre otro, podría decir «círculo» y
    // proponer cuatro lados en la misma pantalla.
    std::vector<std::vector<cv::Point>> dense;
    cv::findContours(piece.mask, dense, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    const std::vector<cv::Point>* outer = nullptr;
    double bestArea = -1.0;
    for (const auto& candidate : dense) {
        const double area = cv::contourArea(candidate);
        if (area > bestArea) {
            bestArea = area;
            outer = &candidate;
        }
    }
    // Una pieza cortada por el encuadre no se puede medir: lo que se ve es un
    // trozo y todas sus cotas son limites inferiores. El proyecto ya lo
    // comprobaba al REGISTRAR y nadie lo miraba al medir.
    report.frameContact = pci::vision::frameContactWarning(
        pci::vision::pieceTouchesFrame(piece.contour.points, gray.size()));
    report.pipelineContourPoints = static_cast<int>(piece.contour.points.size());
    report.contourPoints = outer != nullptr ? static_cast<int>(outer->size()) : 0;

    // La figura se pregunta con la MÁSCARA, no solo con el contorno: sin ella
    // una arandela sale como disco y el Ø interior no aparecería por ningún
    // lado.
    //
    // Y con la máscara CON AGUJEROS, que es la mitad que faltaba: la que
    // devuelve el análisis viene con el contorno exterior relleno, así que
    // pasarla aquí tenía exactamente el efecto que este comentario decía
    // evitar. Se sondeó una arandela de verdad y salió «circulo» con dos cotas.
    const cv::Mat measureMask =
        pci::vision::pieceMaskWithHoles(gray, piece.mask, config.segmentation);
    // Con el contorno afinado cuando lo hay: el diámetro y la redondez son justo
    // donde media décima de píxel se nota. Solo se pasa si corresponde al mismo
    // contorno que se está clasificando.
    const std::vector<cv::Point>& classified =
        outer != nullptr ? *outer : piece.contour.points;
    // La clasificación de figura NO usa el contorno afinado, y es una decisión
    // tomada con el número delante.
    //
    // `classifyShape` juzga con `worstRadialDeviation`, que es un MÁXIMO: basta
    // un punto malo para cambiar el veredicto. El afinado mejora la media —sobre
    // esta bola, la desviación media baja— pero puede empeorar el peor punto,
    // porque donde hay un reflejo especular el perfil cruza el nivel medio dos
    // veces y ese punto se coloca en el cruce equivocado.
    //
    // Medido: con el afinado enchufado aquí, la bola pasaba de «circulo» a
    // «irregular». Una medida más fina que hace fallar la clasificación es peor
    // que la medida de antes, así que no se enchufa.
    //
    // El parámetro de `classifyShape` se queda —probado y documentado— porque es
    // la costura por donde entrará esto el día que el afinado sepa descartar los
    // cruces ambiguos. Hoy no lo sabe.
    const std::vector<cv::Point2f>* refined = nullptr;
    const pci::vision::ShapeClass shape =
        pci::vision::classifyShape(classified, measureMask, {}, refined);
    report.shapeKind = pci::vision::shapeKindName(shape.kind);
    report.shapeSides = shape.sides;
    report.outerDiameterPx = shape.outerDiameter;
    report.innerDiameterPx = shape.innerDiameter;
    report.roundnessPx = shape.roundness;
    report.deviationPx = shape.deviation;
    report.shapeReason = shape.reason;

    double toolsMs = 0.0;
    if (options.measure) {
        const auto started = std::chrono::steady_clock::now();
        const auto proposals = pci::inspection::proposeTools(gray, measureMask, piece.fixture, {},
                                                            report.mmPerPixel);
        toolsMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                           started)
                      .count();
        report.proposals.clear();
        report.proposals.reserve(proposals.size());
        for (const auto& proposal : proposals) {
            pci::probe::ProbeProposal entry;
            entry.name = proposal.config.name;
            entry.type = pci::inspection::toolTypeName(proposal.config.type);
            entry.value = proposal.measured;
            entry.isLength = pci::probe::proposalIsLength(proposal.config.type);
            entry.reason = proposal.reason;
            entry.detail = proposal.detail;
            report.proposals.push_back(std::move(entry));
        }
    }

    accumulated.segment += timings.segment;
    accumulated.contour += timings.contour;
    accumulated.fixture += timings.fixture;
    accumulated.normalize += timings.normalize;
    accumulated.tools += toolsMs;
    // El total del pipeline se mide de punta a punta y no incluye la medición,
    // que corre por encima. Sumarla aquí es lo que hace que el reparto que se
    // imprime sea el del trabajo entero y no el de un trozo.
    accumulated.total += timings.total + toolsMs;
    return {};
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // El informe lleva acentos y el símbolo Ø. Sin esto la consola de Windows
    // los escribe en su página de códigos heredada y el resultado es ilegible;
    // un informe que no se puede leer no informa de nada.
    ::SetConsoleOutputCP(CP_UTF8);
#endif

    auto parsed = pci::probe::parseProbeArgs(argc, argv);
    if (!parsed.isOk()) {
        std::fprintf(stderr, "%s\n", parsed.error().message.c_str());
        return pci::probe::kExitBadArguments;
    }
    const ProbeOptions options = parsed.value();

    // Primero como imagen y luego como vídeo: `imread` falla en silencio con un
    // vídeo, y `VideoCapture` abre también imágenes sueltas, así que el orden
    // inverso llamaría «vídeo de un frame» a un PNG.
    const cv::Mat still = cv::imread(options.source, cv::IMREAD_GRAYSCALE);
    cv::VideoCapture capture;
    const bool isVideo = still.empty();
    if (isVideo && !capture.open(options.source)) {
        std::fprintf(stderr,
                     "No se pudo abrir «%s» ni como imagen ni como vídeo. Comprueba la ruta y "
                     "que el formato tenga códec instalado.\n",
                     options.source.c_str());
        return pci::probe::kExitCannotOpen;
    }

    ProbeReport report;
    report.source = options.source;
    report.subpixel = options.subpixel;
    report.video = isVideo;

    pci::probe::ProbeStageTimes accumulated;
    std::string lastError = "no se llegó a leer ningún frame";
    const int wanted = isVideo ? options.frames : 1;

    for (int i = 0; i < wanted; ++i) {
        cv::Mat gray;
        if (isVideo) {
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty()) {
                break;  // fin del vídeo: no es un error, es que se acabó
            }
            if (frame.channels() == 1) {
                gray = frame;
            } else {
                cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            }
        } else {
            gray = still;
        }

        if (report.framesRead == 0) {
            report.width = gray.cols;
            report.height = gray.rows;

            // La zona se recorta al frame igual que hace el pipeline, y se
            // guarda ya recortada: imprimir la que se pidió cuando se está
            // usando otra sería mentir sobre dónde se buscó.
            const cv::Rect frameRect(0, 0, gray.cols, gray.rows);
            if (options.zone.area() > 0) {
                const cv::Rect effective = options.zone & frameRect;
                if (effective.area() <= 0) {
                    std::fprintf(stderr,
                                 "La zona x=%d y=%d w=%d h=%d no toca el frame de %dx%d px: el "
                                 "pipeline la ignoraría y analizaría la imagen entera.\n",
                                 options.zone.x, options.zone.y, options.zone.width,
                                 options.zone.height, gray.cols, gray.rows);
                    return pci::probe::kExitBadArguments;
                }
                if (effective != options.zone) {
                    std::fprintf(stderr,
                                 "Aviso: la zona se recortó al frame, de w=%d h=%d a w=%d h=%d.\n",
                                 options.zone.width, options.zone.height, effective.width,
                                 effective.height);
                }
                report.zone = effective;
            }

            // La escala sale de la misma función que usa la aplicación, con el
            // ancho del frame: así el banco no tiene su propia aritmética de
            // calibración que pudiera divergir de la de producción.
            if (options.calibrated()) {
                const auto calibration = pci::domain::calibrationFromKnownLength(
                    options.calibrationPx, options.calibrationMm, gray.cols, 60.0);
                report.mmPerPixel = calibration.mmPerPixel;
            }
        }

        ++report.framesRead;
        const std::string error = analyzeOneFrame(gray, options, report, accumulated);
        if (error.empty()) {
            ++report.framesWithPiece;
        } else {
            lastError = error;
        }
    }

    if (report.framesRead == 0) {
        std::fprintf(stderr, "«%s» se abrió pero no dio ningún frame legible.\n",
                     options.source.c_str());
        return pci::probe::kExitCannotOpen;
    }

    report.ok = report.framesWithPiece > 0;
    report.measured = options.measure;
    if (!report.ok) {
        report.message = lastError;
    } else {
        // Media por frame CON pieza: con vídeo, los frames en los que no había
        // nada no cuentan, porque su reparto de tiempos es el de un pipeline que
        // se paró a medio camino.
        const double n = static_cast<double>(report.framesWithPiece);
        report.times.segment = accumulated.segment / n;
        report.times.contour = accumulated.contour / n;
        report.times.fixture = accumulated.fixture / n;
        report.times.normalize = accumulated.normalize / n;
        report.times.tools = accumulated.tools / n;
        report.times.total = accumulated.total / n;
    }

    const std::string text =
        options.json ? pci::probe::renderJson(report) : pci::probe::renderText(report);
    std::fwrite(text.data(), 1, text.size(), stdout);

    // Sin pieza no hay nada que verificar, y el banco tiene que decirlo con el
    // código de salida: un script de CI no lee el motivo, lee el número.
    return report.ok ? pci::probe::kExitOk : pci::probe::kExitNoPiece;
}
