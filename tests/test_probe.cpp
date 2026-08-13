// Banco de `pci_probe`, el banco sin cámara.
//
// Lo que se prueba aquí NO es el pipeline —de eso ya se ocupan test_vision,
// test_shape_class y test_shape_measure— sino las dos cosas que solo existen en
// esta herramienta y que son su CONTRATO con quien la consume: qué acepta por la
// línea de órdenes y qué escribe.
//
// Importa porque `pci_probe` existe para correr desde un script: un argumento
// mal parseado que se ignora en silencio, o un JSON que un día deja de ser
// válido, convierten al banco en una fuente de datos falsos, y un banco en el
// que no se puede confiar es peor que no tenerlo.
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "../tools/probe_options.h"

using pci::probe::ProbeOptions;
using pci::probe::ProbeProposal;
using pci::probe::ProbeReport;
using pci::probe::parseProbeArgs;
using pci::probe::renderJson;
using pci::probe::renderText;

namespace {

pci::core::Result<ProbeOptions> parse(const std::vector<const char*>& args) {
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    argv.push_back("pci_probe");
    for (const char* arg : args) {
        argv.push_back(arg);
    }
    return parseProbeArgs(static_cast<int>(argv.size()), argv.data());
}

// ---------------------------------------------------------------------------
// Un validador de JSON de verdad, escrito aquí a mano
// ---------------------------------------------------------------------------
//
// A propósito no se usa `cv::FileStorage`: es tolerante con cosas que no son
// JSON y estricta con cosas que sí lo son, así que pasar por ella no
// demostraría que la salida sea JSON. Este recorre la gramática entera y, de
// paso, apunta el CAMINO de cada clave («pieza.area_px», «propuestas[].valor»),
// que es lo que permite comprobar que están los campos prometidos y no
// solamente que el texto parsea.
class JsonCheck {
public:
    explicit JsonCheck(std::string text) : text_(std::move(text)) {}

    bool parse() {
        skipSpace();
        if (!value("")) {
            return false;
        }
        skipSpace();
        if (at_ != text_.size()) {
            return fail("sobra texto tras el valor raiz");
        }
        return true;
    }

    [[nodiscard]] bool has(const std::string& path) const { return paths_.count(path) > 0; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] std::size_t keyCount() const { return paths_.size(); }

private:
    bool fail(const std::string& why) {
        if (error_.empty()) {
            error_ = why + " (en el byte " + std::to_string(at_) + ")";
        }
        return false;
    }

