// TODO LO QUE UNA MISMA FIGURA PUEDE MEDIR.
//
// Petición de uso: «que hubiera como dos partes en lo de herramientas, una de
// la herramienta en general y otra de todas las secciones-medidas de esa
// herramienta».
//
// Cinco clases llevan selector de medida —Región entre seis, Ranura y Chaflán
// entre tres, Acuerdo y Extremos entre dos—. Al dibujarlas se escoge UNA y las
// otras quedaban invisibles, aunque salen del mismo trazo y del mismo fixture.
//
// LO QUE ARRIESGA ESTA FUNCIÓN, y por lo que existe esta prueba: la pestaña
// promete seis medidas DISTINTAS de una figura. Si `setMeasureChoice` no
// llegara a cambiar lo que `runTools` calcula, las seis filas enseñarían el
// mismo número y nadie se daría cuenta — las pruebas de la ventana usan valores
// puestos a mano y no pueden ver eso. Aquí se ejecutan de verdad.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"

using namespace pci;

namespace {

// Un cuadrado negro con una muesca: solidez y circularidad lejos de 1, para que
// las seis medidas de la región tengan valores que se puedan distinguir.
cv::Mat notchedSquare() {
    cv::Mat gray(320, 320, CV_8UC1, cv::Scalar(235));
    cv::rectangle(gray, cv::Rect(80, 80, 160, 160), cv::Scalar(30), cv::FILLED,
                  cv::LINE_8);
    cv::rectangle(gray, cv::Rect(150, 60, 40, 60), cv::Scalar(235), cv::FILLED,
                  cv::LINE_8);
    return gray;
}

}  // namespace

TEST(SiblingMeasures, TheSameFigureGivesADifferentNumberForEachOfItsMeasures) {
    const cv::Mat gray = notchedSquare();
    const vision::Fixture fixture{{160.0, 160.0}, 0.0};

    inspection::RegionGeometry region;
    region.center = {0.0F, 0.0F};
    region.width = 220.0F;
    region.height = 220.0F;
    region.measure = inspection::RegionMeasure::Area;
    const inspection::ToolGeometry geometry{region};

    const auto choices = inspection::measureChoicesOf(geometry);
    ASSERT_EQ(choices.options.size(), 6U) << "la región dejó de ofrecer sus seis medidas";

    std::vector<inspection::ToolConfig> siblings;
    for (const auto& option : choices.options) {
        inspection::ToolGeometry copy = geometry;
        ASSERT_TRUE(inspection::setMeasureChoice(copy, option.value))
            << "no se puede pedir «" << option.label << "», que la propia herramienta ofrece";
        inspection::ToolConfig config;
        config.type = inspection::ToolType::Region;
        config.name = std::string("zona · ") + option.label;
        config.geometryJson = inspection::toJson(copy);
        siblings.push_back(std::move(config));
    }

    const auto results = inspection::runTools(gray, fixture, siblings, 0.0,
                                              inspection::LengthUnit::Pixels);
    ASSERT_EQ(results.size(), 6U) << "no se midieron las seis";

    std::set<std::string> distinct;
    for (std::size_t i = 0; i < results.size(); ++i) {
        std::printf("  [hermanas] %-22s = %.3f\n", choices.options[i].label.c_str(),
                    results[i].measured);
        distinct.insert(std::to_string(results[i].measured));
    }

    // SEIS NÚMEROS, NO SEIS COPIAS. Si `setMeasureChoice` escribiera en una copia
    // que se descarta, o si el ejecutor ignorara el campo, todas saldrían igual y
    // la pestaña estaría enseñando el mismo valor con seis nombres.
    EXPECT_EQ(distinct.size(), 6U)
        << "las seis medidas de la misma figura dan " << distinct.size()
        << " valor(es) distinto(s): elegir la medida no está cambiando lo que se mide";
}

TEST(SiblingMeasures, AToolWithoutASelectorOffersNothingToOpenInto) {
    // Un calibre mide una distancia y nada más: la pestaña no debe abrirlo. Si
    // `measureChoicesOf` devolviera opciones para él, el árbol enseñaría un
    // desplegable que no lleva a ninguna parte.
    const inspection::ToolGeometry caliper{
        inspection::CaliperGeometry{{-40.0F, 0.0F}, {40.0F, 0.0F}, 12.0F}};
    EXPECT_TRUE(inspection::measureChoicesOf(caliper).options.empty());
}

