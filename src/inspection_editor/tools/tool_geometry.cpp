#include "inspection_editor/tools/tool_geometry.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <functional>
#include <type_traits>

namespace pci::inspection {

const char* toolTypeName(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return "caliper";
        case ToolType::Circle: return "circle";
        case ToolType::PointToLine: return "point_to_line";
        case ToolType::EdgeFlaw: return "edge_flaw";
        case ToolType::Blob: return "blob";
    }
    return "unknown";
}

core::Result<ToolType> toolTypeFromName(const std::string& name) {
    for (const ToolType type : {ToolType::Caliper, ToolType::Circle, ToolType::PointToLine,
                                ToolType::EdgeFlaw, ToolType::Blob}) {
        if (name == toolTypeName(type)) {
            return core::Result<ToolType>::ok(type);
        }
    }
    return core::Result<ToolType>::err("Tipo de herramienta desconocido: '" + name + "'");
}

const char* toolTypeDescription(ToolType type) {
    switch (type) {
        case ToolType::Caliper:
            return "Caliper — mide la distancia entre dos bordes (px).\n"
                   "Dibuja una línea que CRUCE perpendicularmente los dos bordes a medir\n"
                   "(p. ej. de lado a lado del ancho de un brazo o una ranura).";
        case ToolType::Circle:
            return "Círculo — mide el diámetro y la redondez de un contorno circular.\n"
                   "Arrastra desde el CENTRO del círculo (o agujero) hasta su borde;\n"
                   "el borde se busca en una banda alrededor de ese radio.";
        case ToolType::PointToLine:
            return "Punto-Línea — mide la distancia perpendicular de un borde a una\n"
                   "línea de referencia. Dibuja la línea de referencia; el escaneo que\n"
                   "localiza el borde queda perpendicular en su punto medio\n"
                   "(muévelo con Mover/Elegir si hace falta).";
        case ToolType::EdgeFlaw:
            return "Borde liso — detecta irregularidades (muescas, rebabas, golpes) en\n"
                   "un borde que debería ser recto. Dibuja una línea SOBRE el borde a\n"
                   "vigilar; se mide la desviación máxima respecto a la recta ideal.";
        case ToolType::Blob:
            return "Blob — cuenta manchas, agujeros o elementos dentro de una región.\n"
                   "Arrastra un rectángulo sobre la zona a vigilar; por defecto busca\n"
                   "elementos oscuros sobre fondo claro (área mínima 20 px²).";
    }
    return "";
}

void suggestTolerances(ToolType type, double measured, double& toleranceMin,
                       double& toleranceMax) {
    switch (type) {
        case ToolType::Blob:
            // Conteo: se exige exactamente lo que hay en la pieza buena.
            toleranceMin = measured;
            toleranceMax = measured;
            return;
        case ToolType::EdgeFlaw:
            // Desviación: la pieza buena define el piso; techo holgado.
            toleranceMin = 0.0;
            toleranceMax = std::max(measured * 1.5, 2.0);
            return;
        case ToolType::Caliper:
        case ToolType::Circle:
        case ToolType::PointToLine: {
            // Banda de ±10% con un mínimo de ±2 px para medidas pequeñas.
            const double band = std::max(measured * 0.10, 2.0);
            toleranceMin = std::max(0.0, measured - band);
            toleranceMax = measured + band;
            return;
        }
    }
}

ToolType typeOf(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> ToolType {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry>) {
                return ToolType::Caliper;
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return ToolType::Circle;
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return ToolType::PointToLine;
            } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                return ToolType::EdgeFlaw;
            } else {
                return ToolType::Blob;
            }
        },
        geometry);
}

namespace {

std::string writeJson(const std::function<void(cv::FileStorage&)>& body) {
    cv::FileStorage fs("{}", cv::FileStorage::WRITE | cv::FileStorage::MEMORY |
                                 cv::FileStorage::FORMAT_JSON);
    body(fs);
    return fs.releaseAndGetString();
}

// Lectura con validación: una clave ausente es un error controlado, no un 0.
class JsonReader {
public:
    explicit JsonReader(const std::string& json)
        : fs_(json,
              cv::FileStorage::READ | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON) {}

