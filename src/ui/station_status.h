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

struct StationIndicator {
    QString label;   // texto corto con su símbolo
    StationLight light = StationLight::Neutral;
    QString reason;  // qué significa y qué hacer, para el tooltip
    int tab = -1;    // pestaña de «Configurar» que lo arregla; −1 = ninguna
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

// Índices de las pestañas de «Configurar», para que el clic lleve a la que
// arregla cada cosa. Están aquí y no en el diálogo porque la regla es la que
// decide a dónde apunta cada indicador.
inline constexpr int kCameraTab = 0;
inline constexpr int kPerformanceTab = 3;

[[nodiscard]] std::vector<StationIndicator> stationStatus(const StationState& state);

}  // namespace pci::ui
