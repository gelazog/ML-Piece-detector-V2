// EL TAMAÑO DEL PINCEL, CAMBIADO DESDE SU MENÚ.
//
// Queja del taller: «al momento de cambiar el tamaño por el menú que dice ahí,
// no se quiso cambiar hasta que refresqué la herramienta».
//
// Esta prueba existe para separar dos cosas que desde fuera se viven igual:
//
//   1. Que el ajuste NO llegue al lienzo hasta que se reactiva la herramienta.
//      Eso sería un fallo de cableado.
//   2. Que llegue al instante pero no se VEA, porque lo único que enseña el
//      tamaño es el anillo bajo el cursor, y mientras el menú está abierto el
//      cursor está sobre el menú y no sobre la imagen.
//
// Son arreglos distintos y hay que saber cuál toca antes de tocar nada.

#include <gtest/gtest.h>

#include <QApplication>
#include <QSlider>

#include <cstdio>

#include "inspection_editor/canvas/editor_canvas.h"
#include "ui/main_window.h"

using namespace pci;

TEST(BrushSizeFromMenu, MovingTheSliderReachesTheCanvasImmediately) {
    ui::MainWindow window;
    window.resize(900, 600);

    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr) << "no se encuentra el lienzo";
    auto* slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
    ASSERT_NE(slider, nullptr)
        << "no se encuentra el deslizador del tamaño de pincel por su nombre";

    const int before = canvas->brushRadius();
    const int wanted = before + 17 <= slider->maximum() ? before + 17 : before - 17;
    slider->setValue(wanted);
    // SIN reactivar la herramienta ni tocar nada más: el ajuste tiene que haber
    // llegado ya.
    std::printf("  [pincel] deslizador %d -> el lienzo dice %d\n", wanted,
                canvas->brushRadius());
    EXPECT_EQ(canvas->brushRadius(), wanted)
        << "mover el deslizador del menú no llega al lienzo hasta que se reactiva la "
           "herramienta, así que el operador cambia el tamaño y sigue pintando con el "
           "de antes";
}

TEST(BrushSizeFromMenu, TheCanvasAndTheSliderAlwaysSayTheSameNumber) {
    // Los dos son el MISMO ajuste. Si cada uno enseña su número, el operador no
    // sabe con cuál está pintando — y es lo que pasaba antes de que el lienzo
    // avisara de sus propios cambios.
    ui::MainWindow window;
    window.resize(900, 600);
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    auto* slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(slider, nullptr);

    // Desde el lienzo, que es lo que hacen la rueda y las teclas [ ].
    canvas->setBrushRadius(41);
    std::printf("  [pincel] el lienzo pasa a 41 -> el deslizador dice %d\n",
                slider->value());
    EXPECT_EQ(slider->value(), 41)
        << "el lienzo cambió de tamaño y el deslizador se quedó en el suyo: dos números "
           "para el mismo ajuste";

    // Y desde el deslizador.
    slider->setValue(23);
    std::printf("  [pincel] el deslizador pasa a 23 -> el lienzo dice %d\n",
                canvas->brushRadius());
    EXPECT_EQ(canvas->brushRadius(), 23);
}
