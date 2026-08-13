#pragma once

#include <QString>

#include <vector>

namespace pci::ui {

// Estado de la estación de un vistazo (I1).
//
// Los cuatro datos que deciden si una medida vale —escala calibrada, exposición
// fija, enfoque fijo y zona de trabajo— estaban repartidos por las pestañas de
// «Configurar». Para saber si estabas midiendo en condiciones había que abrir
// el panel y recorrerlas, que es justo lo que nadie hace antes de medir.
//
// La regla que gobierna el color, y que es lo único aquí que puede estar mal:
// **ámbar solo cuando de verdad afecta a la medida**. Sin calibración, el
// enfoque automático es una comodidad legítima —las medidas van en píxeles y
// nadie ha prometido milímetros—; con calibración, ese mismo automático puede
// cambiar todas las cotas a la vez. Cuatro luces siempre encendidas serían
// cuatro luces que nadie mira.

enum class StationLight {
    Good,     // en condiciones
    Neutral,  // no configurado, pero no es un problema en este contexto
    Warning,  // afecta a la medida
    Bad,      // afecta a la medida Y hay milímetros de por medio
};

// A dónde lleva el clic de un indicador. Es un NOMBRE y no un índice de
// pestaña a propósito: la primera versión llevaba índices y este código ya
// había pagado ese error antes —hay un comentario en las pruebas del panel que
// dice que se rompió dos veces seguidas al añadir páginas—. Un índice se queda
// mal en silencio; un nombre lo resuelve el diálogo, que es quien sabe dónde
// tiene cada cosa.
enum class ConfigureTarget {
    None,
    Camera,
    Performance,
};

struct StationIndicator {
    QString label;   // texto corto con su símbolo
    StationLight light = StationLight::Neutral;
    QString reason;  // qué significa y qué hacer, para el tooltip
    ConfigureTarget target = ConfigureTarget::None;
};

// Lo que hace falta saber para pintar la tira. Son datos planos a propósito:
// así la regla se prueba entera sin ventana, sin cámara y sin calibrar nada.
struct StationState {
    bool calibrated = false;
    bool calibrationStale = false;  // otra resolución u otra cámara
    bool autoExposureOn = false;
    bool autoFocusOn = false;
    bool exposureAdjustable = true;  // false = la cámara no deja fijarla
    bool focusAdjustable = true;
    bool zoneActive = false;  // recortando (automática o fija)
    bool streaming = false;
};

[[nodiscard]] std::vector<StationIndicator> stationStatus(const StationState& state);

}  // namespace pci::ui
