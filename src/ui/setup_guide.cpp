#include "ui/setup_guide.h"

#include <QObject>

namespace pci::ui {

SetupStep nextSetupStep(const SetupState& state) {
    if (state.alreadyGuided) {
        return SetupStep::Done;
    }
    // Sin cámara en marcha no hay nada que guiar: el botón de arrancar está a
    // la vista y decirle «enfoca» a quien todavía no ve imagen sería ruido.
    if (!state.cameraRunning) {
        return SetupStep::Done;
    }
    // El orden no es una preferencia: es el único que funciona. Calibrar con la
    // imagen desenfocada fija una escala mala, y registrar una pieza antes de
    // calibrar guarda sus medidas en píxeles de esta resolución.
    if (!state.calibrated) {
        return SetupStep::Calibrate;
    }
    if (!state.anyPieceRegistered) {
        return SetupStep::Register;
    }
    return SetupStep::Done;
}

QString setupHint(SetupStep step) {
    switch (step) {
        case SetupStep::Done:
            return {};
        case SetupStep::Focus:
            return QObject::tr(
                "Primero, el enfoque: Cámara ▸ Configurar ▸ Cámara e imagen tiene una "
                "barra que sube cuanto más nítida está la pieza.");
        case SetupStep::Calibrate:
            return QObject::tr(
                "Para empezar: enfoca la pieza y pulsa C para calibrar la escala. Hasta "
                "entonces se mide en píxeles — mira los indicadores de abajo a la "
                "derecha, que dicen en todo momento si estás midiendo en condiciones.");
        case SetupStep::Register:
            return QObject::tr(
                "Ya mides en milímetros. Falta registrar una pieza buena (Pieza ▸ "
                "Registrar con asistente…) para poder comparar contra ella.");
    }
    return {};
}

}  // namespace pci::ui