    void skipSpace() {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' ||
                text_[at_] == '\t')) {
            ++at_;
        }
    }

    bool eat(char expected) {
        if (at_ >= text_.size() || text_[at_] != expected) {
            return fail(std::string("se esperaba '") + expected + "'");
        }
        ++at_;
        return true;
    }

    bool value(const std::string& path) {
        if (at_ >= text_.size()) {
            return fail("valor vacio");
        }
        switch (text_[at_]) {
            case '{': return object(path);
            case '[': return array(path);
            case '"': {
                std::string ignored;
                return string(ignored);
            }
            case 't': return word("true");
            case 'f': return word("false");
            case 'n': return word("null");
            default: return number();
        }
    }

    bool word(const char* expected) {
        for (const char* c = expected; *c != '\0'; ++c) {
            if (at_ >= text_.size() || text_[at_] != *c) {
                return fail(std::string("literal mal escrito, se esperaba ") + expected);
            }
            ++at_;
        }
        return true;
    }

    bool object(const std::string& path) {
        if (!eat('{')) {
            return false;
        }
        skipSpace();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return true;
        }
        // Las claves repetidas se vigilan DENTRO de cada objeto, no en el camino
        // global: dos elementos de un array comparten camino
        // («propuestas[].nombre») y eso es lo normal, no un duplicado.
        std::set<std::string> seen;
        while (true) {
            skipSpace();
            std::string key;
            if (!string(key)) {
                return false;
            }
            const std::string child = path.empty() ? key : path + "." + key;
            if (!seen.insert(key).second) {
                // Una clave repetida en el mismo objeto es JSON legal y un
                // desastre práctico: cada parser se queda con una distinta.
                return fail("clave repetida: " + child);
            }
            paths_.insert(child);
            skipSpace();
            if (!eat(':')) {
                return false;
            }
            skipSpace();
            if (!value(child)) {
                return false;
            }
            skipSpace();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            return eat('}');
        }
    }

    bool array(const std::string& path) {
        if (!eat('[')) {
            return false;
        }
        skipSpace();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return true;
        }
        while (true) {
            skipSpace();
            if (!value(path + "[]")) {
                return false;
            }
            skipSpace();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            return eat(']');
        }
    }

    bool string(std::string& out) {
        out.clear();
        if (!eat('"')) {
            return false;
        }
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (at_ >= text_.size()) {
                    return fail("escape sin terminar");
                }
                const char escaped = text_[at_++];
                if (escaped == 'u') {
                    for (int i = 0; i < 4; ++i) {
                        if (at_ >= text_.size() || !std::isxdigit(
                                                       static_cast<unsigned char>(text_[at_]))) {
                            return fail("\\u sin cuatro hexadecimales");
                        }
                        ++at_;
                    }
                } else if (std::string("\"\\/bfnrt").find(escaped) == std::string::npos) {
                    return fail(std::string("escape no valido: \\") + escaped);
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                // Es justo lo que rompe a un consumidor: un salto de linea
                // crudo dentro de una cadena.
                return fail("caracter de control sin escapar dentro de una cadena");
            }
            out.push_back(c);
        }
        return fail("cadena sin cerrar");
    }

    bool number() {
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-') {
            ++at_;
        }
        if (at_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[at_])) == 0) {
            return fail("numero sin digitos (¿nan o inf?)");
        }
        while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_])) != 0) {
            ++at_;
        }
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            if (at_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[at_])) == 0) {
                return fail("punto decimal sin digitos detras");
            }
            while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_])) != 0) {
                ++at_;
            }
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
                ++at_;
            }
            if (at_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[at_])) == 0) {
                return fail("exponente sin digitos");
            }
            while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_])) != 0) {
                ++at_;
            }
        }
        return at_ > start;
    }

    std::string text_;
    std::size_t at_ = 0;
    std::set<std::string> paths_;
    std::string error_;
};

// Un informe completo y con datos plausibles: los tests de formato tienen que
// recorrer todas las ramas de la plantilla, y un informe a cero dejaría sin
// probar justo las partes opcionales (mm, propuestas, redondez).
ProbeReport fullReport() {
    ProbeReport report;
    report.source = "sample_images/pieza_demo.png";
    report.video = false;
    report.width = 640;
    report.height = 480;
    report.framesRead = 1;
    report.framesWithPiece = 1;
    report.ok = true;
    report.measured = true;
    report.areaPx = 14646.5;
    report.frameFraction = 0.0477;
    report.perimeterPx = 691.9;
    report.contourPoints = 689;
    report.pipelineContourPoints = 19;
    report.centroid = {319.4F, 239.4F};
    report.boxWidth = 149.0;
    report.boxHeight = 199.0;
    report.boxAngleDeg = -90.0;
    report.fixtureOrigin = {319.5F, 239.5F};
    report.fixtureAngleDeg = 0.0;
    report.anisotropy = 0.5;
    report.shapeKind = "poligono";
    report.shapeSides = 6;
    report.deviationPx = 1.96;
    report.shapeReason = "contorno de 6 lados rectos";
    report.times.segment = 2.3;
    report.times.contour = 0.5;
    report.times.fixture = 0.4;
    report.times.normalize = 0.7;
    report.times.tools = 20.1;
    report.times.total = 24.1;

    ProbeProposal ruler;
    ruler.name = "Largo total";
    ruler.type = "ruler";
    ruler.value = 199.0;
    ruler.isLength = true;
    ruler.reason = "Dimension general de la pieza.";
    ruler.detail = "L=199.0px";
    report.proposals.push_back(ruler);

    ProbeProposal angle;
    angle.name = "Angulo 3";
    angle.type = "angle";
    angle.value = 92.29;
    angle.isLength = false;
    angle.reason = "Esquina entre dos caras rectas.";
    angle.detail = "angulo=92.29";
    report.proposals.push_back(angle);
    return report;
}

