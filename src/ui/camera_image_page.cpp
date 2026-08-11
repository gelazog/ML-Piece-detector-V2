#include "ui/camera_image_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

#include "camera/camera_controller.h"

namespace pci::ui {

namespace {

// Los deslizadores de Qt son enteros; los controles pueden ser decimales
// (escala 0..1), así que se trabaja en pasos y se convierte al aplicar.
int toSteps(double value, const camera::PropertyRange& range) {
    return static_cast<int>(std::lround((value - range.min) / range.step));
}

double fromSteps(int steps, const camera::PropertyRange& range) {
    return range.min + static_cast<double>(steps) * range.step;
}

QString formatValue(double value, const camera::PropertyRange& range) {
    return QString::number(value, 'f', range.step < 1.0 ? 2 : 0);
}

}  // namespace

CameraImagePage::CameraImagePage(
    camera::CameraController& controller,
    const std::vector<camera::CameraControlState>& probed,
    const std::vector<camera::CameraResolution>& knownResolutions,
    const camera::CameraResolution& currentResolution, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Ajustes de la propia cámara, no del procesado. Lo que tu cámara no "
           "soporta aparece deshabilitado. Los cambios se ven en el vídeo al "
           "instante y se guardan para la próxima sesión."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    // Resolución: la lista tarda un instante en llegar porque hay que
    // preguntarle a la cámara resolución por resolución (OpenCV no las lista).
    auto* resolutionRow = new QHBoxLayout();
    resolutionRow->addWidget(new QLabel(tr("Resolución:"), this));
    resolutionCombo_ = new QComboBox(this);
    resolutionCombo_->setToolTip(
        tr("Resoluciones que esta cámara acepta de verdad.\n"
           "Más resolución = más detalle y medidas más finas, pero más CPU por\n"
           "frame. Al cambiarla, la calibración en mm deja de ser válida (se\n"
           "avisa) y la zona de detección y el cero fijado se reajustan solos."));
    resolutionRow->addWidget(resolutionCombo_, 1);
    probeButton_ = new QPushButton(tr("Buscar…"), this);
    probeButton_->setToolTip(
        tr("Pregunta a la cámara resolución por resolución.\n"
           "Tarda unos segundos y el vídeo se detiene mientras dura, por eso el\n"
           "resultado se recuerda y no hace falta repetirlo."));
    resolutionRow->addWidget(probeButton_);
    root->addLayout(resolutionRow);

    connect(&controller, &camera::CameraController::resolutionsProbed, this,
            &CameraImagePage::onResolutionsProbed);
    connect(probeButton_, &QPushButton::clicked, this, [this] {
        probeButton_->setEnabled(false);
        probeButton_->setText(tr("Buscando…"));
        controller_.requestResolutionProbe();
    });

    if (!knownResolutions.empty()) {
        onResolutionsProbed(knownResolutions, currentResolution);
    } else {
        // Sin lista conocida no se sondea solo: se muestra la actual y se deja
        // que el operador decida pagar la pausa del vídeo.
        resolutionCombo_->addItem(
            currentResolution.valid()
                ? QStringLiteral("%1 × %2").arg(currentResolution.width)
                      .arg(currentResolution.height)
                : tr("(desconocida)"),
            QVariant::fromValue(currentResolution));
        resolutionCombo_->setEnabled(false);
    }

    auto* form = new QFormLayout();
    for (const auto& state : probed) {
        const camera::PropertyRange range =
            camera::rangeFor(state.property, state.min, state.max);
        const QString label = QString::fromUtf8(camera::propertyLabel(state.property));
        const QString help = QString::fromUtf8(camera::propertyHelp(state.property));

        Row row;
        row.property = state.property;
        row.supported = state.supported;
        if (camera::isToggle(state.property)) {
            row.toggle = new QCheckBox(tr("activado"), this);
            row.toggle->setChecked(state.value > 0.0);
            row.toggle->setEnabled(state.supported);
            row.toggle->setToolTip(help);
            connect(row.toggle, &QCheckBox::toggled, this,
                    [this, property = state.property](bool on) {
                        apply(property, on ? 1.0 : 0.0);
                        syncAutoDependencies();
                    });
            form->addRow(label, row.toggle);
        } else {
            auto* line = new QHBoxLayout();
            row.slider = new QSlider(Qt::Horizontal, this);
            row.slider->setRange(toSteps(range.min, range), toSteps(range.max, range));
            row.slider->setValue(toSteps(state.value, range));
            row.slider->setEnabled(state.supported);
            row.slider->setToolTip(
                help + tr("\n\nRango admitido por esta cámara: %1 a %2.")
                           .arg(range.min, 0, 'f', range.step < 1.0 ? 2 : 0)
                           .arg(range.max, 0, 'f', range.step < 1.0 ? 2 : 0));
            auto* readout = new QLabel(formatValue(state.value, range), this);
            readout->setMinimumWidth(48);
            readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            connect(row.slider, &QSlider::valueChanged, this,
                    [this, property = state.property, range, readout](int steps) {
                        const double value = fromSteps(steps, range);
                        readout->setText(formatValue(value, range));
                        apply(property, value);
                    });
            line->addWidget(row.slider, 1);
            line->addWidget(readout);
            form->addRow(label, line);
        }
        if (!state.supported) {
            // Decir POR QUÉ está apagado evita que parezca un fallo de la app.
            // Se comprobó al abrir: la cámara rechazó escribir la propiedad.
            auto* note = new QLabel(tr("(esta cámara no deja cambiarlo)"), this);
            note->setStyleSheet(QStringLiteral("color:#888;"));
            form->addRow(QString(), note);
        }
        rows_.push_back(row);
    }
    root->addLayout(form);
    root->addStretch(1);

    syncAutoDependencies();
}

void CameraImagePage::onResolutionsProbed(
    const std::vector<camera::CameraResolution>& available,
    const camera::CameraResolution& current) {
    if (probeButton_ != nullptr) {
        probeButton_->setEnabled(true);
        probeButton_->setText(tr("Buscar…"));
    }
    QSignalBlocker blocker(resolutionCombo_);
    resolutionCombo_->clear();
    for (const auto& resolution : available) {
        resolutionCombo_->addItem(
            QStringLiteral("%1 × %2").arg(resolution.width).arg(resolution.height),
            QVariant::fromValue(resolution));
        if (resolution == current) {
            resolutionCombo_->setCurrentIndex(resolutionCombo_->count() - 1);
        }
    }
    resolutionCombo_->setEnabled(resolutionCombo_->count() > 1);
    if (resolutionCombo_->count() <= 1) {
        // Una sola opcion: la camara no deja elegir, y decirlo evita que
        // parezca que el desplegable esta roto.
        resolutionCombo_->setToolTip(tr("Esta cámara solo ofrece una resolución."));
    }
    blocker.unblock();

    if (comboWired_) {
        return;
    }
    comboWired_ = true;
    connect(resolutionCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const auto resolution =
            resolutionCombo_->itemData(index).value<camera::CameraResolution>();
        if (!resolution.valid()) {
            return;
        }
        controller_.requestResolution(resolution);
        emit resolutionChosen(resolution);
    });
}

bool CameraImagePage::autoActive(camera::CameraProperty autoProperty) const {
    for (const auto& row : rows_) {
        if (row.property == autoProperty && row.toggle != nullptr) {
            return row.toggle->isEnabled() && row.toggle->isChecked();
        }
    }
    return false;
}

void CameraImagePage::syncAutoDependencies() {
    const bool autoExposure = autoActive(camera::CameraProperty::AutoExposure);
    const bool autoFocus = autoActive(camera::CameraProperty::AutoFocus);
    for (const auto& row : rows_) {
        if (row.slider == nullptr || !row.supported) {
            continue;
        }
        if (row.property == camera::CameraProperty::Exposure) {
            row.slider->setEnabled(!autoExposure);
        } else if (row.property == camera::CameraProperty::Focus) {
            row.slider->setEnabled(!autoFocus);
        }
    }
}

void CameraImagePage::apply(camera::CameraProperty property, double value) {
    const camera::CameraControlValue control{property, value};
    controller_.requestControls({control});
    emit controlChanged(control);
}

}  // namespace pci::ui
