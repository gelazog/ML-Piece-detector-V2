// Herramienta de setup (no forma parte de la app): recorta el clasificador de
// un ONNX de clasificación para que la salida del grafo sean los features del
// último GlobalAveragePool (embedding real) en lugar del softmax de 1000
// clases. Fallback: entrada del Softmax final (logits). ONNX Runtime poda los
// nodos que quedan inalcanzables al cargar el modelo.
//
// Uso: prepare_model entrada.onnx salida.onnx

#include "onnx-ml.pb.h"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Uso: prepare_model entrada.onnx salida.onnx\n";
        return 1;
    }

    onnx::ModelProto model;
    {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input || !model.ParseFromIstream(&input)) {
            std::cerr << "No se pudo leer el modelo: " << argv[1] << "\n";
            return 1;
        }
    }

    auto* graph = model.mutable_graph();
    std::string target;
    std::string kind;

    // En exports de TF el GAP global suele ser un AveragePool con kernel del
    // tamaño espacial completo; en EfficientNet es el único pooling del grafo.
    for (const auto& node : graph->node()) {
        if ((node.op_type() == "GlobalAveragePool" || node.op_type() == "AveragePool") &&
            node.output_size() > 0) {
            target = node.output(0);
            kind = node.op_type();
        }
    }
    if (target.empty()) {
        for (const auto& node : graph->node()) {
            if (node.op_type() == "ReduceMean" && node.output_size() > 0) {
                target = node.output(0);
                kind = "ReduceMean";
            }
        }
    }
    if (target.empty()) {
        for (const auto& node : graph->node()) {
            if (node.op_type() == "Softmax" && node.input_size() > 0) {
                target = node.input(0);
                kind = "logits (entrada del Softmax)";
                break;
            }
        }
    }

    if (target.empty()) {
        std::cout << "No se encontró GAP/Softmax: se copia el modelo sin cambios\n";
    } else {
        graph->clear_output();
        auto* output = graph->add_output();
        output->set_name(target);
        // Solo el tipo de elemento; ONNX Runtime infiere la forma al cargar.
        output->mutable_type()->mutable_tensor_type()->set_elem_type(
            onnx::TensorProto::FLOAT);
        std::cout << "Nueva salida del modelo: " << target << " [" << kind << "]\n";
    }

    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output || !model.SerializeToOstream(&output)) {
        std::cerr << "No se pudo escribir: " << argv[2] << "\n";
        return 1;
    }
    return 0;
}
