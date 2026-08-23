#include "ui/pieces_page.h"

#include <algorithm>

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pci::ui {

PiecesPage::PiecesPage(int expectedPieces, QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Cuántas piezas debería haber en la imagen. Si aparecen más o menos, la "
           "inspección da NG diciendo cuántas esperaba y cuántas ve — sin necesidad "
           "de tener ninguna herramienta dibujada.\n\nEl número se guarda con la "
           "pieza seleccionada, no con la máquina."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    // El modo, primero: decide qué significa todo lo que hay debajo.
    automatic_ = new QRadioButton(tr("Automática: cuenta las que haya"), this);
    automatic_->setToolTip(
        tr("El programa dice cuántas piezas ve y no se queja del número.\n"
           "Para cuando la cantidad cambia de una bandeja a otra."));
    root->addWidget(automatic_);
    manual_ = new QRadioButton(tr("Manual: deben ser exactamente"), this);
    manual_->setToolTip(
        tr("Tú dices cuántas tiene que haber. Si aparecen más o menos, es NG\n"
           "diciendo cuántas esperaba y cuántas ve.\n\n"
           "Con UNA pieza, además, el programa deja de enumerar: mide la mayor\n"
           "y una sombra o un reflejo ya no se cuentan como una segunda pieza."));

    auto* manualRow = new QHBoxLayout();
    manualRow->addWidget(manual_);
    expected_ = new QSpinBox(this);
    expected_->setRange(1, 64);
    expected_->setSuffix(tr(" piezas"));
    expected_->setValue(expectedPieces > 0 ? expectedPieces : 1);
    manualRow->addWidget(expected_);
    manualRow->addStretch(1);
    root->addLayout(manualRow);

    // El cero guardado sigue significando «no vigilar»: es el mismo dato de
    // siempre, solo que ahora se enseña como un modo en vez de como un valor
    // especial escondido dentro de un campo numérico.
    automatic_->setChecked(expectedPieces <= 0);
    manual_->setChecked(expectedPieces > 0);

    useDetected_ = new QPushButton(tr("Usar lo que se ve ahora"), this);
    useDetected_->setToolTip(
        tr("Pone en el campo el número de piezas que la cámara está detectando\n"
           "en este momento. Colócalas como deben ir y pulsa aquí."));
    root->addWidget(useDetected_);
    connect(useDetected_, &QPushButton::clicked, this, &PiecesPage::useDetectedRequested);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    root->addWidget(status_);
    root->addStretch(1);

    connect(expected_, &QSpinBox::valueChanged, this, [this] { refreshStatus(); });
    // El campo solo tiene sentido en manual: dejarlo manejable en automático
    // invita a ponerle un número que no se va a usar.
    const auto syncEnabled = [this] {
        expected_->setEnabled(manual_->isChecked());
        refreshStatus();
    };
    connect(manual_, &QRadioButton::toggled, this, syncEnabled);
    syncEnabled();
}

PiecesPage::CountMode PiecesPage::countMode() const {
    return (manual_ != nullptr && manual_->isChecked()) ? CountMode::Manual
                                                        : CountMode::Automatic;
}

int PiecesPage::expectedPieces() const {
    if (countMode() == CountMode::Automatic) {
        return 0;  // 0 = no vigilar, igual que antes
    }
    return expected_ != nullptr ? expected_->value() : 1;
}

void PiecesPage::setExpectedPieces(int expected) {
    if (expected_ == nullptr) {
        return;
    }
    if (expected <= 0) {
        if (automatic_ != nullptr) {
            automatic_->setChecked(true);
        }
    } else {
        expected_->setValue(std::min(expected, expected_->maximum()));
        if (manual_ != nullptr) {
            manual_->setChecked(true);
        }
    }
    refreshStatus();
}

void PiecesPage::setDetectedCount(int found) {
    detected_ = found;
    refreshStatus();
}

void PiecesPage::refreshStatus() {
    if (status_ == nullptr) {
        return;
    }
    if (detected_ < 0) {
        status_->setText(tr("Todavía no se ha analizado ninguna imagen."));
        useDetected_->setEnabled(false);
        return;
    }
    useDetected_->setEnabled(true);
    // Decir aquí si cuadra o no evita descubrirlo cuando ya está en producción
    // y cada pieza da NG.
    const int expected = expectedPieces();
    if (expected <= 0) {
        status_->setText(tr("Se ven %1 pieza(s). El recuento no se está vigilando.")
                             .arg(detected_));
    } else if (detected_ == expected) {
        status_->setText(tr("Se ven %1 pieza(s): coincide con lo esperado.").arg(detected_));
    } else {
        status_->setText(tr("Se ven %1 pieza(s) y se esperan %2: ahora mismo esto daría NG.")
                             .arg(detected_)
                             .arg(expected));
    }
}

}  // namespace pci::ui
