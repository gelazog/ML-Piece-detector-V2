// LA MISMA DETECCIÓN EN TODOS LOS CAMINOS.
//
// Queja del taller: «sigue habiendo problemas con el color del fondo y el de la
// pieza, y el cómo detecta las piezas». Persiguiéndola aparecieron TRES sitios
// distintos que segmentaban con los valores de fábrica en vez de con los que el
// operador tenía configurados, y los tres por el mismo motivo: una llamada que
// se escribió sin pasar la configuración, y nadie volvió a mirarla.
//
//   onOpenEditorClicked     analizaba sin configuración para sacar el fixture
//                           con el que abrir el editor. Sobre una mesa de color
//                           eso no daba un fixture torcido: el editor NO ABRÍA,
//                           mientras la ventana enseñaba la pieza bien detectada
//                           al lado.
//   RegistrationWizard      construía su sesión sin configuración, así que
//                           APRENDÍA la pieza con la detección de fábrica —
//                           mientras el botón «Registrar y activar», que hace lo
//                           mismo, usaba la del operador.
//   DetectionProfiles       guardaba cuatro de los ocho campos, así que cargar
//                           un perfil devolvía a fábrica los otros cuatro.
//
// Ninguno falla ni avisa. Los tres detectan PEOR, en silencio, y desde fuera
// eso se vive como «el programa va peor desde hace un tiempo».
//
// Esta prueba no comprueba una llamada: comprueba que ningún camino de registro
// o de edición se quede sin la configuración. Es el patrón, no el caso.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

// Se ancla en un fichero conocido, no en la carpeta: `../src/ui` existe también
// dentro del árbol de compilación y está vacío. Este proyecto ya ha tropezado
// dos veces con eso.
std::filesystem::path sources() {
    for (const auto* candidate : {"src", "../src", "../../src", "../../../src"}) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(candidate) / "ui" / "theme.h", ec)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

TEST(SameDetectionEverywhere, NobodyAnalysesAFrameWithoutSayingHowToDetectIt) {
    const auto root = sources();
    ASSERT_FALSE(root.empty()) << "no se encuentra src/: la guardia no comprueba nada";

    // `analyzeFrame(x)` y `analyzeFrames(x)` con UN solo argumento: eso es
    // «detecta como venga de fábrica».
    const std::regex bare(R"(analyzeFrames?\s*\(\s*[^,;()]*(\([^()]*\))?[^,;()]*\)\s*[;,)])");

    struct Offence {
        std::string file;
        int line;
        std::string text;
    };
    std::vector<Offence> offences;
    int filesChecked = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".cpp") {
            continue;
        }
        // `vision/` es quien DEFINE el valor por defecto y quien lo prueba: ahí
        // llamar sin configuración es legítimo.
        const auto path = entry.path().string();
        if (path.find("vision") != std::string::npos) {
            continue;
        }
        ++filesChecked;
        std::ifstream file(entry.path());
        std::string line;
        int number = 0;
        while (std::getline(file, line)) {
            ++number;
            const auto comment = line.find("//");
            const std::string code =
                comment == std::string::npos ? line : line.substr(0, comment);
            if (std::regex_search(code, bare)) {
                offences.push_back(
                    {entry.path().filename().string(), number, code.substr(0, 88)});
            }
        }
    }

    std::printf("  [detección] %d ficheros revisados, %zu análisis sin configurar\n",
                filesChecked, offences.size());
    for (const auto& one : offences) {
        std::printf("  [detección]    %s:%d  %s\n", one.file.c_str(), one.line,
                    one.text.c_str());
    }
    EXPECT_GT(filesChecked, 40)
        << "casi no se ha revisado nada: la ruta no es la que se cree";
    EXPECT_TRUE(offences.empty())
        << "hay código que analiza un frame sin decir CÓMO detectar la pieza, así que usa "
           "los valores de fábrica. El operador ajusta la detección, la ve funcionar, y "
           "por este camino se detecta de otra manera. No falla ni avisa: detecta peor.";
}

TEST(SameDetectionEverywhere, TheRegistrationWizardIsGivenTheConfiguredDetection) {
    // El asistente SEGMENTA cada captura para sacar el recorte del que nace el
    // embedding de referencia. Aprender la pieza con una detección y luego
    // inspeccionarla con otra es comparar contra una referencia torcida.
    const auto root = sources();
    ASSERT_FALSE(root.empty());

    std::ifstream file(root / "ui" / "main_window.cpp");
    ASSERT_TRUE(file.is_open());
    std::string line;
    int built = 0;
    int withConfig = 0;
    std::string pending;
    while (std::getline(file, line)) {
        if (line.find("RegistrationWizard wizard(") != std::string::npos) {
            ++built;
            pending = line;
            continue;
        }
        if (!pending.empty()) {
            // La llamada se parte en dos líneas: la configuración va en la
            // segunda.
            if ((pending + line).find("inspectionConfig()") != std::string::npos) {
                ++withConfig;
            }
            pending.clear();
        }
    }
    std::printf("  [detección] asistente de registro: %d aperturas, %d con la "
                "configuración del operador\n",
                built, withConfig);
    ASSERT_GT(built, 0) << "no se encuentra ninguna apertura del asistente: la guardia no "
                           "estaría comprobando nada";
    EXPECT_EQ(withConfig, built)
        << "alguna apertura del asistente de registro no le pasa la detección configurada, "
           "así que aprenderá la pieza con los valores de fábrica mientras el botón de "
           "«Registrar y activar» —que hace lo mismo— usa los del operador.";
}
