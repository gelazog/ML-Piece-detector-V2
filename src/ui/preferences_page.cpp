#include "ui/preferences_page.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pci::ui {

PreferencesPage::PreferencesPage(int autoIntervalMs, double kSigma, bool passTrigger,
                                 int settleMs, int rearmMs, QWidget* parent)
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

    // --- Disparo por PASO DE PIEZA -----------------------------------------
    //
    // Petición de uso: «un tiempo de espera entre que sale y entra una/varias
    // piezas en el enfoque». Con el temporizador solo, sobre una cinta se mide
    // media pieza al entrar, la misma pieza doce veces mientras cruza, y a veces
    // ninguna.
    passCheck_ = new QCheckBox(tr("Medir una vez por pieza que pasa"), this);
    passCheck_->setObjectName(QStringLiteral("passTriggerCheck"));
    passCheck_->setChecked(passTrigger);
    passCheck_->setToolTip(
        tr("Para vídeo y cámara en marcha. En vez de medir cada N ms, se mide\n"
           "cuando la escena está quieta y ninguna pieza toca el borde — y no se\n"
           "vuelve a medir hasta que el encuadre se vacía.\n\n"
           "Así cada pieza que cruza se mide UNA vez, entera."));
    form->addRow(passCheck_);

    settleSpin_ = new QSpinBox(this);
    settleSpin_->setObjectName(QStringLiteral("settleSpin"));
    settleSpin_->setRange(0, 10000);
    settleSpin_->setSingleStep(100);
    settleSpin_->setSuffix(tr(" ms"));
    settleSpin_->setValue(settleMs);
    settleSpin_->setToolTip(
        tr("Cuánto tiene que llevar la escena quieta antes de medir. Si se mide\n"
           "en marcha, las cotas salen movidas y el veredicto es del momento en\n"
           "que se miró, no de la pieza."));
    form->addRow(tr("   Asentamiento:"), settleSpin_);

    rearmSpin_ = new QSpinBox(this);
    rearmSpin_->setObjectName(QStringLiteral("rearmSpin"));
    rearmSpin_->setRange(0, 10000);
    rearmSpin_->setSingleStep(100);
    rearmSpin_->setSuffix(tr(" ms"));
    rearmSpin_->setValue(rearmMs);
    rearmSpin_->setToolTip(
        tr("Cuánto tiene que estar VACÍO el encuadre para volver a medir. Es lo\n"
           "que separa una pieza de la siguiente: sin esto, la misma pieza se\n"
           "mediría una vez por fotograma mientras cruza."));
    form->addRow(tr("   Rearme al vaciarse:"), rearmSpin_);

    // Los dos tiempos solo significan algo con el disparo encendido. Dejarlos
    // activos sugeriría que hacen algo, y no harían nada.
    const auto syncPassRow = [this] {
        settleSpin_->setEnabled(passCheck_->isChecked());
        rearmSpin_->setEnabled(passCheck_->isChecked());
    };
    connect(passCheck_, &QCheckBox::toggled, this, syncPassRow);
    syncPassRow();

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

bool PreferencesPage::passTrigger() const { return passCheck_->isChecked(); }

int PreferencesPage::settleMs() const { return settleSpin_->value(); }

int PreferencesPage::rearmMs() const { return rearmSpin_->value(); }

double PreferencesPage::kSigma() const {
    return sigmaSpin_->value();
}

}  // namespace pci::ui
