// EL ÁREA Y EL PERÍMETRO SON LAS DOS ÚNICAS COTAS QUE MIRAN LA PIEZA ENTERA.
//
// Todo lo que el proponedor ofrecía medía un rasgo: este diámetro, aquel lado,
// esta esquina. Una pieza puede pasar las doce cotas y estar mellada justo donde
// ninguna caía. Y en dos piezas del banco —`rosca-1` pieza 1 y `tornillo-ojo-4`
// pieza 2— la lista entera llevaba el descargo de «vale como cota de
// referencia, no como comprobación»: el operador aceptaba cinco propuestas y se
// quedaba con una plantilla incapaz de rechazar nada.
//
// Esta prueba existe para sostener las TRES afirmaciones que el motivo de cada
// propuesta le hace al operador, porque un motivo que afirma algo falso es peor
// que no dar motivo:
//
//   1. «vigila la pieza ENTERA» — una mella lejos de toda cota mueve el área
//      mientras el largo y el ancho de la envolvente no se enteran.
//   2. «no se estropean igual» — un borde dentado alarga el perímetro mucho más
//      de lo que toca el área, así que las dos juntas cogen lo que cada una
//      sola se deja.
//   3. Que sean COMPROBACIÓN y no referencia: la Región vuelve a umbralizar y a
//      recorrer el contorno en cada inspección, así que no lleva el descargo.
//
// Las tres se comprueban con números, no se dan por buenas.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;

namespace {

// Una pieza clara sobre fondo oscuro, para que la mella no se confunda con el
// fondo por casualidad.
cv::Mat plate() {
    cv::Mat frame(400, 500, CV_8UC1, cv::Scalar(20));
    cv::rectangle(frame, cv::Rect(150, 120, 200, 160), cv::Scalar(230), cv::FILLED);
    return frame;
}

struct Dimensions {
    double area = 0.0;
    double perimeter = 0.0;
    double longSide = 0.0;
    bool areaCarriesTheDisclaimer = false;
};

// Mide por el camino de verdad: se piden las propuestas y se leen las que
// interesan. Si el proponedor dejara de ofrecerlas, esto se entera.
Dimensions measure(const cv::Mat& frame) {
    vision::PipelineConfig config;
    auto all = vision::analyzeFrames(frame, config);
    EXPECT_TRUE(all.isOk());
    if (!all.isOk() || all.value().empty()) {
        return {};
    }
    const auto& piece = all.value()[vision::largestPieceIndex(all.value())];
    const cv::Mat mask = vision::pieceMaskWithHoles(frame, piece.mask, config.segmentation);
    Dimensions out;
    for (const auto& proposal :
         inspection::proposeTools(frame, mask, piece.fixture, {}, 0.0, nullptr)) {
        if (proposal.config.name == "Área") {
            out.area = proposal.measured;
            out.areaCarriesTheDisclaimer =
                proposal.reason.find("no como comprobación") != std::string::npos;
        } else if (proposal.config.name == "Perímetro") {
            out.perimeter = proposal.measured;
        } else if (proposal.config.name == "Largo total") {
            out.longSide = proposal.measured;
        }
    }
    return out;
}

}  // namespace

TEST(WholePieceDimensions, TheyAreOfferedAndTheyAreChecks) {
    const Dimensions d = measure(plate());
    std::printf("  [silueta] placa 200x160: área %.0f px², perímetro %.0f px, largo %.0f px\n",
                d.area, d.perimeter, d.longSide);
    ASSERT_GT(d.area, 0.0) << "no se propone el área de la silueta";
    ASSERT_GT(d.perimeter, 0.0) << "no se propone el perímetro de la silueta";
    // Que midan lo que dicen medir, y no un número cualquiera: 200x160 son
    // 32 000 px² y 720 px de contorno, con holgura por el umbral.
    EXPECT_NEAR(d.area, 32000.0, 3000.0);
    EXPECT_NEAR(d.perimeter, 720.0, 60.0);
    EXPECT_FALSE(d.areaCarriesTheDisclaimer)
        << "el área sale marcada como cota de referencia. Si eso es cierto, esta "
           "propuesta no arregla nada — era justo el hueco que venía a tapar";
}

TEST(WholePieceDimensions, AChipMovesTheAreaAndNotTheEnvelope) {
    // La afirmación 1, y la razón de ser de la cota. La mella se hace en mitad
    // de un lado, sin tocar las esquinas: así el rectángulo mínimo no cambia.
    const Dimensions sound = measure(plate());
    cv::Mat chipped = plate();
    cv::rectangle(chipped, cv::Rect(220, 262, 60, 20), cv::Scalar(20), cv::FILLED);
    const Dimensions defective = measure(chipped);

    const double areaShift = 100.0 * std::abs(defective.area - sound.area) / sound.area;
    const double sideShift =
        100.0 * std::abs(defective.longSide - sound.longSide) / sound.longSide;
    std::printf("  [silueta] mella de 60x20: el área se mueve %.1f %%, el largo %.1f %%\n",
                areaShift, sideShift);

    EXPECT_LT(sideShift, 1.0)
        << "la envolvente SÍ se entera de la mella: entonces esta escena no reproduce "
           "el problema y la prueba no está comprobando lo que cree";
    EXPECT_GT(areaShift, 2.0)
        << "el área no se entera de una mella de 1200 px²: no vigila la pieza entera, "
           "que es lo único que se le pide";
}

