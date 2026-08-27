#include "ui/background_patch_dialog.h"

#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <functional>

#include "camera/frame_utils.h"
#include "ui/dialog_buttons.h"
#include "ui/theme.h"
#include "vision/pipeline.h"

namespace pci::ui {

// LA VISTA: la imagen a escala, el rectángulo que se arrastra y, encima, lo que
// esa elección deja como pieza.
//
// El recuadro se guarda SIEMPRE en coordenadas de la imagen y se escala al
// pintar, nunca al revés. Guardarlo en píxeles de pantalla haría que cambiar el
// tamaño de la ventana moviera la selección — el operador vería desplazarse lo
// que acaba de señalar.
class BackgroundPatchDialog::PatchView : public QWidget {
public:
    explicit PatchView(QWidget* parent) : QWidget(parent) {
        setMinimumSize(360, 270);
        setCursor(Qt::CrossCursor);
    }

    void setFrame(const QImage& frame) {
        frame_ = frame;
        update();
    }

    // La máscara de lo que saldría como PIEZA con el color elegido. Vacía = aún
    // no se ha señalado nada.
    void setPreview(const QImage& mask) {
        mask_ = mask;
        update();
    }

    void setPatch(const QRect& patch) {
        patch_ = patch;
        update();
    }

    std::function<void(QRect)> onPatch;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        anchor_ = toImage(event->position().toPoint());
        dragging_ = true;
        patch_ = QRect(anchor_, anchor_);
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_) {
            return;
        }
        patch_ = QRect(anchor_, toImage(event->position().toPoint())).normalized();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!dragging_) {
            return;
        }
        dragging_ = false;
        patch_ = QRect(anchor_, toImage(event->position().toPoint())).normalized();
        // La muestra se toma AL SOLTAR y no en cada movimiento: la vista previa
        // corre la segmentación entera, y hacerlo por cada píxel de arrastre
        // dejaría la ventana pegada mientras se dibuja el recuadro.
        if (onPatch) {
            onPatch(patch_);
        }
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), theme::color(theme::kSurfaceDark));
        if (frame_.isNull()) {
            return;
        }
        const QRect target = fitted();
        painter.drawImage(target, frame_);
        if (!mask_.isNull()) {
            // Se tiñe la PIEZA y no el fondo porque es lo que el operador viene
            // a comprobar: «¿salen mis arandelas?». Marcar el fondo dejaría la
            // respuesta en negativo, y hay que leerla de un vistazo.
            painter.setOpacity(0.45);
            painter.drawImage(target, mask_);
            painter.setOpacity(1.0);
        }
        if (patch_.isValid() && !patch_.isEmpty()) {
            painter.setPen(QPen(theme::color(theme::kGoodOnDark), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(toScreen(patch_));
        }
    }

private:
    // El sitio de la imagen dentro del widget, guardando la proporción.
    [[nodiscard]] QRect fitted() const {
        if (frame_.isNull()) {
            return rect();
        }
        const QSize scaled = frame_.size().scaled(size(), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2),
                     scaled);
    }

    [[nodiscard]] QPoint toImage(const QPoint& screen) const {
        const QRect target = fitted();
        if (target.width() <= 0 || target.height() <= 0 || frame_.isNull()) {
            return {};
        }
        const double x =
            static_cast<double>(screen.x() - target.x()) * frame_.width() / target.width();
        const double y =
            static_cast<double>(screen.y() - target.y()) * frame_.height() / target.height();
        return {std::clamp(static_cast<int>(x), 0, frame_.width() - 1),
                std::clamp(static_cast<int>(y), 0, frame_.height() - 1)};
    }

    [[nodiscard]] QRect toScreen(const QRect& image) const {
        const QRect target = fitted();
        if (frame_.isNull() || frame_.width() == 0 || frame_.height() == 0) {
            return {};
        }
        const double sx = static_cast<double>(target.width()) / frame_.width();
        const double sy = static_cast<double>(target.height()) / frame_.height();
        return QRect(target.x() + static_cast<int>(image.x() * sx),
                     target.y() + static_cast<int>(image.y() * sy),
                     static_cast<int>(image.width() * sx),
                     static_cast<int>(image.height() * sy));
    }

    QImage frame_;
    QImage mask_;
    QRect patch_;
    QPoint anchor_;
    bool dragging_ = false;
};

namespace {

// La máscara de piezas, teñida, con el fondo transparente. Se construye aquí y
// no en la vista para que la vista solo pinte.
QImage tinted(const cv::Mat& mask) {
    QImage image(mask.cols, mask.rows, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QColor ink = theme::color(theme::kGoodOnDark);
    const QRgb piece = qRgba(ink.red(), ink.green(), ink.blue(), 255);
    for (int y = 0; y < mask.rows; ++y) {
        const auto* row = mask.ptr<unsigned char>(y);
        auto* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < mask.cols; ++x) {
            out[x] = row[x] != 0 ? piece : qRgba(0, 0, 0, 0);
        }
    }
    return image;
}

}  // namespace

