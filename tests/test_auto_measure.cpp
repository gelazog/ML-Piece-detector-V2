// Pruebas del generador de propuestas. Lo que se comprueba no es que salga
// "algo", sino que salga lo que un operador habría dibujado a mano y con las
// medidas correctas: si las propuestas no son las buenas, revisarlas cuesta más
// que dibujar las herramientas desde cero.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "inspection_editor/auto_measure.h"

using pci::inspection::AutoProposal;
using pci::inspection::proposeTools;
using pci::inspection::ProposeOptions;
using pci::inspection::ToolType;

namespace {

struct Scene {
    cv::Mat gray;
    cv::Mat mask;
};

// Pieza clara sobre fondo oscuro, con su máscara. Es el caso de contraluz que
// estas medidas piden.
Scene sceneFrom(const cv::Mat& mask) {
    Scene scene;
    scene.mask = mask;
    scene.gray = cv::Mat(mask.size(), CV_8UC1, cv::Scalar(30));
    scene.gray.setTo(cv::Scalar(220), mask);
    return scene;
}

cv::Mat blank(int size = 500) { return cv::Mat(size, size, CV_8UC1, cv::Scalar(0)); }

int countType(const std::vector<AutoProposal>& proposals, ToolType type) {
    return static_cast<int>(std::count_if(
        proposals.begin(), proposals.end(),
        [type](const AutoProposal& p) { return p.config.type == type; }));
}

const AutoProposal* findNamed(const std::vector<AutoProposal>& proposals,
                              const std::string& needle) {
    for (const auto& p : proposals) {
        if (p.config.name.find(needle) != std::string::npos) {
            return &p;
        }
    }
    return nullptr;
}

pci::vision::Fixture identity() { return {}; }

}  // namespace

TEST(AutoMeasure, ProposesTheOverallDimensionsFirst) {
    // Largo y ancho son las dos primeras medidas que toma cualquiera con un pie
    // de rey, así que se proponen siempre.
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 140, 280, 180), cv::Scalar(255), cv::FILLED);
    const Scene scene = sceneFrom(mask);

    const auto proposals = proposeTools(scene.gray, scene.mask, identity());
    ASSERT_FALSE(proposals.empty());
    for (const auto& p : proposals) {
        std::printf("  %-16s %8.2f  %s\n", p.config.name.c_str(), p.measured,
                    p.reason.c_str());
    }

    const AutoProposal* largo = findNamed(proposals, "Largo");
    const AutoProposal* ancho = findNamed(proposals, "Ancho");
    ASSERT_NE(largo, nullptr);
    ASSERT_NE(ancho, nullptr);
    EXPECT_NEAR(largo->measured, 280.0, 4.0);
    EXPECT_NEAR(ancho->measured, 180.0, 4.0);
}

TEST(AutoMeasure, ProposesACircleForEachHole) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(80, 80, 340, 340), cv::Scalar(255), cv::FILLED);
    cv::circle(mask, {170, 170}, 35, cv::Scalar(0), cv::FILLED);
    cv::circle(mask, {330, 170}, 50, cv::Scalar(0), cv::FILLED);
    const Scene scene = sceneFrom(mask);

    const auto proposals = proposeTools(scene.gray, scene.mask, identity());
    EXPECT_EQ(countType(proposals, ToolType::Circle), 2)
        << "un círculo por agujero, ni más ni menos";

    // Y sus diámetros son los dibujados.
    std::vector<double> diameters;
    for (const auto& p : proposals) {
        if (p.config.type == ToolType::Circle) {
            diameters.push_back(p.measured);
        }
    }
    std::sort(diameters.begin(), diameters.end());
    ASSERT_EQ(diameters.size(), 2U);
    std::printf("  agujeros: Ø%.1f y Ø%.1f (dibujados 70 y 100)\n", diameters[0],
                diameters[1]);
    EXPECT_NEAR(diameters[0], 70.0, 4.0);
    EXPECT_NEAR(diameters[1], 100.0, 4.0);
}

TEST(AutoMeasure, ProposesAnArcForEachRoundedCorner) {
    constexpr int kRadius = 45;
    cv::Mat mask = blank();
    const cv::Rect box(100, 110, 300, 260);
    cv::rectangle(mask, cv::Rect(box.x + kRadius, box.y, box.width - 2 * kRadius, box.height),
                  cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, cv::Rect(box.x, box.y + kRadius, box.width, box.height - 2 * kRadius),
                  cv::Scalar(255), cv::FILLED);
    for (const auto& c :
         {cv::Point(box.x + kRadius, box.y + kRadius),
          cv::Point(box.x + box.width - kRadius, box.y + kRadius),
          cv::Point(box.x + kRadius, box.y + box.height - kRadius),
          cv::Point(box.x + box.width - kRadius, box.y + box.height - kRadius)}) {
        cv::circle(mask, c, kRadius, cv::Scalar(255), cv::FILLED);
    }
    const Scene scene = sceneFrom(mask);

    const auto proposals = proposeTools(scene.gray, scene.mask, identity());
    const int arcs = countType(proposals, ToolType::Arc);
    std::printf("  esquinas redondeadas propuestas: %d\n", arcs);
    EXPECT_GE(arcs, 3) << "las cuatro esquinas son el rasgo más evidente de la pieza";

    for (const auto& p : proposals) {
        if (p.config.type == ToolType::Arc) {
            EXPECT_NEAR(p.measured, kRadius, kRadius * 0.2) << p.detail;
        }
    }
}

