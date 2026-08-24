#include "ui/lens_calibration_dialog.h"

#include <QDialogButtonBox>

#include "ui/dialog_buttons.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

#include "camera/frame_utils.h"

namespace pci::ui {

namespace {

// Verde cuando la zona ya tiene una toma, gris cuando falta. Las esquinas se
// dibujan con borde porque son las que no se pueden dejar sin cubrir.
QString cellStyle(bool touched, bool corner) {
    const QString background = touched ? QStringLiteral("#2e7d32") : QStringLiteral("#3a3a3a");
    const QString border = corner ? QStringLiteral("2px solid #ffc861")
                                  : QStringLiteral("1px solid #555");
    return QStringLiteral("background:%1; border:%2; border-radius:4px; color:#ddd;")
        .arg(background, border);
}

}  // namespace

LensCalibrationDialog::LensCalibrationDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Calibrar la lente"));
    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Imprime un tablero de ajedrez, pégalo a algo rígido y enséñaselo a la cámara "
           "desde varios sitios y con varias inclinaciones.\n\n"
           "LO IMPORTANTE es llevarlo a las ESQUINAS del encuadre, no solo al centro. La "
           "curvatura de la lente crece hacia el borde: si el cálculo no ve nunca una "
           "esquina, la adivina — y no avisa de que la está adivinando. Medido, una "
           "calibración hecha solo por el centro deja el borde un 35 % desviado, que es "
           "peor que no corregir nada."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* columns = new QHBoxLayout();

    preview_ = new QLabel(this);
    preview_->setMinimumSize(420, 320);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet(QStringLiteral("background:#1a1a1a; border:1px solid #444;"));
    preview_->setText(tr("Sin señal"));
    columns->addWidget(preview_, 3);

    auto* side = new QVBoxLayout();

    auto* boardBox = new QGroupBox(tr("El tablero que estás usando"), this);
    auto* boardForm = new QFormLayout(boardBox);
    innerCols_ = new QSpinBox(boardBox);
    innerCols_->setRange(3, 30);
    innerCols_->setValue(9);
    innerRows_ = new QSpinBox(boardBox);
    innerRows_->setRange(3, 30);
    innerRows_->setValue(6);
    const QString cornerTip =
        tr("Se cuentan las esquinas INTERIORES, no los cuadros: un tablero de 10x7\n"
           "cuadros tiene 9x6 esquinas interiores. Es el error más común al calibrar.");
    innerCols_->setToolTip(cornerTip);
    innerRows_->setToolTip(cornerTip);
    squareMm_ = new QDoubleSpinBox(boardBox);
    squareMm_->setRange(1.0, 200.0);
    squareMm_->setDecimals(2);
    squareMm_->setValue(20.0);
    squareMm_->setSuffix(tr(" mm"));
    squareMm_->setToolTip(
        tr("Mide un cuadro del tablero YA IMPRESO con una regla. Casi ninguna\n"
           "impresora saca el tamaño exacto que decía el fichero."));
    boardForm->addRow(tr("Esquinas interiores en horizontal:"), innerCols_);
    boardForm->addRow(tr("Esquinas interiores en vertical:"), innerRows_);
    boardForm->addRow(tr("Lado del cuadro:"), squareMm_);
    side->addWidget(boardBox);

    auto* coverageBox = new QGroupBox(tr("Zonas del encuadre cubiertas"), this);
    auto* grid = new QGridLayout(coverageBox);
    grid->setSpacing(4);
    for (int i = 0; i < 9; ++i) {
        cells_[i] = new QLabel(coverageBox);
        cells_[i]->setFixedSize(46, 34);
        cells_[i]->setAlignment(Qt::AlignCenter);
        grid->addWidget(cells_[i], i / 3, i % 3);
    }
    side->addWidget(coverageBox);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    side->addWidget(status_);
    advice_ = new QLabel(this);
    advice_->setWordWrap(true);
    advice_->setStyleSheet(QStringLiteral("color:#ffc861;"));
    side->addWidget(advice_);

    capture_ = new QPushButton(tr("Guardar esta toma"), this);
    capture_->setEnabled(false);
    side->addWidget(capture_);
    forget_ = new QPushButton(tr("Empezar de nuevo"), this);
    side->addWidget(forget_);
    side->addStretch(1);

    columns->addLayout(side, 2);
    root->addLayout(columns);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    nameButtonsInSpanish(buttons);
    calibrate_ = buttons->addButton(tr("Calibrar"), QDialogButtonBox::AcceptRole);
    calibrate_->setEnabled(false);
    root->addWidget(buttons);

