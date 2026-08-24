#include "ui/piece_mosaic.h"

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
    empty_->setStyleSheet(QStringLiteral("color:#999; padding:12px;"));
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

void PieceMosaic::setPieces(const QImage& frame, const std::vector<QPolygonF>& outlines,
                            int measured) {
    frame_ = frame;
    outlines_ = outlines;
    measured_ = measured;
    rebuild();
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
