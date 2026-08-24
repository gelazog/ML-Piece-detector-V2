#include "ui/preferences_page.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pci::ui {

PreferencesPage::PreferencesPage(int autoIntervalMs, double kSigma, QWidget* parent)
    : QWidget(parent) {
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
    // DOS DECIMALES, aunque las flechas sigan moviendo de una en una décima.
    //
    // Con uno solo, un valor que llegara con más precisión —importando una
    // configuración de otro puesto, por ejemplo— se redondeaba **al abrir la
    // ventana**: entrabas a cambiar otra cosa, aceptabas, y de paso se te había
    // movido la sensibilidad. Nadie relaciona eso con la ventana de ajustes; se
    // nota semanas después como «esto juzga distinto desde hace un tiempo».
    //
    // El paso sigue siendo 0,1 porque es la granularidad con la que se ajusta a
    // mano; los dos decimales son para no ESTROPEAR lo que ya había.
    sigmaSpin_->setDecimals(2);
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

    root->addStretch(1);
}

int PreferencesPage::autoIntervalMs() const {
    return intervalSpin_->value();
}

double PreferencesPage::kSigma() const {
    return sigmaSpin_->value();
}

}  // namespace pci::ui
