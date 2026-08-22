#pragma once

// Banco sin cámara: lo que `pci_probe` ACEPTA y lo que ESCRIBE.
//
// Vive fuera del `main` porque el contrato de esta herramienta no es lo que
// calcula por dentro —eso ya lo prueban los bancos de `vision` y de
// `inspection`— sino su línea de órdenes y su salida: es lo que consume un
// script de CI, y es justo lo que se puede romper sin que nadie se entere hasta
// que el script empieza a leer basura. Aquí está en funciones PURAS para poder
// probarlo sin abrir una imagen, un vídeo ni una cámara.
//
// Nada de excepciones: los errores salen por `Result`, como en el resto del
// proyecto. Un banco que aborta no informa de nada.

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/result.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::probe {

// Códigos de salida. Existen separados a propósito: un banco que devuelve
// siempre 0 no sirve para CI, y uno que devuelve siempre 1 obliga a leer el
// texto para saber si el fallo fue del fichero o de la pieza.
enum ExitCode {
    kExitOk = 0,
    kExitBadArguments = 2,
    kExitCannotOpen = 3,
    kExitNoPiece = 4,
};

inline const char* probeUsage() {
    return "Uso: pci_probe <imagen|video> [--json] [--calibrar-largo PX=MM] [--medir]\n"
           "                 [--zona x,y,w,h] [--frames N] [--subpixel]\n"
           "Salida: 0 analizado, 2 argumentos, 3 no se pudo abrir, 4 sin pieza.\n";
}

struct ProbeOptions {
    std::string source;   // imagen o vídeo; obligatorio
    bool json = false;    // misma información, en JSON de una sola pieza
    bool measure = false; // añadir las propuestas de medición
    // Afinado subpíxel del borde. Está aquí para poder COMPARAR sobre el
    // material de cada cual antes de encenderlo en la aplicación: la misma
    // imagen con y sin la bandera dice, en números, qué cambia.
    bool subpixel = false;

    // Calibración por objeto de referencia (`--calibrar-largo PX=MM`): una
    // distancia medida en píxeles cuyo tamaño real se conoce. Se guardan los dos
    // números sin combinarlos porque `calibrationFromKnownLength` necesita
    // además el ancho del frame, que aquí todavía no se conoce.
    double calibrationPx = 0.0;
    double calibrationMm = 0.0;

    cv::Rect zone;  // vacía = el frame entero

    // Solo aplica a vídeo. 30 ≈ un segundo a 30 fps: bastante para ver si la
    // medida se mueve entre frames, y lo bastante corto para que el banco siga
    // costando menos de lo que tarda uno en leer la salida.
    int frames = 30;

    [[nodiscard]] bool calibrated() const {
        return calibrationPx > 0.0 && calibrationMm > 0.0;
    }
    [[nodiscard]] double mmPerPixel() const {
        return calibrated() ? calibrationMm / calibrationPx : 0.0;
    }
};

namespace detail {

// `strtod`/`strtol` exigiendo que se consuma la cadena ENTERA. `std::stod`
// acepta "12abc" y devuelve 12, y además lanza cuando no puede: las dos cosas
// están mal aquí. Un argumento a medias tiene que ser un error con nombre, no un
// número truncado en silencio.
inline bool parseNumber(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

inline bool parseInteger(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value < -2147483647L || value > 2147483647L) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

inline std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

}  // namespace detail

inline core::Result<ProbeOptions> parseProbeArgs(int argc, const char* const* argv) {
    using core::Result;
    ProbeOptions options;
    bool haveSource = false;

    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            return Result<ProbeOptions>::err("Argumento nulo en la posición " +
                                             std::to_string(i) + ".");
        }
        const std::string arg = argv[i];

        // El valor de una opción es SIEMPRE el argumento siguiente. Se avanza
        // `i` aquí para que el bucle no lo vuelva a leer como si fuera la
        // fuente: sin esto, `--zona 0,0,10,10 a.png` tomaría la zona por fuente.
        const auto nextValue = [&](std::string& into) -> bool {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                return false;
            }
            into = argv[i + 1];
            ++i;
            return true;
        };

        if (arg == "--json") {
            options.json = true;
        } else if (arg == "--medir") {
            options.measure = true;
        } else if (arg == "--subpixel") {
            options.subpixel = true;
        } else if (arg == "--calibrar-largo") {
            std::string value;
            if (!nextValue(value)) {
                return Result<ProbeOptions>::err(
                    "«--calibrar-largo» necesita un valor PX=MM (por ejemplo 240=50).");
            }
            const auto parts = detail::split(value, '=');
            if (parts.size() != 2) {
                return Result<ProbeOptions>::err(
                    "«--calibrar-largo» espera PX=MM con un solo «=» (por ejemplo 240=50); "
                    "llegó «" + value + "».");
            }
            double px = 0.0;
            double mm = 0.0;
            if (!detail::parseNumber(parts[0], px) || !detail::parseNumber(parts[1], mm)) {
                return Result<ProbeOptions>::err(
                    "«--calibrar-largo» espera dos números PX=MM; llegó «" + value + "».");
            }
            if (px <= 0.0 || mm <= 0.0) {
                // Una escala negativa o cero no es una escala: dividiría por cero
                // o daría milímetros con signo. Mejor parar aquí que publicar
                // medidas imposibles.
                return Result<ProbeOptions>::err(
                    "«--calibrar-largo» necesita px y mm mayores que cero; llegó «" + value +
                    "».");
            }
            options.calibrationPx = px;
            options.calibrationMm = mm;
        } else if (arg == "--zona") {
            std::string value;
            if (!nextValue(value)) {
                return Result<ProbeOptions>::err(
                    "«--zona» necesita un valor x,y,w,h en píxeles (por ejemplo 10,10,200,150).");
            }
            const auto parts = detail::split(value, ',');
            if (parts.size() != 4) {
                return Result<ProbeOptions>::err(
                    "«--zona» espera cuatro números x,y,w,h; llegó «" + value + "» (" +
                    std::to_string(parts.size()) + " campos).");
            }
            int numbers[4] = {0, 0, 0, 0};
            for (std::size_t k = 0; k < 4; ++k) {
                if (!detail::parseInteger(parts[k], numbers[k])) {
                    return Result<ProbeOptions>::err("«--zona»: «" + parts[k] +
                                                     "» no es un entero.");
                }
            }
            if (numbers[0] < 0 || numbers[1] < 0) {
                return Result<ProbeOptions>::err(
                    "«--zona» no admite esquina negativa; llegó x=" + std::to_string(numbers[0]) +
                    " y=" + std::to_string(numbers[1]) + ".");
            }
            if (numbers[2] <= 0 || numbers[3] <= 0) {
                // Un rectángulo de área cero se cruza a nada con el frame, y el
                // pipeline lo trata como «sin zona»: la orden se cumpliría al
                // revés de como se pidió.
                return Result<ProbeOptions>::err(
                    "«--zona» necesita ancho y alto mayores que cero; llegó w=" +
                    std::to_string(numbers[2]) + " h=" + std::to_string(numbers[3]) + ".");
            }
            options.zone = cv::Rect(numbers[0], numbers[1], numbers[2], numbers[3]);
        } else if (arg == "--frames") {
            std::string value;
            if (!nextValue(value)) {
                return Result<ProbeOptions>::err("«--frames» necesita un número de frames.");
            }
            int frames = 0;
            if (!detail::parseInteger(value, frames)) {
                return Result<ProbeOptions>::err("«--frames»: «" + value +
                                                 "» no es un entero.");
            }
            if (frames <= 0) {
                return Result<ProbeOptions>::err(
                    "«--frames» necesita un entero mayor que cero; llegó " +
                    std::to_string(frames) + ".");
            }
            options.frames = frames;
        } else if (!arg.empty() && arg[0] == '-') {
            return Result<ProbeOptions>::err("Opción desconocida «" + arg + "».\n" +
                                             probeUsage());
        } else if (!haveSource) {
            options.source = arg;
            haveSource = true;
        } else {
            // Dos fuentes es casi siempre una opción mal escrita que se comió su
            // valor. Analizar la primera en silencio dejaría al operador leyendo
            // los números del fichero equivocado.
            return Result<ProbeOptions>::err("Solo se analiza una fuente por ejecución; ya está "
                                             "«" + options.source + "» y llegó «" + arg + "».");
        }
    }

    if (!haveSource) {
        return Result<ProbeOptions>::err("Falta la imagen o el vídeo que analizar.\n" +
                                         std::string(probeUsage()));
    }
    return Result<ProbeOptions>::ok(std::move(options));
}

