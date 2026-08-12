#include "ui/rate_readout.h"

#include <QCoreApplication>
#include <QObject>

namespace pci::ui {

QString formatRates(int width, int height, double captureFps, double analysisFps,
                    double droppedFps) {
    const QString size = QStringLiteral("%1x%2").arg(width).arg(height);
    const QString capture = QStringLiteral("%1 fps").arg(captureFps, 0, 'f', 1);

    // El umbral no es cero, y eso es a propósito. Dos contadores por ventana
    // deslizante nunca dan exactamente lo mismo aunque el análisis siga el
    // ritmo: basta con que un frame caiga a un lado del borde de la ventana. Un
    // descarte suelto por segundo no es que el análisis no llegue, es aliasing
    // de la medida, y enseñarlo entrenaría al operador a ignorar el aviso.
    constexpr double kNoticeableDrops = 2.0;
    if (droppedFps < kNoticeableDrops) {
        return QStringLiteral("%1 — %2").arg(size, capture);
    }
    return QObject::tr("%1 — %2 · analiza %3 · descarta %4")
        .arg(size, capture)
        .arg(analysisFps, 0, 'f', 1)
        .arg(droppedFps, 0, 'f', 0);
}

}  // namespace pci::ui
