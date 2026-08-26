#include "ui/inspection_result_dialog.h"
#include "ui/theme.h"

#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QSaveFile>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include "camera/frame_utils.h"
#include <algorithm>
#include "vision/difference_map.h"
#include "inspection_editor/execution/measurement_report.h"
#include "ui/video_widget.h"

namespace pci::ui {

namespace {

// El punto peor, dicho en palabras.
//
// Unas coordenadas del recorte normalizado no le sirven de nada a quien tiene la
// pieza en la mano: ese recorte está centrado y girado al eje principal, así que
// «x=100, y=150» no señala ningún sitio del mundo. Partido en nueve, en cambio,
// sí se puede ir a mirar.
QString describeSpot(const cv::Point& point, const cv::Size& crop) {
    if (crop.width <= 0 || crop.height <= 0) {
        return QObject::tr("en algún punto");
    }
    const int col = std::clamp(point.x * 3 / crop.width, 0, 2);
    const int row = std::clamp(point.y * 3 / crop.height, 0, 2);
    static const char* const kRows[3] = {QT_TR_NOOP("arriba"), QT_TR_NOOP("en el centro"),
                                         QT_TR_NOOP("abajo")};
    static const char* const kCols[3] = {QT_TR_NOOP("a la izquierda"), QT_TR_NOOP(""),
                                         QT_TR_NOOP("a la derecha")};
    const QString vertical = QObject::tr(kRows[row]);
    const QString horizontal = QObject::tr(kCols[col]);
    if (horizontal.isEmpty()) {
        return row == 1 ? QObject::tr("justo en el centro") : vertical;
    }
    return vertical + QStringLiteral(" ") + horizontal;
}

}  // namespace

InspectionResultDialog::InspectionResultDialog(
    const QImage& frame, engine::InspectionEngine::Outcome outcome,
    engine::InspectionEngine* engine, std::int64_t pieceId, const QImage& referenceThumb,
    domain::ScaleCalibration calibration, QWidget* parent)
    : QDialog(parent),
      outcome_(std::move(outcome)),
      calibration_(calibration),
      engine_(engine),
      pieceId_(pieceId) {
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

    // DÓNDE se diferencia, y no sólo cuánto.
    //
    // Hasta aquí el diálogo daba dos miniaturas y un número de similitud, y
    // encontrar qué le pasa a la pieza era cosa del ojo del operador. Con
    // piezas pequeñas o defectos finos eso no se puede hacer, y lo que acaba
    // pasando es que se acepta el veredicto sin entenderlo.
    //
    // La tercera miniatura sólo aparece si hay algo que señalar. Un hueco vacío
    // donde a veces sale una imagen se entiende peor que no tener el hueco, y
    // un mapa que siempre enseña algo enseña a ignorarlo.
    QString differenceNote;
    if (!referenceThumb.isNull() && !outcome_.analysis.normalized.empty()) {
        const cv::Mat reference = camera::qImageToMat(referenceThumb);
        const auto map = vision::compareToReference(outcome_.analysis.normalized, reference);
        if (map.ok && map.worstValue >= 0.05) {
            const cv::Mat painted =
                vision::paintDifference(outcome_.analysis.normalized, map);
            addThumb(tr("Dónde difiere"), camera::matToQImage(painted).copy());
            differenceNote =
                tr("Lo más distinto está %1 de la pieza, y ocupa el %2 % de su superficie.")
                    .arg(describeSpot(map.worst, outcome_.analysis.normalized.size()))
                    .arg(100.0 * map.litFraction, 0, 'f', 1);
        }
    }

    compareLayout->addStretch(1);
    sideLayout->addLayout(compareLayout);

    if (!differenceNote.isEmpty()) {
        auto* where = new QLabel(differenceNote, this);
        where->setWordWrap(true);
        sideLayout->addWidget(where);
    }

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

    // Reglas de posición del modo Especial (M4): se explican aquí para que el
    // operador vea POR QUÉ una pieza bien medida puede salir NG.
    if (const auto& position = outcome_.verdict.position; position.evaluated) {
        QStringList parts;
        if (position.radiusEvaluated) {
            parts << tr("desviación %1 px (máx %2)")
                         .arg(position.radius, 0, 'f', 1)
                         .arg(position.maxRadius, 0, 'f', 1);
        }
        if (position.angleEvaluated) {
            parts << tr("giro %1° (máx %2°)")
                         .arg(position.angleDeg, 0, 'f', 1)
                         .arg(position.maxAngleDeg, 0, 'f', 1);
        }
        auto* label = new QLabel(tr("Posición en el tablero: %1 — %2")
                                     .arg(parts.join(QStringLiteral(", ")),
                                          position.ok ? tr("dentro de tolerancia")
                                                      : tr("FUERA DE TOLERANCIA")),
                                 this);
        label->setWordWrap(true);
        if (!position.ok) {
            label->setStyleSheet(
            theme::textStyle(theme::kBadOnDark, QStringLiteral("font-weight:bold;")));
        }
        sideLayout->addWidget(label);
    }
    if (const auto& position = outcome_.verdict.position; !position.note.empty()) {
        auto* note = new QLabel(QString::fromStdString(position.note), this);
        note->setWordWrap(true);
        note->setStyleSheet(theme::textStyle(theme::kWarnOnDark));
        sideLayout->addWidget(note);
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
        // Un punto construido no tiene medida: sus coordenadas van en el
        // detalle. Escribir "0,0 px" sería un número inventado. El resto lo
        // rotula `formatMeasure`: la unidad se decide en un solo sitio.
        const QString measure =
            result.informative
                ? QStringLiteral("—")
                : QString::fromStdString(inspection::formatMeasure(
                      result, calibration.mmPerPixel, inspection::LengthUnit::Auto));
        table->setItem(row, 1, new QTableWidgetItem(measure));
        // Una construcción que salió bien no es un OK verde: no ha juzgado
        // nada, solo ha calculado el datum. Que falle sí es un NG, porque deja
        // sin referencia a todo lo que la usaba.
        const bool neutral = result.informative && result.ok;
        auto* state = new QTableWidgetItem(neutral  ? QStringLiteral("—")
                                           : result.ok ? QStringLiteral("OK")
                                                       : QStringLiteral("NG"));
        // Esta tabla va sobre fondo OSCURO: los tokens claros no valen aquí.
        state->setForeground(neutral    ? QBrush(theme::color(theme::kInkMutedOnDark))
                             : result.ok ? QBrush(theme::color(theme::kGoodOnDark))
                                         : QBrush(theme::color(theme::kBadOnDark)));
        table->setItem(row, 2, state);
        table->setItem(row, 3,
                       new QTableWidgetItem(QString::fromStdString(result.detail)));
    }
    sideLayout->addWidget(table, 1);

    if (!outcome_.persistError.empty()) {
        auto* persist = new QLabel(tr("Aviso: historial no guardado (%1)")
                                       .arg(QString::fromStdString(outcome_.persistError)),
                                   this);
        persist->setStyleSheet(theme::textStyle(theme::kWarnOnDark));
        persist->setWordWrap(true);
        sideLayout->addWidget(persist);
    }

    contentLayout->addLayout(sideLayout, 2);
    rootLayout->addLayout(contentLayout, 1);

    // Aprendizaje incremental: solo si fue OK y hubo embedding.
    auto* bottomLayout = new QHBoxLayout();
    learnStatus_ = new QLabel(this);
    bottomLayout->addWidget(learnStatus_, 1);

    // Las dos salidas, y son dos porque sirven para cosas distintas: el CSV va
    // a una hoja de cálculo y el portapapeles a un correo o a un parte. Dar
    // solo una obligaría a la mitad de la gente a reformatear a mano.
    auto* copyButton = new QPushButton(tr("Copiar medidas"), this);
    copyButton->setToolTip(
        tr("Copia la tabla como texto alineado, listo para pegar en un correo o\n"
           "en un parte de inspección."));
    bottomLayout->addWidget(copyButton);
    auto* exportButton = new QPushButton(tr("Exportar CSV…"), this);
    exportButton->setToolTip(
        tr("Guarda las medidas con su unidad, sus píxeles y su tolerancia, en\n"
           "columnas que una hoja de cálculo puede sumar y promediar."));
    bottomLayout->addWidget(exportButton);
    connect(copyButton, &QPushButton::clicked, this,
            &InspectionResultDialog::onCopyMeasurementsClicked);
    connect(exportButton, &QPushButton::clicked, this,
            &InspectionResultDialog::onExportMeasurementsClicked);
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
        learnStatus_->setStyleSheet(theme::textStyle(theme::kGood));
        learnStatus_->setText(tr("Referencia actualizada a la versión %1 (las versiones "
                                 "anteriores se conservan).")
                                  .arg(version.value()));
    } else {
        learnStatus_->setStyleSheet(theme::textStyle(theme::kBadOnDark));
        learnStatus_->setText(QString::fromStdString(version.error().message));
        learnButton_->setEnabled(true);
    }
}

