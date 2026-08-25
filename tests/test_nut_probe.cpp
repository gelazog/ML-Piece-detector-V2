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

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/segmentation.h"
#include "vision/quality_metrics.h"
#include "vision/shape_class.h"

namespace {

// Las imágenes con las que el operador está probando de verdad. Están fuera del
// repositorio a propósito —son suyas— y por eso esta sonda se salta sin ruido si
// no están: nadie más que él las tiene.
std::filesystem::path ownImages() {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    std::error_code ec;
    return std::filesystem::exists(dir, ec) ? dir : std::filesystem::path();
}

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

void reportAt(const std::filesystem::path& dir, const char* file, int maxPieces);

void report(const char* file, int maxPieces) {
    reportAt(corpus(), file, maxPieces);
}

void reportAt(const std::filesystem::path& dir, const char* file, int maxPieces) {
    if (dir.empty()) {
        return;
    }
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
    report("tuerca_dominio_publico.jpg", 7);
    reportAt(ownImages(), "Producto_Tuerca_Liv_02.jpg", 1);
    reportAt(ownImages(), "producto-tuercas-prueba.jpg", 100);
    std::printf("\n  [tuercas] imágenes anotadas en: %s\n", outputDir().string().c_str());
    SUCCEED();
}

// LAS DEMÁS PIEZAS DEL USUARIO: engranajes y tornillos.
//
// Es una SONDA, no una guarda: informa de lo que hace la detección sobre las
// fotos reales y no falla nunca. Lo que se puede afirmar de verdad —que un
// número es el correcto y no solo repetible— está en
// `test_synthetic_measures.cpp`, donde la cota va antes que la imagen. En una
// foto de un engranaje no se sabe cuánto mide su agujero de verdad.
//
// Aquí se busca lo otro: que las formas reales no rompan el pipeline por sitios
// que las figuras dibujadas no tocan — brillos especulares, sombras de
// contacto, fondos que no son planos.
TEST(NutProbe, WhatTheDetectionDoesWithGearsAndScrews) {
    if (ownImages().empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    // El segundo número es cuántas piezas se ven a simple vista, para poder
    // comparar de un vistazo con lo que encuentra el programa.
    reportAt(ownImages(), "engranaje-1.png", 1);
    reportAt(ownImages(), "engranajes-1.jpg", 2);
    reportAt(ownImages(), "tornillo-1.png", 1);
    reportAt(ownImages(), "tornillo-2.png", 1);
    reportAt(ownImages(), "tornillos-1.png", 3);
    std::printf("\n  [piezas] imágenes anotadas en: %s\n", outputDir().string().c_str());
    SUCCEED();
}

// ¿CUÁNTO CUESTA MIRAR TODAS LAS PIEZAS EN VEZ DE UNA?
//
// El motor solo buscaba las demás piezas cuando había un número declarado, y la
// razón escrita era el coste: «buscar todas las piezas del frame cuesta, y quien
// inspecciona de una en una no tiene por qué pagarlo». El efecto secundario era
// que el modo automático no medía nada más que la mayor.
//
// Antes de quitar esa condición conviene saber de cuánto se habla, sobre las
// imágenes reales del usuario y no sobre una suposición.
TEST(NutProbe, WhatItCostsToLookAtEveryPiece) {
    if (ownImages().empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    for (const char* file : {"Producto_Tuerca_Liv_02.jpg", "producto-tuercas-prueba.jpg"}) {
        const cv::Mat image =
            cv::imread((ownImages() / file).string(), cv::IMREAD_GRAYSCALE);
        if (image.empty()) {
            continue;
        }
        pci::vision::PipelineConfig config;

        const auto time = [&](auto&& call) {
            call();  // calentamiento
            const auto started = std::chrono::steady_clock::now();
            constexpr int kPasses = 5;
            for (int i = 0; i < kPasses; ++i) {
                call();
            }
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started)
                       .count() /
                   kPasses;
        };

        const double one = time([&] { (void)pci::vision::analyzeFrame(image, config); });
        const double every = time([&] { (void)pci::vision::analyzeFrames(image, config); });
        auto all = pci::vision::analyzeFrames(image, config);
        const int found = all.isOk() ? static_cast<int>(all.value().size()) : -1;

        std::printf("  [coste] %-32s %3d piezas | una: %6.2f ms | todas: %6.2f ms "
                    "| extra: %+6.2f ms (x%.2f)\n",
                    file, found, one, every, every - one, one > 0.0 ? every / one : 0.0);
    }
    SUCCEED();
}

// EL RECUENTO DE AGUJEROS NO SIRVE EN ESTAS PIEZAS, y conviene tenerlo medido.
//
// Es una SONDA, no una guarda: informa y no falla. Pero el número que imprime
// decide si a esa cota se le puede poner una tolerancia, y hoy la respuesta es
// que no.
//
// Medido sobre las imágenes reales, contando los hijos del contorno exterior:
//
//                        a la vista   >=12px²  >=40px²  >=250px²
//   un tornillo solo              0       111      103        29
//   tres tornillos                0        24       24         4
//   un engranaje                 11         2        2         2
//
// Las dos direcciones fallan a la vez: cuenta 29 agujeros en un tornillo que
// NO TIENE NINGUNO —son los valles de la rosca, que se cierran como regiones
// dentro de la silueta— y encuentra 2 de los 11 que sí tiene un engranaje.
//
// Y no es cuestión de afinar el umbral. Se probó filtrar por FORMA, que sería
// lo razonable —un taladro es redondo y un valle de rosca es una tira—: con
// circularidad ≥ 0,75 los tornillos siguen dando 13, 15 y 11 agujeros
// inexistentes mientras el engranaje se queda en 1. Ni el área ni la forma los
// separan.
//
// La raíz es la misma que la de la otra discrepancia de la Región: la
// herramienta REBINARIZA con su propio Otsu dentro del recuadro en vez de usar
// la silueta que el pipeline ya calculó, y `pieceMaskWithHoles` —que sí existe
// y sí recupera los agujeros de verdad— no entra en juego. Cambiar eso mueve
// las medidas de todo el mundo, así que es una decisión de quien mantiene el
// producto y no algo que arreglar de paso.
TEST(NutProbe, WhatTheHoleCountActuallyCounts) {
    if (ownImages().empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    struct Caso {
        const char* file;
        int aLaVista;
    };
    const Caso casos[] = {
        {"engranaje-1.png", 11},  {"engranajes-1.jpg", 22},
        {"tornillo-1.png", 0},    {"tornillo-2.png", 0},
        {"tornillos-1.png", 0},   {"Producto_Tuerca_Liv_02.jpg", 1},
    };
    std::printf("  [agujeros] %-28s %10s %8s %8s %9s\n", "imagen", "a la vista", ">=12px2",
                ">=40px2", ">=250px2");
    for (const auto& caso : casos) {
        const cv::Mat gray =
            cv::imread((ownImages() / caso.file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            continue;
        }
        auto mask = pci::vision::segmentPiece(gray, {});
        if (!mask.isOk()) {
            continue;
        }
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(mask.value(), contours, hierarchy, cv::RETR_CCOMP,
                         cv::CHAIN_APPROX_SIMPLE);
        int outer = -1;
        double biggest = 0.0;
        for (std::size_t i = 0; i < contours.size(); ++i) {
            if (hierarchy[i][3] >= 0) {
                continue;
            }
            const double area = cv::contourArea(contours[i]);
            if (area > biggest) {
                biggest = area;
                outer = static_cast<int>(i);
            }
        }
        if (outer < 0) {
            continue;
        }
        std::printf("  [agujeros] %-28s %10d", caso.file, caso.aLaVista);
        for (double floorArea : {12.0, 40.0, 250.0}) {
            int holes = 0;
            for (int i = hierarchy[static_cast<std::size_t>(outer)][2]; i >= 0;
                 i = hierarchy[static_cast<std::size_t>(i)][0]) {
                if (cv::contourArea(contours[static_cast<std::size_t>(i)]) >= floorArea) {
                    ++holes;
                }
            }
            std::printf(" %8d", holes);
        }
        std::printf("\n");
    }
    SUCCEED();
}