TEST(SiblingMeasures, EveryToolThatOffersMeasuresCanActuallySwitchToAllOfThem) {
    // Las cinco clases con selector, de una vez: ofrecer una medida que luego no
    // se puede poner dejaría una fila en la pestaña que no se puede marcar.
    const std::vector<inspection::ToolGeometry> withSelector{
        inspection::ToolGeometry{inspection::RegionGeometry{}},
        inspection::ToolGeometry{inspection::GrooveGeometry{}},
        inspection::ToolGeometry{inspection::ChamferGeometry{}},
        inspection::ToolGeometry{inspection::FilletGeometry{}},
        inspection::ToolGeometry{inspection::ExtremesGeometry{}},
    };
    for (const auto& geometry : withSelector) {
        const auto choices = inspection::measureChoicesOf(geometry);
        ASSERT_GE(choices.options.size(), 2U) << "una clase con selector perdió sus opciones";
        for (const auto& option : choices.options) {
            inspection::ToolGeometry copy = geometry;
            EXPECT_TRUE(inspection::setMeasureChoice(copy, option.value))
                << "ofrece «" << option.label << "» y no la acepta";
            EXPECT_EQ(inspection::measureChoicesOf(copy).current, option.value)
                << "acepta «" << option.label << "» y se queda midiendo otra cosa";
        }
    }
}

// LO QUE CUESTA ABRIR LA HERRAMIENTA.
//
// Enseñar las seis medidas cuesta seis ejecuciones donde antes había una, y lo
// paga el botón de medir. El número no se supone: se mide, y se compara con la
// ejecución que ya se hacía de todas formas. Si un día abrir una región costara
// diez veces medirla, esto lo dice.
TEST(SiblingMeasures, OpeningAToolCostsWhatSixReadingsCost) {
    const cv::Mat gray = notchedSquare();
    const vision::Fixture fixture{{160.0, 160.0}, 0.0};

    inspection::RegionGeometry region;
    region.width = 220.0F;
    region.height = 220.0F;
    const inspection::ToolGeometry geometry{region};
    const auto choices = inspection::measureChoicesOf(geometry);

    auto configsFor = [&](std::size_t howMany) {
        std::vector<inspection::ToolConfig> out;
        for (std::size_t i = 0; i < howMany; ++i) {
            inspection::ToolGeometry copy = geometry;
            inspection::setMeasureChoice(copy, choices.options[i].value);
            inspection::ToolConfig config;
            config.type = inspection::ToolType::Region;
            config.name = "zona " + std::to_string(i);
            config.geometryJson = inspection::toJson(copy);
            out.push_back(std::move(config));
        }
        return out;
    };

    const auto one = configsFor(1);
    const auto six = configsFor(choices.options.size());
    constexpr int kRounds = 60;

    auto timeOf = [&](const std::vector<inspection::ToolConfig>& configs) -> double {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRounds; ++i) {
            const auto results = inspection::runTools(gray, fixture, configs, 0.0,
                                                      inspection::LengthUnit::Pixels);
            EXPECT_EQ(results.size(), configs.size());
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
                   .count() /
               kRounds;
    };

    const double justTheOne = timeOf(one);
    const double allSix = timeOf(six);
    const double ratio = allSix / std::max(justTheOne, 1e-6);
    std::printf("  [hermanas] 1 medida %.3f ms · las 6 %.3f ms · x%.2f\n", justTheOne,
                allSix, ratio);

    // SEIS LECTURAS NO PUEDEN COSTAR MENOS DE DOS: si costaran igual que una,
    // sería que cinco no se están ejecutando y las filas vienen de otro sitio.
    EXPECT_GT(ratio, 1.5) << "las seis medidas cuestan lo mismo que una: no se ejecutan";
    // Y NO PUEDEN COSTAR MÁS DE DOCE: son seis, comparten imagen y contorno.
    // Pasarse de ahí sería que cada una rehace el trabajo de las otras.
    EXPECT_LT(ratio, 12.0) << "abrir una región cuesta desproporcionadamente";
}
