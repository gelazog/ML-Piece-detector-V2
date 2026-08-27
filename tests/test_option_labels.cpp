// UNA OPCIÓN NOMBRA LO QUE ES; NO PREGUNTA NI HABLA EN PRIMERA PERSONA.
//
// Segunda vuelta sobre la misma queja, y por eso hay guardia. La primera vez fue
// «los botones suenan como preguntas, en lugar de opciones para el usuario»; la
// segunda, con el ejemplo dentro: «sé más intuitivo con los botones de menús,
// como eso de "color lo elijo yo", en lugar "fondo de color manual, o
// automático", entre muchos otros diálogos».
//
// Las dos apuntan a lo mismo. Un desplegable rotulado
//
//     Claridad (lo habitual) / Color, detectado solo / Color, lo elijo yo
//
// obliga a reconstruir la conversación de la que salieron esas respuestas.
// Manual y automático son las palabras que este operador ya conoce de cualquier
// otro equipo del taller, y no hay ninguna razón para inventar otras:
//
//     Claridad (predeterminado)
//     Color del fondo (automático)
//     Color del fondo (manual)
//
// La regla que fija este fichero: un rótulo de OPCIÓN —lo que va en un
// desplegable, una casilla, un radio o el título de un grupo— nombra la cosa,
// en tercera persona. Ni interrogaciones, ni «yo», ni «tú».
//
// Los MENSAJES quedan fuera a propósito. «¿Sobre qué imagen quieres editar la
// plantilla?» es una pregunta de verdad, en un diálogo que pregunta, y ahí la
// primera persona es lo correcto. Prohibirla en todas partes sería cambiar un
// exceso por otro.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path sources() {
    for (const auto* candidate : {"src", "../src", "../../src", "../../../src"}) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(candidate) / "ui" / "theme.h", ec)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::filesystem::path> uiSources(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const auto* folder : {"ui", "inspection_editor"}) {
        for (const auto& entry : std::filesystem::directory_iterator(root / folder)) {
            if (entry.path().extension() == ".cpp") {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

}  // namespace

TEST(OptionLabels, NoOptionAsksAQuestionOrSpeaksInTheFirstPerson) {
    const std::filesystem::path root = sources();
    ASSERT_FALSE(root.empty()) << "no se encuentra src/: esta prueba no comprueba nada";

    // Los cuatro sitios donde vive un rótulo de opción. `setToolTip` NO está:
    // la explicación larga puede y debe tutear — es donde se cuenta cuándo usar
    // cada cosa y qué cuesta.
    const std::regex option(
        // Delimitador propio: con el corriente, el `)"` de `([^"]*)"`
        // cierra la cadena en bruto antes de tiempo.
        R"rx((?:addItem|QRadioButton|QCheckBox|QGroupBox)\(\s*tr\("([^"]*)")rx");
    // «yo» y «tú» sueltos, no dentro de otra palabra: si no, «mayor» y «tuerca»
    // saltarían solas.
    const std::vector<std::pair<std::regex, const char*>> banned = {
        {std::regex(R"(\?|¿)"), "es una pregunta"},
        {std::regex(R"((^|[ (,])(yo|mí|mi)([ ).,]|$))"), "habla en primera persona"},
        {std::regex(R"((^|[ (,])(tú|tu|te|quieres|puedes)([ ).,]|$))"), "tutea al operador"},
        {std::regex(R"(lo habitual|lo de siempre)"), "dice «lo habitual» en vez de «predeterminado»"},
    };

    int checked = 0;
    std::vector<std::string> offenders;
    for (const auto& file : uiSources(root)) {
        std::ifstream stream(file);
        std::ostringstream text;
        text << stream.rdbuf();
        const std::string source = text.str();
        for (auto it = std::sregex_iterator(source.begin(), source.end(), option);
             it != std::sregex_iterator(); ++it) {
            const std::string label = (*it)[1].str();
            if (label.empty()) {
                continue;
            }
            ++checked;
            for (const auto& [pattern, why] : banned) {
                if (std::regex_search(label, pattern)) {
                    offenders.push_back(file.filename().string() + ": «" + label + "» " + why);
                }
            }
        }
    }

    std::printf("  [rótulos] %d rótulos de opción revisados, %d fuera de estilo\n", checked,
                static_cast<int>(offenders.size()));
    // Sin esto la prueba pasaría en verde el día que alguien cambie cómo se
    // construyen los controles y el barrido no encuentre ninguno. Es el fallo
    // que ya mordió con `--smoke`: una comprobación que no comprueba nada.
    EXPECT_GT(checked, 40) << "el barrido apenas encuentra rótulos: la expresión ya no "
                              "reconoce cómo se construyen los controles";
    for (const auto& offender : offenders) {
        ADD_FAILURE() << offender;
    }
}
