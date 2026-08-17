#include "vision/pipeline.h"

#include <chrono>

#include <opencv2/imgproc.hpp>

#include <utility>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/position_fixture.h"
#include "vision/segmentation.h"

namespace pci::vision {

namespace {

// La zona efectiva: el rectángulo con el que se recorta.
//
// Con polígono, es su envolvente — así el recorte y su ganancia de velocidad se
// conservan tal cual, y el polígono solo añade precisión encima. Sin polígono,
// el rectángulo que puso el operador.
cv::Rect croppingRect(const PipelineConfig& config, const cv::Rect& frameRect) {
    if (config.roiPolygon.size() >= 3) {
        return cv::boundingRect(config.roiPolygon) & frameRect;
    }
    return config.roi & frameRect;
}

// Borra de la máscara todo lo que cae FUERA del polígono.
//
// Se aplica sobre la máscara ya segmentada y no sobre la imagen, y la
// diferencia importa: recortar la imagen antes metería un borde artificial
// —negro contra la pieza— que la segmentación tomaría por un contorno de
// verdad. Sobre la máscara, lo de fuera simplemente deja de existir.
void keepOnlyInsidePolygon(cv::Mat& mask, const PipelineConfig& config,
                           const cv::Rect& crop, bool cropped) {
    if (config.roiPolygon.size() < 3 || mask.empty()) {
        return;
    }
    // El polígono viene en coordenadas de la imagen completa; la máscara está
    // en las del recorte.
    std::vector<cv::Point> local;
    local.reserve(config.roiPolygon.size());
    const cv::Point offset = cropped ? crop.tl() : cv::Point(0, 0);
    for (const auto& point : config.roiPolygon) {
        local.push_back(point - offset);
    }
    cv::Mat inside = cv::Mat::zeros(mask.size(), CV_8UC1);
    cv::fillPoly(inside, std::vector<std::vector<cv::Point>>{local}, cv::Scalar(255));
    cv::bitwise_and(mask, inside, mask);
}

// Convierte un contorno ya aceptado en un PieceAnalysis completo.
//
// La máscara limpia se construye **solo dentro de la envolvente de la pieza**:
// con varias piezas, hacerlo a tamaño de frame para cada una multiplicaría por
// N el coste de la parte que C4b acababa de abaratar.
core::Result<PieceAnalysis> analyzePiece(const cv::Mat& working, PieceContour contour,
                                         const PipelineConfig& config) {
    const cv::Rect box = cv::boundingRect(contour.points) &
                         cv::Rect(0, 0, working.cols, working.rows);
    if (box.empty()) {
        return core::Result<PieceAnalysis>::err("Contorno degenerado");
    }

    cv::Mat pieceMask = cv::Mat::zeros(box.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> shifted{contour.points};
    for (auto& point : shifted.front()) {
        point -= box.tl();
    }
    cv::drawContours(pieceMask, shifted, 0, cv::Scalar(255), cv::FILLED);

    auto fixture = computeFixture(pieceMask, config.autoOrient);
    if (!fixture.isOk()) {
        return core::Result<PieceAnalysis>::err(fixture.error().message);
    }
    fixture.value().origin += cv::Point2f(box.tl());

    Fixture local = fixture.value();
    local.origin -= cv::Point2f(box.tl());
    auto normalized =
        normalizePiece(working(box), pieceMask, local, config.canonicalSize);
    if (!normalized.isOk()) {
        return core::Result<PieceAnalysis>::err(normalized.error().message);
    }

    PieceAnalysis analysis;
    analysis.contour = std::move(contour);
    analysis.fixture = fixture.value();
    analysis.normalized = std::move(normalized.value());
    analysis.mask = cv::Mat::zeros(working.size(), CV_8UC1);
    pieceMask.copyTo(analysis.mask(box));
    return core::Result<PieceAnalysis>::ok(std::move(analysis));
}

// Lleva un análisis del marco del recorte al de la imagen completa.
void shiftToFullFrame(PieceAnalysis& analysis, const cv::Rect& roi, const cv::Size& full) {
    const cv::Point offset = roi.tl();
    for (auto& point : analysis.contour.points) {
        point += offset;
    }
    analysis.contour.centroid += cv::Point2f(offset);
    analysis.contour.rotatedRect.center += cv::Point2f(offset);
    analysis.fixture.origin += cv::Point2f(offset);

    cv::Mat fullMask = cv::Mat::zeros(full, CV_8UC1);
    analysis.mask.copyTo(fullMask(roi));
    analysis.mask = std::move(fullMask);
}

}  // namespace

core::Result<std::vector<PieceAnalysis>> analyzeFrames(const cv::Mat& image,
                                                       const PipelineConfig& config) {
    if (image.empty()) {
        return core::Result<std::vector<PieceAnalysis>>::err("Imagen vacía");
    }
    const cv::Rect frameRect(0, 0, image.cols, image.rows);
    const cv::Rect roi = croppingRect(config, frameRect);
    const bool useRoi = roi.area() > 0 && roi != frameRect;
    const cv::Mat working = useRoi ? image(roi) : image;

    auto mask = segmentPiece(working, config.segmentation);
    if (!mask.isOk()) {
        return core::Result<std::vector<PieceAnalysis>>::err(mask.error().message);
    }
    keepOnlyInsidePolygon(mask.value(), config, roi, useRoi);

    auto contours =
        findPieceContours(mask.value(), config.minAreaFraction, config.maxAreaFraction);
    if (contours.empty()) {
        return core::Result<std::vector<PieceAnalysis>>::err(
            "No se encontró ninguna pieza en la imagen");
    }

    std::vector<PieceAnalysis> pieces;
    pieces.reserve(contours.size());
    for (auto& contour : contours) {
        auto analysis = analyzePiece(working, std::move(contour), config);
        if (!analysis.isOk()) {
            continue;  // una pieza degenerada no invalida a las demás
        }
        if (useRoi) {
            shiftToFullFrame(analysis.value(), roi, image.size());
        }
        pieces.push_back(std::move(analysis.value()));
    }
    if (pieces.empty()) {
        return core::Result<std::vector<PieceAnalysis>>::err(
            "No se encontró ninguna pieza en la imagen");
    }
    return core::Result<std::vector<PieceAnalysis>>::ok(std::move(pieces));
}

cv::Mat pieceMaskWithHoles(const cv::Mat& image, const cv::Mat& filledMask,
                           const SegmentationOptions& options) {
    if (image.empty() || filledMask.empty() || image.size() != filledMask.size()) {
        return filledMask;  // sin nada que cruzar, lo que había es lo mejor que hay
    }
    auto segmented = segmentPiece(image, options);
    if (!segmented.isOk()) {
        // Si la segmentación falla ahora, la máscara rellena sigue siendo
        // válida como silueta: se pierden los agujeros y no se pierde la pieza.
        return filledMask;
    }
    cv::Mat withHoles;
    cv::bitwise_and(filledMask, segmented.value(), withHoles);
    // Un cruce que se queda sin pieza significa que la segunda segmentación no
    // vio lo mismo que la primera (otra polaridad, otro umbral automático). En
    // ese caso manda la máscara original: perder los agujeros es un
    // inconveniente, perder la pieza es no medir nada.
    if (cv::countNonZero(withHoles) < cv::countNonZero(filledMask) / 2) {
        return filledMask;
    }
    return withHoles;
}

core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image, const PipelineConfig& config,
                                         StageTimings* timings) {
    if (image.empty()) {
        return core::Result<PieceAnalysis>::err("Imagen vacía");
    }

    // El cronómetro solo existe si alguien lo pidió. `mark` devuelve los ms
    // transcurridos y reinicia, de forma que las etapas se reparten el total
    // sin huecos ni solapes — que es lo que permite comprobar que la suma
    // cuadra, y un desglose cuya suma no cuadra está mintiendo.
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    auto last = started;
    const auto mark = [&last](double* into) {
        if (into == nullptr) {
            return;
        }
        const auto now = Clock::now();
        *into = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
    };

    // Zona de detección: todo el pipeline trabaja sobre el recorte y al final
    // los resultados se llevan a coordenadas de la imagen completa.
    const cv::Rect frameRect(0, 0, image.cols, image.rows);
    cv::Rect roi = croppingRect(config, frameRect);
    const bool useRoi = roi.area() > 0 && roi != frameRect;
    const cv::Mat working = useRoi ? image(roi) : image;

    auto mask = segmentPiece(working, config.segmentation);
    if (!mask.isOk()) {
        return core::Result<PieceAnalysis>::err(mask.error().message);
    }
    keepOnlyInsidePolygon(mask.value(), config, roi, useRoi);
    mark(timings != nullptr ? &timings->segment : nullptr);

    auto contour =
        findLargestContour(mask.value(), config.minAreaFraction, config.maxAreaFraction);
    if (!contour.isOk()) {
        return core::Result<PieceAnalysis>::err(contour.error().message);
    }
    mark(timings != nullptr ? &timings->contour : nullptr);

    // Máscara reconstruida solo con el contorno mayor: los blobs de ruido que
    // sobrevivieron a la morfología no deben sesgar el fixture ni el recorte.
    cv::Mat cleanMask = cv::Mat::zeros(mask.value().size(), CV_8UC1);
    const std::vector<std::vector<cv::Point>> fill{contour.value().points};
    cv::drawContours(cleanMask, fill, 0, cv::Scalar(255), cv::FILLED);

    const auto fixture = computeFixture(cleanMask, config.autoOrient);
    if (!fixture.isOk()) {
        return core::Result<PieceAnalysis>::err(fixture.error().message);
    }
    mark(timings != nullptr ? &timings->fixture : nullptr);

    auto normalized =
        normalizePiece(working, cleanMask, fixture.value(), config.canonicalSize);
    if (!normalized.isOk()) {
        return core::Result<PieceAnalysis>::err(normalized.error().message);
    }
    mark(timings != nullptr ? &timings->normalize : nullptr);

    PieceAnalysis analysis;
    analysis.contour = std::move(contour.value());
    analysis.fixture = fixture.value();
    analysis.normalized = std::move(normalized.value());

    if (useRoi) {
        // Desplazar contorno, fixture y máscara al marco de la imagen completa.
        const cv::Point offset = roi.tl();
        for (auto& point : analysis.contour.points) {
            point += offset;
        }
        analysis.contour.centroid += cv::Point2f(offset);
        analysis.contour.rotatedRect.center += cv::Point2f(offset);
        analysis.fixture.origin += cv::Point2f(offset);

        cv::Mat fullMask = cv::Mat::zeros(image.size(), CV_8UC1);
        cleanMask.copyTo(fullMask(roi));
        analysis.mask = std::move(fullMask);
    } else {
        analysis.mask = std::move(cleanMask);
    }

    if (timings != nullptr) {
        // El total se mide de punta a punta, NO sumando las etapas. Así, si
        // alguna vez el desglose deja de cuadrar con el total, la diferencia
        // aparece en vez de esconderse: es el trozo de trabajo que nadie está
        // atribuyendo a nada.
        timings->total =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }
    return core::Result<PieceAnalysis>::ok(std::move(analysis));
}

}  // namespace pci::vision
