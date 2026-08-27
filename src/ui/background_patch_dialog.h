#pragma once

#include <QDialog>
#include <QRect>

#include <opencv2/core.hpp>

#include "vision/segmentation.h"

class QLabel;
class QPushButton;

namespace pci::ui {

// SEÑALAR UN TROZO DE MESA, EN VEZ DE TECLEAR UN COLOR.
//
// Petición del taller: «lo del color de fondo, al momento de seleccionarlo, el
// usuario debería poder recortar o seleccionar un área del fondo por la
// textura, y la descarte, para poder tomar las piezas correctamente».
//
// Lo que había eran dos formas de decir cuál es el fondo, y las dos fallan por
// sitios distintos:
//
//     la mediana del marco   se contamina cuando la pieza llega al borde
//     el selector de color   pide un RGB que nadie sabe de su propia mesa
//
// El segundo es el que hacía falta arreglar. «Color del fondo del puesto» abría
// la rueda de colores de Qt, y ahí el operador tiene que ADIVINAR el rojo de su
// cartón. El color está delante de él, en la imagen: lo único que faltaba era
// dejarle apuntar.
//
// LA VISTA PREVIA ES LA MITAD QUE IMPORTA.
//
// Un color de fondo mal elegido no da un error: da una detección peor, y meses
// después. Medido sobre `arandelas-1`, señalando por error una arandela en vez
// de la mesa: la segmentación pasa de 12 piezas y el 23,9 % del cuadro a CERO
// piezas y el 87,8 % marcado — la escena entera del revés, y nada que lo diga.
//
// Así que esta ventana no devuelve un color a ciegas: con cada selección corre
// la segmentación DE VERDAD, con los mismos ajustes que están puestos, y pinta
// encima lo que saldría. El operador no elige un color, elige un resultado.
class BackgroundPatchDialog : public QDialog {
    Q_OBJECT

public:
    // `options` son los ajustes de detección tal y como están ahora mismo. La
    // vista previa los usa enteros —umbral, morfología, recuperación de
    // brillos— y solo les cambia la clave de color: enseñar una segmentación
    // con otros ajustes sería enseñar otra cosa.
    BackgroundPatchDialog(const cv::Mat& frame, vision::SegmentationOptions options,
                          QWidget* parent = nullptr);

    // El color elegido, en BGR. Solo tiene sentido si `sample().valid`.
    [[nodiscard]] vision::BackgroundSample sample() const { return sample_; }

    // SEÑALAR UN PARCHE SIN RATÓN, en coordenadas de la imagen.
    //
    // Es lo que llama el arrastre, y es por donde entran las pruebas: sin esto
    // habría que simular píxel a píxel un gesto sobre un widget escalado, que
    // es probar el ratón de Qt y no lo que esta ventana decide.
    void selectPatch(const cv::Rect& patch);

    // Lo que se vería tras la selección: cuántas piezas y cuánto del cuadro.
    // Público porque es lo que la prueba tiene que poder leer — es el número
    // que el operador está mirando cuando decide.
    [[nodiscard]] int previewPieces() const { return previewPieces_; }
    [[nodiscard]] double previewCoverage() const { return previewCoverage_; }

private:
    void refresh();

    cv::Mat frame_;
    vision::SegmentationOptions options_;
    vision::BackgroundSample sample_;
    cv::Rect patch_;
    int previewPieces_ = 0;
    double previewCoverage_ = 0.0;

    class PatchView;
    PatchView* view_ = nullptr;
    QLabel* swatch_ = nullptr;
    QLabel* verdict_ = nullptr;
    QLabel* preview_ = nullptr;
    QPushButton* okButton_ = nullptr;
};

}  // namespace pci::ui
