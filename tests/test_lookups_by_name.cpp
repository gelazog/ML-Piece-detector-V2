// UNA PRUEBA QUE SE CAE AL REESCRIBIR UNA ETIQUETA DESANIMA A REESCRIBIRLA.
//
// Ha pasado CUATRO veces en este proyecto, y siempre igual: el taller pide que
// un rótulo diga mejor lo que hace, se reescribe, y se caen unas pruebas que no
// tienen nada que ver con ese rótulo — solo lo usaban para encontrar el control.
//
//     «¿Está el umbral cortando la pieza?»  ->  «Comprobar el corte»
//     «Separar por el color del fondo»      ->  «Separar por color»
//     «Usar lo que se ve ahora»             ->  «Usar el recuento detectado»
//     «Calibrar»                            ->  «Ca&librar»
//
// Ninguna de esas roturas señalaba un fallo. Todas costaban un rato y, peor,
// enseñaban a no tocar los rótulos — que es justo lo contrario de lo que se
// pedía.
//
// La diferencia está entre AFIRMAR sobre el texto y LOCALIZAR por el texto:
//
//     EXPECT_TRUE(aviso->text().contains("0,74"));   // bien: eso se comprueba
//     for (auto* b : ...) if (b->text() == "X") ok = b;  // mal: eso se busca
//
// Lo primero es el trabajo de la prueba. Lo segundo es una forma frágil de decir
// `findChild<QPushButton*>("okButton")`, que no lo lee nadie y puede quedarse
// quieto mientras el texto mejora.
//
// Esto es un TRINQUETE, como el de los colores escritos a mano: no exige
// arreglar los que hay, exige que no salga uno más. Se van bajando cuando una
// etiqueta se toca, que es cuando duelen.
//
// Van 36 -> 29 -> 16 -> 0. Los siete de la primera vuelta eran todas las
// ACCIONES DE MENÚ que se buscaban por su rótulo, y son las que más duelen: el
// taller pide a menudo que una entrada de menú diga mejor lo que hace y cada
// rótulo mejorado tiraba pruebas de otra cosa. Los trece de la segunda son las
// pastillas de estado y los botones de la ventana. Los dieciséis de la tercera
// son los que quedaban repartidos por los diálogos: Mover/Elegir en la paleta,
// la pausa del vídeo, «Vigilar estas cotas», la casilla del subpíxel, el par de
// radios del contador, el mapa de diferencias y el asistente de acabados.
//
// Y AL CONVERTIR EL ÚLTIMO TERCIO APARECIÓ EL OTRO DAÑO, que no es la
// fragilidad. Media docena de estas búsquedas comprobaba con el texto la cosa
// que había venido a comprobar:
//
//   - «existe una etiqueta que empieza por *Dónde difiere*» era como se
//     comprobaba que el mapa de diferencias APARECE — o sea que un rótulo
//     reescrito se leía como «con un defecto no sale el mapa».
//   - «hay un botón que dice *Zona* y tiene menú» daba por buena la mitad que
//     el test afirmaba: que la zona es UN control con su menú.
//   - «hay una etiqueta que dice *ya tienes*» comprobaba a la vez que el
//     diálogo no cierra en silencio y que explica por qué.
//
// Separadas —el control por su nombre, el texto por una aserción aparte— el
// fallo dice cuál de las dos cosas se rompió, y de paso enseña lo que decía.
//
// Y LA SEGUNDA VUELTA ENSEÑÓ POR QUÉ IMPORTA, en el acto. Al convertir la
// pastilla que dice cuántas piezas se ven, se apuntó a `modeChip`... que es la
// que dice el modo de medición. Las dos llevan la palabra «pieza», así que la
// búsqueda por texto se quedaba con la que llegara última — y eso ya estaba
// AVISADO en `main_window.cpp`, donde se le puso nombre a `piecesChip`
// precisamente porque «en cuanto apareció otra etiqueta que también dice pieza
// empezaron a leer la equivocada».
//
// O sea que estas pruebas no solo se rompen al mejorar un rótulo: pueden estar
// mirando el control equivocado sin que nadie se entere, porque el que leen
// también contiene la palabra.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path testsFolder() {
    for (const auto* candidate : {"tests", "../tests", "../../tests", "../../../tests"}) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(candidate) / "CMakeLists.txt",
                                    ec)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::string> linesOf(const std::filesystem::path& file) {
    std::ifstream stream(file);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST(LookupsByName, NoNewTestFindsAWidgetByTheWordsPrintedOnIt) {
    const std::filesystem::path folder = testsFolder();
    ASSERT_FALSE(folder.empty()) << "no se encuentra tests/: esta prueba no comprueba nada";

    // Un bucle sobre `findChildren` que COMPARA el texto y además se lleva el
    // widget fuera —lo asigna o lo devuelve— está localizando por texto.
    //
    // Las dos condiciones hacen falta. Sin la segunda entrarían las
    // comprobaciones legítimas, del tipo «alguna etiqueta de la paleta dice el
    // nombre de esta familia», que recorren igual pero no se llevan nada: lo que
    // hacen es afirmar.
    const std::regex loop(R"rx(for\s*\(\s*auto\*\s+(\w+)\s*:.*findChildren<)rx");
    const std::regex compares(R"rx(->text\(\)\s*(==|\.contains|\.startsWith|\.endsWith))rx");

    int found = 0;
    int filesScanned = 0;
    std::vector<std::string> where;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".cpp") {
            continue;
        }
        ++filesScanned;
        const auto lines = linesOf(entry.path());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::smatch match;
            if (!std::regex_search(lines[i], match, loop)) {
                continue;
            }
            const std::string var = match[1].str();
            // El cuerpo del bucle, acotado a lo que cabe leer de un vistazo.
            std::ostringstream body;
            for (std::size_t j = i; j < std::min(i + 10, lines.size()); ++j) {
                body << lines[j] << '\n';
            }
            const std::string text = body.str();
            const std::regex captures(R"(\w+\s*=\s*)" + var + R"(\s*;|return\s+)" + var +
                                      R"(\s*;)");
            if (std::regex_search(text, compares) && std::regex_search(text, captures)) {
                ++found;
                where.push_back(entry.path().filename().string() + ":" +
                                std::to_string(i + 1));
            }
        }
    }

    std::printf("  [texto] %d ficheros de prueba, %d controles localizados por su texto\n",
                filesScanned, found);
    // Que el barrido esté mirando de verdad. Sin esto, la prueba pasaría en
    // verde el día que la expresión dejara de reconocer cómo se escriben los
    // bucles — que es el fallo de `--smoke`, otra vez.
    ASSERT_GT(filesScanned, 40) << "apenas se leen ficheros de prueba: el barrido no está "
                                   "mirando donde cree";

    // EL TOPE ES EL DE HOY. Baja cuando se arregla uno; no sube nunca. Está en
    // cero: ya no queda ninguno, así que lo que vigila ahora es que no vuelva.
    constexpr int kToday = 0;
    EXPECT_LE(found, kToday)
        << "hay " << found << " controles localizados por su texto y el tope es " << kToday
        << ". Cada uno es una prueba que se caerá el día que alguien mejore ese "
           "rótulo, sin que haya ningún fallo — y que enseña a no mejorarlos. Usa "
           "`setObjectName` y `findChild<T*>(nombre)`.";
    if (found < kToday) {
        ADD_FAILURE() << "quedan " << found << " y el tope sigue en " << kToday
                      << ": baja `kToday` a " << found
                      << " para que no se puedan volver a colar. Un trinquete que no se "
                         "aprieta deja de serlo.";
    }
}