// Los campos que la herramienta PROMETE. Están en una lista porque el contrato
// es la lista: quien quite uno tiene que venir aquí a borrarlo, que es
// exactamente el momento en el que se da cuenta de que hay scripts leyéndolo.
const std::vector<std::string>& promisedJsonPaths() {
    static const std::vector<std::string> paths{
        "fuente", "tipo_fuente", "ancho_px", "alto_px", "frames_analizados",
        "frames_con_pieza", "zona.x", "zona.y", "zona.w", "zona.h", "escala_mm_por_px", "ok",
        "mensaje", "pieza.area_px", "pieza.fraccion_frame", "pieza.perimetro_px",
        "pieza.contorno_puntos", "pieza.contorno_puntos_pipeline", "pieza.centroide_x",
        "pieza.centroide_y", "pieza.envolvente_ancho_px", "pieza.envolvente_alto_px",
        "pieza.envolvente_angulo_deg", "fixture.origen_x", "fixture.origen_y",
        "fixture.angulo_deg", "fixture.anisotropia", "figura.clase", "figura.lados",
        "figura.diametro_exterior_px", "figura.diametro_interior_px", "figura.redondez_px",
        "figura.desviacion_px", "figura.motivo", "propuestas[].nombre", "propuestas[].tipo",
        "propuestas[].valor", "propuestas[].es_longitud", "propuestas[].motivo",
        "propuestas[].lectura", "tiempos_ms.segmentar", "tiempos_ms.contorno",
        "tiempos_ms.fixture", "tiempos_ms.normalizar", "tiempos_ms.medir",
        "tiempos_ms.suma_etapas", "tiempos_ms.total"};
    return paths;
}

}  // namespace

// ---------------------------------------------------------------------------
// Argumentos: los que valen
// ---------------------------------------------------------------------------

TEST(ProbeArgs, TheDefaultsAreTheOnesDocumented) {
    // Si estos cambian sin querer, cambia lo que hace el banco en CI sin que
    // nadie toque el script que lo llama.
    auto parsed = parse({"imagen.png"});
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    const ProbeOptions& options = parsed.value();
    EXPECT_EQ(options.source, "imagen.png");
    EXPECT_FALSE(options.json);
    EXPECT_FALSE(options.measure);
    EXPECT_EQ(options.frames, 30) << "el valor documentado es 30 (~1 s a 30 fps)";
    EXPECT_EQ(options.zone.area(), 0) << "sin --zona se analiza el frame entero";
    EXPECT_FALSE(options.calibrated());
    EXPECT_DOUBLE_EQ(options.mmPerPixel(), 0.0) << "sin calibrar, todo queda en px";
}

TEST(ProbeArgs, EveryOptionLandsWhereItShould) {
    auto parsed = parse({"--medir", "--zona", "10,20,300,150", "video.mp4", "--frames", "7",
                         "--calibrar-largo", "200=50", "--json"});
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    const ProbeOptions& options = parsed.value();
    EXPECT_EQ(options.source, "video.mp4");
    EXPECT_TRUE(options.json);
    EXPECT_TRUE(options.measure);
    EXPECT_EQ(options.frames, 7);
    EXPECT_EQ(options.zone, cv::Rect(10, 20, 300, 150));
    ASSERT_TRUE(options.calibrated());
    EXPECT_DOUBLE_EQ(options.mmPerPixel(), 0.25) << "50 mm en 200 px son 0,25 mm/px";
}

