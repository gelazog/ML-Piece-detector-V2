// DOS ACABADOS LEGÍTIMOS DE LA MISMA PIEZA.
//
// La referencia de una pieza es HOY una sola media. Eso da por supuesto que
// todas las piezas buenas se parecen entre sí, y en producción no siempre es
// verdad: la misma pieza de dos proveedores, o con dos acabados admisibles, o
// antes y después de un cambio de lote, forma DOS grupos y no uno.
//
// Este fichero no propone la solución todavía. Mide qué hace el programa hoy con
// ese caso, porque «una media entre dos grupos no vale» es una intuición y hace
// falta el número: puede que rechace las dos variantes, puede que ensanche tanto
// la banda que deje pasar un defecto de verdad, o puede que no pase nada.
//
// La segunda posibilidad es la peligrosa, y es la que no se ve: una referencia
// bimodal no falla ruidosamente, se queda CIEGA.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ml/reference.h"

using pci::ml::cosineSimilarity;
using pci::ml::isAnomalous;
using pci::ml::Reference;
using pci::ml::ReferenceBuilder;

namespace {

constexpr int kDim = 64;

// Un embedding sintético: un vector base con una dirección de «acabado» sumada
// con peso `finish`, y un poco de ruido reproducible.
std::vector<float> embeddingFor(double finish, int seed) {
    std::vector<float> value(kDim, 0.0F);
    for (int i = 0; i < kDim; ++i) {
        const double base = std::sin(0.7 * i + 1.3);
        const double direction = std::cos(0.31 * i + 0.2);
        const double noise = 0.004 * std::sin(13.0 * seed + 2.1 * i);
        value[static_cast<std::size_t>(i)] =
            static_cast<float>(base + finish * direction + noise);
    }
    return value;
}

// Una pieza con un defecto: se le mete una dirección distinta, la que no está
// en ninguno de los dos acabados.
std::vector<float> defectiveEmbedding(int seed) {
    std::vector<float> value = embeddingFor(0.5, seed);
    for (int i = 0; i < kDim; ++i) {
        value[static_cast<std::size_t>(i)] +=
            static_cast<float>(0.42 * std::sin(0.13 * i + 2.9));
    }
    return value;
}

Reference buildFrom(const std::vector<std::vector<float>>& samples) {
    ReferenceBuilder builder;
    for (const auto& sample : samples) {
        (void)builder.add(sample);
    }
    return builder.build().value();
}

}  // namespace

// LO QUE PASA HOY con dos acabados metidos en la misma referencia.
TEST(Variants, ASingleMeanBetweenTwoFinishesLosesItsGrip) {
    std::vector<std::vector<float>> onlyFirst;
    std::vector<std::vector<float>> both;
    for (int i = 0; i < 15; ++i) {
        onlyFirst.push_back(embeddingFor(0.0, i));
        both.push_back(embeddingFor(0.0, i));
    }
    for (int i = 0; i < 15; ++i) {
        both.push_back(embeddingFor(1.0, 100 + i));
    }

    const Reference single = buildFrom(onlyFirst);
    const Reference mixed = buildFrom(both);

    // Lo lejos que están los dos acabados entre sí, para saber que el montaje
    // representa dos grupos y no dos nubes del mismo.
    const double between =
        cosineSimilarity(embeddingFor(0.0, 500), embeddingFor(1.0, 501));
    std::printf("  [variantes] parecido entre los dos acabados: %.4f\n", between);
    ASSERT_LT(between, 0.99) << "los dos acabados de la prueba son casi el mismo: no hay "
                                "dos grupos que separar";

    const auto defect = defectiveEmbedding(700);
    const auto goodFirst = embeddingFor(0.0, 900);
    const auto goodSecond = embeddingFor(1.0, 901);

    std::printf("  [variantes] referencia de UN acabado:  banda %.4f  |  acabado 1 %.4f, "
                "acabado 2 %.4f, defecto %.4f\n",
                single.simMean - std::max(3.0 * single.simStd, 0.02),
                cosineSimilarity(goodFirst, single.mean),
                cosineSimilarity(goodSecond, single.mean),
                cosineSimilarity(defect, single.mean));
    std::printf("  [variantes] referencia de LOS DOS:     banda %.4f  |  acabado 1 %.4f, "
                "acabado 2 %.4f, defecto %.4f\n",
                mixed.simMean - std::max(3.0 * mixed.simStd, 0.02),
                cosineSimilarity(goodFirst, mixed.mean),
                cosineSimilarity(goodSecond, mixed.mean),
                cosineSimilarity(defect, mixed.mean));

    const bool firstRejected = isAnomalous(goodFirst, single);
    const bool secondRejected = isAnomalous(goodSecond, single);
    const bool defectCaught = isAnomalous(defect, single);
    std::printf("  [variantes] con UN acabado registrado -> acabado 1: %s, acabado 2: %s, "
                "defecto: %s\n",
                firstRejected ? "RECHAZADO" : "acepta", secondRejected ? "RECHAZADO" : "acepta",
                defectCaught ? "detectado" : "SE COLÓ");

    const bool firstRejectedMixed = isAnomalous(goodFirst, mixed);
    const bool secondRejectedMixed = isAnomalous(goodSecond, mixed);
    const bool defectCaughtMixed = isAnomalous(defect, mixed);
    std::printf("  [variantes] con LOS DOS mezclados     -> acabado 1: %s, acabado 2: %s, "
                "defecto: %s\n",
                firstRejectedMixed ? "RECHAZADO" : "acepta",
                secondRejectedMixed ? "RECHAZADO" : "acepta",
                defectCaughtMixed ? "detectado" : "SE COLÓ");

    // No se afirma todavía qué debería pasar: lo que se quiere de esta prueba es
    // el CUADRO, para decidir con él. Lo único que se exige es que el montaje
    // sea el que se cree — un acabado registrado, el otro no, y un defecto que
    // se distingue de los dos.
    EXPECT_TRUE(defectCaught)
        << "ni siquiera con un solo acabado se detecta el defecto: el montaje no sirve "
           "para medir nada";
    EXPECT_TRUE(secondRejected)
        << "el segundo acabado pasa sin haberlo registrado: entonces no hay problema que "
           "resolver y este fichero sobra";
}

