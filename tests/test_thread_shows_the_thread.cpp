// «LA ROSCA TOMA EL CENTRO Y DOS LÍNEAS PARALELAS, PERO NO SIGUE LA ROSCA.»
//
// Queja de uso, y en pantalla era cierta. Lo único que se dibujaba de una Rosca
// era su EJE y las dos rayas del alcance de búsqueda — o sea **dónde mira**, no
// lo que encuentra. El operador veía tres rectas sobre un tornillo y un número
// de paso, sin nada que relacionara una cosa con la otra.
//
// Cómo funciona de verdad: la herramienta no persigue el filete con un trazo.
// Lanza N escaneos PERPENDICULARES al eje —240 de fábrica—, y en cada uno anota
// a qué distancia del eje está el borde. Esa lista de distancias sube y baja con
// las crestas y los valles, y el paso sale de su PERIODO, medido a los dos lados
// por separado. Por eso el eje se traza por el centro de la caña y la banda se
// pone lo justo para alcanzar el borde: son los dos parámetros del escaneo.
//
// Así que la herramienta sí sigue la rosca — pero no lo enseñaba. Su hermana la
// del engranaje ya lo hacía: pinta las puntas de diente que encontró. Ahora la
// Rosca pinta el perfil entero, uniendo los bordes hallados a cada lado.
//
// Lo que esta prueba fija es que ese dibujo SEA el perfil y no una raya: sobre
// una rosca de verdad tiene que ondular con la misma altura que el filete que se
// midió, y sobre una pieza lisa no puede aparecer.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "vision/pipeline.h"

using namespace pci;
using namespace pci::inspection;

namespace {

// Cuánto se aparta del eje el perfil dibujado: la diferencia entre el punto más
// lejano y el más cercano. En una rosca eso es la altura del filete; en una
// caña lisa, el ruido del borde.
double profileSwing(const ToolRunResult& result, const cv::Point2f& from,
                    const cv::Point2f& to) {
    const cv::Point2f axis = to - from;
    const double length = cv::norm(axis);
    if (length < 1.0 || result.overlaySegments.empty()) {
        return 0.0;
    }
    const cv::Point2f u = axis / static_cast<float>(length);
    double closest = 1e9;
    double farthest = 0.0;
    for (const auto& segment : result.overlaySegments) {
        for (const auto& point : segment) {
            const cv::Point2f d = point - from;
            const double along = d.x * u.x + d.y * u.y;
            const double across = std::abs(d.x * u.y - d.y * u.x);
            // El propio eje entra en los segmentos —se dibuja también— y no
            // cuenta: su distancia al eje es cero por construcción.
            if (across < 1e-6 && along > -1e-6) {
                continue;
            }
            closest = std::min(closest, across);
            farthest = std::max(farthest, across);
        }
    }
    return farthest > 0.0 ? farthest - closest : 0.0;
}

}  // namespace

TEST(ThreadShowsTheThread, TheProfileItReadIsDrawnAndItUndulates) {
    const cv::Mat image =
        cv::imread("C:/Users/furro/Pictures/IMG-MC/rosca-1.png", cv::IMREAD_COLOR);
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    auto analysis = vision::analyzeFrame(image, config);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    const vision::Fixture fixture = analysis.value().fixture;

    // El eje a lo largo de la varilla, en coordenadas de pieza: la pieza sale
    // enderezada por el fixture, así que su eje mayor es horizontal.
    const cv::RotatedRect box = cv::minAreaRect(analysis.value().contour.points);
    const double half = std::max(box.size.width, box.size.height) * 0.35;
    const double band = std::min(box.size.width, box.size.height) * 0.6;
    ThreadGeometry geometry;
    geometry.axisFrom = {static_cast<float>(-half), 0.0F};
    geometry.axisTo = {static_cast<float>(half), 0.0F};
    geometry.searchBand = static_cast<float>(band);
    geometry.stations = 240;

    ToolConfig tool;
    tool.id = 1;
    tool.name = "Rosca 1";
    tool.type = ToolType::Thread;
    tool.geometryJson = toJson(ToolGeometry(geometry));
    const auto results = runTools(image, fixture, {tool}, 0.0, LengthUnit::Pixels);
    ASSERT_EQ(results.size(), 1U);
    const ToolRunResult& thread = results.front();
    std::printf("  [rosca] %s\n", thread.detail.c_str());
    if (thread.measured <= 0.0) {
        GTEST_SKIP() << "la rosca no midió sobre esta foto con este eje: " << thread.detail;
    }

    // Antes esto valía 1: sólo el segmento del eje. Ahora tiene que traer el
    // perfil de los dos lados.
    std::printf("  [rosca] %d segmentos dibujados\n",
                static_cast<int>(thread.overlaySegments.size()));
    EXPECT_GT(thread.overlaySegments.size(), 50U)
        << "la Rosca sigue dibujando sólo su eje: en pantalla no hay forma de saber si "
           "está siguiendo el filete o si el número sale de cualquier sitio";

    // Y ese dibujo tiene que ONDULAR. Un perfil que no se aparta del eje sería
    // una raya paralela más, que es justo de lo que se quejaba el operador.
    const cv::Point2f from(fixture.origin.x + geometry.axisFrom.x,
                           fixture.origin.y + geometry.axisFrom.y);
    const cv::Point2f to(fixture.origin.x + geometry.axisTo.x,
                         fixture.origin.y + geometry.axisTo.y);
    const double swing = profileSwing(thread, from, to);
    std::printf("  [rosca] el perfil dibujado oscila %.1f px\n", swing);
    EXPECT_GT(swing, 2.0)
        << "el perfil dibujado no ondula: son dos rectas paralelas al eje, que es "
           "exactamente lo que el operador veía antes";
}

TEST(ThreadShowsTheThread, APieceWithoutAThreadDrawsNoProfileBecauseItRefuses) {
    // La otra mitad, y la que impide «arreglarlo» dibujando siempre algo: sobre
    // una pieza lisa la herramienta se NIEGA a medir —ya lo hacía, y está
    // medido: decía que sí en las dieciséis fotos del banco, arandelas
    // incluidas, con perlas como «paso = 1,3 px»—. Si se negara y aun así
    // pintara un perfil, el dibujo diría que ha seguido algo que no existe.
    cv::Mat plain(300, 400, CV_8UC1, cv::Scalar(30));
    cv::rectangle(plain, cv::Rect(60, 120, 280, 60), cv::Scalar(225), cv::FILLED);
    const vision::Fixture fixture{{200.0F, 150.0F}, 0.0};

    ThreadGeometry geometry;
    geometry.axisFrom = {-120.0F, 0.0F};
    geometry.axisTo = {120.0F, 0.0F};
    geometry.searchBand = 50.0F;
    geometry.stations = 240;
    ToolConfig tool;
    tool.id = 1;
    tool.name = "Rosca 1";
    tool.type = ToolType::Thread;
    tool.geometryJson = toJson(ToolGeometry(geometry));

    const auto results = runTools(plain, fixture, {tool}, 0.0, LengthUnit::Pixels);
    ASSERT_EQ(results.size(), 1U);
    std::printf("  [rosca] pieza lisa -> %s\n", results.front().detail.c_str());
    EXPECT_EQ(results.front().measured, 0.0)
        << "mide un paso sobre una caña lisa: es el fallo que ya se arregló una vez";
    EXPECT_TRUE(results.front().overlaySegments.empty())
        << "se niega a medir y aun así dibuja un perfil: el dibujo estaría afirmando que "
           "ha seguido un filete que no existe";
}
