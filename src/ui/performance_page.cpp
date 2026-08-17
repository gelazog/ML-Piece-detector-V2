#include "ui/performance_page.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <array>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace pci::ui {

PerformancePage::PerformancePage(vision::WorkingZoneMode mode, bool hasFixedZone,
                                 bool hasFreeZone, QWidget* parent)
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
    free_ = new QRadioButton(tr("Zona libre"), this);
    free_->setToolTip(
        tr("Usa el contorno que dibujaste con «Zona libre», que no tiene por qué\n"
           "ser un rectángulo.\n"
           "Es la respuesta para lo que un rectángulo no puede separar: el borde del\n"
           "útil pegado a la pieza, la sombra de un lado, la pieza de al lado en\n"
           "diagonal. Recorta igual de rápido —por dentro sigue usando la envolvente—\n"
           "y además descarta lo que quede fuera del contorno."));
    free_->setEnabled(hasFreeZone);
    if (!hasFreeZone) {
        free_->setToolTip(free_->toolTip() +
                          tr("\n\nNo hay ninguna zona libre dibujada todavía."));
    }
    for (auto* button : {off_, automatic_, fixed_, free_}) {
        group->addButton(button);
        root->addWidget(button);
    }
    switch (mode) {
        case vision::WorkingZoneMode::Off: off_->setChecked(true); break;
        case vision::WorkingZoneMode::Automatic: automatic_->setChecked(true); break;
        case vision::WorkingZoneMode::Fixed:
            (hasFixedZone ? fixed_ : off_)->setChecked(true);
            break;
        case vision::WorkingZoneMode::Free:
            (hasFreeZone ? free_ : off_)->setChecked(true);
            break;
    }

    status_ = new QLabel(tr("Procesando la imagen entera."), this);
    status_->setWordWrap(true);
    root->addWidget(status_);

    // --- Dónde se va el tiempo (R2) ---
    //
    // Los tiempos que hay documentados se midieron una vez con un programa
    // suelto. Sirvió para decidir entonces y no sirve para saber si hoy sigue
    // siendo verdad en otra máquina, con otra resolución y con herramientas
    // dibujadas — que es justo cuando alguien abre esta pestaña.
    auto* timingBox = new QGroupBox(tr("Dónde se va el tiempo"), this);
    auto* timingLayout = new QVBoxLayout(timingBox);
    measureStages_ = new QCheckBox(tr("Medir el reparto por etapas"), timingBox);
    measureStages_->setToolTip(
        tr("Cronometra cada etapa del análisis. Va apagado por defecto porque "
           "esto corre en CADA frame: apagado no cuesta ni una llamada al reloj. "
           "Enciéndelo solo mientras miras, y apágalo al terminar."));
    timingLayout->addWidget(measureStages_);

    stageBreakdown_ = new QLabel(tr("Sin medir."), timingBox);
    stageBreakdown_->setWordWrap(true);
    stageBreakdown_->setTextFormat(Qt::PlainText);
    timingLayout->addWidget(stageBreakdown_);
    root->addWidget(timingBox);

    connect(measureStages_, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) {
            stageBreakdown_->setText(tr("Sin medir."));
        }
        emit stageMeasurementToggled(on);
    });

    root->addStretch(1);

    // `this->` a proposito: el parametro del constructor se llama igual que
    // el metodo, y sin cualificar el lambda se referiria al parametro.
    const auto emitMode = [this] { emit modeChanged(this->mode()); };
    connect(off_, &QRadioButton::toggled, this, emitMode);
    connect(automatic_, &QRadioButton::toggled, this, emitMode);
    connect(fixed_, &QRadioButton::toggled, this, emitMode);
    connect(free_, &QRadioButton::toggled, this, emitMode);
}

void PerformancePage::showMode(vision::WorkingZoneMode mode, bool hasFixedZone,
                               bool hasFreeZone) {
    if (off_ == nullptr) {
        return;
    }
    fixed_->setEnabled(hasFixedZone);
    free_->setEnabled(hasFreeZone);
    // Los botones se bloquean a la vez: marcar uno desmarca otro, y cada
    // `toggled` dispararía `modeChanged` de vuelta hacia quien nos está
    // sincronizando.
    const QSignalBlocker blockOff(off_);
    const QSignalBlocker blockAuto(automatic_);
    const QSignalBlocker blockFixed(fixed_);
    const QSignalBlocker blockFree(free_);
    switch (mode) {
        case vision::WorkingZoneMode::Off: off_->setChecked(true); break;
        case vision::WorkingZoneMode::Automatic: automatic_->setChecked(true); break;
        case vision::WorkingZoneMode::Fixed:
            (hasFixedZone ? fixed_ : off_)->setChecked(true);
            break;
        case vision::WorkingZoneMode::Free:
            (hasFreeZone ? free_ : off_)->setChecked(true);
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
    if (free_ != nullptr && free_->isChecked()) {
        return vision::WorkingZoneMode::Free;
    }
    return vision::WorkingZoneMode::Off;
}

void PerformancePage::setStageStats(const vision::StageStats& stats) {
    if (stageBreakdown_ == nullptr || measureStages_ == nullptr ||
        !measureStages_->isChecked()) {
        return;
    }
    if (stats.count() == 0) {
        stageBreakdown_->setText(tr("Midiendo…"));
        return;
    }
    const vision::StageTimings mean = stats.mean();
    const auto share = [&mean](double stage) {
        return mean.total > 0.0 ? stage / mean.total * 100.0 : 0.0;
    };
    // Se dan los ms Y el porcentaje: el porcentaje dice dónde apretar y los ms
    // dicen si merece la pena apretar en algún sitio.
    QString text = tr("Media de %1 análisis · total %2 ms\n")
                       .arg(stats.count())
                       .arg(mean.total, 0, 'f', 2);
    const std::array<std::pair<QString, double>, 5> rows{
        {{tr("segmentar"), mean.segment},
         {tr("contorno"), mean.contour},
         {tr("fixture"), mean.fixture},
         {tr("normalizar"), mean.normalize},
         {tr("herramientas"), mean.tools}}};
    for (const auto& [name, value] : rows) {
        text += QStringLiteral("  %1 %2 ms (%3 %)\n")
                    .arg(name, -14)
                    .arg(value, 0, 'f', 2)
                    .arg(share(value), 0, 'f', 0);
    }
    // El hueco se enseña a propósito: si crece, hay trabajo real fuera de las
    // etapas medidas y el reparto estaría señalando el sitio equivocado.
    text += tr("  sin atribuir  %1 ms").arg(stats.unaccounted(), 0, 'f', 2);
    stageBreakdown_->setText(text);
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
