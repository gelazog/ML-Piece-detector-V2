#pragma once

#include <array>
#include <cstddef>

#include "vision/pipeline.h"

namespace pci::vision {

// Media de las últimas N ejecuciones (R2).
//
// Media y no último valor porque un solo frame no dice nada: el coste de un
// análisis varía con lo que haya en la imagen, y el operador que abre la
// pestaña de Rendimiento para decidir dónde apretar necesita el
// comportamiento, no una muestra.
//
// Ventana fija y sin reservar memoria: esto lo alimenta el camino más caliente
// del programa, y un contenedor que crece sería exactamente el tipo de coste
// que un medidor de rendimiento no debe introducir.
class StageStats {
public:
    static constexpr std::size_t kWindow = 30;

    void add(const StageTimings& timings) {
        samples_[next_] = timings;
        next_ = (next_ + 1) % kWindow;
        if (count_ < kWindow) {
            ++count_;
        }
    }

    void clear() {
        next_ = 0;
        count_ = 0;
    }

    [[nodiscard]] std::size_t count() const { return count_; }

    // Media de cada etapa. Con cero muestras devuelve todo a cero, que es lo
    // que hay que enseñar antes del primer frame: un desglose inventado sería
    // peor que un hueco.
    [[nodiscard]] StageTimings mean() const {
        StageTimings sum;
        if (count_ == 0) {
            return sum;
        }
        for (std::size_t i = 0; i < count_; ++i) {
            sum.segment += samples_[i].segment;
            sum.contour += samples_[i].contour;
            sum.fixture += samples_[i].fixture;
            sum.normalize += samples_[i].normalize;
            sum.tools += samples_[i].tools;
            sum.total += samples_[i].total;
        }
        const auto n = static_cast<double>(count_);
        sum.segment /= n;
        sum.contour /= n;
        sum.fixture /= n;
        sum.normalize /= n;
        sum.tools /= n;
        sum.total /= n;
        return sum;
    }

    // Lo que el desglose NO está atribuyendo a ninguna etapa. Se enseña a
    // propósito: si crece, es que hay trabajo real fuera de las etapas medidas
    // y el reparto está engañando sobre dónde apretar.
    [[nodiscard]] double unaccounted() const {
        const StageTimings average = mean();
        return average.total - average.stagesSum();
    }

private:
    std::array<StageTimings, kWindow> samples_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

}  // namespace pci::vision
