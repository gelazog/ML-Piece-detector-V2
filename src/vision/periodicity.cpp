#include "vision/periodicity.h"

#include <algorithm>
#include <cmath>

namespace pci::vision {

namespace {

// Quita la media (y, en el caso lineal, la tendencia recta). En una pieza
// torneada esa recta es la conicidad: separarla deja solo el rizado periódico,
// que es lo que se quiere medir.
std::vector<double> detrended(const std::vector<double>& signal, bool circular) {
    const auto n = static_cast<double>(signal.size());
    double mean = 0.0;
    for (const double v : signal) {
        mean += v;
    }
    mean /= n;

    std::vector<double> out(signal.size());
    if (circular) {
        for (std::size_t i = 0; i < signal.size(); ++i) {
            out[i] = signal[i] - mean;
        }
        return out;
    }

    // Pendiente por mínimos cuadrados sobre (índice, valor).
    const double meanIndex = (n - 1.0) / 2.0;
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const double di = static_cast<double>(i) - meanIndex;
        num += di * (signal[i] - mean);
        den += di * di;
    }
    const double slope = den > 0.0 ? num / den : 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i) {
        out[i] = signal[i] - (mean + slope * (static_cast<double>(i) - meanIndex));
    }
    return out;
}

// Autocorrelación normalizada al desfase `lag`, en [-1, 1]. Normalizar importa:
// la suma cruda decrece con el desfase (menos solape) y el máximo caería
// siempre en el desfase más pequeño.
double correlationAt(const std::vector<double>& x, int lag, bool circular) {
    const int n = static_cast<int>(x.size());
    double cross = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    if (circular) {
        for (int i = 0; i < n; ++i) {
            const double a = x[static_cast<std::size_t>(i)];
            const double b = x[static_cast<std::size_t>((i + lag) % n)];
            cross += a * b;
            normA += a * a;
        }
        normB = normA;  // la misma señal rotada: misma energía
    } else {
        for (int i = 0; i + lag < n; ++i) {
            const double a = x[static_cast<std::size_t>(i)];
            const double b = x[static_cast<std::size_t>(i + lag)];
            cross += a * b;
            normA += a * a;
            normB += b * b;
        }
    }
    const double denom = std::sqrt(normA * normB);
    return denom > 1e-12 ? cross / denom : 0.0;
}

}  // namespace

PeriodEstimate dominantPeriod(const std::vector<double>& signal, double minPeriod,
                              double maxPeriod, bool circular) {
    PeriodEstimate estimate;
    const int n = static_cast<int>(signal.size());
    if (n < 4 || !(minPeriod >= 2.0) || !(maxPeriod > minPeriod)) {
        return estimate;
    }
    // Hacen falta dos repeticiones para poder hablar de periodo. En el caso
    // circular la señal cierra, así que basta con que el desfase quepa.
    const int limit = circular ? n - 1 : n / 2;
    const int minLag = static_cast<int>(std::floor(minPeriod));
    const int maxLag = std::min(static_cast<int>(std::ceil(maxPeriod)), limit);
    if (minLag < 2 || maxLag <= minLag) {
        return estimate;
    }

    const std::vector<double> x = detrended(signal, circular);

    std::vector<double> correlation(static_cast<std::size_t>(maxLag - minLag + 1), 0.0);
    int bestLag = minLag;
    double best = -2.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double c = correlationAt(x, lag, circular);
        correlation[static_cast<std::size_t>(lag - minLag)] = c;
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    if (best <= 0.0) {
        return estimate;  // nada que se repita
    }

    // Corrección del error de octava. La autocorrelación pica también en los
    // MÚLTIPLOS del periodo, y a veces uno de ellos gana por poco: devolver el
    // doble del paso —o la mitad de los dientes— sería un error clamoroso y
    // silencioso. Así que se parte del máximo global y se comprueba si alguno
    // de sus SUBMÚLTIPLOS también pica; se prefiere el más pequeño que lo haga,
    // que es el fundamental.
    //
    // La comprobación va hacia abajo desde el máximo y no hacia arriba desde el
    // desfase mínimo: cerca del origen la correlación todavía es alta sin que
    // haya ningún periodo ahí, y buscando desde la izquierda el desfase mínimo
    // siempre ganaba (con 17 dientes devolvía 240).
    auto isLocalPeak = [&correlation](int lag, int firstLag) {
        const auto i = static_cast<std::size_t>(lag - firstLag);
        // Solo los puntos interiores pueden ser picos: en un extremo no se sabe
        // si la curva seguía subiendo.
        if (i == 0 || i + 1 >= correlation.size()) {
            return false;
        }
        return correlation[i] >= correlation[i - 1] && correlation[i] >= correlation[i + 1];
    };

    int peak = bestLag;
    for (int divisor = bestLag / minLag; divisor >= 2; --divisor) {
        const double exact = static_cast<double>(bestLag) / divisor;
        // Se busca en una VENTANA alrededor del submúltiplo, no en el valor
        // redondeado: con un periodo fraccionario el pico real cae al lado. Con
        // periodo 17,4 el máximo global sale en el desfase 35 (2,0115 periodos
        // alinean mejor que 0,977), el submúltiplo redondea a 18 y el pico está
        // en 17 — probar solo el 18 dejaba pasar el error de octava.
        const int window = std::max(1, static_cast<int>(exact * 0.05));
        int localBest = -1;
        double localValue = -2.0;
        for (int lag = static_cast<int>(std::lround(exact)) - window;
             lag <= static_cast<int>(std::lround(exact)) + window; ++lag) {
            if (lag < minLag || lag > maxLag) {
                continue;
            }
            const double c = correlation[static_cast<std::size_t>(lag - minLag)];
            if (c > localValue) {
                localValue = c;
                localBest = lag;
            }
        }
        if (localBest > 0 && localValue >= 0.85 * best && isLocalPeak(localBest, minLag)) {
            peak = localBest;  // el submúltiplo más pequeño que pica de verdad
            break;
        }
    }

    // Refinamiento subMUESTRA: el vértice de la parábola que pasa por el pico y
    // sus dos vecinos. Sin esto el periodo saldría siempre entero y el paso de
    // una rosca se redondearía al muestreo.
    double refined = peak;
    const auto i = static_cast<std::size_t>(peak - minLag);
    if (i > 0 && i + 1 < correlation.size()) {
        const double a = correlation[i - 1];
        const double b = correlation[i];
        const double c = correlation[i + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-12) {
            const double delta = 0.5 * (a - c) / denom;
            if (std::abs(delta) <= 1.0) {
                refined = peak + delta;
            }
        }
    }

    estimate.period = refined;
    estimate.confidence = std::clamp(correlation[i], 0.0, 1.0);
    estimate.valid = true;
    return estimate;
}

}  // namespace pci::vision
