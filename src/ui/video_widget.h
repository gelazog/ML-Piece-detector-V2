#pragma once

#include <QImage>
#include <QWidget>

namespace pci::ui {

// Pinta el último frame directamente en paintEvent (sin QLabel/QPixmap
// intermedios): una sola copia por frame y repintados coalescidos por Qt.
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

public slots:
    void setFrame(const QImage& frame);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage frame_;
};

}  // namespace pci::ui
