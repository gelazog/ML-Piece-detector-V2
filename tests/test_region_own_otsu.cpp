// LA REGIÓN SE BINARIZA POR SU CUENTA.
//
// `runRegion` recorta el recuadro y le aplica SU PROPIO Otsu, sin mirar nada de
// lo que el operador configuró: ni umbral manual, ni polaridad, ni morfología,
// ni el método por canto. Mide sobre otra silueta que el resto del programa.
//
// Esta prueba no arregla nada: PUBLICA EL DESACUERDO con números, sobre las
// fotos reales, para poder decidir con ellos delante.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>
#include <filesystem>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/piece_report.h"
#include "vision/geometry_features.h"
#include "vision/pipeline.h"
#include "vision/segmentation.h"

using namespace pci;

namespace {

std::filesystem::path corpusDir() {
    for (const auto* c : {"testdata/real", "../testdata/real", "../../testdata/real",
                          "../../../testdata/real"}) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) {
            return std::filesystem::path(c);
        }
    }
    return {};
}

}  // namespace

TEST(RegionOwnOtsu, ItDisagreesWithTheContourTheRestOfTheAppMeasures) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    const char* files[] = {"tuerca_dominio_publico.jpg", "arandelas_con_agujero.jpg",
                           "perno_cromado_con_arandela.jpg", "pinon_corona_dentada.jpg"};

    int looked = 0;
    for (const char* file : files) {
        const cv::Mat gray = cv::imread((dir / file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            continue;
        }
        const auto analysed = vision::analyzeFrame(gray, {});
        if (!analysed.isOk()) {
            continue;
        }
        ++looked;
        const auto& piece = analysed.value();
        const double contourArea = piece.contour.area;
        const cv::Rect box = cv::boundingRect(piece.contour.points);

        // Una región que abarca la pieza entera, con holgura.
        inspection::RegionGeometry region;
        region.center = {0.0F, 0.0F};
        region.width = static_cast<float>(box.width) * 1.3F;
        region.height = static_cast<float>(box.height) * 1.3F;

        const auto measure = [&](inspection::RegionMeasure what) {
            region.measure = what;
            inspection::ToolConfig config;
            config.type = inspection::ToolType::Region;
            config.name = "zona";
            config.geometryJson =
                inspection::toJson(inspection::ToolGeometry(region));
            const auto results = inspection::runTools(gray, piece.fixture, {config}, 0.0,
                                                      inspection::LengthUnit::Pixels);
            return results.empty() ? -1.0 : results.front().measured;
        };

        const double regionArea = measure(inspection::RegionMeasure::Area);
        const double holes = measure(inspection::RegionMeasure::HoleCount);
        const double gap = 100.0 * (regionArea - contourArea) / contourArea;
        std::printf("  [región] %-34s contorno %9.0f  región %9.0f  (%+6.1f%%)  "
                    "agujeros que cuenta: %.0f\n",
                    file, contourArea, regionArea, gap, holes);
    }
    EXPECT_GT(looked, 2) << "no se pudo analizar casi ninguna: el corpus no está";
}

// ¿SE PUEDE SABER CUÁNDO EL OTSU LOCAL ESTÁ INVENTANDO?
//
// Otsu SIEMPRE devuelve un corte, tenga la ventana dos poblaciones o una sola.
// Con una sola —el recuadro cae entero dentro de la pieza, o entero sobre la
// mesa— parte el RUIDO en dos y devuelve una figura que no existe. Esa es la
// sospecha de por qué `HoleCount` cuenta 215 agujeros en una arandela.
//
// Otsu trae de serie una medida de lo bien que separa: la varianza ENTRE clases
// dividida por la varianza total. Vale casi 1 cuando hay dos poblaciones claras
// y se desploma cuando solo hay una. Aquí se comprueba si de verdad distingue
// los dos casos, sobre recortes reales.
TEST(RegionOwnOtsu, TheSeparabilityTellsARealBoundaryFromInventedNoise) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    const char* files[] = {"tuerca_dominio_publico.jpg", "arandelas_con_agujero.jpg",
                           "perno_cromado_con_arandela.jpg", "pinon_corona_dentada.jpg"};
    int looked = 0;
    for (const char* file : files) {
        const cv::Mat gray = cv::imread((dir / file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) continue;
        const auto analysed = vision::analyzeFrame(gray, {});
        if (!analysed.isOk()) continue;
        ++looked;
        const auto& piece = analysed.value();
        const cv::Rect box = cv::boundingRect(piece.contour.points);

        // (a) UN RECORTE HONRADO: la pieza con su fondo alrededor. Dos
        //     poblaciones de verdad.
        cv::Rect honest = box;
        honest -= cv::Point(box.width / 6, box.height / 6);
        honest += cv::Size(box.width / 3, box.height / 3);
        honest &= cv::Rect(0, 0, gray.cols, gray.rows);

        // (b) UN RECORTE CIEGO: un trozo pequeño del INTERIOR de la pieza. Una
        //     sola población, y sin embargo Otsu devolverá un corte igual.
        const cv::Rect blind(box.x + box.width * 2 / 5, box.y + box.height * 2 / 5,
                             std::max(8, box.width / 6), std::max(8, box.height / 6));

        for (const auto& [what, roi] : {std::pair<const char*, cv::Rect>{"pieza+fondo", honest},
                                        {"solo interior", blind & cv::Rect(0,0,gray.cols,gray.rows)}}) {
            if (roi.area() < 64) continue;
            const cv::Mat crop = gray(roi);
            cv::Mat binary;
            const double otsu = cv::threshold(crop, binary, 0.0, 255.0,
                                              cv::THRESH_BINARY | cv::THRESH_OTSU);
            // Separabilidad: varianza entre clases / varianza total.
            cv::Scalar mean, stddev;
            cv::meanStdDev(crop, mean, stddev);
            const double total = stddev[0] * stddev[0];
            cv::Mat fg = crop > otsu;
            const double n = static_cast<double>(crop.total());
            const double n1 = static_cast<double>(cv::countNonZero(fg));
            const double w1 = n1 / n, w0 = 1.0 - w1;
            cv::Scalar m1 = cv::mean(crop, fg);
            cv::Scalar m0 = cv::mean(crop, ~fg);
            const double between = w0 * w1 * (m1[0] - m0[0]) * (m1[0] - m0[0]);
            const double eta = total > 0.0 ? between / total : 0.0;
            std::vector<std::vector<cv::Point>> cs;
            std::vector<cv::Vec4i> h;
            cv::findContours(binary, cs, h, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);
            int holes = 0;
            for (std::size_t i = 0; i < cs.size(); ++i) { if (h[i][3] >= 0) ++holes; }
            std::printf("  [otsu] %-30s %-14s eta %.3f  corte %3.0f  huecos %4d\n",
                        file, what, eta, otsu, holes);
        }
    }
    EXPECT_GT(looked, 2);
}

