// LAS MEDIDAS NO SE MUEVEN NI UN BIT AL ORDENAR EL CÓDIGO QUE LAS SACA.
//
// `tool_executor.cpp` tenía el mismo preámbulo —recuadro de pieza →
// cuadrilátero en imagen → máscara de Otsu dentro de él— copiado en nueve
// herramientas, y el mismo escaneo perpendicular a lo largo de un tramo en
// cuatro. Juntarlos en un ayudante es justo el tipo de cambio que mueve una
// medida sin que nadie lo vea: basta con que una copia calcule la posición de
// cada escaneo en otro orden de operaciones. Rebabas y mellas lo hace —
// `float(step * k)` en doble precisión donde las otras tres hacen
// `length * k / (scans - 1)` en float—, y sobre un tramo de 137,3 px con 60
// escaneos las dos fórmulas ponen el escaneo en otro sitio en 15 de los 60,
// por ocho millonésimas de píxel. Eso no lo ve ninguna prueba con margen, y es
// otra medida.
//
// Por eso aquí no hay margen: se ejecutan las herramientas tocadas sobre tres
// escenas dibujadas y sobre las fotos del banco, con cuatro encuadres (dos de
// ellos girados, para que el recuadro sea un cuadrilátero de verdad y no un
// rectángulo) y con la imagen tal cual e invertida, y se compara una huella de
// TODO lo que devuelven —valor, estado, texto, puntos y segmentos del dibujo,
// elemento derivado— contra la que daban antes de unificar: 19 herramientas,
// 456 ejecuciones sobre lo dibujado y 2584 sobre las fotos, y todas las
// tocadas llegan a medir en alguna. La huella es un FNV-1a de los bytes, así
// que un solo bit distinto en cualquier float cambia el número.
//
// Si un cambio DELIBERADO en una medida hace fallar esto, la prueba imprime la
// línea nueva lista para pegar; lo que no puede pasar es que cambie sin querer.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "sample_geometries.h"
#include "synthetic_scenes.h"

using namespace pci;
using namespace pci::inspection;

namespace {

// Las que pasan por los ayudantes nuevos, más las que usan π (que dejó de ir
// escrito a mano). Rebabas y mellas no está: su escaneo no se tocó.
const std::vector<ToolType> kTouched = {
    // recuadro → cuadrilátero → Otsu dentro
    ToolType::Blob, ToolType::PolyBlob, ToolType::Fillet, ToolType::Chamfer, ToolType::Extremes,
    ToolType::BoltPattern, ToolType::Clearance, ToolType::Polygon, ToolType::Symmetry,
    ToolType::Region,
    // escaneo a lo largo de un tramo
    ToolType::EdgeFlaw, ToolType::Orientation, ToolType::Straightness,
    // solo π
    ToolType::Angle, ToolType::LineToLine, ToolType::Circle, ToolType::Thread,
    ToolType::Roundness, ToolType::MedianAxis,
};

// Las de ejemplo, salvo las de tramo —cuya muestra es más corta que su número
// de escaneos y no llegaría a escanear— y el polígono libre, que aquí es
// cóncavo para que `fillPoly` tenga algo que hacer.
ToolGeometry geometryFor(ToolType type) {
    const cv::Point2f a(-100.0F, -120.0F);
    const cv::Point2f b(100.0F, -120.0F);
    switch (type) {
        case ToolType::EdgeFlaw: return EdgeFlawGeometry{a, b, 24.0F, 40};
        case ToolType::Straightness: return StraightnessGeometry{a, b, 24.0F, 60};
        case ToolType::Orientation: return OrientationGeometry{a, b, 24.0F, 60, 0.0F};
        case ToolType::PolyBlob:
            return PolyBlobGeometry{
                {{-90.0F, -70.0F}, {80.0F, -60.0F}, {20.0F, 0.0F}, {90.0F, 75.0F}, {-85.0F, 60.0F}},
                20.0F, true};
        default: return pci::inspection::testing_support::sampleGeometry(type);
    }
}

// Una placa con de todo: un chaflán, un acuerdo, una esquina viva y seis
// agujeros, con bordes suavizados para que los escaneos caigan entre píxeles.
cv::Mat plate() {
    cv::Mat frame(420, 420, CV_8UC1, cv::Scalar(30));
    std::vector<cv::Point> outline{{100, 140}, {140, 100}, {320, 100}};
    std::vector<cv::Point> arc;
    cv::ellipse2Poly(cv::Point(275, 275), cv::Size(45, 45), 0, 0, 90, 5, arc);
    outline.insert(outline.end(), arc.begin(), arc.end());
    outline.emplace_back(100, 320);
    cv::fillPoly(frame, std::vector<std::vector<cv::Point>>{outline}, cv::Scalar(225),
                 cv::LINE_AA);
    for (int k = 0; k < 6; ++k) {
        const double angle = k * CV_PI / 3.0;
        cv::circle(frame,
                   cv::Point(210 + static_cast<int>(std::lround(70.0 * std::cos(angle))),
                             210 + static_cast<int>(std::lround(70.0 * std::sin(angle)))),
                   12, cv::Scalar(30), cv::FILLED, cv::LINE_AA);
    }
    return frame;
}

// La de `test_no_silent_tool.cpp`: caras rectas, borde curvo y un agujero.
cv::Mat pieceWithEverything() {
    cv::Mat frame(420, 420, CV_8UC1, cv::Scalar(30));
    cv::rectangle(frame, cv::Rect(90, 90, 240, 240), cv::Scalar(225), cv::FILLED);
    cv::circle(frame, {330, 330}, 90, cv::Scalar(225), cv::FILLED);
    cv::circle(frame, {180, 180}, 34, cv::Scalar(30), cv::FILLED);
    return frame;
}

struct Fingerprint {
    std::uint64_t value = 14695981039346656037ULL;
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            value = (value ^ p[i]) * 1099511628211ULL;
        }
    }
    template <typename T>
    void add(const T& v) {
        bytes(&v, sizeof v);
    }
    void add(const cv::Point2f& p) {
        add(p.x);
        add(p.y);
    }
    void add(const ToolRunResult& r) {
        add(r.measured);
        add(r.ok);
        add(r.kind);
        add(r.informative);
        add(r.detail.size());
        bytes(r.detail.data(), r.detail.size());
        add(r.overlayPoints.size());
        for (const auto& p : r.overlayPoints) {
            add(p);
        }
        add(r.overlaySegments.size());
        for (const auto& s : r.overlaySegments) {
            add(s[0]);
            add(s[1]);
        }
        add(r.derived.kind);
        add(r.derived.point);
        add(r.derived.direction);
        add(r.derived.radius);
    }
};