BackgroundPatchDialog::BackgroundPatchDialog(const cv::Mat& frame,
                                             vision::SegmentationOptions options,
                                             QWidget* parent)
    : QDialog(parent), frame_(frame.clone()), options_(options) {
    setWindowTitle(tr("Señalar el fondo"));
    setObjectName(QStringLiteral("backgroundPatchDialog"));

    auto* layout = new QVBoxLayout(this);

    auto* howto = new QLabel(
        tr("Arrastra un recuadro sobre un trozo de mesa VACÍO. En verde, lo que quedaría "
           "como pieza."),
        this);
    howto->setObjectName(QStringLiteral("howto"));
    howto->setWordWrap(true);
    layout->addWidget(howto);

    view_ = new PatchView(this);
    view_->setFrame(camera::matToQImage(frame_));
    view_->onPatch = [this](QRect patch) {
        selectPatch(cv::Rect(patch.x(), patch.y(), patch.width(), patch.height()));
    };
    layout->addWidget(view_, 1);

    swatch_ = new QLabel(this);
    swatch_->setObjectName(QStringLiteral("swatch"));
    layout->addWidget(swatch_);

    verdict_ = new QLabel(this);
    verdict_->setObjectName(QStringLiteral("verdict"));
    verdict_->setWordWrap(true);
    layout->addWidget(verdict_);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("preview"));
    preview_->setWordWrap(true);
    layout->addWidget(preview_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    nameButtonsInSpanish(buttons);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    okButton_->setObjectName(QStringLiteral("takeColour"));
    okButton_->setText(tr("Usar este fondo"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refresh();
}

void BackgroundPatchDialog::selectPatch(const cv::Rect& patch) {
    patch_ = patch;
    sample_ = vision::sampleBackground(frame_, patch_);
    previewPieces_ = 0;
    previewCoverage_ = 0.0;

    if (sample_.valid) {
        // LA VISTA PREVIA CORRE LA SEGMENTACIÓN DE VERDAD.
        //
        // Podría enseñarse «lo que se parece al color elegido» con un umbral
        // inventado aquí: sería más rápido y más bonito. Pero entonces la
        // ventana enseñaría una cosa y el programa haría otra, que es
        // exactamente el fallo que esta ventana viene a evitar.
        //
        // Y lo que cuesta lo aguanta el gesto: medido, 32 ms sobre 993x903 y
        // 363 ms sobre 3972x3612 —una foto de 14 megapíxeles— contando la
        // segmentación Y el análisis. Por eso se hace al SOLTAR y no mientras
        // se arrastra: por fotograma de arrastre sí se notaría.
        vision::SegmentationOptions preview = options_;
        preview.backgroundKey = vision::SegmentationOptions::BackgroundKey::Fixed;
        preview.background = sample_.colour;
        auto mask = vision::segmentPiece(frame_, preview);
        if (mask.isOk()) {
            const cv::Mat& binary = mask.value();
            previewCoverage_ =
                100.0 * cv::countNonZero(binary) / static_cast<double>(binary.total());
            view_->setPreview(tinted(binary));
        }
        vision::PipelineConfig config;
        config.segmentation = preview;
        auto pieces = vision::analyzeFrames(frame_, config);
        if (pieces.isOk()) {
            previewPieces_ = static_cast<int>(pieces.value().size());
        }
    } else {
        view_->setPreview({});
    }
    view_->setPatch(QRect(patch_.x, patch_.y, patch_.width, patch_.height));
    refresh();
}

void BackgroundPatchDialog::refresh() {
    if (!sample_.valid) {
        swatch_->setText(tr("Sin señalar."));
        swatch_->setStyleSheet(theme::textStyle(theme::kInkMuted));
        verdict_->clear();
        preview_->clear();
        // Sin selección no hay color que llevarse: aceptar dejaría el ajuste
        // como estaba pero haría creer que se cambió algo.
        okButton_->setEnabled(false);
        return;
    }
    okButton_->setEnabled(true);

    const QColor colour(sample_.colour[2], sample_.colour[1], sample_.colour[0]);
    swatch_->setText(tr("Fondo: %1").arg(colour.name().toUpper()));
    swatch_->setStyleSheet(theme::chipStyle(colour.name().toUtf8().constData()));

    if (sample_.looksUniform) {
        verdict_->setText(
            tr("Parece mesa: varía %1 sobre sí misma.").arg(sample_.spread, 0, 'f', 0));
        verdict_->setStyleSheet(theme::noticeStyle(theme::kGood, theme::kGoodField));
    } else {
        // Con el número dentro, no una corazonada: el operador puede volver a
        // arrastrar y ver cómo baja cuando acierta con un trozo de mesa limpio.
        verdict_->setText(
            tr("Esto no parece mesa: varía %1 sobre sí misma, y una mesa vacía varía "
               "menos de %2. Habrás cogido pieza dentro del recuadro.")
                .arg(sample_.spread, 0, 'f', 0)
                .arg(vision::kBackgroundPatchIsUniform, 0, 'f', 0));
        verdict_->setStyleSheet(theme::noticeStyle(theme::kWarn, theme::kWarnField));
    }

    preview_->setText(tr("Con este fondo se ven %1 piezas, el %2 % del cuadro.")
                          .arg(previewPieces_)
                          .arg(previewCoverage_, 0, 'f', 1));
    preview_->setStyleSheet(theme::textStyle(theme::kInkMuted));
}

}  // namespace pci::ui
