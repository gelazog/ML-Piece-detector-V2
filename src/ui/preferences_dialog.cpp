#include "ui/preferences_dialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pci::ui {

PreferencesDialog::PreferencesDialog(int autoIntervalMs, double kSigma, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Preferencias"));

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    intervalSpin_ = new QSpinBox(this);
    intervalSpin_->setRange(200, 10000);
    intervalSpin_->setSingleStep(100);
    intervalSpin_->setSuffix(tr(" ms"));
    intervalSpin_->setValue(autoIntervalMs);
    intervalSpin_->setToolTip(
        tr("Cada cuánto inspecciona la auto-inspección. Menor = más rápido pero "
           "más carga de CPU."));
    form->addRow(tr("Intervalo de auto-inspección:"), intervalSpin_);

    sigmaSpin_ = new QDoubleSpinBox(this);
    sigmaSpin_->setRange(0.5, 6.0);
    sigmaSpin_->setSingleStep(0.1);
    sigmaSpin_->setDecimals(1);
    sigmaSpin_->setValue(kSigma);
    sigmaSpin_->setToolTip(
        tr("Sensibilidad de anomalía de apariencia (k·σ). Más bajo = más estricto: "
           "marca NG con desviaciones de similitud más pequeñas."));
    form->addRow(tr("Sensibilidad de anomalía (kσ):"), sigmaSpin_);

    root->addLayout(form);

    auto* note = new QLabel(
        tr("Los cambios se aplican al aceptar y quedan guardados."), this);
    note->setWordWrap(true);
    root->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

int PreferencesDialog::autoIntervalMs() const {
    return intervalSpin_->value();
}

double PreferencesDialog::kSigma() const {
    return sigmaSpin_->value();
}

}  // namespace pci::ui
