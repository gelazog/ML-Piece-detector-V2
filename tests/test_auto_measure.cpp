// Pruebas del generador de propuestas. Lo que se comprueba no es que salga
// "algo", sino que salga lo que un operador habría dibujado a mano y con las
// medidas correctas: si las propuestas no son las buenas, revisarlas cuesta más
// que dibujar las herramientas desde cero.
#include <gtest/gtest.h>
#include <cstdio>
#include <map>
#include <set>

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

// Una pieza que da COTAS DE VARIAS CLASES a la vez: rectángulo con esquinas
// redondeadas (arcos), caras enfrentadas (calibres) y dos taladros (círculos).
// Hace falta así para que filtrar por clase signifique algo: con una sola clase
// el filtro no se podría distinguir de no filtrar.
Scene richScene() {
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
    cv::circle(mask, {190, 240}, 34, cv::Scalar(0), cv::FILLED);
    cv::circle(mask, {320, 240}, 28, cv::Scalar(0), cv::FILLED);
    return sceneFrom(mask);
}

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

// ELEGIR QUÉ CLASES DE COTA PROPONE LA MEDICIÓN AUTOMÁTICA.
//
// Petición de uso: «que el usuario elija qué herramientas se van a usar y
// cuáles no para la medición automática». Tiene sentido — el proponedor ofrece
// hasta doce cotas de siete clases, y quien solo inspecciona diámetros acaba
// desmarcando nueve propuestas cada vez. Decirlo una vez por adelantado es
// menos trabajo y menos ocasiones de dejar marcada una que no se quería.
//
// Lo que decide si esto sirve de algo es DÓNDE se aplica el filtro. Va ANTES
// del recorte por el tope: al revés, los doce huecos se gastarían en cotas que
// el operador no quiere y luego se filtrarían, y le llegarían tres diámetros de
// los doce que había.
TEST(AutoMeasure, TheOperatorCanChooseWhichKindsOfMeasurementAreProposed) {
    const Scene scene = richScene();
    const auto& gray = scene.gray;
    const auto& mask = scene.mask;
    const pci::vision::Fixture fixture = identity();

    const auto everything = proposeTools(gray, mask, fixture, {});
    ASSERT_FALSE(everything.empty()) << "la escena de prueba no propone nada";

    // Cuántas clases distintas salen sin filtro. Si fuera una sola, filtrar no
    // demostraría nada.
    std::set<ToolType> kinds;
    for (const auto& proposal : everything) {
        kinds.insert(proposal.config.type);
    }
    std::printf("  [proponer] sin filtro: %zu cotas de %zu clases\n", everything.size(),
                kinds.size());
    ASSERT_GE(kinds.size(), 2U) << "la escena solo da una clase: el filtro no se probaría";

    // Se pide UNA sola clase, la primera que salió.
    const ToolType wanted = *kinds.begin();
    ProposeOptions onlyOne;
    onlyOne.allowedTypes = {wanted};
    const auto filtered = proposeTools(gray, mask, fixture, onlyOne);

    std::printf("  [proponer] pidiendo solo «%s»: %zu cotas\n",
                pci::inspection::toolTypeName(wanted), filtered.size());
    ASSERT_FALSE(filtered.empty()) << "pedir una clase concreta no propone nada de ella";
    for (const auto& proposal : filtered) {
        EXPECT_EQ(proposal.config.type, wanted)
            << "se cuela una cota de otra clase: " << proposal.config.name;
    }
}