TEST(ProbeArgs, TheValueOfAnOptionIsNotMistakenForTheSource) {
    // El fallo concreto que evita: si el bucle no se saltara el valor, «--zona
    // 0,0,10,10 a.png» tomaría «0,0,10,10» por fuente y luego se quejaría de que
    // «a.png» sobra. El banco analizaría un fichero que no existe.
    auto parsed = parse({"--zona", "0,0,10,10", "a.png"});
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    EXPECT_EQ(parsed.value().source, "a.png");
    EXPECT_EQ(parsed.value().zone, cv::Rect(0, 0, 10, 10));
}

// ---------------------------------------------------------------------------
// Argumentos: los que no valen
// ---------------------------------------------------------------------------

TEST(ProbeArgs, WithoutASourceThereIsNothingToAnalyse) {
    auto parsed = parse({});
    ASSERT_FALSE(parsed.isOk());
    EXPECT_NE(parsed.error().message.find("Falta"), std::string::npos);
    // El mensaje tiene que traer el uso: quien lo ve es alguien que acaba de
    // teclear mal la orden.
    EXPECT_NE(parsed.error().message.find("--zona"), std::string::npos);

    auto two = parse({"a.png", "b.png"});
    ASSERT_FALSE(two.isOk());
    EXPECT_NE(two.error().message.find("b.png"), std::string::npos)
        << "el motivo tiene que decir CUÁL sobra";
}

TEST(ProbeArgs, AMalformedZoneIsRefusedAndSaysWhy) {
    // Cada una de estas colaba de una forma distinta si el parseo fuera
    // permisivo, y todas terminan en el mismo sitio: analizar una zona que no
    // es la que se pidió y publicar los números como si lo fuera.
    const std::vector<std::vector<const char*>> bad{
        {"a.png", "--zona"},                  // sin valor
        {"a.png", "--zona", "1,2,3"},         // tres campos
        {"a.png", "--zona", "1,2,3,4,5"},     // cinco campos
        {"a.png", "--zona", "1,2,3,x"},       // no es un entero
        {"a.png", "--zona", "1,2,3,"},        // campo vacío
        {"a.png", "--zona", "10,10,0,50"},    // ancho cero: el pipeline la ignoraría
        {"a.png", "--zona", "10,10,50,-5"},   // alto negativo
        {"a.png", "--zona", "-1,0,50,50"},    // esquina negativa
        {"a.png", "--zona", "10 10 50 50"},   // separado por espacios
    };
    for (const auto& args : bad) {
        auto parsed = parse(args);
        ASSERT_FALSE(parsed.isOk()) << "coló: " << (args.size() > 2 ? args[2] : "(sin valor)");
        EXPECT_NE(parsed.error().message.find("zona"), std::string::npos)
            << "el motivo no nombra la opción: " << parsed.error().message;
    }
}

TEST(ProbeArgs, ABadCalibrationIsRefusedInsteadOfPublishingFalseMillimetres) {
    // Este es el peor de los errores silenciosos posibles en esta herramienta:
    // una escala mal leída no rompe nada, solo hace que todos los milímetros
    // que salen sean mentira.
    const std::vector<std::vector<const char*>> bad{
        {"a.png", "--calibrar-largo"},           // sin valor
        {"a.png", "--calibrar-largo", "240"},    // sin «=»
        {"a.png", "--calibrar-largo", "240="},   // sin mm
        {"a.png", "--calibrar-largo", "=50"},    // sin px
        {"a.png", "--calibrar-largo", "240=50=1"},  // dos «=»
        {"a.png", "--calibrar-largo", "-240=50"},   // px negativo
        {"a.png", "--calibrar-largo", "240=-50"},   // mm negativo
        {"a.png", "--calibrar-largo", "0=50"},      // divide por cero
        {"a.png", "--calibrar-largo", "240=cero"},  // no es un número
        {"a.png", "--calibrar-largo", "24 0=50"},   // basura con espacio
    };
    for (const auto& args : bad) {
        auto parsed = parse(args);
        ASSERT_FALSE(parsed.isOk()) << "coló: " << (args.size() > 2 ? args[2] : "(sin valor)");
        EXPECT_NE(parsed.error().message.find("calibrar-largo"), std::string::npos)
            << parsed.error().message;
    }
}