TEST(WholePieceDimensions, ASerratedEdgeMovesThePerimeterFarMoreThanTheArea) {
    // La afirmación 2, que es la que justifica proponer LAS DOS y no solo el
    // área. Un borde dentado quita y pone material a partes iguales —el área
    // apenas se mueve— pero alarga el recorrido del contorno.
    const Dimensions smooth = measure(plate());
    cv::Mat serrated = plate();
    for (int x = 155; x < 345; x += 10) {
        cv::rectangle(serrated, cv::Rect(x, 272, 5, 8), cv::Scalar(20), cv::FILLED);
        cv::rectangle(serrated, cv::Rect(x + 5, 112, 5, 8), cv::Scalar(230), cv::FILLED);
    }
    const Dimensions rough = measure(serrated);

    const double areaShift = 100.0 * std::abs(rough.area - smooth.area) / smooth.area;
    const double perimeterShift =
        100.0 * std::abs(rough.perimeter - smooth.perimeter) / smooth.perimeter;
    std::printf("  [silueta] borde dentado: el perímetro se mueve %.1f %%, el área %.1f %%\n",
                perimeterShift, areaShift);

    EXPECT_GT(perimeterShift, 5.0)
        << "el perímetro no se entera de un borde dentado, que es el defecto para el "
           "que se propone";
    EXPECT_GT(perimeterShift, areaShift * 2.0)
        << "el perímetro NO se estropea distinto que el área en este defecto. Entonces "
           "proponer las dos es proponer la misma cota dos veces, y el motivo que lee "
           "el operador afirma algo que no pasa: hay que reescribirlo o quitar una";
}

TEST(WholePieceDimensions, TheCutDoesNotEmptyOneFamilyToFillAnother) {
    // LA CONSECUENCIA DE AÑADIR DOS COTAS: el tope es de doce, y dos más
    // significan dos fuera. Quién cae lo decidía un orden por tamaño dentro de
    // cada clase de medida, y ese orden tenía una trampa.
    //
    // Una placa con dos agujeros da ocho longitudes:
    //
    //     Perímetro 1328 · Largo 339 · Lado 337 · Lado 337 · Lado 336 ·
    //     Lado 336 · Ø agujero 100 · Ø agujero 70
    //
    // Por tamaño, el recorte se llevaba LOS DOS AGUJEROS: cuatro lados que
    // repiten el mismo número echaban fuera las dos únicas cotas del interior.
    // Y poniendo delante lo que comprueba pasaba lo contrario — desaparecía el
    // lado más largo, la primera medida que cualquiera busca.
    //
    // Esta prueba fija las dos mitades a la vez, que es lo único que impide
    // arreglar una rompiendo la otra: al cortar tienen que sobrevivir el
    // agujero y el lado.
    cv::Mat mask(500, 500, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(80, 80, 340, 340), cv::Scalar(255), cv::FILLED);
    cv::circle(mask, {170, 170}, 35, cv::Scalar(0), cv::FILLED);
    cv::circle(mask, {330, 170}, 50, cv::Scalar(0), cv::FILLED);
    cv::Mat gray(mask.size(), CV_8UC1, cv::Scalar(30));
    gray.setTo(cv::Scalar(220), mask);

    int dropped = 0;
    const auto kept =
        inspection::proposeTools(gray, mask, {}, {}, 0.0, &dropped);
    ASSERT_GT(dropped, 0) << "no se recorta nada: esta prueba no comprueba un recorte";

    int holes = 0;
    double longestSide = 0.0;
    bool hasArea = false;
    for (const auto& proposal : kept) {
        if (proposal.config.name.rfind("Ø agujero", 0) == 0) {
            ++holes;
        }
        if (proposal.config.name.rfind("Lado", 0) == 0) {
            longestSide = std::max(longestSide, proposal.measured);
        }
        if (proposal.config.name == "Área") {
            hasArea = true;
        }
    }
    std::printf("  [recorte] %d propuestas, %d fuera: %d agujeros, lado mayor %.0f px\n",
                static_cast<int>(kept.size()), dropped, holes, longestSide);

    EXPECT_EQ(holes, 2)
        << "el recorte se lleva los agujeros: son las únicas cotas del interior de la "
           "pieza y las echan fuera cuatro lados que repiten el mismo número";
    EXPECT_GT(longestSide, 300.0)
        << "el recorte se lleva los lados: la primera medida que busca cualquiera "
           "desaparece por dar paso a los agujeros. Las dos tienen que caber";
    EXPECT_TRUE(hasArea)
        << "el área se cae del recorte, y era la cota que venía a tapar el hueco de "
           "las piezas sin nada comprobable";
}