// Huella de cada herramienta sobre todas las escenas, encuadres y polaridades.
std::map<std::string, std::uint64_t> fingerprints(const std::vector<cv::Mat>& scenes) {
    DerivedElements refs;
    refs["datum"].kind = DerivedKind::Line;
    refs["datum"].point = {0.0F, 0.0F};
    refs["datum"].direction = {1.0F, 0.0F};

    std::map<std::string, std::uint64_t> out;
    for (const ToolType type : kTouched) {
        ToolConfig config;
        config.id = 1;
        config.type = type;
        config.name = toolTypeName(type);
        config.geometryJson = toJson(geometryFor(type));
        if (type == ToolType::Orientation) {
            config.reference = "datum";
        }
        Fingerprint print;
        int measured = 0;
        int runs = 0;
        for (const cv::Mat& scene : scenes) {
            const float w = static_cast<float>(scene.cols);
            const float h = static_cast<float>(scene.rows);
            const vision::Fixture framings[] = {
                {{0.5F * w, 0.5F * h}, 0.0},
                {{0.5F * w + 0.37F, 0.5F * h + 0.61F}, 23.5},
                {{0.3F * w + 0.4F, 0.3F * h + 0.6F}, -61.0},
                {{0.7F * w + 0.25F, 0.7F * h + 0.15F}, 10.0},
            };
            for (const cv::Mat& image : {scene, cv::Mat(255 - scene)}) {
                for (const vision::Fixture& fixture : framings) {
                    const auto result = runTool(image, fixture, config, 0.0, LengthUnit::Pixels,
                                                cv::Mat(), nullptr, -1.0, &refs);
                    if (!result.isOk()) {
                        ADD_FAILURE() << config.name << ": " << result.error().message;
                        continue;
                    }
                    print.add(result.value());
                    measured += result.value().ok ? 1 : 0;
                    ++runs;
                }
            }
        }
        std::printf("  %-14s midió en %3d de %3d  huella 0x%016llxULL\n", config.name.c_str(),
                    measured, runs, static_cast<unsigned long long>(print.value));
        out[config.name] = print.value;
    }
    return out;
}

