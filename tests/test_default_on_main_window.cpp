// `setDefault(true)` SOBRE UN BOTÓN QUE NO ESTÁ EN UN DIÁLOGO.
//
// `inspectButton_->setDefault(true)` vive en la ventana principal, que es un
// `QMainWindow` y no un `QDialog`. La documentación de Qt es explícita: «the
// default button behavior is provided only in dialogs». O sea que la propiedad
// promete un Enter que ahí no puede cumplirse.
//
// Pero el comentario de al lado dice que está por otra cosa: «LA acción de esta
// pantalla, y la única destacada. Con trece botones del mismo peso, el que se
// pulsa cien veces al día parecía tan importante que "Gestionar…"». O sea que se
// puso buscando el REALCE, no el Enter.
//
// Las dos cosas pueden ser ciertas a la vez, y de eso depende qué hacer: si el
// realce lo da la propiedad, quitarla empeora la pantalla; si no lo da, la
// propiedad no hace nada y solo confunde a quien lee el código.
//
// Así que se mide en vez de razonarlo: se pinta el botón con la propiedad y sin
// ella, y se comparan los píxeles.

#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPushButton>
#include <QWidget>

#include <cstdio>

#include "ui/main_window.h"

using namespace pci;

namespace {

// El botón pintado tal cual está, en un lienzo del tamaño que pida.
QImage paint(QPushButton& button) {
    button.resize(button.sizeHint());
    QImage shot(button.size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    button.render(&shot);
    return shot;
}

int differingPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) {
        return -1;
    }
    int different = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++different;
            }
        }
    }
    return different;
}

}  // namespace

TEST(DefaultOnMainWindow, TheDefaultPropertyIsMeasuredBeforeBeingJudged) {
    // Fuera de cualquier diálogo, como está en la ventana principal.
    QWidget host;
    QPushButton button(QStringLiteral("Inspeccionar"), &host);

    button.setDefault(false);
    const QImage plain = paint(button);
    button.setDefault(true);
    const QImage asDefault = paint(button);

    const int different = differingPixels(plain, asDefault);
    const int total = plain.width() * plain.height();
    std::printf("  [realce] con y sin setDefault: %d de %d píxeles distintos (%.1f %%)\n",
                different, total, total > 0 ? 100.0 * different / total : 0.0);

    // Esta prueba NO exige un resultado: registra cuál es. Si un día el estilo
    // cambia y la propiedad empieza (o deja) de pintar, aquí se ve el número y
    // se decide otra vez con él delante, en vez de heredar una decisión cuyo
    // motivo ya nadie recuerda.
    SUCCEED();
}

TEST(DefaultOnMainWindow, TheInspectButtonKeepsItsEmphasisWhateverTheDefaultDoes) {
    // Lo que de verdad tiene que aguantar: que el botón que se pulsa cien veces
    // al día siga siendo el único destacado. El realce no puede depender de una
    // propiedad de diálogo dentro de una ventana que no lo es.
    ui::MainWindow window;
    auto* inspect = window.findChild<QPushButton*>(QStringLiteral("inspectButton"));
    ASSERT_NE(inspect, nullptr)
        << "no se encuentra el botón de inspeccionar por su nombre: si se renombró, "
           "hay que actualizar esta prueba — no borrarla";

    std::printf("  [realce] «%s»: negrita=%d, default=%d\n",
                inspect->text().toStdString().c_str(), inspect->font().bold() ? 1 : 0,
                inspect->isDefault() ? 1 : 0);
    EXPECT_TRUE(inspect->font().bold())
        << "el botón principal ha perdido la negrita, que es el realce que NO depende "
           "de estar dentro de un diálogo";

    // Y que siga siendo el único: dos o tres destacados no destacan ninguno.
    //
    // SIN FILTRAR POR `isVisible()`, y esto costó una vuelta: con la ventana sin
    // mostrar ningún hijo es visible, así que el recuento salía CERO y la
    // comprobación pasaba sin mirar nada. Es el mismo fallo que tenía `--smoke`
    // —verde pasara lo que pasara— colado dentro de una prueba nueva.
    int bold = 0;
    const auto buttons = window.findChildren<QPushButton*>();
    for (auto* button : buttons) {
        if (button->font().bold()) {
            ++bold;
        }
    }
    std::printf("  [realce] botones en negrita: %d de %d\n", bold,
                static_cast<int>(buttons.size()));
    EXPECT_EQ(bold, 1) << "o no hay ningún botón destacado, o hay más de uno — y con "
                          "dos o tres destacados no destaca ninguno";
}