TEST(AutoMeasure, EveryProposalCarriesAReasonAndSuggestedTolerances) {
    // Sin el porqué, doce propuestas no se revisan: se aceptan todas o se
    // descartan todas. Y sin tolerancia, la herramienta insertada no da
    // veredicto, que es la mitad de su valor.
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 140, 280, 180), cv::Scalar(255), cv::FILLED);
    cv::circle(mask, {200, 230}, 40, cv::Scalar(0), cv::FILLED);
    const Scene scene = sceneFrom(mask);

    const auto proposals = proposeTools(scene.gray, scene.mask, identity());
    ASSERT_FALSE(proposals.empty());
    for (const auto& p : proposals) {
        EXPECT_FALSE(p.reason.empty()) << p.config.name;
        EXPECT_FALSE(p.config.name.empty());
        EXPECT_FALSE(p.detail.empty()) << p.config.name;
        EXPECT_FALSE(p.config.geometryJson.empty()) << p.config.name;
        EXPECT_LE(p.config.toleranceMin, p.measured + 1e-6) << p.config.name;
        EXPECT_GE(p.config.toleranceMax, p.measured - 1e-6) << p.config.name;
        EXPECT_LT(p.config.toleranceMax, 1e9) << "la tolerancia debe quedar sugerida";
    }
}

TEST(AutoMeasure, TheProposalsAreLimitedInNumber) {
    // Cincuenta propuestas son tan inútiles como ninguna: no se revisan.
    cv::Mat mask = blank(700);
    cv::rectangle(mask, cv::Rect(60, 60, 580, 580), cv::Scalar(255), cv::FILLED);
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            cv::circle(mask, {120 + i * 110, 120 + j * 110}, 30, cv::Scalar(0), cv::FILLED);
        }
    }
    const Scene scene = sceneFrom(mask);

    ProposeOptions options;
    options.maxProposals = 8;
    const auto proposals = proposeTools(scene.gray, scene.mask, identity(), options);
    std::printf("  pieza con 25 agujeros -> %zu propuestas (tope 8)\n", proposals.size());
    EXPECT_LE(proposals.size(), 8U);
    EXPECT_FALSE(proposals.empty());
}

TEST(AutoMeasure, TinyFeaturesAreNotProposed) {
    // Un rasgo de 3 px no merece una herramienta.
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 140, 280, 180), cv::Scalar(255), cv::FILLED);
    cv::circle(mask, {200, 230}, 3, cv::Scalar(0), cv::FILLED);  // pinchazo
    const Scene scene = sceneFrom(mask);

    const auto proposals = proposeTools(scene.gray, scene.mask, identity());
    EXPECT_EQ(countType(proposals, ToolType::Circle), 0)
        << "un agujero de 6 px de diámetro no es un rasgo a medir";
}

TEST(AutoMeasure, TheGeometryTravelsWithThePiece) {
    // Las propuestas se guardan en coordenadas de PIEZA, como todas las
    // herramientas: si la pieza llega girada, deben medir lo mismo.
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 140, 280, 180), cv::Scalar(255), cv::FILLED);
    const Scene scene = sceneFrom(mask);

    pci::vision::Fixture rotated;
    rotated.origin = {240.0F, 230.0F};
    rotated.angleDeg = 35.0;
    const auto plain = proposeTools(scene.gray, scene.mask, identity());
    const auto turned = proposeTools(scene.gray, scene.mask, rotated);

    const AutoProposal* a = findNamed(plain, "Largo");
    const AutoProposal* b = findNamed(turned, "Largo");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    // La geometría guardada es distinta (coordenadas de pieza distintas) pero
    // la medida sobre la misma imagen tiene que coincidir.
    EXPECT_NEAR(a->measured, b->measured, 1.0);
    EXPECT_NE(a->config.geometryJson, b->config.geometryJson);
}

TEST(AutoMeasure, RefusesWhatItCannotLookAt) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 140, 280, 180), cv::Scalar(255), cv::FILLED);
    const Scene scene = sceneFrom(mask);
    EXPECT_TRUE(proposeTools(cv::Mat(), scene.mask, identity()).empty());
    EXPECT_TRUE(proposeTools(scene.gray, cv::Mat(), identity()).empty());
    // Máscara vacía: no hay pieza que medir.
    EXPECT_TRUE(proposeTools(scene.gray, blank(), identity()).empty());
}