TEST(AutoMeasure, AnEmptyFilterMeansEverythingAndNotNothing) {
    // `allowedTypes` vacío tiene que significar TODAS. Si significara «ninguna»,
    // cualquier llamante que no sepa de esta opción dejaría de proponer y nadie
    // sabría por qué — un modo silencioso en el que la función no hace nada.
    const Scene scene = richScene();
    const auto& gray = scene.gray;
    const auto& mask = scene.mask;
    const pci::vision::Fixture fixture = identity();

    ProposeOptions empty;
    ASSERT_TRUE(empty.allowedTypes.empty());
    const auto withEmpty = proposeTools(gray, mask, fixture, empty);
    const auto withDefault = proposeTools(gray, mask, fixture, {});
    EXPECT_EQ(withEmpty.size(), withDefault.size())
        << "una lista de clases vacía cambia el resultado: se está leyendo como "
           "«ninguna» en vez de «todas»";

    // Y `allows` lo dice igual para cualquier clase.
    for (const auto type : pci::inspection::proposableTypes()) {
        EXPECT_TRUE(empty.allows(type)) << "con la lista vacía rechaza una clase";
    }
}

TEST(AutoMeasure, TheCapIsSpentOnWhatWasAskedFor) {
    // LA RAZÓN DE QUE EL FILTRO VAYA ANTES DEL TOPE, con el caso más claro que
    // da esta escena.
    //
    // Con el tope en 8 y sin filtro salen 6 reglas y 2 círculos, y los arcos
    // **desaparecen del todo**: cero de los cuatro que hay. Un operador que
    // quisiera medir los redondeos de las esquinas no vería ni uno, y no
    // tendría forma de saber que existían.
    //
    // Pidiendo arcos, los cuatro. Eso es lo que compra poner el filtro antes
    // del recorte — si fuera al revés, filtrar sobre una lista que ya perdió
    // los arcos seguiría dando cero.
    //
    // El tope se baja a propósito: con el de fábrica esta escena da 12 cotas
    // justas, el recorte no muerde, y la prueba pasaría sin demostrar nada.
    // Así estaba escrita la primera vez.
    const Scene scene = richScene();
    const pci::vision::Fixture fixture = identity();

    ProposeOptions tight;
    tight.maxProposals = 8;
    int dropped = 0;
    const auto mixed = proposeTools(scene.gray, scene.mask, fixture, tight, 0.0, &dropped);
    ASSERT_GT(dropped, 0) << "el tope no está mordiendo: la prueba no diría nada";

    std::map<ToolType, int> mixedCount;
    for (const auto& proposal : mixed) {
        ++mixedCount[proposal.config.type];
    }
    std::printf("  [proponer] tope 8 sin filtro:");
    for (const auto& [type, count] : mixedCount) {
        std::printf(" %s=%d", pci::inspection::toolTypeName(type), count);
    }
    std::printf(" (%d fuera)\n", dropped);

    // Cuántos arcos hay de verdad en la pieza, sin tope que estorbe.
    ProposeOptions roomy;
    roomy.maxProposals = 100;
    const auto everything = proposeTools(scene.gray, scene.mask, fixture, roomy);
    int arcsInThePiece = 0;
    for (const auto& proposal : everything) {
        if (proposal.config.type == ToolType::Arc) {
            ++arcsInThePiece;
        }
    }
    ASSERT_GT(arcsInThePiece, 0) << "la escena ya no tiene esquinas redondeadas";

    // Compitiendo por el tope, los arcos se quedan sin sitio.
    const int arcsWhenSharing = mixedCount.count(ToolType::Arc) > 0
                                    ? mixedCount.at(ToolType::Arc)
                                    : 0;
    // Y pidiéndolos solos, salen todos.
    ProposeOptions onlyArcs = tight;
    onlyArcs.allowedTypes = {ToolType::Arc};
    const auto alone = proposeTools(scene.gray, scene.mask, fixture, onlyArcs);
    std::printf("  [proponer] arcos: %d en la pieza | %d compartiendo el tope | "
                "%zu pidiéndolos solos\n",
                arcsInThePiece, arcsWhenSharing, alone.size());

    EXPECT_GT(alone.size(), static_cast<std::size_t>(arcsWhenSharing))
        << "pedir sólo arcos no aprovecha el tope: se está filtrando DESPUÉS de "
           "recortar, y el operador recibe las migajas de lo que pidió";
    EXPECT_EQ(alone.size(), static_cast<std::size_t>(arcsInThePiece))
        << "pidiendo sólo arcos y con sitio de sobra, no salen todos los que hay";
}
