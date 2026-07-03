#include "ml/reference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pci::ml {

double cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) {
        return 0.0;
    }
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        normA += static_cast<double>(a[i]) * a[i];
        normB += static_cast<double>(b[i]) * b[i];
    }
    if (normA <= 0.0 || normB <= 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

ReferenceBuilder::ReferenceBuilder(const Reference& existing) {
    if (existing.sampleCount <= 0 || existing.mean.empty()) {
        return;
    }
    count_ = existing.sampleCount;
    mean_.assign(existing.mean.begin(), existing.mean.end());
    m2_.resize(existing.mean.size(), 0.0);
    if (existing.stddev.size() == existing.mean.size() && count_ > 1) {
        for (std::size_t i = 0; i < m2_.size(); ++i) {
            m2_[i] = static_cast<double>(existing.stddev[i]) * existing.stddev[i] *
                     (count_ - 1);
        }
    }
    simMean_ = existing.simMean;
    simMin_ = existing.simMin;
    simCount_ = count_ > 1 ? count_ - 1 : 0;
    if (simCount_ > 1) {
        simM2_ = existing.simStd * existing.simStd * (simCount_ - 1);
    }
}

core::Result<void> ReferenceBuilder::add(const std::vector<float>& embedding) {
    if (embedding.empty()) {
        return core::Result<void>::err("Embedding vacío");
    }
    if (mean_.empty()) {
        mean_.resize(embedding.size(), 0.0);
        m2_.resize(embedding.size(), 0.0);
    } else if (mean_.size() != embedding.size()) {
        return core::Result<void>::err(
            "Dimensión de embedding incompatible con la referencia (" +
            std::to_string(embedding.size()) + " vs " + std::to_string(mean_.size()) + ")");
    }

    // Similitud contra la media vigente ANTES de incorporar la muestra;
    // la primera muestra no tiene media contra la cual medirse.
    if (count_ > 0) {
        const std::vector<float> currentMean(mean_.begin(), mean_.end());
        const double sim = cosineSimilarity(embedding, currentMean);
        simMin_ = std::min(simMin_, sim);
        ++simCount_;
        const double delta = sim - simMean_;
        simMean_ += delta / simCount_;
        simM2_ += delta * (sim - simMean_);
    }

    ++count_;
    for (std::size_t i = 0; i < mean_.size(); ++i) {
        const double delta = embedding[i] - mean_[i];
        mean_[i] += delta / count_;
        m2_[i] += delta * (embedding[i] - mean_[i]);
    }
    return core::Result<void>::ok();
}

core::Result<Reference> ReferenceBuilder::build() const {
    if (count_ < 2) {
        return core::Result<Reference>::err(
            "Se necesitan al menos 2 muestras para construir una referencia (hay " +
            std::to_string(count_) + ")");
    }

    Reference reference;
    reference.sampleCount = count_;
    reference.mean.assign(mean_.begin(), mean_.end());
    reference.stddev.resize(m2_.size());
    for (std::size_t i = 0; i < m2_.size(); ++i) {
        reference.stddev[i] = static_cast<float>(std::sqrt(m2_[i] / (count_ - 1)));
    }
    reference.simMean = simMean_;
    reference.simStd = simCount_ > 1 ? std::sqrt(simM2_ / (simCount_ - 1)) : 0.0;
    reference.simMin = simMin_;
    return core::Result<Reference>::ok(std::move(reference));
}

bool isAnomalous(const std::vector<float>& embedding, const Reference& reference,
                 double kSigma, double minBand) {
    const double sim = cosineSimilarity(embedding, reference.mean);
    const double band = std::max(kSigma * reference.simStd, minBand);
    return sim < reference.simMean - band;
}

}  // namespace pci::ml