    connect(capture_, &QPushButton::clicked, this, [this] { captureCurrent(); });
    connect(forget_, &QPushButton::clicked, this, [this] {
        views_.clear();
        result_.reset();
        refreshState();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(calibrate_, &QPushButton::clicked, this, [this] {
        const QString problem = tryCalibrate();
        if (problem.isEmpty()) {
            accept();
        } else {
            advice_->setText(problem);
        }
    });
    // Cambiar la descripción del tablero invalida lo recogido: las esquinas
    // guardadas son de una rejilla de otro tamaño y mezclarlas daría un modelo
    // sin sentido, sin que nada lo dijera.
    const auto boardChanged = [this] {
        if (!views_.empty()) {
            views_.clear();
            result_.reset();
            advice_->setText(tr("Has cambiado el tablero: las tomas anteriores ya no valen "
                                "y se han descartado."));
        }
        refreshState();
    };
    connect(innerCols_, &QSpinBox::valueChanged, this, boardChanged);
    connect(innerRows_, &QSpinBox::valueChanged, this, boardChanged);

    refreshState();
}

vision::BoardSpec LensCalibrationDialog::boardSpec() const {
    vision::BoardSpec spec;
    spec.innerCols = innerCols_ != nullptr ? innerCols_->value() : 9;
    spec.innerRows = innerRows_ != nullptr ? innerRows_->value() : 6;
    spec.squareMm = squareMm_ != nullptr ? squareMm_->value() : 20.0;
    return spec;
}

vision::BoardCoverage LensCalibrationDialog::coverage() const {
    return vision::coverageOf(views_);
}

bool LensCalibrationDialog::offerFrame(const QImage& frame) {
    if (frame.isNull()) {
        return false;
    }
    pendingFrame_ = frame;
    const cv::Mat image = camera::qImageToMat(frame);
    pending_ = vision::findBoard(image, boardSpec());
    showPreview(frame, pending_.has_value());
    refreshState();
    return pending_.has_value();
}

bool LensCalibrationDialog::captureCurrent() {
    if (!pending_.has_value()) {
        return false;
    }
    views_.push_back(*pending_);
    pending_.reset();
    result_.reset();
    refreshState();
    return true;
}

QString LensCalibrationDialog::tryCalibrate() {
    const auto calibration = vision::calibrateLens(views_, boardSpec());
    if (!calibration.isOk()) {
        return QString::fromStdString(calibration.error().message);
    }
    result_ = calibration.value();
    return {};
}

void LensCalibrationDialog::showPreview(const QImage& frame, bool found) {
    if (preview_ == nullptr) {
        return;
    }
    QImage shown = frame.convertToFormat(QImage::Format_RGB888);
    if (found && pending_.has_value()) {
        // Las esquinas encontradas, dibujadas encima. Es la única forma de que
        // el operador sepa que el tablero se está viendo ENTERO antes de pulsar:
        // media rejilla detectada da esquinas válidas y ancla el modelo a una
        // zona, que es justo lo que este asistente intenta evitar.
        QPainter painter(&shown);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0, 220, 0), 2.0));
        const auto& corners = pending_->corners;
        for (std::size_t i = 0; i + 1 < corners.size(); ++i) {
            painter.drawLine(QPointF(corners[i].x, corners[i].y),
                             QPointF(corners[i + 1].x, corners[i + 1].y));
        }
        painter.setBrush(QColor(255, 200, 0));
        painter.setPen(Qt::NoPen);
        for (const auto& corner : corners) {
            painter.drawEllipse(QPointF(corner.x, corner.y), 3.0, 3.0);
        }
    }
    preview_->setPixmap(QPixmap::fromImage(shown).scaled(
        preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void LensCalibrationDialog::refreshState() {
    const auto covered = coverage();
    for (int i = 0; i < 9; ++i) {
        if (cells_[i] == nullptr) {
            continue;
        }
        const bool corner = i == 0 || i == 2 || i == 6 || i == 8;
        cells_[i]->setStyleSheet(cellStyle(covered.touched[i], corner));
        cells_[i]->setText(covered.touched[i] ? QStringLiteral("✓")
                                              : (corner ? QStringLiteral("!") : QString()));
    }

    if (capture_ != nullptr) {
        capture_->setEnabled(pending_.has_value());
        capture_->setToolTip(pending_.has_value()
                                 ? tr("Guarda esta toma.")
                                 : tr("No se ve el tablero ENTERO. Acércalo, endereza la "
                                      "cámara o mejora la luz."));
    }
    if (status_ != nullptr) {
        status_->setText(tr("Tomas guardadas: %1 (hacen falta al menos %2).\n"
                            "Esquinas cubiertas: %3 de 4. Zonas: %4 de 9.")
                             .arg(viewCount())
                             .arg(vision::kMinimumViews)
                             .arg(covered.cornersTouched)
                             .arg(covered.cellsTouched));
    }
    const bool enough = viewCount() >= vision::kMinimumViews && covered.goodEnough();
    if (calibrate_ != nullptr) {
        calibrate_->setEnabled(enough);
    }
    if (advice_ != nullptr && !views_.empty()) {
        advice_->setText(enough ? tr("Ya se puede calibrar.")
                                : QString::fromStdString(covered.advice()));
    }
}

}  // namespace pci::ui
