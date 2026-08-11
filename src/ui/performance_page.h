#pragma once

#include <QWidget>

#include "vision/auto_roi.h"

class QLabel;
class QRadioButton;

namespace pci::ui {

// Página «Rendimiento» del panel Configurar (C3): dónde busca la pieza el
// programa en cada frame.
//
// Es lo que el usuario pidió como «zoom automático para que el programa
// trabaje menos»: si la pieza ocupa un 8 % de la imagen, segmentar el frame
// entero es tirar el 92 % del trabajo. Medido sobre 1280×720 con una pieza de
// 180×140, recortar va **6 veces más rápido**.
class PerformancePage : public QWidget {
    Q_OBJECT

public:
    PerformancePage(vision::WorkingZoneMode mode, bool hasFixedZone,
                    QWidget* parent = nullptr);

    [[nodiscard]] vision::WorkingZoneMode mode() const;

    // Refleja el modo y si hay zona dibujada SIN emitir `modeChanged`. Hace
    // falta porque dibujar una zona sobre el vídeo cambia el modo por sí solo:
    // si el panel estuviera abierto y no se enterara, enseñaría un modo y el
    // programa estaría usando otro.
    void showMode(vision::WorkingZoneMode mode, bool hasFixedZone);

    // Estado en vivo de la zona: qué se está procesando ahora mismo y, si el
    // seguimiento se rindió, por qué. Un recorte que aparece y desaparece sin
    // explicación parece un fallo de la aplicación.
    void setZoneStatus(const cv::Rect& activeZone, const cv::Size& frameSize,
                       vision::AutoRoiGiveUp lastGiveUp);

signals:
    void modeChanged(pci::vision::WorkingZoneMode mode);

private:
    QRadioButton* off_ = nullptr;
    QRadioButton* automatic_ = nullptr;
    QRadioButton* fixed_ = nullptr;
    QLabel* status_ = nullptr;
};

}  // namespace pci::ui
