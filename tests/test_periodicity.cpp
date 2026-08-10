// Pruebas del cálculo del periodo. De aquí salen el paso de una rosca y el
// número de dientes de un engranaje, así que lo que se comprueba no es solo que
// acierte con una señal limpia, sino que aguante lo que trae una pieza real:
// ruido, conicidad y un diente estropeado.
#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

#include "vision/periodicity.h"

using pci::vision::dominantPeriod;
using pci::vision::PeriodEstimate;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> sine(int count, double period, double amplitude = 1.0,
                         double noiseSigma = 0.0, unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noiseSigma);
    std::vector<double> signal;
    signal.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        double v = amplitude * std::sin(2.0 * kPi * i / period);
        if (noiseSigma > 0.0) {
            v += noise(rng);
        }
        signal.push_back(v);
    }
    return signal;
}

// Perfil parecido al de una rosca vista de canto: cresta plana, valle plano y
// flancos rectos. Se parece mucho más a la señal real que una senoide.
std::vector<double> threadLikeProfile(int count, double period, double crest, double root) {
    std::vector<double> signal;
    signal.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double phase = std::fmod(static_cast<double>(i), period) / period;
        double v = 0.0;
        if (phase < 0.25) {
            v = root + (crest - root) * (phase / 0.25);  // flanco de subida
        } else if (phase < 0.5) {
            v = crest;
        } else if (phase < 0.75) {
            v = crest - (crest - root) * ((phase - 0.5) / 0.25);  // bajada
        } else {
            v = root;
        }
        signal.push_back(v);
    }
    return signal;
}

}  // namespace

TEST(Periodicity, RecoversThePeriodOfACleanSignal) {
    for (const double period : {8.0, 15.0, 32.0, 50.0}) {
        const PeriodEstimate e = dominantPeriod(sine(600, period), 4.0, 120.0);
        ASSERT_TRUE(e.valid) << "periodo " << period;
        EXPECT_NEAR(e.period, period, period * 0.02) << "periodo pedido " << period;
        EXPECT_GT(e.confidence, 0.9);
    }
}

TEST(Periodicity, ResolvesAFractionalPeriod) {
    // El refinamiento parabólico es lo que evita que el paso de una rosca se
    // redondee al muestreo. Sin él, 17,4 saldría 17.
    const PeriodEstimate e = dominantPeriod(sine(800, 17.4), 5.0, 60.0);
    ASSERT_TRUE(e.valid);
    std::printf("  periodo real 17.400 -> medido %.3f\n", e.period);
    EXPECT_NEAR(e.period, 17.4, 0.3);
    EXPECT_NE(e.period, std::round(e.period)) << "debería tener parte fraccionaria";
}

TEST(Periodicity, SurvivesNoise) {
    // Ruido de la mitad de la amplitud de la señal: mal contraste, pero medible.
    const PeriodEstimate e = dominantPeriod(sine(800, 24.0, 1.0, 0.5, 7), 5.0, 80.0);
    ASSERT_TRUE(e.valid);
    std::printf("  con ruido 0.5: periodo %.3f, confianza %.2f\n", e.period, e.confidence);
    EXPECT_NEAR(e.period, 24.0, 0.8);
}

TEST(Periodicity, ATaperDoesNotFoolIt) {
    // Una pieza cónica: al rizado se le suma una recta. Si no se quitara la
    // tendencia, la correlación la seguiría a ella y no al paso.
    std::vector<double> signal = sine(600, 20.0);
    for (std::size_t i = 0; i < signal.size(); ++i) {
        signal[i] += 0.05 * static_cast<double>(i);  // conicidad fuerte
    }
    const PeriodEstimate e = dominantPeriod(signal, 5.0, 80.0);
    ASSERT_TRUE(e.valid);
    EXPECT_NEAR(e.period, 20.0, 0.5);
}

TEST(Periodicity, ADamagedToothLowersConfidenceWithoutChangingThePeriod) {
    // El motivo de usar autocorrelación en vez de contar picos. Con un periodo
    // estropeado, contar picos se descuadra; aquí el resultado aguanta y lo
    // único que baja es la confianza, que es la señal correcta al operador.
    std::vector<double> clean = threadLikeProfile(600, 25.0, 1.0, 0.0);
    const PeriodEstimate before = dominantPeriod(clean, 6.0, 80.0);

    std::vector<double> damaged = clean;
    for (int i = 300; i < 325; ++i) {  // un periodo entero arrasado
        damaged[static_cast<std::size_t>(i)] = 0.4;
    }
    const PeriodEstimate after = dominantPeriod(damaged, 6.0, 80.0);

    ASSERT_TRUE(before.valid && after.valid);
    std::printf("  sano: periodo %.2f conf %.3f | dañado: periodo %.2f conf %.3f\n",
                before.period, before.confidence, after.period, after.confidence);
    EXPECT_NEAR(after.period, 25.0, 0.6) << "el periodo no debe moverse";
    EXPECT_LT(after.confidence, before.confidence) << "pero la confianza sí debe bajar";
}

