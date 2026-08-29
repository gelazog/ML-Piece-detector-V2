// «VARIAS HERRAMIENTAS NO MUESTRAN MEDIDAS.»
//
// Petición de uso, literal. Y es de las quejas difíciles de perseguir porque
// «no muestra medida» tiene dos causas que se ven igual en pantalla:
//
//   - la herramienta CORRIÓ y no encontró lo que buscaba —el trazo cae donde no
//     hay borde, la pieza no tiene ese rasgo— y entonces lo correcto es no dar
//     número, pero hay que DECIRLO;
//   - o la herramienta no sabe medir eso y devuelve un cero silencioso, que es
//     un fallo del programa disfrazado de resultado.
//
// El proyecto ya tiene el barrido gemelo para el dibujo —«ninguna herramienta
// muda» (R3), que nació porque Eje, Rosca y Engranaje llevaban desde su entrega
// sin rama en `paintTool` y eran invisibles hasta que medían—. Éste es el mismo
// barrido para la MEDIDA: se recorren las 32 herramientas, cada una sobre una
// escena donde su rasgo existe, y ninguna puede quedarse callada.
//
// La regla que se comprueba no es «todas tienen que medir»: hay herramientas
// que legítimamente no pueden sobre una pieza cualquiera —las construcciones
// geométricas no miden nada, y las de GD&T necesitan un datum declarado—. La
// regla es **que ninguna se calle**: o da un número, o dice por qué no.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "sample_geometries.h"

using namespace pci;
using namespace pci::inspection;

namespace {

constexpr int kScene = 420;

// Una pieza con de todo: caras rectas, una esquina viva, un borde curvo y un
// agujero. La idea es que ninguna herramienta pueda excusarse en que la escena
// no tenía nada que medir.
cv::Mat pieceWithEverything() {
    cv::Mat frame(kScene, kScene, CV_8UC1, cv::Scalar(30));
    cv::rectangle(frame, cv::Rect(90, 90, 240, 240), cv::Scalar(225), cv::FILLED);
    cv::circle(frame, {330, 330}, 90, cv::Scalar(225), cv::FILLED);  // borde curvo
    cv::circle(frame, {180, 180}, 34, cv::Scalar(30), cv::FILLED);   // agujero
    return frame;
}

}  // namespace

TEST(NoSilentTool, EveryToolEitherMeasuresOrSaysWhyNot) {
    const cv::Mat frame = pieceWithEverything();
    // El fixture en el centro de la pieza: las geometrías de ejemplo están
    // escritas alrededor del origen, que es como se guardan de verdad —en
    // coordenadas de PIEZA— y así caen sobre ella.
    const vision::Fixture fixture{{210.0F, 210.0F}, 0.0};

    int measured = 0;
    int explained = 0;
    std::vector<std::string> silent;
    for (const ToolType type : allToolTypes()) {
        ToolConfig config;
        config.id = 1;
        config.type = type;
        config.name = toolTypeName(type);
        config.geometryJson = toJson(testing_support::sampleGeometry(type));

        const auto results = runTools(frame, fixture, {config}, 0.0, LengthUnit::Pixels);
        ASSERT_EQ(results.size(), 1U) << toolTypeLabel(type);
        const ToolRunResult& result = results.front();

        const bool gaveANumber = result.measured != 0.0;
        const bool saidSomething = !result.detail.empty();
        std::printf("  %-18s %s  medida=%-10.3f %s\n", toolTypeLabel(type),
                    result.ok ? "ok " : "NO ", result.measured,
                    result.detail.empty() ? "(sin explicación)" : result.detail.c_str());
        if (gaveANumber) {
            ++measured;
        }
        if (saidSomething) {
            ++explained;
        }
        if (!gaveANumber && !saidSomething) {
            silent.push_back(toolTypeLabel(type));
        }
    }

    std::printf("  [mudas] %d de %d dan número, %d explican algo, %d calladas\n", measured,
                static_cast<int>(allToolTypes().size()), explained,
                static_cast<int>(silent.size()));

    // LA REGLA. Una herramienta que no mide y tampoco dice por qué deja al
    // operador mirando un hueco: no sabe si dibujó mal el trazo, si la pieza no
    // tiene ese rasgo, o si el programa no sabe medirlo.
    std::string names;
    for (const auto& name : silent) {
        names += (names.empty() ? "" : ", ") + name;
    }
    EXPECT_TRUE(silent.empty())
        << "estas herramientas ni miden ni explican por qué: " << names
        << ". En pantalla eso es un hueco, y el operador no puede saber si el trazo estaba "
           "mal puesto o si el programa no sabe medir eso";
}
