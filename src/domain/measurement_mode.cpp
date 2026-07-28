#include "domain/measurement_mode.h"

namespace pci::domain {

std::string_view modeKey(MeasurementMode mode) {
    switch (mode) {
        case MeasurementMode::Real:
            return "real";
        case MeasurementMode::Special:
            return "special";
    }
    return "real";
}

MeasurementMode modeFromKey(std::string_view key, MeasurementMode fallback) {
    if (key == modeKey(MeasurementMode::Real)) {
        return MeasurementMode::Real;
    }
    if (key == modeKey(MeasurementMode::Special)) {
        return MeasurementMode::Special;
    }
    return fallback;
}

const char* modeLabel(MeasurementMode mode) {
    switch (mode) {
        case MeasurementMode::Real:
            return "Posición real (personalizada)";
        case MeasurementMode::Special:
            return "Especial (tablero centrado)";
    }
    return "";
}

const char* modeDescription(MeasurementMode mode) {
    switch (mode) {
        case MeasurementMode::Real:
            return "Mides donde quieras: cada herramienta se juzga con sus propias\n"
                   "tolerancias y no hay reglas de posición. Es el modo de siempre.";
        case MeasurementMode::Special:
            return "Además de las herramientas, la pieza se mide respecto al tablero\n"
                   "centrado (centro = 0): cuánto se desvía y cuánto gira. El tablero\n"
                   "se enciende solo al elegir la pieza y sus reglas entran en el\n"
                   "veredicto OK/NG.";
    }
    return "";
}

}  // namespace pci::domain
