#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "inspection_editor/tools/derived_element.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/board_frame.h"
#include "vision/types.h"

namespace pci::inspection {

// Unidad elegida por el operador para mostrar las medidas.
enum class LengthUnit { Auto, Millimeters, Centimeters, Pixels };

// Qué CLASE de magnitud es `measured`. Lo decide quien mide, que es el único
// que puede saberlo: la Región mide seis cosas distintas con el mismo tipo de
// herramienta, así que preguntarle al tipo no basta.
//
// Existe porque sin ello la interfaz rotulaba en milímetros todo lo que no
// fuera un ángulo, y eso son medidas falsas con aspecto de buenas: un hexágono
// se pintaba como «Lados (6): 6,00 mm», un área en px² se multiplicaba por la
// escala LINEAL —número equivocado y unidad equivocada— y una circularidad,
// que no tiene unidades, salía en milímetros. Un número con la unidad
// equivocada es peor que no dar el número.
enum class MeasuredKind {
    Length,    // píxeles, convertibles a mm/cm con la escala
    Angle,     // grados
    Count,     // un recuento: dientes, agujeros, defectos, lados
    Fraction,  // adimensional (circularidad, solidez, simetría, proporción)
    Area,      // píxeles CUADRADOS: la escala entra al cuadrado, no lineal
};

struct ToolRunResult {
    std::int64_t toolId = -1;
    std::string name;
    ToolType type = ToolType::Caliper;
    bool ok = false;
    double measured = 0.0;  // valor principal (px, px², conteo, grados o fracción)
    MeasuredKind kind = MeasuredKind::Length;
    std::string detail;
    // De qué pieza del frame es esta medida (C6). 0 = la pieza principal, que
    // es el único caso cuando se inspecciona de una en una. Viaja en el propio
    // resultado para que la cadena entera —motor, historial, interfaz— sepa a
    // quién pertenece sin tener que llevar la cuenta por separado.
    int pieceIndex = 0;
    // Elemento geométrico que esta herramienta ofrece como referencia (X0), en
    // coordenadas de pieza. Vacío si no produce ninguno.
    DerivedElement derived;
    // La herramienta NO juzga (X1): las construcciones geométricas no miden
    // nada que pueda estar dentro o fuera de tolerancia, solo calculan un
    // elemento. Con esto la tabla de resultados escribe «—» en vez de un OK
    // verde que no significaría nada. Ojo: informativa **no** quiere decir que
    // no pueda dar NG — si la construcción no se puede hacer, `ok` es false y
    // eso sí es un problema, porque todo lo que la referencia se queda sin
    // datum.
    bool informative = false;
    // Para pintar sobre la imagen inspeccionada (coordenadas de imagen).
    std::vector<cv::Point2f> overlayPoints;
    std::vector<std::array<cv::Point2f, 2>> overlaySegments;
};

// Ejecuta una herramienta sobre la imagen (BGR o gris) usando el fixture de la
// pieza para llevar la geometría de coordenadas de pieza a imagen. Un fallo de
// medición devuelve Result ok con ToolRunResult{ok=false, detail=motivo};
// Result err se reserva para configuración corrupta. mmPerPixel > 0 añade la
// medida en mm/cm a los textos de detalle.
//
// imageToMm (opcional, 3x3): homografía imagen->plano en mm de un marcador
// ArUco. Si se pasa, las herramientas de LONGITUD (Caliper, Regla, Punto-Línea)
// calculan los mm mapeando sus puntos por la homografía (corrige la perspectiva
// lejos del marcador) en vez de multiplicar píxeles por una escala constante.
// El valor principal `measured` sigue en píxeles (las tolerancias son en px).
//
// board (opcional): tablero de referencia ya resuelto para este frame. Solo lo
// usa la herramienta Posición, que mide respecto a su cero. Si no se pasa, se
// asume un tablero centrado en la propia pieza y alineado con la imagen (en ese
// caso la desviación de un rasgo es constante: ver toolTypeDescription).
//
// scaleQuality (0..1): perpendicularidad de la cámara al plano, tal como la
// mide el marcador ArUco (`MarkerScale::quality`). Con la cámara inclinada, un
// círculo se ve como elipse y los diámetros salen cortos, así que las
// herramientas que miden diámetros o radios avisan cuando baja. 1 = de frente;
// un valor negativo significa "no se sabe" y no dispara ningún aviso.
core::Result<ToolRunResult> runTool(const cv::Mat& image, const vision::Fixture& fixture,
                                    const ToolConfig& config, double mmPerPixel = 0.0,
                                    LengthUnit unit = LengthUnit::Auto,
                                    const cv::Mat& imageToMm = cv::Mat(),
                                    const vision::BoardFrame* board = nullptr,
                                    double scaleQuality = -1.0,
                                    const DerivedElements* references = nullptr);

// Ejecuta todas las herramientas habilitadas; nunca lanza. Los errores de
// configuración se convierten en resultados NG con el motivo en detail.
std::vector<ToolRunResult> runTools(const cv::Mat& image, const vision::Fixture& fixture,
                                    const std::vector<ToolConfig>& tools,
                                    double mmPerPixel = 0.0,
                                    LengthUnit unit = LengthUnit::Auto,
                                    const cv::Mat& imageToMm = cv::Mat(),
                                    const vision::BoardFrame* board = nullptr,
                                    double scaleQuality = -1.0, bool parallel = true);

// Formatea una longitud en píxeles según la escala y la unidad elegida.
std::string formatLength(double px, double mmPerPixel, LengthUnit unit);

// El valor medido, con su unidad, sea de la clase que sea. **Único sitio** donde
// se decide cómo se rotula una medida.
//
// Antes lo decidía cada pantalla por su cuenta —el lienzo, el diálogo de
// resultados, la ventana principal y el editor— y las cuatro se equivocaban
// igual: convertían a milímetros todo lo que no fuera un ángulo. Cuatro copias
// de la misma regla es cuatro sitios donde arreglar el mismo fallo, y por eso
// aquí hay una sola.
//
// `compact` recorta el sufijo en píxeles («12,34 mm» en vez de «12,34mm
// (49,4px)»): sirve para la etiqueta que se pinta encima de la pieza, donde el
// sitio es el que es.
[[nodiscard]] std::string formatMeasure(const ToolRunResult& result, double mmPerPixel,
                                        LengthUnit unit, bool compact = false);

// La unidad con la que se guarda esta medida («px», «px²», «°», «n», «—»). La
// base de datos escribía «px» para todo, ángulos y recuentos incluidos, y una
// columna que siempre dice lo mismo no es un dato: es un adorno que además
// miente.
[[nodiscard]] const char* measuredUnitKey(MeasuredKind kind);

}  // namespace pci::inspection