// Y LO QUE PASA AL NO MEZCLARLOS: cada variante conserva su media y su banda, y
// la pieza es buena si ALGUNA la reconoce.
TEST(Variants, KeepingThemApartRestoresTheGrip) {
    std::vector<std::vector<float>> first;
    std::vector<std::vector<float>> second;
    std::vector<std::vector<float>> mixedSamples;
    for (int i = 0; i < 15; ++i) {
        first.push_back(embeddingFor(0.0, i));
        mixedSamples.push_back(embeddingFor(0.0, i));
    }
    for (int i = 0; i < 15; ++i) {
        second.push_back(embeddingFor(1.0, 100 + i));
        mixedSamples.push_back(embeddingFor(1.0, 100 + i));
    }

    const std::vector<pci::ml::Reference> variants = {buildFrom(first), buildFrom(second)};
    const Reference mixed = buildFrom(mixedSamples);

    const auto goodFirst = embeddingFor(0.0, 900);
    const auto goodSecond = embeddingFor(1.0, 901);
    const auto defect = defectiveEmbedding(700);

    const auto matchFirst = pci::ml::matchVariants(goodFirst, variants);
    const auto matchSecond = pci::ml::matchVariants(goodSecond, variants);
    const auto matchDefect = pci::ml::matchVariants(defect, variants);

    std::printf("  [variantes] separadas -> acabado 1: %s (variante %d, %.4f), "
                "acabado 2: %s (variante %d, %.4f), defecto: %s (%.4f)\n",
                matchFirst.anomalous ? "RECHAZADO" : "acepta", matchFirst.index,
                matchFirst.similarity, matchSecond.anomalous ? "RECHAZADO" : "acepta",
                matchSecond.index, matchSecond.similarity,
                matchDefect.anomalous ? "detectado" : "SE COLÓ", matchDefect.similarity);

    // Las dos variantes buenas se aceptan, y cada una por la SUYA: si las dos
    // las reconociera la misma, no habría dos grupos y esto no haría falta.
    EXPECT_FALSE(matchFirst.anomalous);
    EXPECT_FALSE(matchSecond.anomalous);
    EXPECT_EQ(matchFirst.index, 0);
    EXPECT_EQ(matchSecond.index, 1)
        << "las dos variantes se aceptan por la misma referencia: entonces no eran dos "
           "grupos distintos y el montaje no mide lo que dice";

    // Y EL DEFECTO SE VUELVE A DETECTAR, que es lo que se había perdido al
    // mezclar. Con la media mezclada puntuaba 0,9381 contra una banda de 0,6812
    // y se colaba.
    EXPECT_TRUE(matchDefect.anomalous)
        << "el defecto sigue colándose con las variantes separadas: entonces separarlas "
           "no arregla lo que se dijo que arreglaba";
    EXPECT_FALSE(isAnomalous(defect, mixed))
        << "esta prueba supone que el defecto SE COLABA con la referencia mezclada; si ya "
           "se detectaba, la comparación de arriba no demuestra nada";
}

// Sin ninguna variante registrada, la respuesta es «anómala» y no «buena».
//
// Dar por bueno lo que no se ha mirado es exactamente el fallo que este programa
// existe para evitar, y una lista vacía es el caso en que es más fácil colarlo.
TEST(Variants, WithNoReferenceNothingIsGood) {
    const auto anything = embeddingFor(0.3, 42);
    const auto match = pci::ml::matchVariants(anything, {});
    EXPECT_TRUE(match.anomalous)
        << "sin ninguna referencia da la pieza por buena: eso es aceptar sin haber "
           "comprobado nada";
    EXPECT_EQ(match.index, -1);
}

// Con UNA sola variante se comporta exactamente como antes. Quien no tenga
// variantes no puede notar que esto existe.
TEST(Variants, WithOneVariantItMatchesTheOldBehaviour) {
    std::vector<std::vector<float>> samples;
    for (int i = 0; i < 15; ++i) {
        samples.push_back(embeddingFor(0.0, i));
    }
    const Reference only = buildFrom(samples);

    for (const double finish : {0.0, 0.2, 0.6, 1.0}) {
        const auto sample = embeddingFor(finish, 777);
        const bool oldWay = isAnomalous(sample, only);
        const bool newWay = pci::ml::matchVariants(sample, {only}).anomalous;
        EXPECT_EQ(oldWay, newWay)
            << "con una sola variante la decisión cambia respecto a la de siempre, para "
               "acabado " << finish << ": quien no use variantes lo notaría";
    }
    std::printf("  [variantes] con una sola variante, la decisión es la de siempre\n");
}
