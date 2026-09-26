// EL MISMO FOTOGRAMA SEGMENTADO DOS VECES.
//
// `InspectionEngine::inspect`, en modo AUTOMÁTICO (número de piezas esperado
// distinto de uno), llamaba a `vision::analyzeFrame` para la pieza principal y
// LUEGO a `vision::analyzeFrames` para contarlas y medirlas todas. Las dos
// funciones segmentan la imagen desde cero — Otsu o borde, morfología,
// corrección manual, zona libre — así que cada inspección con más de una
// pieza pagaba esa segmentación dos veces por nada: la segunda llamada volvía
// a hacer exactamente el mismo trabajo que la primera ya había hecho.
//
// El arreglo es `vision::analyzeFrameAndPieces`: segmenta una vez y, desde esa
// MISMA máscara, sigue los dos caminos por separado — `mainFromMask` para la
// principal (el contorno mayor tal cual lo ve `analyzeFrame`, SIN pasar por
// `splitTouchingPieces`) y `piecesFromMask` para la lista completa (que sí
// puede partir manchas que se tocan). `InspectionEngine` ahora llama a esto en
// vez de a las dos funciones sueltas.
//
// La trampa que esta prueba existe para evitar: tomar la principal como «la
// mayor de la lista de `analyzeFrames`» en vez de calcularla aparte. No es lo
// mismo, y aquí se demuestra con la misma escena de dos discos que se tocan
// que usa `test_split_touching.cpp` (20 px de solape: «se tocan», no «se
// superponen» — ver ese fichero para el límite medido):
//
//   - Con `splitTouchingPieces` activo, la mancha de los dos discos unidos es
//     UN solo contorno para `analyzeFrame` (nunca pasa por `splitTouchingPieces`)
//     pero DOS para `analyzeFrames`. El área del contorno "mayor" de cada
//     camino no coincide — uno es la mancha entera, el otro es un disco solo.
//   - Con `autoOrient`, el recorte normalizado gira dentro de la envolvente de
//     SU contorno: el de la mancha entera y el de un disco solo tienen
//     envolventes distintas, así que el recorte también sale distinto.
//
// Por eso la prueba no compara «el mayor de todas» contra la principal: eso
// fallaría a propósito y no demostraría nada. Compara `analyzeFrameAndPieces`
// contra las dos llamadas de siempre, una a una, y exige que salgan bit a bit
// iguales — que es la única promesa que hace el cambio.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdio>

#include "synthetic_scenes.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;
using namespace pci::testing_support;

