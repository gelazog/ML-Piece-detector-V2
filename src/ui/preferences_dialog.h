#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;

namespace pci::ui {

// Preferencias unificadas (O1): centraliza valores antes fijos en código.
// Por ahora, intervalo de auto-inspección y sensibilidad de anomalía (kSigma).
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    PreferencesDialog(int autoIntervalMs, double kSigma, QWidget* parent = nullptr);

    [[nodiscard]] int autoIntervalMs() const;
    [[nodiscard]] double kSigma() const;

private:
    QSpinBox* intervalSpin_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
};

}  // namespace pci::ui
