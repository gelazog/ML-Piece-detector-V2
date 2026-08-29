#include "vision/pass_trigger.h"

#include <algorithm>
#include <cmath>

namespace pci::vision {
namespace {

// Si la escena es LA MISMA que la anterior: mismo recuento y ningún centroide
// movido más de lo que se considera ruido.
//
// Se emparejan por cercanía y no por orden: el orden en que llegan las piezas
// depende de por dónde empiece a recorrer el contorno la segmentación, y una
// bandeja que reordena sus piezas parecería en movimiento estando quieta.
bool sameScene(const std::vector<cv::Point2f>& before,
               const std::vector<cv::Point2f>& now, double tolerancePx) {
    if (before.size() != now.size()) {
        return false;
    }
    for (const auto& centre : now) {
        double best = 1e9;
        for (const auto& previous : before) {
            best = std::min(best, cv::norm(centre - previous));
        }
        if (best > tolerancePx) {
            return false;
        }
    }
    return true;
}

}  // namespace

void PassTrigger::reset() {
    armed_ = true;
    hadPrevious_ = false;
    previous_.clear();
    stillSince_ = 0;
    emptySince_ = 0;
    isEmpty_ = false;
}

PassVerdict PassTrigger::observe(const SceneSnapshot& snapshot) {
    PassVerdict verdict;

    // --- El encuadre vacío: es lo que REARMA el disparo -------------------
    if (snapshot.centres.empty()) {
        if (!isEmpty_) {
            isEmpty_ = true;
            emptySince_ = snapshot.atMs;
        }
        hadPrevious_ = false;
        previous_.clear();
        stillSince_ = 0;
        const std::int64_t emptyFor = snapshot.atMs - emptySince_;
        if (!armed_ && emptyFor >= options_.rearmMs) {
            armed_ = true;
        }
        verdict.why = armed_ ? "No hay ninguna pieza en el encuadre."
                             : "El encuadre acaba de vaciarse; en cuanto lleve " +
                                   std::to_string(options_.rearmMs) +
                                   " ms vacío, la siguiente pieza se medirá.";
        return verdict;
    }
    isEmpty_ = false;

    // --- Una pieza a medio entrar no se mide -------------------------------
    //
    // Y esto va ANTES de mirar si está quieta, a propósito: una pieza parada
    // justo en el borde —porque la cinta se detuvo— está quieta y sigue sin
    // poder medirse. Al revés, el asentamiento se cumpliría y se mediría media
    // pieza.
    if (snapshot.someoneTouchesTheEdge) {
        hadPrevious_ = false;
        previous_.clear();
        stillSince_ = 0;
        verdict.why = "Hay una pieza tocando el borde del encuadre: sus cotas serían un "
                      "límite inferior, así que no se mide hasta que entre entera.";
        return verdict;
    }

    // --- Asentamiento ------------------------------------------------------
    if (!hadPrevious_ || !sameScene(previous_, snapshot.centres, options_.stillnessPx)) {
        hadPrevious_ = true;
        previous_ = snapshot.centres;
        stillSince_ = snapshot.atMs;
        verdict.stillMs = 0;
        verdict.why = "La escena se está moviendo.";
        return verdict;
    }
    previous_ = snapshot.centres;
    verdict.stillMs = static_cast<int>(snapshot.atMs - stillSince_);

    if (!armed_) {
        verdict.why = "Ya se midió esta pieza; se volverá a medir cuando el encuadre se "
                      "vacíe.";
        return verdict;
    }
    if (verdict.stillMs < options_.settleMs) {
        verdict.why = "La escena lleva " + std::to_string(verdict.stillMs) + " ms quieta de "
                      "los " + std::to_string(options_.settleMs) + " que hacen falta.";
        return verdict;
    }

    // Se mide, y se desarma hasta que el encuadre se vacíe. Ese desarme es lo
    // que convierte «doce medidas mientras la pieza cruza» en «una por pieza».
    armed_ = false;
    verdict.decision = PassDecision::Measure;
    verdict.why = "Escena quieta " + std::to_string(verdict.stillMs) + " ms y ninguna pieza "
                  "en el borde: se mide.";
    return verdict;
}

}  // namespace pci::vision
