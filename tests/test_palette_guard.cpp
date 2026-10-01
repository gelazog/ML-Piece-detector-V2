// NADIE VUELVE A ESCRIBIR UN COLOR A MANO.
//
// Esta guardia existe porque el desorden que arregló la paleta no llegó de
// golpe: llegó un color cada vez, cada uno escrito en el sitio donde hacía
// falta, cada uno razonable por su cuenta. Al final había cuatro rojos para «no
// cumple» y cinco colores que no llegaban al contraste mínimo, y nadie lo sabía
// porque nadie los había visto juntos nunca.
//
// Una paleta sin guardia se erosiona igual. Un `#999` metido con prisa no lo ve
// ninguna revisión; esto sí.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

// SE ANCLA EN UN FICHERO, NO EN LA CARPETA.
//
// `../src/ui` existe también dentro del árbol de compilación —CMake crea ahí
// los directorios de objetos—, y está vacío de fuentes. La primera versión de
// esta guardia lo encontraba al correr bajo ctest, revisaba CERO ficheros y
// daba por bueno el resultado. Es la segunda vez que este proyecto tropieza con
// eso; la primera fue la guardia de rutas del README.
//
// Buscar `theme.h` dentro distingue la carpeta de verdad de su sombra.
std::filesystem::path uiSources() {
    for (const auto* candidate : {"src/ui", "../src/ui", "../../src/ui", "../../../src/ui"}) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(candidate) / "theme.h", ec)) {
            return std::filesystem::path(candidate);
        }
    }
    return {};
}

// TAMBIÉN EL EDITOR, y entero.
//
// La guardia miraba solo `src/ui`, y sin bajar a subcarpetas. El lienzo del
// editor vive en `src/inspection_editor/canvas/` y tenía 85 colores tecleados
// —más que todo `src/ui` junto cuando se escribió esta guardia— sin que nada
// los contara. Una guardia con un agujero de ese tamaño dice «0» y es mentira.
std::vector<std::filesystem::path> guardedFiles(const std::filesystem::path& ui) {
    std::vector<std::filesystem::path> files;
    for (const auto& dir : {ui, ui.parent_path() / "inspection_editor"}) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.path().extension() == ".cpp" || entry.path().extension() == ".h") {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

}  // namespace

TEST(PaletteGuard, NoHandWrittenColoursOutsideTheTheme) {
    const auto dir = uiSources();
    ASSERT_FALSE(dir.empty()) << "no se encuentra src/ui: la guardia no comprueba nada";
    int editorFiles = 0;

    // Un hexadecimal de color dentro de una cadena, o un QColor con números.
    const std::regex hexColour(R"(#[0-9a-fA-F]{3,8}\b)");
    const std::regex numericColour(R"(QColor\s*\(\s*\d+\s*,\s*\d+\s*,\s*\d+)");

    struct Offence {
        std::string file;
        int line;
        std::string text;
    };
    std::vector<Offence> offences;
    int filesChecked = 0;

    for (const auto& path : guardedFiles(dir)) {
        const auto name = path.filename().string();
        // `theme.h` es DONDE viven los colores: es el único sitio donde un
        // hexadecimal significa algo.
        if (name == "theme.h") {
            continue;
        }
        ++filesChecked;
        if (path.string().find("inspection_editor") != std::string::npos) {
            ++editorFiles;
        }
        std::ifstream file(path);
        std::string line;
        int number = 0;
        while (std::getline(file, line)) {
            ++number;
            // Los comentarios pueden citar un color al explicar por qué se
            // cambió; lo que importa es el código.
            const auto comment = line.find("//");
            std::string code = comment == std::string::npos ? line : line.substr(0, comment);
            // UNA ENTIDAD HTML NO ES UN COLOR, y esta guardia decía que sí.
            //
            // El indicador de estado pinta su punto con `&#9679;` —el carácter
            // «círculo negro»— dentro de un rich text cuyo color SÍ sale de un
            // token: `<span style='color:%1'>`. La expresión veía `#9679` y lo
            // contaba como un hexadecimal de cuatro cifras.
            //
            // Un trinquete con un falso positivo permanente es peor que uno más
            // flojo: quien venga a bajarlo se encuentra con que no puede, y deja
            // de creérselo.
            static const std::regex htmlEntity(R"(&#\d+;)");
            code = std::regex_replace(code, htmlEntity, "");
            if (std::regex_search(code, hexColour) || std::regex_search(code, numericColour)) {
                offences.push_back({name, number, code.substr(0, 90)});
            }
        }
    }

    std::printf("  [paleta] %d ficheros de interfaz revisados, %zu colores a mano\n",
                filesChecked, offences.size());
    EXPECT_GT(filesChecked, 10) << "casi no se ha revisado nada: la ruta no es la que se cree";
    EXPECT_GT(editorFiles, 5) << "no se ha revisado el editor, que es donde vive el lienzo";
    for (const auto& one : offences) {
        std::printf("           %s:%d  %s\n", one.file.c_str(), one.line, one.text.c_str());
    }
    // UN TRINQUETE, NO UN PORTAZO.
    //
    // Cuando esta guardia se escribió había 56 colores a mano repartidos por 55
    // ficheros. Convertirlos todos de una vez sería un cambio enorme tocando
    // diálogos que ahora mismo funcionan, y un cambio así se revisa mal.
    //
    // Así que se apunta el número y se prohíbe que SUBA. Los que se van
    // arreglando bajan la cuenta y hay que bajar también este tope: la deuda no
    // se puede pagar sola, pero tampoco puede crecer mientras nadie mira. Que es
    // exactamente como se llegó a tener cuatro rojos para «no cumple».
    //
    //   56 al escribir la guardia
    //   49 tras unificar los veredictos y los cinco que no contrastaban
    //   41 tras las pastillas de veredicto y la luz de estación
    //   ...
    //    9 tras las bandas que van sobre el vídeo (tres azules que eran uno)
    //    5 tras la rejilla del calibrador de lente
    //    1 tras el banner de veredicto y dos falsos positivos de esta guardia
    //    0 en `src/ui` y en el editor, que entra ahora en la cuenta con 54
    //      colores con papel (van a `ui/theme.h`).
    //
    // El último de `src/ui` era el aviso rojo de «el corte sí toca la pieza»,
    // que se dejó a mano porque mide MEJOR que los tokens (12,83:1 contra
    // 5,55:1). No se ha cambiado su valor: se le ha puesto nombre en el tema
    // (`kAlarmInk`, `kAlarmField`, `kAlarmEdge`). La regla era que el color
    // venga del tema, no que el tema gane siempre, y así se cumplen las dos.
    //
    // La tabla de color por tipo de herramienta del lienzo (`return {0, 200,
    // 255};`) no la ve esta expresión y está bien que no: es una identidad y no
    // un papel, y vive en una sola función.
    constexpr std::size_t kColoursStillHandWritten = 0;
    EXPECT_LE(offences.size(), kColoursStillHandWritten)
        << "han aparecido colores a mano nuevos fuera de ui/theme.h. Así se llegó a "
           "tener cuatro rojos distintos para «no cumple» y cinco colores por debajo "
           "del contraste mínimo: cada uno entró solo y parecía razonable.";
}