// ---------------------------------------------------------------------------
// Lo que se imprime
// ---------------------------------------------------------------------------

// ¿El valor principal de esta herramienta es una LONGITUD en píxeles?
//
// Hace falta para no escribir milímetros donde no los hay: el valor de `Lados`
// es un RECUENTO de caras y el de `Ángulo` son grados, y ninguno de los dos
// cambia con la escala. Multiplicar por mm/px un recuento de seis lados daría
// «1,25 mm de lados», que es exactamente el tipo de número que hace desconfiar
// de toda la salida.
//
// Se listan solo los tipos que `proposeTools` llega a proponer, y el resto cae
// en «no es longitud»: equivocarse hacia ese lado deja una medida en píxeles,
// que es incompleta pero cierta.
inline bool proposalIsLength(inspection::ToolType type) {
    switch (type) {
        case inspection::ToolType::Circle:     // diámetro
        case inspection::ToolType::Ruler:      // distancia
        case inspection::ToolType::Caliper:    // distancia entre dos bordes
        case inspection::ToolType::Arc:        // radio
        case inspection::ToolType::Roundness:  // separación radial
            return true;
        default:
            return false;
    }
}

struct ProbeProposal {
    std::string name;
    std::string type;
    double value = 0.0;    // tal como lo da la herramienta: px, grados o recuento
    bool isLength = true;  // false = no se convierte a mm
    std::string reason;
    std::string detail;
};

