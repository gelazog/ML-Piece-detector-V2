#pragma once

#include <QString>

#include "core/fps_counter.h"

namespace pci::ui {

// La contabilidad de frames del análisis (R1).
//
// Vive aparte de la ventana por la razón de siempre: `MainWindow` no tiene
// banco de pruebas, y aquí está lo único que puede estar mal — la cuenta. Con
// el reloj inyectado se puede simular una cámara rápida y un análisis lento sin
// cámara ni análisis.
//
// La invariante que sostiene todo: **cada frame que llega o se mide o se
// descarta**. Si esa suma no cuadra, el indicador está mintiendo, y un
// indicador de rendimiento que miente es peor que no tenerlo.
class FrameAccounting {
public:
    using Clock = core::FpsCounter::Clock;

    // Llega un frame de la cámara. `analysing` es si el análisis está activo
    // (con el contorno oculto no se analiza a propósito) y `previousStillPending`
    // si el frame anterior sigue esperando turno — que es exactamente cuando el
    // que llega se pisa y no lo mira nadie.
    void frameArrived(bool analysing, bool previousStillPending,
                      Clock::time_point now = Clock::now()) {
        if (analysing && previousStillPending) {
            dropped_.tick(now);
        }
    }

    // Se cuenta al TERMINAR y no al empezar: lo que importa es a qué ritmo
    // salen medidas, no a qué ritmo se lanzan.
    void analysisFinished(Clock::time_point now = Clock::now()) { analysed_.tick(now); }

    [[nodiscard]] double analysisFps(Clock::time_point now = Clock::now()) {
        return analysed_.fps(now);
    }
    [[nodiscard]] double droppedFps(Clock::time_point now = Clock::now()) {
        return dropped_.fps(now);
    }

private:
    core::FpsCounter analysed_;
    core::FpsCounter dropped_;
};

// Cómo se lee la barra de estado.
//
// Hasta ahora decía `640x480 — 8.0 fps`, y esos eran los fps de CAPTURA,
// contados en el hilo de la cámara. El análisis descarta frames cuando no llega
// —`maybeStartAnalysis` se salta el frame nuevo si el anterior sigue
// corriendo— y eso no se veía en ninguna parte. Una cámara a 30 fps con el
// análisis a 8 se ve perfecta en pantalla **y mide uno de cada cuatro**.
//
// La regla: forma corta cuando el análisis va al ritmo de la cámara, larga
// cuando no. Un indicador que enseña tres números siempre se deja de leer, y
// entonces tampoco avisa el día que importa.
//
// `droppedFps` negativo significa «no aplica»: con el contorno oculto no se
// analiza nada a propósito, y llamar «descartados» a esos frames sería contar
// como avería lo que el operador ha pedido.
[[nodiscard]] QString formatRates(int width, int height, double captureFps,
                                  double analysisFps, double droppedFps);

}  // namespace pci::ui
