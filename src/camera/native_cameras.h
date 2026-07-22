#pragma once

#include <string>
#include <vector>

namespace pci::camera {

// Un dispositivo de captura descubierto por el sistema operativo, con su nombre
// amigable real (p. ej. "Integrated Webcam", "DroidCam Source 3").
struct NativeCamera {
    std::string friendlyName;  // nombre legible que ve el usuario
    std::string devicePath;    // ruta única del dispositivo (para distinguir clones)
    int index = -1;            // índice a usar con el backend nativo (DSHOW/V4L2)
};

// Pregunta al SO por la lista de cámaras SIN abrir ninguna ni negociar formato.
// Esto es clave: abrir un dispositivo virtual no listo (AndroidCam antes de
// conectar el celular) hace que su driver divida por cero y tumbe el proceso.
// La enumeración nativa (DirectShow en Windows, V4L2 en Linux) solo lee
// metadatos, así que es segura.
//
// El `index` devuelto coincide con el orden de enumeración del backend nativo
// de OpenCV correspondiente (cv::CAP_DSHOW en Windows, cv::CAP_V4L2 en Linux),
// por lo que puede pasarse tal cual a cv::VideoCapture con ese backend.
//
// En plataformas sin implementación (microcontroladores, etc.) devuelve vacío.
std::vector<NativeCamera> enumerateNativeCameras();

}  // namespace pci::camera