struct ProbeStageTimes {
    double segment = 0.0;
    double contour = 0.0;
    double fixture = 0.0;
    double normalize = 0.0;
    double tools = 0.0;
    double total = 0.0;

    [[nodiscard]] double stagesSum() const {
        return segment + contour + fixture + normalize + tools;
    }
};

struct ProbeReport {
    std::string source;
    // Si el analisis uso el afinado subpixel del borde. Va en el informe y no
    // solo en las opciones porque quien lea dos salidas tiene que poder saber
    // cual es cual sin acordarse de que bandera puso.
    bool subpixel = false;
    bool video = false;
    int width = 0;
    int height = 0;
    int framesRead = 0;       // cuántos frames se llegaron a analizar
    int framesWithPiece = 0;  // en cuántos de ellos había pieza

    bool ok = false;
    std::string message;  // por qué no hay pieza, cuando `ok` es false

    cv::Rect zone;              // vacía = frame entero
    double mmPerPixel = 0.0;    // 0 = sin calibrar

    // Segmentación
    double areaPx = 0.0;
    double frameFraction = 0.0;
    double perimeterPx = 0.0;
    // Dos recuentos y no uno porque NO son el mismo contorno, y la diferencia
    // cambia la respuesta: `analyzeFrame` devuelve el contorno ya simplificado
    // (CHAIN_APPROX_SIMPLE, que de un cuadrado deja cuatro vértices) y el
    // clasificador de figuras necesita el denso. Verlos juntos es lo que
    // permite entender una clasificación rara en vez de creérsela.
    int contourPoints = 0;
    int pipelineContourPoints = 0;
    cv::Point2f centroid{0.0F, 0.0F};
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    double boxAngleDeg = 0.0;

    // Fixture
    cv::Point2f fixtureOrigin{0.0F, 0.0F};
    double fixtureAngleDeg = 0.0;
    double anisotropy = 0.0;

    // Figura reconocida
    std::string shapeKind = "irregular";
    int shapeSides = 0;
    double outerDiameterPx = 0.0;
    double innerDiameterPx = 0.0;
    double roundnessPx = 0.0;
    double deviationPx = 0.0;
    std::string shapeReason;

    bool measured = false;  // se pidió --medir
    std::vector<ProbeProposal> proposals;

    ProbeStageTimes times;
};

