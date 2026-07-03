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

}  // namespace pci::ml
