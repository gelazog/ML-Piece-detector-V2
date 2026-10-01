// Cabecera PRIVADA de la implementación de MainWindow, repartida en
// main_window*.cpp. Solo la incluyen esos ficheros.
//
// Guarda los ayudantes de ámbito de fichero que comparten varios de sus
// trozos, y solo los includes que necesitan ELLOS. Cada main_window_*.cpp
// incluye lo que usa: antes esta cabecera arrastraba los ~90 includes del
// main_window.cpp único a los ocho ficheros, usaran lo que usaran. Los
// ayudantes que usa un solo fichero viven en el espacio de nombres anónimo de
// ese fichero.
#pragma once

#include <opencv2/core/types.hpp>

#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace pci::ui {

namespace {

// Marcadores del desplegable de fuente, guardados en el DATO del elemento. Los
// valores negativos no chocan nunca con un índice de cámara, que es lo que
// permite preguntar «¿qué eligió?» sin depender de dónde caiga en la lista.
constexpr int kSourceOpenImage = -1;
constexpr int kSourceOpenVideo = -2;
// El fichero que está abierto AHORA. Se añade al desplegable al abrirlo y se
// quita al cerrar.
//
// Sin esto, tras abrir «pieza.png» el desplegable seguía diciendo «Abrir
// imagen…»: el operador no tenía dónde leer QUÉ está mirando. Saber siempre
// dónde estás es lo primero que una interfaz tiene que resolver, y aquí encima
// importa el doble, porque la mitad de las decisiones —recalibrar, comparar,
// registrar— dependen de con qué imagen se está trabajando.
constexpr int kSourceOpenedFile = -3;
// Los ficheros abiertos hace poco, una ruta por línea (los 5 últimos).
const char* const kSettingRecentFiles = "recent_files";
const char* const kSettingFreeZone = "det_roi_poly";
constexpr int kCaptureTarget = 30;

// La zona libre se guarda como texto «x,y x,y …». Una zona son unos pocos
// vértices después de simplificar, y darle una tabla propia en la base sería un
// esquema nuevo para un dato que cabe en una línea.
inline std::string encodeZonePolygon(const std::vector<cv::Point>& polygon) {
    std::string text;
    for (const auto& point : polygon) {
        if (!text.empty()) {
            text.push_back(' ');
        }
        text += std::to_string(point.x) + "," + std::to_string(point.y);
    }
    return text;
}

// Lo contrario, y a prueba de basura: cualquier par que no se entienda se salta
// en vez de tumbar el arranque. Un ajuste corrupto tiene que costar una zona,
// no una aplicación que no abre.
inline std::vector<cv::Point> decodeZonePolygon(const std::string& text) {
    std::vector<cv::Point> polygon;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        const auto comma = token.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        try {
            polygon.emplace_back(std::stoi(token.substr(0, comma)),
                                 std::stoi(token.substr(comma + 1)));
        } catch (const std::exception&) {
            continue;
        }
    }
    // Menos de tres vértices no es una zona; devolver «casi una» sería dejar que
    // el resto del programa tuviera que acordarse de comprobarlo.
    return polygon.size() >= 3 ? polygon : std::vector<cv::Point>{};
}

}  // namespace

}  // namespace pci::ui
