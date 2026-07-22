#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <variant>

#include "core/result.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// Geometrías tipadas por herramienta, en coordenadas de PIEZA (píxeles).
// Las distancias se conservan al pasar a coordenadas de imagen porque el
// fixture es rotación + traslación pura (sin escala).

struct CaliperGeometry {
    cv::Point2f p0;
    cv::Point2f p1;
    float bandWidth = 10.0F;  // grosor perpendicular promediado del perfil
};

struct CircleGeometry {
    cv::Point2f center;
    float radius = 50.0F;
    float searchBand = 12.0F;  // banda radial de búsqueda del borde
    int rayCount = 36;         // puntos de escaneo alrededor del círculo
};

struct PointToLineGeometry {
    cv::Point2f lineA;
    cv::Point2f lineB;
    cv::Point2f scanA;  // segmento que localiza el punto (borde más fuerte)
    cv::Point2f scanB;
};

struct EdgeFlawGeometry {
    cv::Point2f p0;  // tramo del borde que debería ser recto
    cv::Point2f p1;
    float scanLength = 16.0F;  // largo de cada escaneo perpendicular
    int scanCount = 20;
};

struct BlobGeometry {
    cv::Point2f center;  // rectángulo alineado a los ejes de la pieza
    float width = 80.0F;
    float height = 60.0F;
    float minArea = 20.0F;
    bool darkBlobs = true;  // buscar manchas oscuras (o claras si false)
};

struct RulerGeometry {
    cv::Point2f p0;  // distancia directa entre dos puntos (sin buscar bordes)
    cv::Point2f p1;
};

struct LineToLineGeometry {
    cv::Point2f a0;  // línea de referencia A
    cv::Point2f a1;
    cv::Point2f b0;  // línea de referencia B
    cv::Point2f b1;
};

struct AngleGeometry {
    cv::Point2f vertex;  // esquina cuyo ángulo se mide
    cv::Point2f end0;    // extremo del primer lado
    cv::Point2f end1;    // extremo del segundo lado
};

using ToolGeometry = std::variant<CaliperGeometry, CircleGeometry, PointToLineGeometry,
                                  EdgeFlawGeometry, BlobGeometry, RulerGeometry,
                                  LineToLineGeometry, AngleGeometry>;

// (De)serialización JSON (cv::FileStorage en memoria). El tipo del JSON debe
// coincidir con config.type al parsear.
std::string toJson(const ToolGeometry& geometry);
core::Result<ToolGeometry> geometryFromJson(ToolType type, const std::string& json);

ToolType typeOf(const ToolGeometry& geometry);

// Descripción de uso para tooltips/ayuda (UTF-8, en español): qué mide la
// herramienta y cómo dibujarla.
const char* toolTypeDescription(ToolType type);

// Tolerancias sugeridas a partir de una primera medición sobre la pieza
// buena: banda de ±10% para distancias/diámetros, conteo exacto para blobs y
// techo holgado para la desviación de borde.
void suggestTolerances(ToolType type, double measured, double& toleranceMin,
                       double& toleranceMax);

}  // namespace pci::inspection
