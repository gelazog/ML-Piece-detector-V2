#pragma once

#include <QWidget>

#include "ui/theme.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace pci::ui {

// Pagina «Preferencias» del panel Configurar (O1): centraliza valores que
// antes estaban fijos en el codigo. Por ahora, intervalo de auto-inspeccion y
// sensibilidad de anomalia (kSigma).
class PreferencesPage : public QWidget {
    Q_OBJECT

public:
    // `passTrigger` y sus dos tiempos: el disparo por PASO DE PIEZA. Con él
    // encendido, la auto-inspección deja de medir «cada N ms» y mide una vez
    // por pieza que cruza el encuadre.
    PreferencesPage(int autoIntervalMs, double kSigma, bool passTrigger = false,
                    int settleMs = 400, int rearmMs = 300, QWidget* parent = nullptr);

    [[nodiscard]] int autoIntervalMs() const;
    [[nodiscard]] double kSigma() const;
    [[nodiscard]] bool passTrigger() const;
    [[nodiscard]] int settleMs() const;
    [[nodiscard]] int rearmMs() const;

    // Claro / Oscuro / Como Windows. Se guarda al aceptar y se aplica al
    // volver a abrir el programa (ver `ui/theme.h`).
    void setThemeChoice(theme::ThemeChoice choice);
    [[nodiscard]] theme::ThemeChoice themeChoice() const;

private:
    QSpinBox* intervalSpin_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
    QCheckBox* passCheck_ = nullptr;
    QSpinBox* settleSpin_ = nullptr;
    QSpinBox* rearmSpin_ = nullptr;
    QComboBox* themeCombo_ = nullptr;
};

}  // namespace pci::ui