namespace {

// La misma escena que prueba que `splitTouchingPieces` separa dos piezas que
// se tocan (`SplitTouching.TwoDiscsThatTouchAreCountedAsTwo`): dos discos de
// 260 px solapados 20 —«tocarse», medido, no «superponerse»—. Reutilizarla en
// vez de inventar una figura propia evita redescubrir a ciegas cuánto solape
// hace falta para que el watershed encuentre dos corazones separados.
cv::Mat twoTouchingDiscs() {
    cv::Mat scene(400, 800, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::circle(scene, {280, 200}, 130, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);
    cv::circle(scene, {520, 200}, 130, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);
    return scene;
}

// Compara dos PieceAnalysis campo a campo. Aparte en vez de un hash: si algo
// difiere, el fallo dice CUÁL, no solo que hay una discrepancia.
void expectSameAnalysis(const vision::PieceAnalysis& a, const vision::PieceAnalysis& b,
                       const char* label) {
    EXPECT_EQ(a.contour.points, b.contour.points) << label << ": puntos del contorno";
    EXPECT_EQ(a.contour.subpixel, b.contour.subpixel) << label << ": puntos subpíxel";
    EXPECT_DOUBLE_EQ(a.contour.area, b.contour.area) << label << ": área";
    EXPECT_DOUBLE_EQ(a.contour.perimeter, b.contour.perimeter) << label << ": perímetro";
    EXPECT_EQ(a.contour.centroid, b.contour.centroid) << label << ": centroide";
    EXPECT_EQ(a.contour.rotatedRect.center, b.contour.rotatedRect.center)
        << label << ": centro de la caja girada";
    EXPECT_EQ(a.contour.rotatedRect.size, b.contour.rotatedRect.size)
        << label << ": tamaño de la caja girada";
    EXPECT_FLOAT_EQ(a.contour.rotatedRect.angle, b.contour.rotatedRect.angle)
        << label << ": ángulo de la caja girada";
    EXPECT_EQ(a.fixture.origin, b.fixture.origin) << label << ": origen del fixture";
    EXPECT_DOUBLE_EQ(a.fixture.angleDeg, b.fixture.angleDeg) << label << ": ángulo del fixture";
    EXPECT_DOUBLE_EQ(a.fixture.anisotropy, b.fixture.anisotropy)
        << label << ": anisotropía del fixture";
    // `norm(..., NORM_INF)` en vez de `countNonZero(a != b)`: la máscara es de
    // un canal pero `normalized` puede ser de varios, y `countNonZero` sobre
    // una comparación de más de un canal revienta con "cn == 1 in function
    // 'countNonZero'". La norma infinito sirve para los dos y cero significa
    // lo mismo: ningún píxel, de ningún canal, difiere.
    ASSERT_EQ(a.mask.size(), b.mask.size()) << label << ": tamaño de la máscara";
    EXPECT_DOUBLE_EQ(0.0, cv::norm(a.mask, b.mask, cv::NORM_INF))
        << label << ": máscara distinta";
    ASSERT_EQ(a.normalized.size(), b.normalized.size()) << label << ": tamaño del recorte";
    ASSERT_EQ(a.normalized.type(), b.normalized.type()) << label << ": tipo del recorte";
    EXPECT_DOUBLE_EQ(0.0, cv::norm(a.normalized, b.normalized, cv::NORM_INF))
        << label << ": recorte normalizado distinto";
}

struct Variant {
    const char* name;
    bool subpixel;
    bool split;
    bool roiAndAutoOrient;
};

const Variant kVariants[] = {
    {"nada", false, false, false},
    {"subpixel", true, false, false},
    {"split", false, true, false},
    {"subpixel+split", true, true, false},
    {"roi+autoOrient", false, false, true},
    {"todo junto", true, true, true},
};

vision::PipelineConfig configFor(const Variant& v, const cv::Mat& image) {
    vision::PipelineConfig config;
    config.subpixelEdges = v.subpixel;
    config.segmentation.splitTouchingPieces = v.split;
    if (v.roiAndAutoOrient) {
        config.roi = cv::Rect(10, 10, image.cols - 20, image.rows - 20);
        config.autoOrient = true;
    }
    return config;
}

}  // namespace

// LA PRINCIPAL: `analyzeFrameAndPieces(...).main` tiene que dar EXACTAMENTE lo
// mismo que `analyzeFrame(...)` a secas, en las seis combinaciones de
// subpíxel, partir piezas que se tocan y zona+orientación automática.
TEST(OneSegmentation, MainMatchesAnalyzeFrameExactly) {
    const cv::Mat image = twoTouchingDiscs();
    for (const auto& variant : kVariants) {
        SCOPED_TRACE(variant.name);
        const vision::PipelineConfig config = configFor(variant, image);

        auto expected = vision::analyzeFrame(image, config);
        auto both = vision::analyzeFrameAndPieces(image, config);

        ASSERT_EQ(expected.isOk(), both.main.isOk())
            << "analyzeFrame y analyzeFrameAndPieces().main discrepan en si hay pieza";
        if (expected.isOk()) {
            expectSameAnalysis(expected.value(), both.main.value(), variant.name);
        }
    }
}

// LA LISTA COMPLETA: `analyzeFrameAndPieces(...).all` tiene que dar
// EXACTAMENTE lo mismo que `analyzeFrames(...)`, pieza a pieza y en el mismo
// orden, en las mismas seis combinaciones.
TEST(OneSegmentation, AllMatchesAnalyzeFramesExactly) {
    const cv::Mat image = twoTouchingDiscs();
    for (const auto& variant : kVariants) {
        SCOPED_TRACE(variant.name);
        const vision::PipelineConfig config = configFor(variant, image);

        auto expected = vision::analyzeFrames(image, config);
        auto both = vision::analyzeFrameAndPieces(image, config);

        ASSERT_EQ(expected.isOk(), both.all.isOk())
            << "analyzeFrames y analyzeFrameAndPieces().all discrepan en si hay piezas";
        if (expected.isOk()) {
            ASSERT_EQ(expected.value().size(), both.all.value().size())
                << variant.name << ": número de piezas distinto";
            for (std::size_t i = 0; i < expected.value().size(); ++i) {
                expectSameAnalysis(expected.value()[i], both.all.value()[i], variant.name);
            }
        }
    }
}

// La escena de prueba de verdad tiene que ejercitar lo que dice el comentario
// de cabecera: con `splitTouchingPieces` la principal (mancha entera) y la
// mayor de la lista (un disco solo) NO son la misma pieza. Si esta prueba
// dejara de cumplirse, las dos de arriba dejarían de estar probando el caso
// que importa — pasarían igual comparando dos cosas que la casualidad hace
// iguales.
TEST(OneSegmentation, TheTestSceneActuallyForksTheTwoPaths) {
    const cv::Mat image = twoTouchingDiscs();
    vision::PipelineConfig config;
    config.segmentation.splitTouchingPieces = true;

    auto main = vision::analyzeFrame(image, config);
    auto all = vision::analyzeFrames(image, config);
    ASSERT_TRUE(main.isOk());
    ASSERT_TRUE(all.isOk());
    ASSERT_EQ(2u, all.value().size())
        << "la escena ya no se parte en dos piezas: ver test_split_touching.cpp";

    const auto& largestOfAll = all.value()[vision::largestPieceIndex(all.value())];
    EXPECT_NE(main.value().contour.area, largestOfAll.contour.area)
        << "con los discos partidos, la mancha entera y el disco mayor deberían "
           "medir distinto: si miden igual la escena ya no ejercita el caso";
}

// EL AHORRO: cuánto cuesta segmentar dos veces frente a una. Se mide en serie,
// mínimo de varias vueltas, sobre la bandeja real de cien tuercas si está
// disponible en esta máquina (si no, se salta: es una medida, no una
// aserción de corrección — esa la dan las tres pruebas de arriba).
TEST(OneSegmentation, MeasuredCostOfSegmentingTwice) {
    const cv::Mat image =
        cv::imread("C:/Users/furro/Pictures/IMG-MC/producto-tuercas-prueba.jpg",
                   cv::IMREAD_COLOR);
    if (image.empty()) {
        GTEST_SKIP() << "no está la bandeja de 100 tuercas en esta máquina";
    }
    vision::PipelineConfig config;
    config.segmentation.splitTouchingPieces = true;

    constexpr int kPasses = 20;
    const auto timeMs = [&](auto&& call) {
        call();  // caliente: no se cuenta el primer arranque de caches
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kPasses; ++i) {
            call();
        }
        return std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - t0)
                  .count() /
              kPasses;
    };

    const double before = timeMs([&] {
        (void)vision::analyzeFrame(image, config);
        (void)vision::analyzeFrames(image, config);
    });
    const double after = timeMs([&] { (void)vision::analyzeFrameAndPieces(image, config); });

    std::printf("  [banco] segmentar dos veces: %.2f ms; una vez para las dos "
               "respuestas: %.2f ms (%d vueltas, en serie)\n",
               before, after, kPasses);

    // No es una cifra fija: pero segmentar una vez en vez de dos no puede ser
    // MÁS lento, y con la bandeja de 100 tuercas la segmentación es la mayor
    // parte del coste, así que el margen sirve de red sin ser un cronómetro de
    // precisión.
    EXPECT_LT(after, before) << "una segmentación debería costar menos que dos";
}
