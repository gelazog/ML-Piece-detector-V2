#include "ui/station_status.h"

#include <QObject>

namespace pci::ui {

namespace {

// El automático de un control: mismo razonamiento para el enfoque y para la
// exposición, distinto solo en qué estropea cada uno. Se escribe una vez.
StationIndicator automaticIndicator(const QString& name, bool automaticOn,
                                    bool adjustable, bool calibrated,
                                    const QString& damage) {
    StationIndicator indicator;
    indicator.tab = kCameraTab;
    if (!adjustable) {
        // No es un fallo de la estación ni algo que el operador pueda arreglar:
        // esta cámara no deja tocarlo. Decirlo evita que parezca una avería.
        indicator.label = name + QObject::tr(" ·");
        indicator.light = StationLight::Neutral;
        indicator.reason =
            QObject::tr("%1: esta cámara no deja fijarlo, así que no hay nada que "
                        "ajustar aquí.")
                .arg(name);
        return indicator;
    }
    if (!automaticOn) {
        indicator.label = name + QObject::tr(" ✓");
        indicator.light = StationLight::Good;
        indicator.reason = QObject::tr("%1 fijo: dos medidas de la misma pieza dan lo "
                                       "mismo.")
                               .arg(name);
        return indicator;
    }
    indicator.label = name + QObject::tr(" auto");
    // La misma condición cambia de gravedad según haya milímetros o no. Sin
    // calibrar es una comodidad legítima; con calibración, un número creíble y
    // falso.
    indicator.light = calibrated ? StationLight::Bad : StationLight::Warning;
    indicator.reason =
        calibrated
            ? QObject::tr("%1 en automático CON la escala calibrada: %2 Apágalo antes "
                          "de fiarte de los milímetros.")
                  .arg(name, damage)
            : QObject::tr("%1 en automático. Sin calibrar no estropea milímetros, pero "
                          "%2")
                  .arg(name, damage);
    return indicator;
}

}  // namespace

std::vector<StationIndicator> stationStatus(const StationState& state) {
    std::vector<StationIndicator> indicators;

    // 1) La escala. Es la primera porque es la que decide si las otras tres
    // importan mucho o poco.
    StationIndicator scale;
    scale.tab = -1;  // se calibra desde su propio diálogo, no desde Configurar
    if (!state.calibrated) {
        scale.label = QObject::tr("px");
        scale.light = StationLight::Neutral;
        scale.reason = QObject::tr("Sin calibrar: las medidas van en píxeles. No es un "
                                   "problema si es lo que quieres.");
    } else if (state.calibrationStale) {
        scale.label = QObject::tr("mm ⚠");
        scale.light = StationLight::Bad;
        scale.reason = QObject::tr("La calibración se hizo con otra resolución u otra "
                                   "cámara: los milímetros ya no valen. Recalibra.");
    } else {
        scale.label = QObject::tr("mm ✓");
        scale.light = StationLight::Good;
        scale.reason = QObject::tr("Escala calibrada: las medidas van en milímetros.");
    }
    indicators.push_back(std::move(scale));

    // 2) y 3) Los dos automáticos. El enfoque va antes que la exposición porque
    // es el que puede estropear TODAS las cotas a la vez.
    indicators.push_back(automaticIndicator(
        QObject::tr("Enfoque"), state.autoFocusOn, state.focusAdjustable, state.calibrated,
        QObject::tr("un reenfoque cambia la magnificación, y con ella todas las cotas a "
                    "la vez.")));
    indicators.push_back(automaticIndicator(
        QObject::tr("Exposición"), state.autoExposureOn, state.exposureAdjustable,
        state.calibrated,
        QObject::tr("mueve el umbral aparente del borde: la misma pieza mide distinto "
                    "según la luz.")));

    // 4) La zona de trabajo. Nunca es ámbar: procesar la imagen entera es más
    // lento pero no está mal, y avisar de algo que no es un problema es la
    // forma más rápida de que se deje de mirar la tira.
    StationIndicator zone;
    zone.tab = kPerformanceTab;
    if (state.zoneActive) {
        zone.label = QObject::tr("Zona ✓");
        zone.light = StationLight::Good;
        zone.reason = QObject::tr("Buscando la pieza en un recorte: más rápido que la "
                                  "imagen entera.");
    } else {
        zone.label = QObject::tr("Zona ·");
        zone.light = StationLight::Neutral;
        zone.reason = QObject::tr("Procesando la imagen entera. Va más lento, pero es lo "
                                  "más difícil de que falle.");
    }
    indicators.push_back(std::move(zone));

    return indicators;
}

}  // namespace pci::ui
