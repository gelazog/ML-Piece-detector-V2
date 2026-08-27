// AL CERRAR UNA IMAGEN, LA FUENTE SE PONÍA SOLA EN LA CÁMARA INTEGRADA.
//
// Queja del taller: «en la fuente, que recuerde lo último que usaste, ya sea
// imagen o vídeo, porque usar imagen, luego cerrarla, y que se ponga cámara
// integrada arruina la experiencia de usuario».
//
// Nadie eligió eso. Al cerrar el fichero se quita su entrada del desplegable, y
// quitar un elemento de un `QComboBox` deja la selección en el que ocupe ese
// sitio — que aquí es la primera cámara. Es la consecuencia de borrar la
// entrada, y desde fuera se vive como que el programa cambia de fuente solo.
//
// Lo que sigue a cerrar una imagen es abrir otra, casi siempre la de al lado en
// la misma carpeta. Así que el desplegable se queda en «Abrir imagen…».
//
// Y SE PRESELECCIONA Y NADA MÁS. Elegir en ese desplegable abre el diálogo de
// fichero, así que la selección se pone con las señales bloqueadas: un programa
// que al cerrar un fichero se pone a abrir otro hace algo que nadie ha pedido.
// Es la misma regla que ya gobierna la fuente restaurada al arrancar.

#include <gtest/gtest.h>

#include <QApplication>
#include <QMetaObject>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>

#include "camera/frame_source.h"
#include "ui/main_window.h"

using namespace pci;

namespace {

// El desplegable de fuente, POR NOMBRE. Buscarlo por su contenido no vale: sus
// entradas aparecen cuando termina la enumeración de cámaras, que es asíncrona,
// y al construir la ventana todavía está vacío.
QComboBox* sourceCombo(ui::MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("sourceCombo"));
}

// Espera a que el desplegable traiga ya la entrada de abrir ficheros.
bool waitForSources(QComboBox* combo) {
    for (int tries = 0; tries < 200; ++tries) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemText(i).contains(QStringLiteral("Abrir imagen"))) {
                return true;
            }
        }
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return false;
}

}  // namespace

TEST(SourceRemembers, ClosingAnImageLeavesTheSourceOnOpenImage) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(200, 150, QImage::Format_RGB888);
    photo.fill(QColor(240, 240, 240));
    const QString path = QDir(dir.path()).filePath(QStringLiteral("pieza.png"));
    ASSERT_TRUE(photo.save(path));

    ui::MainWindow window;
    window.resize(900, 600);
    window.show();
    auto* combo = sourceCombo(window);
    ASSERT_NE(combo, nullptr) << "no se encuentra el desplegable de fuente";
    ASSERT_TRUE(waitForSources(combo))
        << "el desplegable nunca llega a ofrecer «Abrir imagen…»";

    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    std::printf("  [fuente] con la imagen abierta: «%s»\n",
                combo->currentText().toStdString().c_str());

    // Y se cierra, que es el gesto del que se quejaba.
    // Por el meta-objeto: la ranura es privada, y buscar el botón por su texto
    // es justo lo que el trinquete de `test_lookups_by_name.cpp` viene a evitar.
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onStartStopClicked"));
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString after = combo->currentText();
    std::printf("  [fuente] tras cerrarla: «%s»\n", after.toStdString().c_str());

    EXPECT_TRUE(after.contains(QStringLiteral("Abrir imagen")))
        << "tras cerrar una imagen la fuente queda en «" << after.toStdString()
        << "». Nadie lo eligió: es lo que pasa al quitar la entrada del fichero del "
           "desplegable, y lo siguiente que hace el operador es abrir otra imagen.";
}
