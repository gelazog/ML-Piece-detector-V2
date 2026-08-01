#include "ui/camera_controls_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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

CameraControlsDialog::CameraControlsDialog(
    camera::CameraController& controller,
    const std::vector<camera::CameraControlState>& probed, QWidget* parent)
    : QDialog(parent), controller_(controller) {
    setWindowTitle(tr("Controles de la cámara"));

    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Ajustes de la propia cámara, no del procesado. Lo que tu cámara no "
           "soporta aparece deshabilitado. Los cambios se ven en el vídeo al "
           "instante y se guardan para la próxima sesión."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

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

    syncAutoDependencies();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);
}

bool CameraControlsDialog::autoActive(camera::CameraProperty autoProperty) const {
    for (const auto& row : rows_) {
        if (row.property == autoProperty && row.toggle != nullptr) {
            return row.toggle->isEnabled() && row.toggle->isChecked();
        }
    }
    return false;
}

void CameraControlsDialog::syncAutoDependencies() {
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

void CameraControlsDialog::apply(camera::CameraProperty property, double value) {
    const camera::CameraControlValue control{property, value};
    controller_.requestControls({control});
    emit controlChanged(control);
}

}  // namespace pci::ui
