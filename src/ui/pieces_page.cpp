#include "ui/pieces_page.h"

#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
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

    auto* form = new QFormLayout();
    expected_ = new QSpinBox(this);
    expected_->setRange(0, 64);
    expected_->setValue(expectedPieces);
    expected_->setSpecialValueText(tr("no vigilar el recuento"));
    expected_->setToolTip(
        tr("1 es lo de siempre: una pieza por imagen.\n"
           "0 desactiva la comprobación — un aviso que salta siempre acaba\n"
           "ignorándose, así que se puede apagar."));
    form->addRow(tr("Piezas esperadas:"), expected_);
    root->addLayout(form);

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
    refreshStatus();
}

int PiecesPage::expectedPieces() const {
    return expected_ != nullptr ? expected_->value() : 1;
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
