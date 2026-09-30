#include "ui/verdict_board.h"

#include <QFontMetrics>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "ui/theme.h"

namespace pci::ui {

// LOS TAMAÑOS, POR LA DISTANCIA Y NO POR GUSTO.
//
// A 1–1,5 m el texto de trabajo necesita unos 20 px para leerse sin
// acercarse; el motivo va a 22. La palabra va dos escalones por encima en una
// escala de cuarta justa (22 → 29 → 39 → 52), redondeada a 48 para que el
// tablero no le robe al vídeo más de lo que hace falta: con las dos líneas y el
// margen se queda en unos 100 px de alto.
constexpr int kWordPx = 48;
constexpr int kReasonPx = 22;

VerdictLook verdictLook(VerdictState state) {
    switch (state) {
        case VerdictState::Good:
            return {theme::kGoodChip, theme::kInkOnChip, QObject::tr("✓ OK")};
        case VerdictState::Bad:
            return {theme::kBadChip, theme::kInkOnChip, QObject::tr("✕ NG")};
        case VerdictState::Failed:
            // Ámbar y no rojo: no se pudo medir no es que la pieza no valga.
            return {theme::kWarnChip, theme::kInkOnChip, QObject::tr("⚠ Sin medir")};
        case VerdictState::NoPiece:
            return {theme::kChipRest, theme::kInkOnChipRest, QObject::tr("Sin pieza")};
        case VerdictState::Working:
        case VerdictState::Hidden:
            break;
    }
    return {theme::kChipRest, theme::kInkOnChipRest, QObject::tr("En marcha")};
}

VerdictBoard::VerdictBoard(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("verdictBoard"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(12, 4, 12, 6);
    column->setSpacing(0);

    word_ = new QLabel(this);
    word_->setObjectName(QStringLiteral("verdictWord"));
    word_->setAlignment(Qt::AlignCenter);
    column->addWidget(word_);

    reasonLabel_ = new QLabel(this);
    reasonLabel_->setObjectName(QStringLiteral("verdictReason"));
    reasonLabel_->setAlignment(Qt::AlignCenter);
    // Ignorado en horizontal: un motivo largo se corta aquí, no ensancha la
    // ventana entera.
    reasonLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    column->addWidget(reasonLabel_);

    setVisible(false);
}

QString VerdictBoard::word() const { return word_->text(); }

void VerdictBoard::showVerdict(VerdictState state, const QString& reason) {
    if (state == state_ && reason == reason_) {
        return;
    }
    const bool lookChanged = state != state_ || styleSheet().isEmpty();
    state_ = state;
    reason_ = reason;
    if (state == VerdictState::Hidden) {
        setVisible(false);
        return;
    }
    const VerdictLook look = verdictLook(state);
    if (lookChanged) {
        setStyleSheet(QStringLiteral("#verdictBoard { background:%1; border-radius:6px; }"
                                     " QLabel { color:%2; background:transparent; }"
                                     " #verdictWord { font-size:%3px; font-weight:bold; }"
                                     " #verdictReason { font-size:%4px; }")
                          .arg(QString(look.field), QString(look.ink))
                          .arg(kWordPx)
                          .arg(kReasonPx));
    }
    word_->setText(look.word);
    reasonLabel_->setVisible(!reason.isEmpty());
    layoutReason();
    setToolTip(reason);
    setAccessibleName(reason.isEmpty() ? look.word
                                       : QStringLiteral("%1. %2").arg(look.word, reason));
    setVisible(true);
}

void VerdictBoard::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    layoutReason();
}

void VerdictBoard::layoutReason() {
    // La hoja de estilo pone el tamaño de letra al pulir el widget; sin esto la
    // cuenta del corte se haría con la letra pequeña de antes.
    reasonLabel_->ensurePolished();
    const int width = reasonLabel_->width();
    if (width <= 0) {
        reasonLabel_->setText(reason_);
        return;
    }
    reasonLabel_->setText(
        QFontMetrics(reasonLabel_->font()).elidedText(reason_, Qt::ElideRight, width));
}

}  // namespace pci::ui
