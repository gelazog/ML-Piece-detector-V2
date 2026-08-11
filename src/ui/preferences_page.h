#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QSpinBox;

namespace pci::ui {

// Pagina «Preferencias» del panel Configurar (O1): centraliza valores que
// antes estaban fijos en el codigo. Por ahora, intervalo de auto-inspeccion y
// sensibilidad de anomalia (kSigma).
class PreferencesPage : public QWidget {
    Q_OBJECT

public:
    PreferencesPage(int autoIntervalMs, double kSigma, QWidget* parent = nullptr);

    [[nodiscard]] int autoIntervalMs() const;
    [[nodiscard]] double kSigma() const;

private:
    QSpinBox* intervalSpin_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
};

}  // namespace pci::ui