// LO QUE DE VERDAD ROMPE EL CONTEO DE AGUJEROS.
//
// La sospecha anterior —«Otsu inventa cuando la ventana es uniforme»— se cayó al
// medirla: la separabilidad de un recorte ciego (0,690-0,742) se solapa con la
// de uno honrado (0,617-0,866), porque el metal mecanizado tiene contraste real
// por dentro. No hay ahí una señal que distinga nada.
//
// Los mismos números enseñaban lo que sí pasa: 139, 2095, 343 y 156 huecos en
// recortes HONRADOS. No es el umbral: es que NINGÚN hueco se filtra por tamaño.
// `RETR_CCOMP` devuelve como hijo cualquier isla interior, y una superficie
// roscada tiene miles de motas de un puñado de píxeles.
//
// Aquí se mide si un mínimo relativo a la figura los pone en su sitio.
TEST(RegionOwnOtsu, HolesNeedAMinimumSizeToMeanAnything) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    struct Case { const char* file; int truth; const char* what; };
    const Case cases[] = {
        {"tuerca_dominio_publico.jpg", 1, "una tuerca: un taladro"},
        {"moneda_5_yen_con_agujero.png", 1, "moneda de 5 yenes: un agujero"},
        {"pinon_corona_dentada.jpg", 1, "piñón: el agujero del eje"},
    };
    int looked = 0;
    for (const auto& one : cases) {
        const cv::Mat gray = cv::imread((dir / one.file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) continue;
        const auto analysed = vision::analyzeFrame(gray, {});
        if (!analysed.isOk()) continue;
        ++looked;
        const cv::Rect box = cv::boundingRect(analysed.value().contour.points);
        cv::Rect roi = box;
        roi -= cv::Point(box.width / 8, box.height / 8);
        roi += cv::Size(box.width / 4, box.height / 4);
        roi &= cv::Rect(0, 0, gray.cols, gray.rows);
        cv::Mat binary;
        cv::threshold(gray(roi), binary, 0.0, 255.0, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
        std::vector<std::vector<cv::Point>> cs;
        std::vector<cv::Vec4i> h;
        cv::findContours(binary, cs, h, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);

        // La figura: la mayor de las exteriores.
        double figure = 0.0;
        for (std::size_t i = 0; i < cs.size(); ++i) {
            if (h[i][3] < 0) figure = std::max(figure, cv::contourArea(cs[i]));
        }
        std::printf("  [huecos] %-30s verdad %d  ", one.file, one.truth);
        for (const double fraction : {0.0, 0.0005, 0.002, 0.01}) {
            int holes = 0;
            for (std::size_t i = 0; i < cs.size(); ++i) {
                if (h[i][3] >= 0 && cv::contourArea(cs[i]) >= fraction * figure) ++holes;
            }
            std::printf("%s%.2f%%->%-5d", fraction == 0.0 ? "" : " ", 100.0 * fraction, holes);
        }
        std::printf("  (%s)\n", one.what);
    }
    EXPECT_GT(looked, 1);
}

// LA MISMA PREGUNTA, HECHA A LA SEGMENTACIÓN DEL PROGRAMA. Y LA RESPUESTA ES
// QUE TAMPOCO.
//
// El filtro por tamaño no basta: a 1 % de la figura salen 2, 4 y 0 agujeros
// donde hay uno, y al piñón se le borra el del eje. Antes de proponer nada hay
// que saber si el otro camino —contar sobre la máscara que el programa ya
// calcula, con su suavizado y su morfología— da la respuesta buena.
//
// Si la diera, la mejor opción sería que las herramientas miraran ESA silueta.
// Si no la diera, no habría a dónde ir y la recomendación tendría que ser otra.
TEST(RegionOwnOtsu, TheAppsOwnSegmentationMiscountsTheHolesOfACoin) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    // VERDAD DE CAMPO, MIRANDO LAS FOTOS. La primera versión de esta prueba puso
    // «1 agujero» en las cuatro sin abrirlas, y tres de esas cuatro eran falsas:
    //
    //   tuerca_dominio_publico.jpg   SIETE tuercas y racores distintos. La pieza
    //                                mayor es una brida con cuatro taladros más
    //                                el central: sus CINCO agujeros eran
    //                                correctos y se denunciaron como defecto.
    //   pinon_corona_dentada.jpg     un MONTÓN de piñones solapados llenando el
    //                                encuadre, no un piñón con su eje.
    //   arandelas_con_agujero.jpg    una BOLSA con etiqueta impresa en ruso; la
    //                                figura que se mide es el texto, y sus
    //                                «agujeros» son las letras.
    //
    // Solo la moneda es lo que decía ser, y solo ella queda con verdad fijada.
    // Las otras se quedan para publicar su número, sin afirmar nada de él.
    struct Case { const char* file; int truth; };
    const Case cases[] = {
        {"moneda_5_yen_con_agujero.png", 1},
        {"tuerca_dominio_publico.jpg", -1},
        {"pinon_corona_dentada.jpg", -1},
        {"arandelas_con_agujero.jpg", -1},
    };
    int looked = 0;
    for (const auto& one : cases) {
        const cv::Mat gray = cv::imread((dir / one.file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) continue;
        const auto mask = vision::segmentPiece(gray, {});
        if (!mask.isOk()) continue;
        ++looked;
        const auto report = vision::describeContour(mask.value());
        std::printf("  [pipeline] %-30s verdad %2d -> el programa cuenta %zu\n",
                    one.file, one.truth, report.holes.size());
        if (one.truth >= 0) {
            // SE FIJA EL DEFECTO, NO LA ESPERANZA. La segmentación del programa
            // tampoco cuenta bien: 5, 117 y 19 donde hay uno. El día que alguien
            // lo arregle, esta prueba fallará y tendrá que venir a borrarla —
            // que es justo lo que se quiere, porque entonces la recomendación
            // de más abajo cambia.
            EXPECT_GT(static_cast<int>(report.holes.size()), one.truth)
                << one.file
                << ": ahora sí cuenta bien los agujeros. Si es a propósito, quita "
                   "esta prueba y revisa la nota de ARQUITECTURA: mandar las "
                   "herramientas a mirar esta silueta pasa a ser una opción viable";
        }
    }
    EXPECT_GT(looked, 2);
}

// LO QUE «MEDIR PIEZA» LE ENSEÑA AL OPERADOR.
//
// La cuenta de agujeros del informe sale de `contour.holes.size()` sin filtro
// ninguno. Antes de recomendar nada hay que saber si eso llega a la pantalla:
// una cosa es que una herramienta cuente mal por dentro y otra que el informe
// que el operador lee diga que una moneda tiene ciento diecisiete agujeros.
TEST(RegionOwnOtsu, WhatTheReportShowsTheOperator) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    struct Case { const char* file; int truth; };
    const Case cases[] = {{"moneda_5_yen_con_agujero.png", 1}};


    for (const auto& one : cases) {
        const cv::Mat gray = cv::imread((dir / one.file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) continue;
        // EL CAMINO REAL DEL BOTÓN, no uno parecido. La ventana no mide sobre
        // la máscara cruda: la pasa por `pieceMaskWithHoles`, porque la que
        // devuelve el análisis viene RELLENA y sin eso una arandela se mediría
        // como un disco. Medir sobre otra máscara habría dado un número que
        // nadie ve.
        const auto analysed = vision::analyzeFrame(gray, {});
        if (!analysed.isOk()) continue;
        const cv::Mat mask =
            vision::pieceMaskWithHoles(gray, analysed.value().mask, {});
        const auto report = inspection::measureWholePiece(
            gray, mask, analysed.value().fixture, 0.0, inspection::LengthUnit::Pixels);
        if (!report.ok) {
            std::printf("  [informe] %-30s no se pudo medir\n", one.file);
            continue;
        }
        for (const auto& row : report.rows) {
            if (row.tool == "Agujeros") {
                std::printf("  [informe] %-30s verdad %d -> el informe dice %.0f\n",
                            one.file, one.truth, row.value);
            }
        }
    }
}