namespace detail {

inline std::string fmt(const char* format, double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

// El JSON no admite `nan` ni `inf`, y un pipeline que se tuerce los produce. Si
// se colaran, el script que consume la salida moriría al parsear en vez de leer
// un número raro y avisar, que es lo que interesa.
inline std::string jsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "0";
    }
    // Seis decimales y no dos: aquí también va la escala en mm/px, y con dos
    // decimales una escala de 0,0025 mm/px se publicaría como 0,00.
    return fmt("%.6f", value);
}

inline std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;  // el UTF-8 pasa tal cual: es JSON válido
                }
                break;
        }
    }
    out += "\"";
    return out;
}

}  // namespace detail

// Informe legible. Todo lo que se imprime lleva su unidad y, cuando hay escala,
// su equivalente en milímetros al lado — nunca en lugar de los píxeles: las
// tolerancias del proyecto viven en píxeles y perder el número original
// obligaría a deshacer la cuenta a mano.
inline std::string renderText(const ProbeReport& report) {
    using detail::fmt;
    const bool scaled = report.mmPerPixel > 0.0;
    const auto mm = [&](double px, const char* format = " = %.2f mm") {
        return scaled ? fmt(format, px * report.mmPerPixel) : std::string();
    };

    std::string out;
    out += "Fuente     : " + report.source + (report.video ? "  (vídeo)\n" : "  (imagen)\n");
    out += "Tamaño     : " + std::to_string(report.width) + " x " +
           std::to_string(report.height) + " px\n";
    if (report.video) {
        out += "Frames     : " + std::to_string(report.framesRead) + " analizados, " +
               std::to_string(report.framesWithPiece) + " con pieza\n";
    }
    if (report.zone.area() > 0) {
        out += "Zona       : x=" + std::to_string(report.zone.x) + " y=" +
               std::to_string(report.zone.y) + " w=" + std::to_string(report.zone.width) +
               " h=" + std::to_string(report.zone.height) + "\n";
    }
    if (scaled) {
        out += "Escala     : " + fmt("%.6f", report.mmPerPixel) + " mm/px\n";
    }

    if (!report.ok) {
        out += "\nSin pieza  : " + report.message + "\n";
        return out;
    }

    out += "\nSegmentación\n";
    out += "  Área      : " + fmt("%.1f", report.areaPx) + " px2 (" +
           fmt("%.2f", report.frameFraction * 100.0) + " % del frame)";
    if (scaled) {
        out += fmt(" = %.2f mm2", report.areaPx * report.mmPerPixel * report.mmPerPixel);
    }
    out += "\n";
    out += "  Perímetro : " + fmt("%.1f", report.perimeterPx) + " px" + mm(report.perimeterPx) +
           "\n";
    out += "  Envolvente: " + fmt("%.1f", report.boxWidth) + " x " +
           fmt("%.1f", report.boxHeight) + " px, girada " + fmt("%.1f", report.boxAngleDeg) +
           " grados\n";
    out += "  Centroide : (" + fmt("%.1f", report.centroid.x) + ", " +
           fmt("%.1f", report.centroid.y) + ") px\n";
    out += "  Contorno  : " + std::to_string(report.contourPoints) +
           " puntos densos (analyzeFrame devuelve " +
           std::to_string(report.pipelineContourPoints) + " ya simplificados)\n";

    out += "\nFixture\n";
    out += "  Origen    : (" + fmt("%.1f", report.fixtureOrigin.x) + ", " +
           fmt("%.1f", report.fixtureOrigin.y) + ") px\n";
    out += "  Ángulo    : " + fmt("%.2f", report.fixtureAngleDeg) + " grados (anisotropía " +
           fmt("%.3f", report.anisotropy) + ")\n";

    out += "\nFigura\n";
    out += "  Clase     : " + report.shapeKind;
    if (report.shapeSides > 0) {
        out += ", " + std::to_string(report.shapeSides) + " lados";
    }
    out += "\n";
    if (report.outerDiameterPx > 0.0) {
        out += "  Ø exterior: " + fmt("%.1f", report.outerDiameterPx) + " px" +
               mm(report.outerDiameterPx) + "\n";
    }
    if (report.innerDiameterPx > 0.0) {
        out += "  Ø interior: " + fmt("%.1f", report.innerDiameterPx) + " px" +
               mm(report.innerDiameterPx) + "\n";
    }
    if (report.roundnessPx > 0.0) {
        out += "  Redondez  : " + fmt("%.2f", report.roundnessPx) + " px" +
               mm(report.roundnessPx) + "\n";
    }
    out += "  Desviación: " + fmt("%.2f", report.deviationPx) + " px\n";
    out += "  Motivo    : " + report.shapeReason + "\n";

    if (report.measured) {
        out += "\nPropuestas de medición (" + std::to_string(report.proposals.size()) + ")\n";
        if (report.proposals.empty()) {
            out += "  (ninguna: ninguna herramienta consiguió medir sobre esta pieza)\n";
        }
        int index = 0;
        for (const auto& proposal : report.proposals) {
            ++index;
            out += "  " + std::to_string(index) + ". " + proposal.name + " [" + proposal.type +
                   "]  " + fmt("%.2f", proposal.value);
            if (proposal.isLength) {
                out += " px" + mm(proposal.value);
            } else {
                out += "  (no es una longitud: no se convierte a mm)";
            }
            out += "\n";
            out += "     motivo: " + proposal.reason + "\n";
            if (!proposal.detail.empty()) {
                out += "     lectura: " + proposal.detail + "\n";
            }
        }
    }

    out += "\nTiempos (ms";
    if (report.video) {
        out += ", media de " + std::to_string(report.framesWithPiece) + " frames con pieza";
    }
    out += ")\n";
    out += "  segmentar : " + fmt("%.3f", report.times.segment) + "\n";
    out += "  contorno  : " + fmt("%.3f", report.times.contour) + "\n";
    out += "  fixture   : " + fmt("%.3f", report.times.fixture) + "\n";
    out += "  normalizar: " + fmt("%.3f", report.times.normalize) + "\n";
    out += "  medir     : " + fmt("%.3f", report.times.tools) + "\n";
    out += "  suma      : " + fmt("%.3f", report.times.stagesSum()) + "\n";
    // La diferencia entre el total y la suma es trabajo que nadie está
    // atribuyendo a ninguna etapa. Escribirla es la única forma de que se vea.
    out += "  total     : " + fmt("%.3f", report.times.total) + "  (sin atribuir " +
           fmt("%.3f", report.times.total - report.times.stagesSum()) + ")\n";
    return out;
}

