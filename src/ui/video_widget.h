#pragma once

#include <QImage>
#include <QWidget>

#include "ui/analysis_overlay.h"

namespace pci::ui {

// Pinta el último frame directamente en paintEvent (sin QLabel/QPixmap
// intermedios): una sola copia por frame y repintados coalescidos por Qt.
// Opcionalmente superpone el análisis de visión (contorno, centroide, eje).
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

public slots:
    void setFrame(const QImage& frame);
    void setOverlay(const AnalysisOverlay& overlay);
    void clearOverlay();
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage frame_;
    AnalysisOverlay overlay_;
};

}  // namespace pci::ui