TEST(ProbeArgs, FramesMustBeAWholeNumberAboveZero) {
    for (const char* value : {"0", "-3", "dos", "3.5", "1e3", ""}) {
        auto parsed = parse({"v.mp4", "--frames", value});
        EXPECT_FALSE(parsed.isOk()) << "coló --frames " << value;
    }
    auto ok = parse({"v.mp4", "--frames", "1"});
    ASSERT_TRUE(ok.isOk()) << ok.error().message;
    EXPECT_EQ(ok.value().frames, 1) << "un frame es legítimo: es el mínimo, no un error";
}

TEST(ProbeArgs, AnUnknownOptionStopsTheRunAndIsNamed) {
    // Tragarse una opción desconocida es lo que hace que un script crea que pidió
    // algo que no pidió: `--medir-todo` se ignoraría y la salida no traería
    // propuestas, sin una sola queja.
    auto parsed = parse({"a.png", "--medir-todo"});
    ASSERT_FALSE(parsed.isOk());
    EXPECT_NE(parsed.error().message.find("--medir-todo"), std::string::npos);
}

// ---------------------------------------------------------------------------
// La salida JSON
// ---------------------------------------------------------------------------

TEST(ProbeJson, ItIsValidJsonAndCarriesEveryFieldItPromises) {
    JsonCheck check(renderJson(fullReport()));
    ASSERT_TRUE(check.parse()) << check.error();
    for (const auto& path : promisedJsonPaths()) {
        EXPECT_TRUE(check.has(path)) << "falta el campo prometido «" << path << "»";
    }
    EXPECT_GE(check.keyCount(), promisedJsonPaths().size());
}

TEST(ProbeJson, ItStaysValidWithNoPieceAndWithNoProposals) {
    // Las dos ramas que un script encuentra justo cuando algo va mal, que es
    // cuando más falta hace que la salida se pueda leer.
    ProbeReport empty;
    empty.source = "sin_pieza.png";
    empty.ok = false;
    empty.message = "No se encontró ninguna pieza en la imagen";
    JsonCheck check(renderJson(empty));
    ASSERT_TRUE(check.parse()) << check.error();
    EXPECT_TRUE(check.has("mensaje"));
    EXPECT_TRUE(check.has("propuestas"));
    EXPECT_FALSE(check.has("propuestas[].nombre")) << "no hay propuestas que describir";
    EXPECT_NE(renderJson(empty).find("\"ok\": false"), std::string::npos);
}

TEST(ProbeJson, TextThatWouldBreakTheJsonComesOutEscaped) {
    // Los motivos vienen del clasificador y de las herramientas, y hoy ya traen
    // comillas tipográficas, acentos y el símbolo Ø. Mañana uno traerá una
    // comilla recta o un salto de línea, y con eso basta para que el script que
    // lee la salida muera al parsear.
    ProbeReport report = fullReport();
    report.shapeReason = "dice \"redondo\" con \\ barra, salto\ny tabulador\t";
    report.source = "C:\\ruta\\con\\barras\\pieza.png";
    report.proposals.front().reason = "Ø 300 px — «casi» un disco";
    report.message = std::string("byte de control: ") + '\x01';

    const std::string json = renderJson(report);
    JsonCheck check(json);
    ASSERT_TRUE(check.parse()) << check.error();
    EXPECT_NE(json.find("\\\""), std::string::npos) << "la comilla tiene que ir escapada";
    EXPECT_NE(json.find("\\n"), std::string::npos) << "el salto de línea tiene que ir escapado";
    EXPECT_NE(json.find("\\u0001"), std::string::npos) << "el byte de control, en \\u";
    // El UTF-8 se copia tal cual: es JSON válido y así el script lo lee legible.
    EXPECT_NE(json.find("Ø 300 px"), std::string::npos);
}