// El MISMO informe en JSON de una sola pieza. Los nombres van sin acentos a
// propósito: son claves que un script escribe a mano.
inline std::string renderJson(const ProbeReport& report) {
    using detail::jsonNumber;
    using detail::jsonString;
    const bool scaled = report.mmPerPixel > 0.0;

    std::string out = "{\n";
    out += "  \"fuente\": " + jsonString(report.source) + ",\n";
    out += "  \"tipo_fuente\": " + jsonString(report.video ? "video" : "imagen") + ",\n";
    out += "  \"ancho_px\": " + std::to_string(report.width) + ",\n";
    out += "  \"alto_px\": " + std::to_string(report.height) + ",\n";
    out += "  \"frames_analizados\": " + std::to_string(report.framesRead) + ",\n";
    out += "  \"frames_con_pieza\": " + std::to_string(report.framesWithPiece) + ",\n";
    out += "  \"zona\": {\"x\": " + std::to_string(report.zone.x) + ", \"y\": " +
           std::to_string(report.zone.y) + ", \"w\": " + std::to_string(report.zone.width) +
           ", \"h\": " + std::to_string(report.zone.height) + "},\n";
    out += "  \"escala_mm_por_px\": " + jsonNumber(report.mmPerPixel) + ",\n";
    out += "  \"ok\": " + std::string(report.ok ? "true" : "false") + ",\n";
    out += "  \"mensaje\": " + jsonString(report.message) + ",\n";

    out += "  \"pieza\": {\n";
    out += "    \"area_px\": " + jsonNumber(report.areaPx) + ",\n";
    out += "    \"fraccion_frame\": " + jsonNumber(report.frameFraction) + ",\n";
    out += "    \"perimetro_px\": " + jsonNumber(report.perimeterPx) + ",\n";
    out += "    \"contorno_puntos\": " + std::to_string(report.contourPoints) + ",\n";
    out += "    \"contorno_puntos_pipeline\": " + std::to_string(report.pipelineContourPoints) +
           ",\n";
    out += "    \"centroide_x\": " + jsonNumber(report.centroid.x) + ",\n";
    out += "    \"centroide_y\": " + jsonNumber(report.centroid.y) + ",\n";
    out += "    \"envolvente_ancho_px\": " + jsonNumber(report.boxWidth) + ",\n";
    out += "    \"envolvente_alto_px\": " + jsonNumber(report.boxHeight) + ",\n";
    out += "    \"envolvente_angulo_deg\": " + jsonNumber(report.boxAngleDeg);
    if (scaled) {
        out += ",\n    \"area_mm2\": " +
               jsonNumber(report.areaPx * report.mmPerPixel * report.mmPerPixel);
        out += ",\n    \"perimetro_mm\": " + jsonNumber(report.perimeterPx * report.mmPerPixel);
    }
    out += "\n  },\n";

    out += "  \"fixture\": {\n";
    out += "    \"origen_x\": " + jsonNumber(report.fixtureOrigin.x) + ",\n";
    out += "    \"origen_y\": " + jsonNumber(report.fixtureOrigin.y) + ",\n";
    out += "    \"angulo_deg\": " + jsonNumber(report.fixtureAngleDeg) + ",\n";
    out += "    \"anisotropia\": " + jsonNumber(report.anisotropy) + "\n";
    out += "  },\n";

    out += "  \"figura\": {\n";
    out += "    \"clase\": " + jsonString(report.shapeKind) + ",\n";
    out += "    \"lados\": " + std::to_string(report.shapeSides) + ",\n";
    out += "    \"diametro_exterior_px\": " + jsonNumber(report.outerDiameterPx) + ",\n";
    out += "    \"diametro_interior_px\": " + jsonNumber(report.innerDiameterPx) + ",\n";
    out += "    \"redondez_px\": " + jsonNumber(report.roundnessPx) + ",\n";
    out += "    \"desviacion_px\": " + jsonNumber(report.deviationPx) + ",\n";
    out += "    \"motivo\": " + jsonString(report.shapeReason);
    if (scaled && report.outerDiameterPx > 0.0) {
        out += ",\n    \"diametro_exterior_mm\": " +
               jsonNumber(report.outerDiameterPx * report.mmPerPixel);
    }
    out += "\n  },\n";

    out += "  \"propuestas\": [";
    for (std::size_t i = 0; i < report.proposals.size(); ++i) {
        const auto& proposal = report.proposals[i];
        out += (i == 0 ? "\n" : ",\n");
        out += "    {\n";
        out += "      \"nombre\": " + jsonString(proposal.name) + ",\n";
        out += "      \"tipo\": " + jsonString(proposal.type) + ",\n";
        out += "      \"valor\": " + jsonNumber(proposal.value) + ",\n";
        out += "      \"es_longitud\": " + std::string(proposal.isLength ? "true" : "false") +
               ",\n";
        if (scaled && proposal.isLength) {
            out += "      \"valor_mm\": " + jsonNumber(proposal.value * report.mmPerPixel) +
                   ",\n";
        }
        out += "      \"motivo\": " + jsonString(proposal.reason) + ",\n";
        out += "      \"lectura\": " + jsonString(proposal.detail) + "\n";
        out += "    }";
    }
    out += report.proposals.empty() ? "],\n" : "\n  ],\n";

    out += "  \"tiempos_ms\": {\n";
    out += "    \"segmentar\": " + jsonNumber(report.times.segment) + ",\n";
    out += "    \"contorno\": " + jsonNumber(report.times.contour) + ",\n";
    out += "    \"fixture\": " + jsonNumber(report.times.fixture) + ",\n";
    out += "    \"normalizar\": " + jsonNumber(report.times.normalize) + ",\n";
    out += "    \"medir\": " + jsonNumber(report.times.tools) + ",\n";
    out += "    \"suma_etapas\": " + jsonNumber(report.times.stagesSum()) + ",\n";
    out += "    \"total\": " + jsonNumber(report.times.total) + "\n";
    out += "  }\n";
    out += "}\n";
    return out;
}

}  // namespace pci::probe
