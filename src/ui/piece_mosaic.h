#pragma once

#include <QImage>
#include <QPolygonF>
#include <QWidget>

#include <vector>

class QGridLayout;
class QLabel;
class QScrollArea;
class QToolButton;

namespace pci::ui {

// TODAS LAS PIEZAS DEL ENCUADRE, UNA AL LADO DE OTRA.
//
// Con varias piezas, el vídeo las enseña a todas pero al tamaño que tengan: con
// una bandeja de cien tuercas, cada una ocupa ochenta píxeles en pantalla y no
// hay forma de mirar ninguna. Y pasar de una en una con las flechas para revisar
// cien es un trabajo que nadie hace.
//
// Este panel recorta cada pieza y las pone en cuadrícula, cada una con su
// número —el mismo del selector y el del informe— y todas al mismo tamaño. Ahí
// sí se ve de un vistazo cuál es la que desentona.
//
// Pulsar una la ENFOCA, que es lo que conecta este panel con el resto: la que se
// elige aquí es la que miden las herramientas y la que se remarca en el vídeo.
class PieceMosaic : public QWidget {
    Q_OBJECT

public:
    explicit PieceMosaic(QWidget* parent = nullptr);

    // Los recortes salen del frame y de los contornos que ya calculó el
    // análisis: no se vuelve a segmentar nada para pintar esto.
    //
    // `measured` es el número (desde 1) de la que se está midiendo; 0 = ninguna.
    void setPieces(const QImage& frame, const std::vector<QPolygonF>& outlines,
                   int measured);

    // Cuántas baldosas hay ahora mismo. Para las pruebas y para saber si el
    // panel tiene algo que enseñar.
    [[nodiscard]] int tileCount() const;

signals:
    // El operador ha pulsado una pieza. El número empieza en 1.
    void pieceChosen(int number);

private:
    void rebuild();
    // Poner al día lo que se ve SIN destruir las baldosas. Existe porque
    // destruirlas se come los clics: apretar y soltar tienen que caer en el
    // mismo widget, y `setPieces` corre en cada fotograma.
    void refreshTiles(bool measuredChanged);

    QScrollArea* area_ = nullptr;
    QWidget* board_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QLabel* empty_ = nullptr;
    std::vector<QToolButton*> tiles_;

    QImage frame_;
    std::vector<QPolygonF> outlines_;
    int measured_ = 0;
};

}  // namespace pci::ui
