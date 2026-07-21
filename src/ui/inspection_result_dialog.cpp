#include "ui/inspection_result_dialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include "camera/frame_utils.h"
#include "ui/video_widget.h"

namespace pci::ui {

InspectionResultDialog::InspectionResultDialog(
    const QImage& frame, engine::InspectionEngine::Outcome outcome,
    engine::InspectionEngine* engine, std::int64_t pieceId, const QImage& referenceThumb,
    domain::ScaleCalibration calibration, QWidget* parent)
    : QDialog(parent), outcome_(std::move(outcome)), engine_(engine), pieceId_(pieceId) {
    setWindowTitle(tr("Resultado de inspección"));
    resize(1000, 680);

    auto* rootLayout = new QVBoxLayout(this);

    // Banner OK/NG.
    auto* banner = new QLabel(this);
    banner->setAlignment(Qt::AlignCenter);
    banner->setMinimumHeight(48);
    banner->setStyleSheet(outcome_.verdict.ok
                              ? QStringLiteral("background:#1e6f2f; color:white; "
                                               "font-size:20px; font-weight:bold;")
                              : QStringLiteral("background:#8f1f1f; color:white; "
                                               "font-size:20px; font-weight:bold;"));
    banner->setText(QString::fromStdString(outcome_.verdict.summary));
    rootLayout->addWidget(banner);

    // Imagen anotada + tabla lado a lado.
    auto* contentLayout = new QHBoxLayout();
    auto* view = new VideoWidget(this);
    view->setFrame(annotatedFrame(frame));
    contentLayout->addWidget(view, 3);

    auto* sideLayout = new QVBoxLayout();

    // Comparación visual: pieza registrada vs recorte de la pieza actual.
    auto* compareLayout = new QHBoxLayout();
    auto addThumb = [this, compareLayout](const QString& caption, const QImage& image) {
        auto* column = new QVBoxLayout();
        column->addWidget(new QLabel(caption, this));
        auto* thumb = new QLabel(this);
        thumb->setFixedSize(130, 130);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(
            QStringLiteral("background:#1a1a1a; color:#888; border:1px solid #444;"));
        if (image.isNull()) {
            thumb->setText(QStringLiteral("—"));
        } else {
            thumb->setPixmap(QPixmap::fromImage(image).scaled(
                thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        column->addWidget(thumb);
        compareLayout->addLayout(column);
    };
    addThumb(tr("Registrada"), referenceThumb);
    addThumb(tr("Actual"), camera::matToQImage(outcome_.analysis.normalized));
    compareLayout->addStretch(1);
    sideLayout->addLayout(compareLayout);

    if (outcome_.verdict.embedding.evaluated) {
        auto* similarity = new QLabel(
            tr("Similitud de apariencia: %1 (umbral %2)")
                .arg(outcome_.verdict.embedding.similarity, 0, 'f', 4)
                .arg(outcome_.verdict.embedding.threshold, 0, 'f', 4),
            this);
        similarity->setWordWrap(true);
        sideLayout->addWidget(similarity);
    } else {
        sideLayout->addWidget(new QLabel(
            tr("Apariencia no evaluada: %1")
                .arg(QString::fromStdString(outcome_.verdict.embedding.note)),
            this));
    }

    auto* table = new QTableWidget(static_cast<int>(outcome_.toolResults.size()), 4, this);
    table->setHorizontalHeaderLabels(
        {tr("Herramienta"), tr("Medida"), tr("Estado"), tr("Detalle")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < static_cast<int>(outcome_.toolResults.size()); ++row) {
        const auto& result = outcome_.toolResults[static_cast<std::size_t>(row)];
        table->setItem(row, 0,
                       new QTableWidgetItem(QString::fromStdString(result.name)));
        QString measure;
        if (result.measuredIsAngle) {
            measure = QStringLiteral("%1°").arg(result.measured, 0, 'f', 1);
        } else if (result.type == inspection::ToolType::Blob) {
            measure = QString::number(result.measured, 'f', 0);
        } else {
            measure = QString::fromStdString(calibration.formatLength(result.measured));
        }
        table->setItem(row, 1, new QTableWidgetItem(measure));
        auto* state = new QTableWidgetItem(result.ok ? QStringLiteral("OK")
                                                     : QStringLiteral("NG"));
        state->setForeground(result.ok ? QBrush(QColor(0, 170, 0))
                                       : QBrush(QColor(220, 40, 40)));
        table->setItem(row, 2, state);
        table->setItem(row, 3,
                       new QTableWidgetItem(QString::fromStdString(result.detail)));
    }
    sideLayout->addWidget(table, 1);

    if (!outcome_.persistError.empty()) {
        auto* persist = new QLabel(tr("Aviso: historial no guardado (%1)")
                                       .arg(QString::fromStdString(outcome_.persistError)),
                                   this);
        persist->setStyleSheet(QStringLiteral("color:#ff9944;"));
        persist->setWordWrap(true);
        sideLayout->addWidget(persist);
    }

    contentLayout->addLayout(sideLayout, 2);
    rootLayout->addLayout(contentLayout, 1);

    // Aprendizaje incremental: solo si fue OK y hubo embedding.
    auto* bottomLayout = new QHBoxLayout();
    learnStatus_ = new QLabel(this);
    bottomLayout->addWidget(learnStatus_, 1);
    learnButton_ = new QPushButton(tr("Actualizar referencia (aprender)"), this);
    learnButton_->setEnabled(engine_ != nullptr && outcome_.verdict.ok &&
                             !outcome_.embedding.empty());
    if (!learnButton_->isEnabled()) {
        learnButton_->setToolTip(
            tr("Disponible solo tras una inspección OK con modelo de embeddings"));
    }
    bottomLayout->addWidget(learnButton_);
    auto* closeButton = new QPushButton(tr("Cerrar"), this);
    bottomLayout->addWidget(closeButton);
    rootLayout->addLayout(bottomLayout);

    connect(learnButton_, &QPushButton::clicked, this,
            &InspectionResultDialog::onLearnClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

QImage InspectionResultDialog::annotatedFrame(const QImage& frame) const {
    QImage annotated = frame.convertToFormat(QImage::Format_RGB32);
    QPainter painter(&annotated);
    painter.setRenderHint(QPainter::Antialiasing);

    // Contorno de la pieza, coloreado por el veredicto global.
    QPolygonF contour;
    for (const auto& p : outcome_.analysis.contour.points) {
        contour << QPointF(p.x, p.y);
    }
    QPen contourPen(outcome_.verdict.ok ? QColor(0, 220, 0) : QColor(255, 60, 60));
    contourPen.setWidthF(2.0);
    painter.setPen(contourPen);
    painter.drawPolygon(contour);

    // Overlays de cada herramienta, coloreados por su propio resultado.
    for (const auto& result : outcome_.toolResults) {
        QPen pen(result.ok ? QColor(0, 200, 255) : QColor(255, 120, 0));
        pen.setWidthF(2.0);
        painter.setPen(pen);
        for (const auto& segment : result.overlaySegments) {
            painter.drawLine(QPointF(segment[0].x, segment[0].y),
                             QPointF(segment[1].x, segment[1].y));
        }
        for (const auto& point : result.overlayPoints) {
            const QPointF p(point.x, point.y);
            painter.drawLine(p + QPointF(-5, 0), p + QPointF(5, 0));
            painter.drawLine(p + QPointF(0, -5), p + QPointF(0, 5));
        }
        if (!result.overlaySegments.empty()) {
            painter.drawText(QPointF(result.overlaySegments[0][0].x,
                                     result.overlaySegments[0][0].y - 6),
                             QString::fromStdString(result.name));
        }
    }
    return annotated;
}

void InspectionResultDialog::onLearnClicked() {
    learnButton_->setEnabled(false);
    const auto version = engine_->updateReference(pieceId_, outcome_.embedding);
    if (version.isOk()) {
        learnStatus_->setStyleSheet(QStringLiteral("color:#22cc44;"));
        learnStatus_->setText(tr("Referencia actualizada a la versión %1 (las versiones "
                                 "anteriores se conservan).")
                                  .arg(version.value()));
    } else {
        learnStatus_->setStyleSheet(QStringLiteral("color:#ff5555;"));
        learnStatus_->setText(QString::fromStdString(version.error().message));
        learnButton_->setEnabled(true);
    }
}

}  // namespace pci::ui
