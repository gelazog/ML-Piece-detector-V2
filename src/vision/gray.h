#pragma once

#include <opencv2/imgproc.hpp>

namespace pci::vision {

// PASAR UNA IMAGEN A GRIS. UNA SOLA VEZ.
//
// Existe porque el módulo tenía TRES copias de esta función, y no hacían lo
// mismo:
//
//   difference_map.cpp    con un canal devolvía `image.clone()` — una copia
//   edge_segmentation.cpp con un canal devolvía `image` — compartida
//   orientation_anchor.cpp convertía si había tres canales y **devolvía la
//                          imagen tal cual en cualquier otro caso**
//
// La tercera es la que importa: una imagen de cuatro canales (BGRA, que es lo
// que llega de más de una fuente) salía de ahí sin convertir y el resto del
// código la trataba como si fuera gris. No falla: da números. Y `segmentation.cpp`
// tenía una cuarta variante, esa sí con su error para el caso raro.
//
// Cuatro respuestas a la misma pregunta dentro del mismo módulo, tres de ellas
// distintas. Ninguna estaba mal escrita — cada una era razonable donde nació.
//
// AQUÍ NO SE CLONA, y es una decisión: `segmentByEdges` corre en cada frame del
// vídeo, y una copia de la imagen completa por frame se paga en el sitio más
// caliente del programa. `cv::Mat` comparte los datos, así que quien necesite
// modificar el resultado tiene que decirlo escribiendo en otro sitio — que es
// además lo que deja claro en el código que va a modificarlo.
//
// Devuelve una imagen VACÍA cuando no sabe qué hacer, en vez de devolver la
// entrada sin tocar. Un canal desconocido es una pregunta sin respuesta, y
// contestar con la propia entrada es contestar que sí a todo.
[[nodiscard]] inline cv::Mat toGray(const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }
    if (image.channels() == 1) {
        return image;
    }
    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }
    if (image.channels() == 4) {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        return gray;
    }
    return {};
}

}  // namespace pci::vision
