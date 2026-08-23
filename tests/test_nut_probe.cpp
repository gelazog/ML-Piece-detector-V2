// SONDA: qué hace de verdad la detección con tuercas.
//
// El operador informa de que con una tuerca suelta y con varias, ni el recuento
// ni el contorno salen bien. Antes de tocar nada hay que ver QUÉ hace, y una
// tuerca tiene dos cosas que la hacen distinta de todo lo que hay en el banco:
//
//   - Un AGUJERO en medio, que el contorno externo se traga.
//   - Un borde HEXAGONAL con chaflanes, que reflejan y se confunden con el fondo.
//
// Esto no afirma nada todavía: mide y escribe imágenes anotadas para poder
// mirarlas. Las cifras que salgan aquí son las que decidirán qué hay que
// arreglar.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/quality_metrics.h"
#include "vision/shape_class.h"

namespace {

std::filesystem::path corpus() {
    for (const auto* candidate : {"testdata/real", "../testdata/real", "../../testdata/real",
                                  "../../../testdata/real"}) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return std::filesystem::path(candidate);
        }
    }
    return {};
}

std::filesystem::path outputDir() {
    const auto dir = std::filesystem::temp_directory_path() / "pci_sonda_tuercas";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Dibuja lo que la detección cree que hay, para poder mirarlo.
void annotate(const cv::Mat& image, const std::vector<pci::vision::PieceContour>& pieces,
              const std::string& name) {
    cv::Mat shown = image.clone();
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const std::vector<std::vector<cv::Point>> one{pieces[i].points};
        cv::drawContours(shown, one, 0, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
        cv::drawContours(shown, one, 0, cv::Scalar(0, 230, 0), 2, cv::LINE_AA);
        const cv::Rect box = cv::boundingRect(pieces[i].points);
        cv::putText(shown, std::to_string(i + 1), box.tl() + cv::Point(4, 26),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
        cv::putText(shown, std::to_string(i + 1), box.tl() + cv::Point(4, 26),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 230, 255), 2, cv::LINE_AA);
    }
    cv::imwrite((outputDir() / name).string(), shown);
}

void report(const char* file, int maxPieces) {
    const auto dir = corpus();
    const cv::Mat image = cv::imread((dir / file).string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        std::printf("  [tuercas] %s: no se pudo leer\n", file);
        return;
    }

    pci::vision::PipelineConfig config;
    auto all = pci::vision::analyzeFrames(image, config);
    std::printf("\n  [tuercas] === %s (%dx%d) ===\n", file, image.cols, image.rows);
    if (!all.isOk()) {
        std::printf("  [tuercas] la detección dice: %s\n", all.error().message.c_str());
        return;
    }

    std::vector<pci::vision::PieceContour> contours;
    for (const auto& piece : all.value()) {
        contours.push_back(piece.contour);
    }
    std::printf("  [tuercas] piezas encontradas: %d (se esperaban %d)\n",
                static_cast<int>(contours.size()), maxPieces);

    const double frameArea = static_cast<double>(image.total());
    for (std::size_t i = 0; i < contours.size() && i < 12; ++i) {
        const auto& c = contours[i];
        const cv::Rect box = cv::boundingRect(c.points);
        const double ragged = pci::vision::contourRaggedness(c.area, c.perimeter);
        const auto shape = pci::vision::classifyShape(c.points);
        std::printf("  [tuercas]  %2d) area %8.0f (%5.2f %% del encuadre)  caja %4dx%-4d  "
                    "dentado %5.2f  forma: %s(%d) desv %.2f px  %s\n",
                    static_cast<int>(i) + 1, c.area, 100.0 * c.area / frameArea, box.width,
                    box.height, ragged, pci::vision::shapeKindName(shape.kind),
                    shape.sides, shape.deviation, shape.reason.c_str());
    }
    annotate(image, contours, std::string(file) + "_detectado.png");
}

}  // namespace

TEST(NutProbe, WhatTheDetectionActuallyDoesWithNuts) {
    if (corpus().empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    report("tuerca_dominio_publico.jpg", 1);
    report("arandelas_con_agujero.jpg", 6);
    std::printf("\n  [tuercas] imágenes anotadas en: %s\n", outputDir().string().c_str());
    SUCCEED();
}