TEST(ProbeJson, NoNanAndNoInfEverReachTheOutput) {
    // Un pipeline que se tuerce produce NaN, y «nan» dentro de un JSON no es
    // JSON: el consumidor no lee un número raro, se muere al parsear y el fallo
    // aparece en el sitio equivocado.
    ProbeReport report = fullReport();
    report.areaPx = std::numeric_limits<double>::quiet_NaN();
    report.times.total = std::numeric_limits<double>::infinity();
    report.perimeterPx = -std::numeric_limits<double>::infinity();

    const std::string json = renderJson(report);
    JsonCheck check(json);
    ASSERT_TRUE(check.parse()) << check.error();
    EXPECT_EQ(json.find("nan"), std::string::npos);
    EXPECT_EQ(json.find("inf"), std::string::npos);
}

TEST(ProbeJson, MillimetresAppearOnlyWhenThereIsAScaleAndOnlyForLengths) {
    // Sin escala no puede haber ni un milímetro en la salida; con escala, los
    // milímetros de un RECUENTO de lados o de un ángulo seguirían sin existir.
    ProbeReport report = fullReport();
    ASSERT_DOUBLE_EQ(report.mmPerPixel, 0.0);
    JsonCheck plain(renderJson(report));
    ASSERT_TRUE(plain.parse()) << plain.error();
    // Ni un campo en mm sin escala. La clave `escala_mm_por_px` sí está siempre,
    // valiendo 0: es la que dice que NO hay escala.
    for (const char* path : {"pieza.area_mm2", "pieza.perimetro_mm", "propuestas[].valor_mm",
                             "figura.diametro_exterior_mm"}) {
        EXPECT_FALSE(plain.has(path)) << "sin --calibrar-largo no puede haber «" << path << "»";
    }
    EXPECT_TRUE(plain.has("escala_mm_por_px"));

    report.mmPerPixel = 0.25;
    const std::string scaled = renderJson(report);
    JsonCheck check(scaled);
    ASSERT_TRUE(check.parse()) << check.error();
    EXPECT_TRUE(check.has("pieza.area_mm2"));
    EXPECT_TRUE(check.has("pieza.perimetro_mm"));
    EXPECT_TRUE(check.has("propuestas[].valor_mm"));
    // 199 px a 0,25 mm/px son exactamente 49,75 mm.
    EXPECT_NE(scaled.find("\"valor_mm\": 49.750000"), std::string::npos) << scaled;
    // Y el ángulo (92,29) no tiene mm: 92,29 * 0,25 = 23,07 no debe aparecer.
    EXPECT_EQ(scaled.find("23.07"), std::string::npos)
        << "un ángulo convertido a milímetros es un número inventado";
}

// ---------------------------------------------------------------------------
// La salida legible
// ---------------------------------------------------------------------------

TEST(ProbeText, WhenThereIsNoPieceItSaysWhyAndNotJustThatItFailed) {
    // «Error» a secas obliga a repetir el trabajo a mano. El motivo viene del
    // `Result` del pipeline y tiene que llegar entero hasta la pantalla.
    ProbeReport report;
    report.source = "vacia.png";
    report.width = 640;
    report.height = 480;
    report.ok = false;
    report.message = "La segmentación cubre casi toda la imagen (revisa fondo/iluminación)";

    const std::string text = renderText(report);
    EXPECT_NE(text.find("revisa fondo/iluminación"), std::string::npos);
    EXPECT_EQ(text.find("Propuestas"), std::string::npos)
        << "sin pieza no hay medidas que enseñar, y enseñar ceros sería peor";
    EXPECT_EQ(text.find("Figura"), std::string::npos);
}

