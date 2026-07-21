#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/types.h"

namespace pci::inspection {

// Unidad elegida por el operador para mostrar las medidas.
enum class LengthUnit { Auto, Millimeters, Centimeters, Pixels };

struct ToolRunResult {
    std::int64_t toolId = -1;
    std::string name;
    ToolType type = ToolType::Caliper;
    bool ok = false;
    double measured = 0.0;  // valor principal (px, conteo o grados)
    bool measuredIsAngle = false;  // true = 'measured' está en grados
    std::string detail;
    // Para pintar sobre la imagen inspeccionada (coordenadas de imagen).
    std::vector<cv::Point2f> overlayPoints;
    std::vector<std::array<cv::Point2f, 2>> overlaySegments;
};

// Ejecuta una herramienta sobre la imagen (BGR o gris) usando el fixture de la
// pieza para llevar la geometría de coordenadas de pieza a imagen. Un fallo de
// medición devuelve Result ok con ToolRunResult{ok=false, detail=motivo};
// Result err se reserva para configuración corrupta. mmPerPixel > 0 añade la
// medida en mm/cm a los textos de detalle.
core::Result<ToolRunResult> runTool(const cv::Mat& image, const vision::Fixture& fixture,
                                    const ToolConfig& config, double mmPerPixel = 0.0,
                                    LengthUnit unit = LengthUnit::Auto);

// Ejecuta todas las herramientas habilitadas; nunca lanza. Los errores de
// configuración se convierten en resultados NG con el motivo en detail.
std::vector<ToolRunResult> runTools(const cv::Mat& image, const vision::Fixture& fixture,
                                    const std::vector<ToolConfig>& tools,
                                    double mmPerPixel = 0.0,
                                    LengthUnit unit = LengthUnit::Auto);

// Formatea una longitud en píxeles según la escala y la unidad elegida.
std::string formatLength(double px, double mmPerPixel, LengthUnit unit);

}  // namespace pci::inspection
