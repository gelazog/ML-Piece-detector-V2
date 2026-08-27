#include "ui/piece_mosaic.h"
#include "ui/theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pci::ui {

namespace {

// Lado de cada baldosa, en píxeles de pantalla.
//
// 92 sale de lo que hace falta para reconocer una pieza pequeña —una tuerca M3
// fotografiada en una bandeja ocupa unos ochenta píxeles en el original— sin
// que una bandeja de cien se convierta en una pared imposible de recorrer.
constexpr int kTileSide = 92;

// Cuánto se deja alrededor del contorno al recortar, en fracción de su tamaño.
//
// Sin margen, la pieza sale pegada al borde de la baldosa y no se puede ver si
// le falta un trozo justo en el canto — que es exactamente lo que se viene a
// mirar aquí.
constexpr double kMarginFraction = 0.12;

}  // namespace

PieceMosaic::PieceMosaic(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    empty_ = new QLabel(tr("Con varias piezas en el encuadre, aquí sale cada una por "
                           "separado."),
                        this);
    empty_->setWordWrap(true);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(
        theme::textStyle(theme::kInkOff, QStringLiteral("padding:12px;")));
    root->addWidget(empty_);

    board_ = new QWidget(this);
    grid_ = new QGridLayout(board_);
    grid_->setSpacing(4);
    grid_->setContentsMargins(4, 4, 4, 4);
    // Alineado arriba a la izquierda: con pocas piezas, una cuadrícula centrada
    // las coloca en sitios distintos cada vez que cambia el número, y eso hace
    // que el operador tenga que volver a buscar la que estaba mirando.
    grid_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    area_ = new QScrollArea(this);
    area_->setWidget(board_);
    area_->setWidgetResizable(true);
    area_->setVisible(false);
    root->addWidget(area_, 1);
}

int PieceMosaic::tileCount() const {
    return static_cast<int>(tiles_.size());
}

namespace {

// ¿Son las mismas piezas, en el mismo sitio? Se compara la envolvente de cada
// una con una tolerancia: entre fotograma y fotograma una pieza quieta se mueve
// una fracción de píxel por el ruido de la segmentación, y tratar eso como «han
// cambiado» sería no arreglar nada.
bool sameLayout(const std::vector<QPolygonF>& before, const std::vector<QPolygonF>& after) {
    if (before.size() != after.size()) {
        return false;
    }
    constexpr int kTolerancePx = 6;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const QRect a = before[i].boundingRect().toAlignedRect();
        const QRect b = after[i].boundingRect().toAlignedRect();
        if (std::abs(a.x() - b.x()) > kTolerancePx ||
            std::abs(a.y() - b.y()) > kTolerancePx ||
            std::abs(a.width() - b.width()) > kTolerancePx ||
            std::abs(a.height() - b.height()) > kTolerancePx) {
            return false;
        }
    }
    return true;
}

}  // namespace

void PieceMosaic::setPieces(const QImage& frame, const std::vector<QPolygonF>& outlines,
                            int measured) {
    // NO SE RECONSTRUYE SI NO HACE FALTA, y esto es un arreglo, no una
    // optimización.
    //
    // Queja del taller: «en piezas de encuadre, bastantes veces, cuando la
    // presiono no se cambia de imagen».
    //
    // `setPieces` se llama en CADA fotograma analizado, y `rebuild()` destruye
    // todas las baldosas y crea otras. Un clic necesita que apretar y soltar
    // caigan en el MISMO widget: si entre las dos cosas llega un fotograma, el
    // botón que se apretó ya no existe y el clic no llega a ninguna parte. De
    // ahí el «bastantes veces» — depende de si el fotograma cae en medio.
    //
    // Y de paso: con la bandeja de cien tuercas eso era crear y destruir cien
    // QToolButton por fotograma para enseñar lo mismo.
    const bool layoutHeld = sameLayout(outlines_, outlines);
    frame_ = frame;
    outlines_ = outlines;
    const int previousMeasured = measured_;
    measured_ = measured;
    if (layoutHeld && !tiles_.empty()) {
        refreshTiles(previousMeasured != measured_);
        return;
    }
    rebuild();
}