void expectFrozen(const std::map<std::string, std::uint64_t>& now,
                  const std::map<std::string, std::uint64_t>& frozen) {
    for (const auto& [name, value] : now) {
        const auto found = frozen.find(name);
        if (found == frozen.end() || found->second != value) {
            ADD_FAILURE() << name << " ya no da los mismos bits. Si el cambio es "
                          << "deliberado, la línea nueva es:\n        {\"" << name << "\", 0x"
                          << std::hex << value << "ULL},";
        }
    }
}

}  // namespace

TEST(FrozenMeasures, DrawnScenesGiveTheSameBits) {
    const std::vector<cv::Mat> scenes{pieceWithEverything(), plate(),
                                      pci::testing_support::gear().gray};
    const std::map<std::string, std::uint64_t> frozen{
        {"blob", 0x05aa60c35d9f4486ULL},
        {"poly_blob", 0xb51063659fe32c58ULL},
        {"fillet", 0x82f90d6329461757ULL},
        {"chamfer", 0x6ea61ee91e248b06ULL},
        {"extremes", 0x015e6e9f7abf512fULL},
        {"bolt_pattern", 0xf3503d4ec93f435fULL},
        {"clearance", 0xf97a48515e6cf979ULL},
        {"polygon", 0xe69b701bae7076e5ULL},
        {"symmetry", 0xae7bd07d2a0ffd8aULL},
        {"region", 0x7e68c174b56ce002ULL},
        {"edge_flaw", 0x9011f2478def9979ULL},
        {"orientation", 0x597f84c5519e1c13ULL},
        {"straightness", 0xea49e3e0bf9b0771ULL},
        {"angle", 0x222082c1eb0295edULL},
        {"line_to_line", 0x0a00c9b9b5891d45ULL},
        {"circle", 0x7250e6327e963f65ULL},
        {"thread", 0x825e909953813e45ULL},
        {"roundness", 0x6e1ff83508c551a3ULL},
        {"median_axis", 0x8942c0440441faf1ULL},
    };
    expectFrozen(fingerprints(scenes), frozen);
}

TEST(FrozenMeasures, BankPhotosGiveTheSameBits) {
    const std::filesystem::path bank("C:/Users/furro/Pictures/IMG-MC");
    const char* photos[] = {
        "Producto_Tuerca_Liv_02.jpg", "arandelas-1.png",   "arandelas-2.png",
        "arandelas-3.jpg",            "arandelas-4.png",   "arandelas-5.png",
        "engranaje-1.png",            "engranajes-1.jpg",  "producto-tuercas-prueba.jpg",
        "rosca-1.png",                "tablero-ajedrez-medida.png", "tornillo-1.png",
        "tornillo-2.png",             "tornillo-ojo-3.png", "tornillo-ojo-4.png",
        "tornillo-ojo-5.png",         "tornillos-1.png",
    };
    std::vector<cv::Mat> scenes;
    for (const char* photo : photos) {
        const cv::Mat gray = cv::imread((bank / photo).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            GTEST_SKIP() << "sin banco de fotos (falta " << photo << ")";
        }
        // A 420 px de lado mayor, el tamaño de las escenas dibujadas: así las
        // geometrías de ejemplo caen sobre la pieza y no en una esquina.
        const double factor = 420.0 / std::max(gray.cols, gray.rows);
        cv::Mat small;
        cv::resize(gray, small, cv::Size(), factor, factor, cv::INTER_AREA);
        scenes.push_back(small);
    }
    const std::map<std::string, std::uint64_t> frozen{
        {"blob", 0x043f8c780ce8240eULL},
        {"poly_blob", 0xed245605a8b60c38ULL},
        {"fillet", 0x7d62f8e447eabeffULL},
        {"chamfer", 0x5aa542abaa16fd3aULL},
        {"extremes", 0x0cb150209e322869ULL},
        {"bolt_pattern", 0x4ba67c59863ed38dULL},
        {"clearance", 0xdc1e9034df4f1c75ULL},
        {"polygon", 0xe4a62e002d0d7103ULL},
        {"symmetry", 0xb385c727216e778dULL},
        {"region", 0x4ac391a77913d663ULL},
        {"edge_flaw", 0x0053df3f70df0832ULL},
        {"orientation", 0xc9991b9798f848adULL},
        {"straightness", 0x160b641d8b52e507ULL},
        {"angle", 0xd8c8ac721f2b8ca9ULL},
        {"line_to_line", 0x8fcc2ab15a9a10adULL},
        {"circle", 0x2023c048b221aa8fULL},
        {"thread", 0xaba44521edcda3cdULL},
        {"roundness", 0xdf06fff49b074d2dULL},
        {"median_axis", 0xab985b7431393f0dULL},
    };
    expectFrozen(fingerprints(scenes), frozen);
}
