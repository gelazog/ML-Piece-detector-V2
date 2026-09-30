#include "ui/blocking_notice.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme.h"

namespace pci::ui {

namespace {

// El nombre de cada fila, para encontrarla en las pruebas y en un lector de
// pantalla sin depender del texto, que cambia con el mensaje.
QString slug(Blocker blocker) {
    switch (blocker) {
        case Blocker::Source:
            return QStringLiteral("source");
        case Blocker::Marker:
            return QStringLiteral("marker");
        case Blocker::Database:
            break;
    }
    return QStringLiteral("database");
}

}  // namespace

BlockingNotice::BlockingNotice(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("blockingNotice"));
    // El aviso de siempre (`noticeStyle`: tinta de «no cumple» sobre su campo y
    // con borde del mismo color) a 20 px, que es lo que se lee a metro y medio.
    // El botón conserva el aspecto de botón: si se tiñera como el texto dejaría
    // de parecer pulsable.
    setStyleSheet(QStringLiteral("#blockingNotice { %1 }"
                                 " #blockingNotice QLabel { color:%2; background:transparent;"
                                 " font-size:20px; font-weight:bold; }")
                      .arg(theme::noticeStyle(theme::kBad, theme::kBadField),
                           QString(theme::kBad)));
    rows_ = new QVBoxLayout(this);
    rows_->setContentsMargins(4, 2, 4, 2);
    rows_->setSpacing(2);
    setVisible(false);
}

void BlockingNotice::report(Blocker blocker, const QString& text, const QString& action) {
    auto it = reported_.find(blocker);
    if (it != reported_.end() && it->second.text == text && it->second.action == action) {
        return;
    }
    if (it == reported_.end()) {
        Row fresh;
        fresh.row = new QWidget(this);
        fresh.row->setObjectName(QStringLiteral("blocker_%1").arg(slug(blocker)));
        auto* line = new QHBoxLayout(fresh.row);
        line->setContentsMargins(0, 0, 0, 0);
        fresh.label = new QLabel(fresh.row);
        fresh.label->setObjectName(QStringLiteral("blockerText_%1").arg(slug(blocker)));
        fresh.label->setWordWrap(true);
        line->addWidget(fresh.label, 1);
        fresh.button = new QPushButton(fresh.row);
        fresh.button->setObjectName(QStringLiteral("blockerAction_%1").arg(slug(blocker)));
        line->addWidget(fresh.button);
        connect(fresh.button, &QPushButton::clicked, this,
                [this, blocker] { emit actionRequested(blocker); });
        // En su sitio según el orden del enum, no al final: la fuente arriba.
        int position = 0;
        for (const auto& [other, row] : reported_) {
            if (other < blocker) {
                ++position;
            }
        }
        rows_->insertWidget(position, fresh.row);
        // A mano: un hijo creado cuando la banda YA está a la vista nace
        // oculto. Sin esto, el segundo problema (la cámara con la BD ya
        // avisada) salía como una banda con una sola línea, la vieja.
        fresh.row->setVisible(true);
        it = reported_.emplace(blocker, fresh).first;
    }
    Row& row = it->second;
    row.text = text;
    row.action = action;
    // El signo de aviso además del color: el rojo solo no lo distingue todo el
    // mundo.
    row.label->setText(QStringLiteral("⚠ %1").arg(text));
    row.button->setText(action);
    row.button->setVisible(!action.isEmpty());
    row.row->setAccessibleName(text);
    setVisible(true);
}

void BlockingNotice::resolve(Blocker blocker) {
    const auto it = reported_.find(blocker);
    if (it == reported_.end()) {
        return;
    }
    it->second.row->hide();
    it->second.row->deleteLater();
    // Soltarlo del árbol ya: `findChild` no debe encontrar un aviso resuelto
    // mientras espera a borrarse.
    it->second.row->setParent(nullptr);
    reported_.erase(it);
    if (reported_.empty()) {
        setVisible(false);
    }
}

bool BlockingNotice::isReported(Blocker blocker) const { return reported_.count(blocker) > 0; }

QString BlockingNotice::text(Blocker blocker) const {
    const auto it = reported_.find(blocker);
    return it == reported_.end() ? QString() : it->second.text;
}

}  // namespace pci::ui
