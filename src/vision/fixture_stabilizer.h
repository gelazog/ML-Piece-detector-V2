#pragma once

#include "vision/types.h"

namespace pci::vision {

struct StabilizerOptions {
    double positionDeadbandPx = 2.5;  // por debajo: la pieza se considera quieta
    double angleDeadbandDeg = 1.5;
    double smoothing = 0.35;          // EMA fuera de la banda muerta
    double positionSnapPx = 40.0;     // por encima: seguir al instante, sin arrastre
    double angleSnapDeg = 25.0;
    // Continuidad anti-giro de 180°: el signo del momento de 3er orden es
    // ruidoso en piezas casi simétricas y produce giros espontáneos; si el
    // ángulo saltó ~180° se conserva el sentido del frame anterior.
    // Desactivar cuando hay rasgo distintivo (el rasgo es la verdad).
    bool resolveFlips = true;
};

// Estabiliza el fixture medido en este frame contra el mostrado en el frame
// anterior: quieto = clavado; movimiento suave = seguimiento sin vibración;
// movimiento grande = respuesta inmediata. flipped180 avisa al llamador de
// que debe recalcular el recorte normalizado (el fixture giró 180°).
Fixture stabilizeFixture(const Fixture& previous, const Fixture& measured,
                         const StabilizerOptions& options, bool& flipped180);

}  // namespace pci::vision
