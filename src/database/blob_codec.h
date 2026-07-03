#pragma once

#include <cstring>
#include <vector>

#include "core/result.h"

namespace pci::database {

// Serialización de vectores de embedding como BLOB de float32 en orden nativo
// (little-endian en x86/x64; la BD es local a una sola máquina).

inline std::vector<unsigned char> floatsToBlob(const std::vector<float>& values) {
    std::vector<unsigned char> blob(values.size() * sizeof(float));
    if (!values.empty()) {
        std::memcpy(blob.data(), values.data(), blob.size());
    }
    return blob;
}

inline core::Result<std::vector<float>> blobToFloats(const std::vector<unsigned char>& blob) {
    if (blob.size() % sizeof(float) != 0) {
        return core::Result<std::vector<float>>::err(
            "BLOB de embedding corrupto: tamaño no múltiplo de 4 (" +
            std::to_string(blob.size()) + " bytes)");
    }
    std::vector<float> values(blob.size() / sizeof(float));
    if (!values.empty()) {
        std::memcpy(values.data(), blob.data(), blob.size());
    }
    return core::Result<std::vector<float>>::ok(std::move(values));
}

}  // namespace pci::database
