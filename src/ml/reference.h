#pragma once

#include <vector>

#include "core/result.h"

namespace pci::ml {

// Similitud coseno; 0.0 si los vectores están vacíos o difieren en tamaño.
// Invariante a escala: da igual si los vectores están L2-normalizados o no.
double cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

// Referencia estadística de una pieza: nunca se reentrena el modelo, solo se
// actualiza esta aritmética (y se versiona en la base de datos, fase 4).
struct Reference {
    std::vector<float> mean;    // promedio crudo de los embeddings
    std::vector<float> stddev;  // desviación estándar muestral por dimensión
    double simMean = 0.0;       // estadística de similitud de las muestras
    double simStd = 0.0;
    double simMin = 1.0;
    int sampleCount = 0;
};

// Acumulador de Welford: añadir una muestra cuesta O(dim) y no requiere
// conservar los embeddings anteriores — la misma pieza sirve para el registro
// inicial (30-100 fotos) y para el aprendizaje incremental de la fase 6.
// Nota: simMean/simStd se calculan contra la media vigente al momento de cada
// add(); es una estimación en flujo, suficiente para fijar tolerancias.
class ReferenceBuilder {
public:
    ReferenceBuilder() = default;

    // Continúa una referencia existente (aprendizaje incremental).
    explicit ReferenceBuilder(const Reference& existing);

    core::Result<void> add(const std::vector<float>& embedding);
    [[nodiscard]] core::Result<Reference> build() const;
    [[nodiscard]] int count() const { return count_; }

private:
    std::vector<double> mean_;
    std::vector<double> m2_;
    int count_ = 0;

    double simMean_ = 0.0;
    double simM2_ = 0.0;
    double simMin_ = 1.0;
    int simCount_ = 0;
};

// Anómalo si la similitud contra la media cae por debajo de
// simMean - max(kSigma * simStd, minBand). La banda mínima evita que una
// referencia de muestras casi idénticas (simStd ~ 0) rechace todo.
bool isAnomalous(const std::vector<float>& embedding, const Reference& reference,
                 double kSigma = 3.0, double minBand = 0.02);

// VARIANTES ADMISIBLES DE LA MISMA PIEZA.
//
// La referencia es una sola media, y eso da por supuesto que todas las piezas
// buenas se parecen entre si. En produccion no siempre: la misma pieza de dos
// proveedores, con dos acabados admisibles, o antes y despues de un cambio de
// lote, forma DOS grupos y no uno.
//
// Meterlos en la misma media no falla ruidosamente. Falla al reves, y es peor.
// Medido con dos acabados a 0,71 de parecido entre si:
//
//   un solo acabado registrado -> banda 0,9800, el defecto puntua 0,8481: se detecta
//   los dos mezclados          -> banda 0,6812, el defecto puntua 0,9381: SE COLO
//
// La media se coloca entre los dos grupos, asi que ninguna muestra se le parece
// del todo y la banda se ensancha hasta dejar de vigilar. La referencia no
// protesta: se queda CIEGA.
//
// La solucion es no mezclarlos: cada variante conserva su media y su banda, y
// una pieza es buena si ALGUNA de ellas la reconoce.
struct VariantMatch {
    // Que variante la ha reconocido, empezando por 0. -1 = ninguna.
    int index = -1;
    // Parecido contra la variante MAS parecida, se acepte o no. Es lo que hay
    // que enseñar: «se parece un 0,84 a la variante 2» explica un rechazo, y un
    // «no se parece a ninguna» no explica nada.
    double similarity = 0.0;
    bool anomalous = true;
};

// Una pieza es buena si alguna variante la reconoce. Con la lista vacia, la
// respuesta es «anomala» y no «buena»: sin ninguna referencia no se ha
// comprobado nada, y dar por bueno lo que no se ha mirado es el fallo que este
// programa existe para evitar.
[[nodiscard]] VariantMatch matchVariants(const std::vector<float>& embedding,
                                         const std::vector<Reference>& variants,
                                         double kSigma = 3.0, double minBand = 0.02);

}  // namespace pci::ml
