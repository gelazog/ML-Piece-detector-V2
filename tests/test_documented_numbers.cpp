// Las cifras que el proyecto AFIRMA tienen que seguir siendo ciertas.
//
// Este fichero existe por una equivocación concreta y comprobada. ARQUITECTURA
// decía que acotar el suavizado del contorno subía la contradicción entre área
// y perímetro «de 1,75 % a 3,06 %». Era verdad cuando se escribió; después se
// añadió el rechazo de cruces ambiguos y la cifra real bajó a 2,00 %. El
// documento se quedó **exagerando el precio de una decisión** —y alguien que
// leyera eso podría revisitar la decisión por un motivo que ya no existía.
//
// El problema no es esa cifra: es que una cifra escrita en prosa no tiene quien
// la vigile. Un número medido y copiado a mano a un documento empieza a caducar
// el mismo día.
//
// Lo que se hace aquí es atar las afirmaciones NUMÉRICAS más importantes del
// proyecto a una comprobación. No todas —muchas describen estados anteriores a
// un arreglo y su valor es histórico— sino las que dicen **cómo se comporta el
// programa hoy**. Si una deja de ser cierta, este fichero falla y dice cuál.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "vision/pipeline.h"
#include "vision/quality_metrics.h"
#include "vision/subpixel_edge.h"

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

cv::Mat photo(const std::string& name) {
    const auto dir = corpus();
    if (dir.empty()) {
        return {};
    }
    return cv::imread((dir / name).string(), cv::IMREAD_COLOR);
}

}  // namespace

// ARQUITECTURA: «la contradiccion entre area y perimetro subio de 1,75 % a
// 3,06 %, y al rechazar despues los cruces ambiguos volvio a bajar hasta
// 2,00 % (desde el 6,75 % de partida)».
//
// Son tres cifras y las tres se comprueban aquí. La de partida y la de ahora
// salen del programa; las dos intermedias describen versiones anteriores del
// código y por eso NO se comprueban: lo que se exige es que la de ahora sea
// mejor que la de partida y siga en el entorno documentado.
TEST(DocumentedNumbers, TheAreaPerimeterDisagreementIsWhatTheDocsSay) {
    const cv::Mat ball = photo("bola_oscura_sobre_claro_10mm.jpg");
    if (ball.empty()) {
        GTEST_SKIP() << "corpus no descargado: python3 testdata/fetch_real_images.py";
    }

    const auto gap = [&ball](bool subpixel) {
        pci::vision::PipelineConfig config;
        config.roi = cv::Rect(560, 720, 420, 420);
        config.subpixelEdges = subpixel;
        const auto analysis = pci::vision::analyzeFrame(ball, config);
        EXPECT_TRUE(analysis.isOk());
        const double area = analysis.value().contour.area;
        const double perimeter = analysis.value().contour.perimeter;
        const double byArea = std::sqrt(area / CV_PI);
        const double byPerimeter = perimeter / (2.0 * CV_PI);
        return 100.0 * std::abs(byArea - byPerimeter) / byArea;
    };

    const double before = gap(false);
    const double after = gap(true);
    std::printf("  [documentado] desacuerdo area/perimetro: %.2f %% sin afinar, "
                "%.2f %% con afinado\n",
                before, after);

    // «desde el 6,75 % de partida»
    EXPECT_NEAR(before, 6.75, 0.15)
        << "la cifra de partida documentada (6,75 %) ya no es la que sale";
    // «volvio a bajar hasta 2,00 %»
    EXPECT_NEAR(after, 2.00, 0.25)
        << "la cifra documentada (2,00 %) ya no es la que sale. Si el cambio es una "
           "mejora, actualiza ARQUITECTURA; si es una regresion, aqui esta";
}