    core::Result<double> number(const char* key) {
        const cv::FileNode node = fs_[key];
        if (node.empty() || !node.isReal()) {
            if (node.empty() || !node.isInt()) {
                return core::Result<double>::err(std::string("Geometría corrupta: falta '") +
                                                 key + "'");
            }
        }
        return core::Result<double>::ok(static_cast<double>(node.real()));
    }

    // Clave opcional (campos añadidos después de la v1 del formato).
    double numberOr(const char* key, double fallback) {
        const cv::FileNode node = fs_[key];
        if (node.empty() || (!node.isReal() && !node.isInt())) {
            return fallback;
        }
        return static_cast<double>(node.real());
    }

private:
    cv::FileStorage fs_;
};

}  // namespace

std::string toJson(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::string {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y
                       << "band" << g.bandWidth;
                });
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "r" << g.radius << "band"
                       << g.searchBand << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "lax" << g.lineA.x << "lay" << g.lineA.y << "lbx" << g.lineB.x
                       << "lby" << g.lineB.y << "sax" << g.scanA.x << "say" << g.scanA.y
                       << "sbx" << g.scanB.x << "sby" << g.scanB.y;
                });
            } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y
                       << "scanLen" << g.scanLength << "scans" << g.scanCount;
                });
            } else {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "minArea" << g.minArea << "dark" << (g.darkBlobs ? 1 : 0);
                });
            }
        },
        geometry);
}

core::Result<ToolGeometry> geometryFromJson(ToolType type, const std::string& json) {
    using ResultT = core::Result<ToolGeometry>;

    try {
        JsonReader reader(json);
        auto f = [&reader](const char* key) { return reader.number(key); };

        switch (type) {
            case ToolType::Caliper: {
                CaliperGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1"), band = f("band");
                for (const auto* r : {&x0, &y0, &x1, &y1, &band}) {
                    if (!r->isOk()) return ResultT::err(r->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.bandWidth = static_cast<float>(band.value());
                return ResultT::ok(g);
            }
            case ToolType::Circle: {
                CircleGeometry g;
                auto cx = f("cx"), cy = f("cy"), r = f("r"), band = f("band");
                for (const auto* v : {&cx, &cy, &r, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.radius = static_cast<float>(r.value());
                g.searchBand = static_cast<float>(band.value());
                // "rays" llegó después: los JSON viejos usan el valor por defecto.
                g.rayCount = static_cast<int>(reader.numberOr("rays", g.rayCount));
                return ResultT::ok(g);
            }
            case ToolType::PointToLine: {
                PointToLineGeometry g;
                auto lax = f("lax"), lay = f("lay"), lbx = f("lbx"), lby = f("lby");
                auto sax = f("sax"), say = f("say"), sbx = f("sbx"), sby = f("sby");
                for (const auto* v : {&lax, &lay, &lbx, &lby, &sax, &say, &sbx, &sby}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.lineA = {static_cast<float>(lax.value()), static_cast<float>(lay.value())};
                g.lineB = {static_cast<float>(lbx.value()), static_cast<float>(lby.value())};
                g.scanA = {static_cast<float>(sax.value()), static_cast<float>(say.value())};
                g.scanB = {static_cast<float>(sbx.value()), static_cast<float>(sby.value())};
                return ResultT::ok(g);
            }
            case ToolType::EdgeFlaw: {
                EdgeFlawGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                auto len = f("scanLen"), scans = f("scans");
                for (const auto* v : {&x0, &y0, &x1, &y1, &len, &scans}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.scanLength = static_cast<float>(len.value());
                g.scanCount = static_cast<int>(scans.value());
                return ResultT::ok(g);
            }
            case ToolType::Blob: {
                BlobGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                auto minArea = f("minArea"), dark = f("dark");
                for (const auto* v : {&cx, &cy, &w, &h, &minArea, &dark}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.minArea = static_cast<float>(minArea.value());
                g.darkBlobs = dark.value() != 0.0;
                return ResultT::ok(g);
            }
        }
        return ResultT::err("Tipo de herramienta no soportado");
    } catch (const cv::Exception& e) {
        return ResultT::err(std::string("JSON de geometría inválido: ") + e.what());
    }
}

}  // namespace pci::inspection