TEST(ProbeText, EveryNumberComesWithItsUnitAndTheScaleNeverReplacesThePixels) {
    ProbeReport report = fullReport();
    report.mmPerPixel = 0.25;
    const std::string text = renderText(report);

    EXPECT_NE(text.find("0.250000 mm/px"), std::string::npos) << "la escala se declara";
    // Los px se conservan al lado de los mm: las tolerancias del proyecto viven
    // en px, y perder el número original obligaría a deshacer la cuenta a mano.
    EXPECT_NE(text.find("199.00 px = 49.75 mm"), std::string::npos) << text;
    EXPECT_NE(text.find("mm2"), std::string::npos) << "el área va en mm2, no en mm";
    EXPECT_NE(text.find("no es una longitud"), std::string::npos)
        << "el ángulo tiene que decir por qué no lleva mm";
}

TEST(ProbeText, TheTimeThatNobodyAttributesIsPrinted) {
    // La suma de las etapas y el total se miden por separado a propósito (ver
    // StageTimings). Si la diferencia no se imprime, un trozo de trabajo que no
    // está en ninguna etapa se queda invisible justo cuando se optimiza.
    ProbeReport report = fullReport();
    report.times.total = report.times.stagesSum() + 3.0;
    const std::string text = renderText(report);
    EXPECT_NE(text.find("sin atribuir 3.000"), std::string::npos) << text;
}

TEST(ProbeText, AVideoSaysHowManyFramesItLookedAtAndThatTheTimesAreAnAverage) {
    ProbeReport report = fullReport();
    report.video = true;
    report.framesRead = 30;
    report.framesWithPiece = 27;
    const std::string text = renderText(report);
    EXPECT_NE(text.find("30 analizados, 27 con pieza"), std::string::npos) << text;
    EXPECT_NE(text.find("media de 27 frames"), std::string::npos)
        << "un tiempo medio presentado como si fuera de un frame engaña al que optimiza";
}

TEST(ProbeText, TheTwoContourCountsAreBothShown) {
    // No es un detalle interno: `analyzeFrame` devuelve el contorno SIMPLIFICADO
    // y el clasificador de figuras necesita el denso. Sobre
    // sample_images/aruco_4x4_id0.png la diferencia decide la respuesta —12
    // puntos dan «circulo de Ø 844,8 px», los 2392 densos dan «poligono de 4
    // lados»—, así que los dos recuentos tienen que estar a la vista.
    const std::string text = renderText(fullReport());
    EXPECT_NE(text.find("689 puntos densos"), std::string::npos) << text;
    EXPECT_NE(text.find("19 ya simplificados"), std::string::npos) << text;
}

TEST(ProbeReportContract, OnlyLengthToolsAreConvertibleToMillimetres) {
    using pci::inspection::ToolType;
    using pci::probe::proposalIsLength;
    // Las cinco que `proposeTools` propone midiendo una distancia en px.
    EXPECT_TRUE(proposalIsLength(ToolType::Circle));
    EXPECT_TRUE(proposalIsLength(ToolType::Ruler));
    EXPECT_TRUE(proposalIsLength(ToolType::Caliper));
    EXPECT_TRUE(proposalIsLength(ToolType::Arc));
    EXPECT_TRUE(proposalIsLength(ToolType::Roundness));
    // Y las dos que no: su valor es un recuento y unos grados.
    EXPECT_FALSE(proposalIsLength(ToolType::Polygon)) << "el valor de Lados es cuántos hay";
    EXPECT_FALSE(proposalIsLength(ToolType::Angle)) << "un ángulo no cambia con la escala";
    // Lo que no se ha mirado se trata como no convertible: equivocarse hacia ese
    // lado deja una medida en px, que es incompleta pero cierta.
    EXPECT_FALSE(proposalIsLength(ToolType::Blob));
    EXPECT_FALSE(proposalIsLength(ToolType::Gear));
}