// Las filas del informe salen de un solo sitio (`measurementRows`), y por eso
// el CSV, el portapapeles y la tabla de arriba dicen lo mismo. Tenerlo por
// duplicado acabaría con tres respuestas distintas para la misma medida, que es
// exactamente el fallo que se acaba de arreglar en el rotulado.
void InspectionResultDialog::onCopyMeasurementsClicked() {
    const auto rows = inspection::measurementRows(outcome_.toolResults,
                                                  calibration_.mmPerPixel,
                                                  inspection::LengthUnit::Auto);
    QGuiApplication::clipboard()->setText(
        QString::fromStdString(inspection::measurementsToText(rows)));
    learnStatus_->setStyleSheet(theme::textStyle(theme::kGood));
    learnStatus_->setText(tr("%n medida(s) copiadas al portapapeles.", nullptr,
                             static_cast<int>(rows.size())));
}

void InspectionResultDialog::onExportMeasurementsClicked() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar medidas"), QStringLiteral("medidas.csv"),
        tr("CSV (*.csv);;Todos (*)"));
    if (path.isEmpty()) {
        return;  // cancelar no es un error
    }
    const auto rows = inspection::measurementRows(outcome_.toolResults,
                                                  calibration_.mmPerPixel,
                                                  inspection::LengthUnit::Auto);
    const std::string csv = inspection::measurementsToCsv(rows);
    // `QSaveFile` y no `QFile`: escribe a un temporal y renombra al cerrar, así
    // que un fallo a mitad de escritura no deja el fichero anterior destruido y
    // medio sobrescrito.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
        file.write(csv.data(), static_cast<qint64>(csv.size())) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("No se pudo exportar"),
                             tr("No se pudo escribir en %1: %2")
                                 .arg(path, file.errorString()));
        return;
    }
    learnStatus_->setStyleSheet(theme::textStyle(theme::kGood));
    learnStatus_->setText(tr("%n medida(s) exportadas a %1.", nullptr,
                             static_cast<int>(rows.size()))
                              .arg(path));
}

}  // namespace pci::ui
