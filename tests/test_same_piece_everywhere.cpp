// LA MISMA PIEZA EN TODOS LOS CAMINOS.
//
// Hermana de `test_same_detection_everywhere.cpp`, y el mismo patrón de fallo un
// escalón más arriba: allí eran llamadas escritas sin pasar la CONFIGURACIÓN de
// detección; aquí, llamadas escritas sin preguntar QUÉ PIEZA está mirando el
// operador.
//
// Queja del taller: «si hay más de una pieza, y se usa la automedición de
// pieza, esta toma una medición para todas las piezas, en lugar de una medición
// independiente por pieza».
//
// El navegador existía y funcionaba — las flechas cambian la pieza, el vídeo la
// remarca, el rótulo dice «Midiendo la pieza 3 de 5»— pero solo lo entendía el
// camino del vídeo. Cuatro sitios más llamaban a `vision::analyzeFrame`, que
// devuelve LA MAYOR:
//
//   onMeasurePieceClicked   el informe de «Medir pieza», de otra pieza
//   onOpenEditorClicked     el editor se abría sobre la mayor
//   onAutoMeasureClicked    proponía las cotas de la mayor y las anclaba
//                           al fixture de la que se estaba editando
//   ensureContourReport     el contorno que se ve y se exporta, de la mayor
//
// Ninguno falla ni avisa: dan el informe de otra pieza. Medido sobre
// `arandelas-5`, donde la mayor es la #8 en orden de lectura: señalando la 1 el
// informe salía «Arandela, 274 px» cuando la pieza señalada es un «Polígono
// redondeado de 3 lados» de 95 px.
//
// Esta prueba no vigila una llamada: vigila que ningún gesto del operador que
// hable de «la pieza» se salte la pregunta de cuál.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::string read(const std::filesystem::path& file) {
    std::ifstream stream(file);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

// El cuerpo de una función, de su llave de apertura a la llave que la cierra en
// la columna cero. Basta y sobra para este fichero: el proyecto formatea así, y
// una heurística que además entienda cadenas y comentarios sería más código que
// lo que vigila.
std::string bodyOf(const std::string& source, const std::string& signature) {
    const std::size_t start = source.find(signature);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = source.find("\n}\n", start);
    if (end == std::string::npos) {
        return source.substr(start);
    }
    return source.substr(start, end - start);
}

}  // namespace

TEST(SamePieceEverywhere, NoGestureOfTheOperatorMeasuresTheBiggestBehindHisBack) {
    const std::filesystem::path root = sources();
    ASSERT_FALSE(root.empty()) << "no se encuentra src/: esta prueba no comprueba nada";

    struct Gesture {
        const char* file;
        const char* signature;
        const char* helper;
        const char* what;
    };
    // Cada uno es un gesto del operador que habla de «la pieza». Ninguno puede
    // decidir cuál por su cuenta.
    const std::vector<Gesture> gestures = {
        {"ui/main_window.cpp", "void MainWindow::onMeasurePieceClicked()",
         "analyseMeasuredPiece", "el informe de «Medir pieza»"},
        {"inspection_editor/editor_window.cpp", "void EditorWindow::onAutoMeasureClicked()",
         "analyseEditedPiece", "las cotas que propone la medición automática"},
        {"inspection_editor/editor_window.cpp", "void EditorWindow::onRefreshFromCamera()",
         "analyseEditedPiece", "el fixture al recapturar de la cámara"},
        {"inspection_editor/editor_window.cpp", "bool EditorWindow::ensureContourReport()",
         "analyseEditedPiece", "el contorno que se ve y se exporta"},
    };

    for (const auto& gesture : gestures) {
        const std::string body = bodyOf(read(root / gesture.file), gesture.signature);
        ASSERT_FALSE(body.empty())
            << "no se encuentra " << gesture.signature
            << ": si se renombró, hay que actualizar esta prueba — no borrarla";
        std::printf("  [pieza] %-34s -> %s\n", gesture.signature, gesture.helper);

        EXPECT_EQ(body.find("vision::analyzeFrame("), std::string::npos)
            << gesture.signature << " vuelve a analizar la MAYOR del encuadre, así que "
            << gesture.what << " será de una pieza distinta de la que el operador señaló. "
               "No falla ni avisa: da el informe de otra.";
        EXPECT_NE(body.find(gesture.helper), std::string::npos)
            << gesture.signature << " ha dejado de preguntar qué pieza se está midiendo";
    }
}

TEST(SamePieceEverywhere, TheChoiceOfWhichPieceLivesInOneSinglePlace) {
    // `largestPieceIndex` nació porque dos sitios habían copiado el bucle de «la
    // mayor», y su comentario avisa: «qué pieza se mide es exactamente la
    // decisión que no se puede permitir divergir en silencio». La regla con el
    // navegador de por medio tiene que vivir igual de junta, o volvemos a tener
    // dos definiciones de la misma cosa.
    const std::filesystem::path root = sources();
    ASSERT_FALSE(root.empty());

    const std::string vision = read(root / "vision" / "contour_analysis.cpp");
    ASSERT_NE(vision.find("std::size_t measuredPieceIndex("), std::string::npos)
        << "la regla de qué pieza se mide ya no vive en vision/";

    // Y que nadie la reescriba a mano: la marca es comparar el número señalado
    // contra el tamaño de la lista, que es la forma que tenía cuando estaba
    // copiada dentro de la ventana.
    const std::string window = read(root / "ui" / "main_window.cpp");
    EXPECT_EQ(window.find("wantedPiece <= static_cast<int>("), std::string::npos)
        << "la ventana vuelve a decidir por su cuenta qué pieza se mide; entonces hay dos "
           "reglas y solo una se arregla el día que cambie";
    std::printf("  [pieza] la regla vive en vision::measuredPieceIndex y en ningún sitio más\n");
}
