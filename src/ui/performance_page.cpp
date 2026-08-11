#include "ui/performance_page.h"

#include <QButtonGroup>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace pci::ui {

PerformancePage::PerformancePage(vision::WorkingZoneMode mode, bool hasFixedZone,
                                 QWidget* parent)
    : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Dónde busca el programa la pieza en cada imagen. Si la pieza ocupa una "
           "esquina, mirar la imagen entera es tirar el resto del trabajo: sobre "
           "1280×720 con una pieza de 180×140, recortar va unas seis veces más "
           "rápido."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* group = new QButtonGroup(this);
    off_ = new QRadioButton(tr("Imagen entera (siempre)"), this);
    off_->setToolTip(tr("Nunca recorta. Lo más lento y lo más difícil de que falle."));
    automatic_ = new QRadioButton(tr("Zona automática (recomendado)"), this);
    automatic_->setToolTip(
        tr("Sigue a la pieza con un recorte que se ajusta a su tamaño.\n"
           "Ante cualquier duda —se pierde la pieza, toca el borde del recorte o\n"
           "cambia de tamaño de golpe— vuelve solo a la imagen entera y lo dice."));
    fixed_ = new QRadioButton(tr("Zona de detección fija"), this);
    fixed_->setToolTip(
        tr("Usa el rectángulo que dibujaste con «Zona de detección».\n"
           "Útil cuando la pieza siempre llega al mismo sitio y hay luces o\n"
           "reflejos alrededor que conviene dejar fuera."));
    fixed_->setEnabled(hasFixedZone);
    if (!hasFixedZone) {
        fixed_->setToolTip(fixed_->toolTip() + tr("\n\nNo hay ninguna zona dibujada "
                                                  "todavía."));
    }
    for (auto* button : {off_, automatic_, fixed_}) {
        group->addButton(button);
        root->addWidget(button);
    }
    switch (mode) {
        case vision::WorkingZoneMode::Off: off_->setChecked(true); break;
        case vision::WorkingZoneMode::Automatic: automatic_->setChecked(true); break;
        case vision::WorkingZoneMode::Fixed:
            (hasFixedZone ? fixed_ : off_)->setChecked(true);
            break;
    }

    status_ = new QLabel(tr("Procesando la imagen entera."), this);
    status_->setWordWrap(true);
    root->addWidget(status_);
    root->addStretch(1);

    // `this->` a proposito: el parametro del constructor se llama igual que
    // el metodo, y sin cualificar el lambda se referiria al parametro.
    const auto emitMode = [this] { emit modeChanged(this->mode()); };
    connect(off_, &QRadioButton::toggled, this, emitMode);
    connect(automatic_, &QRadioButton::toggled, this, emitMode);
    connect(fixed_, &QRadioButton::toggled, this, emitMode);
}

void PerformancePage::showMode(vision::WorkingZoneMode mode, bool hasFixedZone) {
    if (off_ == nullptr) {
        return;
    }
    fixed_->setEnabled(hasFixedZone);
    // Los tres botones se bloquean a la vez: marcar uno desmarca otro, y cada
    // `toggled` dispararía `modeChanged` de vuelta hacia quien nos está
    // sincronizando.
    const QSignalBlocker blockOff(off_);
    const QSignalBlocker blockAuto(automatic_);
    const QSignalBlocker blockFixed(fixed_);
    switch (mode) {
        case vision::WorkingZoneMode::Off: off_->setChecked(true); break;
        case vision::WorkingZoneMode::Automatic: automatic_->setChecked(true); break;
        case vision::WorkingZoneMode::Fixed:
            (hasFixedZone ? fixed_ : off_)->setChecked(true);
            break;
    }
}

vision::WorkingZoneMode PerformancePage::mode() const {
    if (automatic_ != nullptr && automatic_->isChecked()) {
        return vision::WorkingZoneMode::Automatic;
    }
    if (fixed_ != nullptr && fixed_->isChecked()) {
        return vision::WorkingZoneMode::Fixed;
    }
    return vision::WorkingZoneMode::Off;
}

void PerformancePage::setZoneStatus(const cv::Rect& activeZone, const cv::Size& frameSize,
                                    vision::AutoRoiGiveUp lastGiveUp) {
    if (status_ == nullptr) {
        return;
    }
    const QString reason = QString::fromUtf8(vision::giveUpReason(lastGiveUp));
    if (activeZone.area() <= 0 || frameSize.area() <= 0) {
        status_->setText(reason.isEmpty()
                             ? tr("Procesando la imagen entera.")
                             : tr("Procesando la imagen entera — %1.").arg(reason));
        return;
    }
    const double fraction =
        100.0 * activeZone.area() / static_cast<double>(frameSize.area());
    status_->setText(tr("Zona activa: %1 × %2 px (%3 % de la imagen).")
                         .arg(activeZone.width)
                         .arg(activeZone.height)
                         .arg(fraction, 0, 'f', 1));
}

}  // namespace pci::ui
