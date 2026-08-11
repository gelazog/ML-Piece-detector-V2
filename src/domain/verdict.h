#pragma once

#include <string>
#include <vector>

namespace pci::domain {

struct EmbeddingCheck {
    bool evaluated = false;  // false = sin modelo o sin referencia guardada
    double similarity = 0.0;
    double threshold = 0.0;
    bool anomalous = false;
    std::string note;  // p. ej. "modelo no disponible"
};

struct ToolCheck {
    std::string name;
    bool ok = false;
    double measured = 0.0;
    std::string detail;
};

// Reglas del modo Especial (M4): cuánto puede desviarse la pieza del cero del
// tablero y cuánto puede estar girada. Una tolerancia <= 0 significa "no
// vigilar eso", así que quien no configure nada sigue trabajando como antes.
struct PositionCheck {
    bool evaluated = false;   // false = modo Real, sin tolerancias o sin pieza
    bool radiusEvaluated = false;
    bool angleEvaluated = false;
    double radius = 0.0;      // desviación del centro respecto al cero (px)
    double maxRadius = 0.0;
    double angleDeg = 0.0;    // giro respecto a los ejes del tablero
    double maxAngleDeg = 0.0;
    bool ok = true;
    std::string note;  // p. ej. "giro no evaluado: pieza casi simétrica"
};

// Recuento de piezas (C5). Hasta ahora la aplicación se quedaba con la pieza
// mayor y borraba el resto en silencio: una bandeja con cinco tornillos y otra
// con seis daban exactamente el mismo resultado. Con el número esperado
// declarado, que falte una **es un NG por sí solo**, sin necesidad de tener
// ninguna herramienta dibujada.
struct CountCheck {
    bool evaluated = false;  // false = no se declaró cuántas piezas esperar
    int expected = 0;
    int found = 0;
    bool ok = true;
};

struct InspectionVerdict {
    bool ok = false;
    EmbeddingCheck embedding;
    std::vector<ToolCheck> tools;
    PositionCheck position;
    CountCheck count;
    std::string summary;  // legible: "OK" o los motivos del NG
};

// Combina apariencia (embeddings), mediciones geométricas y, si el modo
// Especial lo pide, la posición de la pieza respecto al tablero: OK solo si la
// apariencia no es anómala (cuando pudo evaluarse), todas las herramientas
// están dentro de tolerancia y la pieza está bien colocada.
InspectionVerdict combineVerdict(const EmbeddingCheck& embedding,
                                 const std::vector<ToolCheck>& tools,
                                 const PositionCheck& position = {},
                                 const CountCheck& count = {});

// Compara las piezas encontradas con las esperadas. `expected <= 0` significa
// "no vigilar el recuento", así que quien no lo configure sigue igual que
// antes: la regla de siempre, un aviso que salta siempre es un aviso que se
// aprende a ignorar.
[[nodiscard]] CountCheck evaluatePieceCount(int expected, int found);

// Evalúa las reglas de posición del modo Especial. Las tolerancias <= 0 no se
// vigilan; `axisReliable` en false salta la comprobación de giro (pieza casi
// simétrica: su eje principal no es de fiar y daría NG falsos).

[[nodiscard]] PositionCheck evaluatePosition(double radius, double maxRadius, double angleDeg,
                                             double maxAngleDeg, bool axisReliable);

}  // namespace pci::domain
