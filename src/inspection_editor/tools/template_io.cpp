#include "inspection_editor/tools/template_io.h"

#include <opencv2/core.hpp>

#include <string>

#include "inspection_editor/tools/tool_geometry.h"

namespace pci::inspection {

namespace {

// cv::FileStorage en JSON es quisquilloso con las secuencias de mapas, así que
// la plantilla se guarda como campos planos numerados (tool_<i>_<campo>), que es
// el patrón robusto que ya usa el resto del proyecto (solo escalares).

std::string readString(const cv::FileNode& node, const std::string& key) {
    const cv::FileNode field = node[key];
    return field.isString() ? static_cast<std::string>(field) : std::string();
}

double readNumber(const cv::FileNode& node, const std::string& key, double fallback) {
    const cv::FileNode field = node[key];
    return (field.isReal() || field.isInt()) ? static_cast<double>(field) : fallback;
}

std::string prefix(std::size_t index) {
    return "tool_" + std::to_string(index) + "_";
}

}  // namespace

std::string exportTemplateJson(const std::vector<ToolConfig>& tools) {
    cv::FileStorage fs("{}", cv::FileStorage::WRITE | cv::FileStorage::MEMORY |
                                 cv::FileStorage::FORMAT_JSON);
    fs << "version" << 1;
    fs << "count" << static_cast<int>(tools.size());
    for (std::size_t i = 0; i < tools.size(); ++i) {
        const auto& tool = tools[i];
        const std::string p = prefix(i);
        // write() explícito para los campos string: el operator<< interpreta un
        // valor que empieza con '{' o '[' (como geometryJson) como apertura de
        // estructura y corrompe el archivo.
        fs.write(p + "type", std::string(toolTypeName(tool.type)));
        fs.write(p + "name", tool.name);
        fs.write(p + "geometry", tool.geometryJson);
        fs.write(p + "params", tool.paramsJson);
        fs << (p + "tol_min") << tool.toleranceMin;
        fs << (p + "tol_max") << tool.toleranceMax;
        fs << (p + "enabled") << (tool.enabled ? 1 : 0);
    }
    return fs.releaseAndGetString();
}

core::Result<std::vector<ToolConfig>> importTemplateJson(const std::string& json) {
    using ResultT = core::Result<std::vector<ToolConfig>>;
    try {
        cv::FileStorage fs(json, cv::FileStorage::READ | cv::FileStorage::MEMORY |
                                     cv::FileStorage::FORMAT_JSON);
        const cv::FileNode root = fs.root();
        const cv::FileNode countNode = root["count"];
        if (countNode.empty() || (!countNode.isInt() && !countNode.isReal())) {
            return ResultT::err("Archivo de plantilla inválido: falta el número de herramientas");
        }
        const int count = static_cast<int>(countNode.real());

        std::vector<ToolConfig> tools;
        for (int i = 0; i < count; ++i) {
            const std::string p = prefix(static_cast<std::size_t>(i));

            auto type = toolTypeFromName(readString(root, p + "type"));
            if (!type.isOk()) {
                return ResultT::err(type.error().message);
            }
            ToolConfig config;
            config.id = -1;  // herramienta nueva en la pieza/plantilla destino
            config.type = type.value();
            config.name = readString(root, p + "name");
            if (config.name.empty()) {
                return ResultT::err("Una herramienta del archivo no tiene nombre");
            }
            config.geometryJson = readString(root, p + "geometry");
            // La geometría debe ser coherente con el tipo declarado.
            if (auto geometry = geometryFromJson(config.type, config.geometryJson);
                !geometry.isOk()) {
                return ResultT::err("Herramienta '" + config.name +
                                    "': geometría inválida (" + geometry.error().message + ")");
            }
            const std::string params = readString(root, p + "params");
            config.paramsJson = params.empty() ? "{}" : params;
            config.toleranceMin = readNumber(root, p + "tol_min", 0.0);
            config.toleranceMax = readNumber(root, p + "tol_max", 1e9);
            config.enabled = readNumber(root, p + "enabled", 1.0) != 0.0;
            tools.push_back(std::move(config));
        }
        return ResultT::ok(std::move(tools));
    } catch (const cv::Exception& e) {
        return ResultT::err(std::string("Archivo de plantilla ilegible: ") + e.what());
    }
}

}  // namespace pci::inspection
