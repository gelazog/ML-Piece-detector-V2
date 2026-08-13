#pragma once

#include <QString>

namespace pci::ui {

// Qué le falta a una estación recién instalada (I3).
//
// Una instalación nueva abre con la cámara sin ajustar, sin calibrar y sin
// ninguna pieza registrada, y no dice por dónde empezar. Son tres pasos y
// siempre los mismos —**enfocar, calibrar, registrar la pieza**— pero solo se
// saben si alguien te los ha dicho una vez.
//
// La forma es tan importante como el contenido, y por eso esto NO es un
// asistente modal: esos se cierran sin leer, y encima bloquean justo la ventana
// que hay que mirar para hacer el primer paso. Es una línea que señala la tira
// de estado, que es la que va a seguir ahí después.
enum class SetupStep {
    Done,      // nada que decir
    Focus,     // hay cámara pero la imagen no está a punto
    Calibrate, // se ve bien pero las medidas van en píxeles
    Register,  // se mide en mm pero no hay ninguna pieza que comparar
};

struct SetupState {
    bool cameraRunning = false;
    bool calibrated = false;
    bool anyPieceRegistered = false;
    // Ya se le dijo alguna vez. La guía es para el primer arranque: repetirla
    // cada día sería un cartel que se aprende a no ver, y la tira de estado ya
    // lleva el estado permanentemente.
    bool alreadyGuided = false;
    // ¿Se puede enfocar esta fuente? Con una imagen o un vídeo de archivo, no:
    // la nitidez es la que se grabó. Un primer consejo que empieza por «enfoca
    // la pieza» manda a hacer lo imposible, y un asistente que pide lo
    // imposible se deja de leer entero.
    bool canFocus = true;
};

// El SIGUIENTE paso, no la lista entera. Enseñar tres cosas a la vez cuando
// solo se puede hacer una es la manera de que no se haga ninguna.
[[nodiscard]] SetupStep nextSetupStep(const SetupState& state);

// Qué se le dice al operador en ese paso, o vacío si no hay nada que decir.
//
// `canFocus` cambia el consejo, no el paso: calibrar se calibra igual, pero
// «enfoca la pieza y pulsa C» sobra cuando la fuente es un fichero.
[[nodiscard]] QString setupHint(SetupStep step, bool canFocus = true);

}  // namespace pci::ui
