#pragma once

#include <opencv2/core.hpp>

#include <functional>
#include <vector>

#include "core/result.h"

namespace pci::engine {

// Función de extracción de embeddings inyectable: en producción envuelve al
// EmbeddingExtractor (ONNX); en tests, embeddings sintéticos deterministas.
// Nula = modelo no disponible (la app degrada a solo herramientas).
using EmbedFn =
    std::function<core::Result<std::vector<float>>(const cv::Mat& normalizedBgr)>;

}  // namespace pci::engine
