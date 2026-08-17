#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/types.h"

namespace pci::inspection {

// Medición automática: mira la pieza y propone qué medir.
//
// La decisión que gobierna todo esto: **no imprime una lista de números, genera
// propuestas de HERRAMIENTAS**. Unos números sueltos serían un callejón sin
// salida —sin tolerancia, sin veredicto, sin guardarse en la plantilla, sin
// seguir a la pieza con el fixture y sin aparecer en el histórico—, y todo eso
// ya existe y funciona para las herramientas. Así el operador pasa de *dibujar
// veinte herramientas* a *revisar veinte propuestas*.

struct AutoProposal {
    ToolConfig config;      // tipo, nombre y tolerancias ya sugeridas
    ToolGeometry geometry;  // en coordenadas de PIEZA, como todas
    double measured = 0.0;  // lo que mide sobre esta pieza
    // De qué clase es ese número. Sin esto, la tabla de propuestas escribía
    // «6.00» en la misma columna para un diámetro en píxeles, un ángulo en
    // grados y un recuento de lados — tres unidades distintas presentadas como
    // si fueran la misma.
    MeasuredKind kind = MeasuredKind::Length;
    std::string detail;     // la lectura completa, tal como la da la herramienta
    // Por qué se propone, en una frase. Es lo que permite al operador decidir
    // rápido: una lista de doce herramientas sin explicación no se revisa, se
    // acepta entera o se descarta entera.
    std::string reason;
};

struct ProposeOptions {
    // Tope de propuestas. Cincuenta son tan inútiles como ninguna: no se
    // revisan. Se ordenan por tamaño del rasgo y se corta.
    int maxProposals = 12;
    // Rasgos más pequeños que esto no merecen una herramienta.
    double minFeatureLength = 25.0;
    // Dos caras se consideran enfrentadas si sus direcciones no difieren más de
    // esto (grados) y se solapan al proyectarlas.
    double parallelToleranceDeg = 8.0;
    // Un ángulo entre caras solo se propone si está lejos de 0° y de 180°.
    double minCornerAngleDeg = 20.0;
};

// Propone herramientas a partir de la pieza ya detectada.
//
// `gray` es la imagen (para MEDIR cada propuesta), `mask` la máscara binaria de
// la pieza (para los agujeros) y `fixture` su sistema de coordenadas.
//
// Cada propuesta **se ejecuta antes de proponerse**: si la herramienta no
// consigue medir sobre esta pieza, se descarta en vez de ofrecérsela al
// operador. Lo que llega a la lista ya funciona, y su tolerancia sugerida sale
// de una medida real y no de una estimación geométrica.
[[nodiscard]] std::vector<AutoProposal> proposeTools(const cv::Mat& gray, const cv::Mat& mask,
                                                     const vision::Fixture& fixture,
                                                     const ProposeOptions& options = {},
                                                     double mmPerPixel = 0.0);

}  // namespace pci::inspection