// ARQUITECTURA y `quality_metrics.h` traen una tabla entera de valores de
// dentado por fotografía. Es la que sostiene la eleccion del umbral de aviso, y
// si esos numeros se mueven, el umbral deja de estar donde se dijo que estaba.
TEST(DocumentedNumbers, TheRaggednessTableStillHolds) {
    if (corpus().empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }

    struct Expected {
        const char* file;
        double ragged;
    };
    // Las cifras exactas de la tabla documentada.
    const Expected table[] = {
        {"bola_oscura_sobre_claro_20mm.jpg", 1.59},
        {"bolas_tres_sobre_negro.jpg", 1.72},
        {"tuerca_dominio_publico.jpg", 2.42},
        {"pinon_corona_dentada.jpg", 5.88},
        {"arandelas_con_agujero.jpg", 6.26},
        {"moneda_5_yen_con_agujero.png", 10.11},
        {"bola_oscura_sobre_claro_10mm.jpg", 21.96},
    };

    int checked = 0;
    for (const auto& row : table) {
        const cv::Mat image = photo(row.file);
        if (image.empty()) {
            continue;
        }
        const auto analysis = pci::vision::analyzeFrame(image, {});
        if (!analysis.isOk()) {
            continue;
        }
        const double ragged = pci::vision::contourRaggedness(
            analysis.value().contour.area, analysis.value().contour.perimeter);
        std::printf("  [documentado] %-34s documentado %5.2f, hoy %5.2f\n", row.file,
                    row.ragged, ragged);
        EXPECT_NEAR(ragged, row.ragged, 0.05)
            << row.file << ": la tabla de ARQUITECTURA dice " << row.ragged
            << " y hoy sale " << ragged;
        ++checked;
    }
    EXPECT_GT(checked, 4) << "no se pudo comprobar casi ninguna fila de la tabla";

    // Y la separacion que justifica el umbral: el contorno legitimo mas dentado
    // (la tuerca, 2,42) por debajo, y el caso sospechoso mas suave (el pinon,
    // 5,88) por encima. Si esa banda se cierra, el umbral deja de tener sentido.
    EXPECT_LT(2.42, pci::vision::kRaggedContourWarning);
    EXPECT_GT(5.88, pci::vision::kRaggedContourWarning);
}

// ARQUITECTURA: «radio verdadero 60,50 px; umbral duro 60,083 (error 0,417 px);
// subpixel 60,475 (error 0,025 px). Diecisiete veces mas exacto».
//
// Es la cifra que justifica que el afinado exista, asi que merece su vigilante.
TEST(DocumentedNumbers, TheSubpixelAccuracyClaimStillHolds) {
    const double trueRadius = 60.5;
    cv::Mat image(200, 200, CV_8UC1, cv::Scalar(30));
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            const double d = std::hypot(x - 100.0, y - 100.0);
            const double t = std::clamp((trueRadius + 1.0 - d) / 2.0, 0.0, 1.0);
            image.at<unsigned char>(y, x) = static_cast<unsigned char>(30 + 190 * t);
        }
    }
    cv::Mat mask;
    cv::threshold(image, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    ASSERT_FALSE(contours.empty());

    const auto meanRadius = [](const std::vector<cv::Point2f>& points) {
        double sum = 0.0;
        for (const auto& p : points) {
            sum += std::hypot(static_cast<double>(p.x) - 100.0,
                              static_cast<double>(p.y) - 100.0);
        }
        return sum / static_cast<double>(points.size());
    };

    std::vector<cv::Point2f> raw;
    for (const auto& p : contours.front()) {
        raw.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    const auto refined = pci::vision::refineContourSubpixel(image, contours.front());

    const double errorThreshold = std::abs(meanRadius(raw) - trueRadius);
    const double errorSubpixel = std::abs(meanRadius(refined.points) - trueRadius);
    std::printf("  [documentado] error del umbral %.3f px, del subpixel %.3f px "
                "(%.0f veces mejor)\n",
                errorThreshold, errorSubpixel,
                errorSubpixel > 0.0 ? errorThreshold / errorSubpixel : 0.0);

    EXPECT_NEAR(errorThreshold, 0.417, 0.02) << "cambio el error documentado del umbral";
    EXPECT_NEAR(errorSubpixel, 0.025, 0.02) << "cambio el error documentado del subpixel";
    EXPECT_GT(errorThreshold / std::max(errorSubpixel, 1e-9), 10.0)
        << "el afinado ya no es un orden de magnitud mejor: la razon por la que "
           "existe deja de sostenerse";
}
