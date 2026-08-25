#include "ui/pieces_page.h"

#include "vision/contour_analysis.h"

#include <algorithm>

#include <QCheckBox>
#include <QSignalBlocker>
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
    // «CONTADOR AUTOMÁTICO DE PIEZAS», con ese nombre y no «Automática».
    //
    // Petición de uso, y con razón: «automática» a secas no dice automática
    // QUÉ. Puesto al lado del campo del número, el nombre tiene que decir por
    // sí solo qué hace la casilla — que es contar.
    automatic_ = new QRadioButton(tr("Contador automático de piezas"), this);
    automatic_->setToolTip(
        tr("El programa cuenta las que haya y NO se queja del número: mide\n"
           "todas, y ninguna cantidad da NG por sí sola.\n\n"
           "Para cuando la cantidad cambia de una bandeja a otra."));
    root->addWidget(automatic_);
    manual_ = new QRadioButton(tr("Deben ser exactamente"), this);
    manual_->setToolTip(
        tr("Tú dices cuántas tiene que haber. Si aparecen más o menos, es NG\n"
           "diciendo cuántas esperaba y cuántas ve.\n\n"
           "Con UNA pieza, además, el programa deja de enumerar: mide la mayor\n"
           "y una sombra o un reflejo ya no se cuentan como una segunda pieza."));

    auto* manualRow = new QHBoxLayout();
    manualRow->addWidget(manual_);
    expected_ = new QSpinBox(this);
    // HASTA DONDE LLEGA EL DETECTOR, ni una menos.
    //
    // Estaba en 64 mientras el detector acepta 256, así que una bandeja de cien
    // tuercas —que es la que usa el usuario para probar— no se podía ni
    // declarar: el campo se paraba en 64 sin decir por qué.
    expected_->setRange(1, vision::kMaxPieces);
    expected_->setSuffix(tr(" piezas"));
    expected_->setToolTip(
        tr("Cuántas piezas tiene que haber en el encuadre. Si aparecen más o\n"
           "menos, la inspección da NG diciendo cuántas esperaba y cuántas ve,\n"
           "sin necesidad de tener ninguna herramienta dibujada.\n\n"
           "Además el programa se queda con las N MAYORES: una sombra o un\n"
           "reflejo de más deja de contar como pieza.\n\n"
           "Llega hasta 256, que es el tope del detector.\n\n"
           "El número se guarda con la pieza seleccionada, no con la máquina:\n"
           "«seis tornillos en bandeja» es una propiedad del trabajo."));
    expected_->setValue(expectedPieces > 0 ? expectedPieces : 1);
    manualRow->addWidget(expected_);

    // El cero guardado sigue significando «no vigilar»: es el mismo dato de
    // siempre, solo que ahora se enseña como un modo en vez de como un valor
    // especial escondido dentro de un campo numérico.
    automatic_->setChecked(expectedPieces <= 0);
    manual_->setChecked(expectedPieces > 0);

    // EL BOTÓN, PEGADO AL NÚMERO QUE CAMBIA.
    //
    // Estaba en su propia fila, debajo, y desde ahí no se ve a qué campo
    // afecta. Va donde actúa.
    useDetected_ = new QPushButton(tr("Usar lo que se ve ahora"), this);
    useDetected_->setToolTip(
        tr("Pone en el campo de al lado EL MISMO número que dice el aviso de\n"
           "abajo: las piezas que la cámara está viendo ahora mismo.\n\n"
           "Coloca las piezas como deben ir y pulsa aquí.\n\n"
           "Si el número que ves no es el que hay de verdad —una sombra o un\n"
           "reflejo contando como pieza— lo que hay que arreglar es la\n"
           "detección, no este campo."));
    manualRow->addWidget(useDetected_);
    manualRow->addStretch(1);
    root->addLayout(manualRow);
    connect(useDetected_, &QPushButton::clicked, this, &PiecesPage::useDetectedRequested);

    // VER TODAS LAS PIEZAS A LA VEZ.
    //
    // Petición directa: poder pedir el mosaico desde aquí. Va en esta página y
    // no en el menú de Ver porque es una propiedad del TRABAJO —«esta pieza es
    // una bandeja»— y se guarda con la pieza, igual que el número. Quien pasa
    // de una bandeja a una pieza suelta en el mismo turno no tiene por qué
    // acordarse de abrir y cerrar un panel cada vez.
    mosaic_ = new QCheckBox(tr("Ver todas las piezas en mosaico"), this);
    mosaic_->setToolTip(
        tr("Abre un panel con cada pieza del encuadre recortada y numerada,\n"
           "todas al mismo tamaño. Con una bandeja llena es la única forma\n"
           "de ver si a alguna le falta algo: en el vídeo cada pieza ocupa\n"
           "unos pocos píxeles.\n\n"
           "Pulsar una la ENFOCA: pasa a ser la que miden las herramientas,\n"
           "la que compara el panel de registrada/actual y la que se\n"
           "remarca en el vídeo.\n\n"
           "Con una sola pieza en el encuadre no enseña nada: el vídeo ya\n"
           "la da entera y más grande."));
    root->addWidget(mosaic_);

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

    // El aviso en vivo va DESPUÉS de `syncEnabled`, para que montar la ventana
    // no lo dispare: al construirla nadie ha cambiado nada todavía.
    const auto announce = [this] { emit this->expectedPiecesChangedLive(this->expectedPieces()); };
    connect(expected_, &QSpinBox::valueChanged, this, announce);
    connect(manual_, &QRadioButton::toggled, this, announce);
    connect(mosaic_, &QCheckBox::toggled, this,
            [this](bool on) { emit this->showMosaicChangedLive(on); });
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

bool PiecesPage::showMosaic() const {
    return mosaic_ != nullptr && mosaic_->isChecked();
}

void PiecesPage::setShowMosaic(bool on) {
    if (mosaic_ != nullptr) {
        // Sin disparar el aviso en vivo: cargar lo guardado no es que el
        // operador haya cambiado nada.
        const QSignalBlocker quiet(mosaic_);
        mosaic_->setChecked(on);
    }
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