TEST(Periodicity, ReturnsTheFundamentalNotAMultiple) {
    // La autocorrelación también pica en los múltiplos del periodo. Quedarse con
    // el máximo global daría el doble del paso — o la mitad de los dientes, que
    // en un engranaje es un error clamoroso y silencioso.
    const auto profile = threadLikeProfile(1000, 40.0, 1.0, 0.0);
    const PeriodEstimate e = dominantPeriod(profile, 6.0, 300.0);
    ASSERT_TRUE(e.valid);
    std::printf("  fundamental 40 -> medido %.2f\n", e.period);
    EXPECT_NEAR(e.period, 40.0, 1.0) << "no debe devolver 80, 120...";
}

TEST(Periodicity, CircularModeUsesTheWrapAround) {
    // El perfil radial de un engranaje cierra sobre sí mismo. Con 24 dientes en
    // 720 muestras el periodo es 30 exacto, y la correlación circular usa todas
    // las muestras en todos los desfases.
    constexpr int kSamples = 720;
    constexpr int kTeeth = 24;
    std::vector<double> gear;
    gear.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        gear.push_back(std::cos(2.0 * kPi * kTeeth * i / kSamples));
    }
    const PeriodEstimate e = dominantPeriod(gear, 6.0, 200.0, /*circular=*/true);
    ASSERT_TRUE(e.valid);
    const double teeth = kSamples / e.period;
    std::printf("  engranaje: periodo %.3f muestras -> %.2f dientes (reales %d)\n", e.period,
                teeth, kTeeth);
    EXPECT_NEAR(e.period, 30.0, 0.3);
    EXPECT_EQ(static_cast<int>(std::lround(teeth)), kTeeth);
    EXPECT_GT(e.confidence, 0.95) << "una señal que cierra exacta debe dar confianza alta";
}

TEST(Periodicity, CircularModeCountsOddToothNumbersToo) {
    // Un número primo de dientes no divide bien el muestreo: el periodo cae
    // entre muestras y hace falta el refinamiento para redondear al entero
    // correcto. Es el caso que rompería un contador de picos ingenuo.
    for (const int teeth : {17, 23, 31, 47}) {
        constexpr int kSamples = 1440;
        std::vector<double> gear;
        gear.reserve(kSamples);
        for (int i = 0; i < kSamples; ++i) {
            gear.push_back(std::cos(2.0 * kPi * teeth * i / kSamples));
        }
        const PeriodEstimate e = dominantPeriod(gear, 6.0, 200.0, /*circular=*/true);
        ASSERT_TRUE(e.valid) << "dientes " << teeth;
        EXPECT_EQ(static_cast<int>(std::lround(kSamples / e.period)), teeth)
            << "periodo medido " << e.period;
    }
}

TEST(Periodicity, NoiseAloneGivesLowConfidence) {
    // Una señal sin estructura no puede devolver un periodo creíble: es lo que
    // impide publicar un paso inventado cuando el borde no se ve.
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 1.0);
    std::vector<double> random(800);
    for (double& v : random) {
        v = noise(rng);
    }
    const PeriodEstimate e = dominantPeriod(random, 5.0, 100.0);
    std::printf("  ruido puro: confianza %.3f\n", e.confidence);
    EXPECT_LT(e.confidence, 0.5) << "el ruido no puede pasar por señal periódica";
}

TEST(Periodicity, RefusesWhatItCannotMeasure) {
    EXPECT_FALSE(dominantPeriod({}, 5.0, 50.0).valid);
    EXPECT_FALSE(dominantPeriod(sine(100, 10.0), 1.0, 50.0).valid) << "periodo mínimo < 2";
    EXPECT_FALSE(dominantPeriod(sine(100, 10.0), 50.0, 20.0).valid) << "máximo < mínimo";
    // Señal más corta que dos periodos: no hay repetición que observar.
    EXPECT_FALSE(dominantPeriod(sine(20, 15.0), 15.0, 40.0).valid);
    // Señal constante: no hay nada que se repita de forma medible.
    EXPECT_FALSE(dominantPeriod(std::vector<double>(400, 7.0), 5.0, 60.0).valid);
}