// Poner al día lo que se ve sin tocar los widgets: qué baldosa lleva el marco y
// qué número va resaltado. La imagen del recorte no se rehace — entre
// fotogramas de la misma escena no cambia lo bastante como para pagar cien
// escalados, y el operador no lo distinguiría.
void PieceMosaic::refreshTiles(bool measuredChanged) {
    if (!measuredChanged) {
        return;
    }
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        const int number = static_cast<int>(i) + 1;
        tiles_[i]->setChecked(number == measured_);
        tiles_[i]->setStyleSheet(
            number == measured_
                ? QStringLiteral("QToolButton { border:2px solid %1; border-radius:4px; }")
                      .arg(QString(theme::kChipEdited))
                : QStringLiteral("QToolButton { border:1px solid %1; border-radius:4px; }")
                      .arg(QString(theme::kInkMuted)));
    }
}

void PieceMosaic::rebuild() {
    for (auto* tile : tiles_) {
        grid_->removeWidget(tile);
        tile->deleteLater();
    }
    tiles_.clear();

    // Con una sola pieza esto no aporta: el vídeo ya la enseña entera y más
    // grande. Un panel que se queda abierto enseñando una baldosa ocupa sitio
    // para no decir nada.
    const bool worthShowing = outlines_.size() > 1 && !frame_.isNull();
    empty_->setVisible(!worthShowing);
    area_->setVisible(worthShowing);
    if (!worthShowing) {
        return;
    }

    // Cuántas caben a lo ancho. Se recalcula porque el panel es acoplable y su
    // anchura cambia cuando el operador lo arrastra.
    const int available = std::max(kTileSide, area_->viewport()->width() - 16);
    const int columns = std::max(1, available / (kTileSide + 4));

    for (std::size_t i = 0; i < outlines_.size(); ++i) {
        const QPolygonF& outline = outlines_[i];
        if (outline.isEmpty()) {
            continue;
        }
        QRect box = outline.boundingRect().toAlignedRect();
        const int margin = static_cast<int>(
            std::lround(kMarginFraction * std::max(box.width(), box.height())));
        box.adjust(-margin, -margin, margin, margin);
        box &= QRect(0, 0, frame_.width(), frame_.height());
        if (box.width() < 2 || box.height() < 2) {
            continue;
        }

        const int number = static_cast<int>(i) + 1;
        auto* tile = new QToolButton(board_);
        tile->setIconSize(QSize(kTileSide - 12, kTileSide - 12));
        tile->setFixedSize(kTileSide, kTileSide);
        tile->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tile->setAutoRaise(true);
        tile->setCheckable(true);
        tile->setChecked(number == measured_);
        tile->setToolTip(tr("Pieza %1. Púlsala para medir esta.").arg(number));

        QPixmap art = QPixmap::fromImage(frame_.copy(box))
                          .scaled(QSize(kTileSide - 12, kTileSide - 12), Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation);
        // El número va PINTADO sobre el recorte y no en el texto del botón: con
        // baldosas de noventa píxeles, un texto debajo se come la mitad del
        // sitio que necesita la pieza.
        {
            QPainter painter(&art);
            painter.setRenderHint(QPainter::Antialiasing);
            const QString label = QString::number(number);
            const QFontMetrics metrics(painter.font());
            const QRectF badge(2, 2, metrics.horizontalAdvance(label) + 8,
                               metrics.height() + 2);
            painter.setPen(Qt::NoPen);
            painter.setBrush(number == measured_ ? QColor(0, 190, 0, 220)
                                                 : QColor(0, 0, 0, 170));
            painter.drawRoundedRect(badge, 3.0, 3.0);
            painter.setPen(number == measured_ ? QColor(10, 30, 10) : QColor(230, 230, 230));
            painter.drawText(badge, Qt::AlignCenter, label);
        }
        tile->setIcon(QIcon(art));
        // La que se está midiendo lleva marco: el estado marcado de un botón
        // plano es demasiado sutil para verlo entre cien.
        tile->setStyleSheet(
            number == measured_
                ? QStringLiteral("QToolButton { border:2px solid #00be00; border-radius:4px; }")
                : QStringLiteral("QToolButton { border:1px solid #444; border-radius:4px; }"));

        connect(tile, &QToolButton::clicked, this,
                [this, number] { emit pieceChosen(number); });
        grid_->addWidget(tile, static_cast<int>(tiles_.size()) / columns,
                         static_cast<int>(tiles_.size()) % columns);
        tiles_.push_back(tile);
    }
}

}  // namespace pci::ui
