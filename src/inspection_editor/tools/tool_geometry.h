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

using ToolGeometry = std::variant<CaliperGeometry, CircleGeometry, PointToLineGeometry,
                                  EdgeFlawGeometry, BlobGeometry>;

// (De)serialización JSON (cv::FileStorage en memoria). El tipo del JSON debe
// coincidir con config.type al parsear.
std::string toJson(const ToolGeometry& geometry);
core::Result<ToolGeometry> geometryFromJson(ToolType type, const std::string& json);

ToolType typeOf(const ToolGeometry& geometry);

}  // namespace pci::inspection
