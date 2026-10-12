// UNA OPCIÓN ENCENDIDA QUE NO PUEDE HACER NADA.
//
// Queja de uso: «las opciones, aunque no esté nada activo… estoy en imagen y le
// doy a alguna, la abre, la mide, pero no abrió nada».
//
// La aplicación arranca sin cámara, sin imagen y sin pieza seleccionada. En ese
// estado, la mayoría de sus comandos no tienen sobre qué actuar: medir sin
// imagen, inspeccionar sin pieza, guardar una plantilla que no existe. Un
// comando encendido es una PROMESA —«esto se puede hacer ahora»— y cuando no se
// puede, el operador se lo encuentra de una de estas tres formas, todas malas:
//
//   - no pasa nada visible, y concluye que el programa está roto;
//   - pasa algo a medias sobre datos que no hay;
//   - sale un aviso que explica lo que debería haber estado apagado.
//
// La aplicación YA sabe hacerlo bien en un sitio: la auto-inspección se queda
// apagada con el motivo en su tooltip —«hace falta una pieza y una cámara en
// marcha»— y eso se lee ANTES de pulsar. Esta prueba mide cuántos comandos
// siguen sin esa cortesía.
//
// NO se comprueba «todos apagados»: hay comandos que sí se pueden usar en seco
// —abrir la configuración, buscar cámaras, ver el manual, cambiar la unidad—.
// Lo que se fija es el TRINQUETE: la lista de los que quedan encendidos sin
// poder hacer nada no puede crecer.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>

#include "camera/frame_source.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "ui/main_window.h"

using namespace pci;

namespace {

// Los comandos que SÍ tienen sentido con la aplicación recién abierta: no
// necesitan imagen, ni pieza, ni cámara en marcha.
//
// Se nombran uno a uno y no por prefijo a propósito: cada vez que alguien añada
// un comando tendrá que decidir a qué grupo pertenece, en vez de colarlo por
// parecerse a otro.
bool worksWithNothingLoaded(const QString& text) {
    static const QStringList kFine = {
        // Abrir un fichero es justo lo que se hace con nada cargado. «Abrir
        // reciente» no entra: con la lista vacía está apagado.
        QStringLiteral("Abrir imagen o vídeo…"),
        // Poner a punto la máquina: nada de esto necesita una pieza delante.
        QStringLiteral("Configurar…"),
        QStringLiteral("Buscar cámaras de nuevo"),
        QStringLiteral("Exportar configuración…"),
        QStringLiteral("Importar configuración…"),
        QStringLiteral("Restablecer configuración de fábrica…"),
        QStringLiteral("Calibrar la lente…"),
        // Elegir en qué unidad se leerá lo que se mida después.
        QStringLiteral("Automática (mm/cm)"), QStringLiteral("Milímetros"),
        QStringLiteral("Centímetros"), QStringLiteral("Píxeles"),
        QStringLiteral("Pulgadas"), QStringLiteral("Unidad de medida"),
        // Gestión de lo que ya está guardado en la base.
        QStringLiteral("Gestionar piezas…"), QStringLiteral("Gestionar plantillas…"),
        QStringLiteral("Registrar con asistente…"),
        // Ver: paneles y ayudas de dibujo. Encender un panel vacío es una
        // decisión legítima del operador, y apagarlos sería peor.
        QStringLiteral("Panel de herramientas"), QStringLiteral("Panel de comparación"),
        QStringLiteral("Piezas del encuadre (mosaico)"),
        QStringLiteral("Medidas en vivo (tabla)"), QStringLiteral("Capturas"),
        QStringLiteral("Mostrar contorno"), QStringLiteral("Tablero de referencia (centro = 0)"),
        QStringLiteral("Regla graduada"), QStringLiteral("Realzar la imagen para verla"),
        QStringLiteral("Seguir rotación de la pieza"), QStringLiteral("Origen del tablero"),
        QStringLiteral("Centro de la pieza"), QStringLiteral("Esquina de la imagen"),
        QStringLiteral("Centro de la imagen"), QStringLiteral("Punto fijo…"),
        QStringLiteral("Ejes girados con la pieza"),
        // AJUSTES, no acciones. La diferencia es la que importa: una acción
        // promete hacer algo AHORA —y sin imagen no puede—, mientras que un
        // ajuste deja puesto algo que se aplicará cuando haya pieza delante.
        // Apagarlos obligaría a abrir una fuente para poder preparar la
        // máquina, que es justo al revés de como se trabaja.
        QStringLiteral("Escala por marcador ArUco (en vivo)"),
        QStringLiteral("Automático: centro del contorno"),
        QStringLiteral("Automático: centro de masa"),
        QStringLiteral("Automático: centro de la imagen"),
        // Y el manual NO está en esta lista a propósito: se marca con el ratón
        // sobre la imagen, así que sin imagen el lienzo se queda esperando un
        // clic que no puede llegar.
        // Ayuda.
        QStringLiteral("Atajos de teclado…"),
    };
    return std::any_of(kFine.begin(), kFine.end(), [&text](const QString& fine) {
        return text.startsWith(fine);
    });
}

}  // namespace

TEST(DeadControls, NoCommandPromisesSomethingItCannotDoYet) {
    // La ventana recién construida: sin cámara en marcha, sin imagen abierta y
    // sin pieza seleccionada. Es lo que ve cualquiera al abrir el programa.
    ui::MainWindow window;
    window.resize(1400, 800);

    QStringList promising;
    int total = 0;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            if (action->isSeparator() || action->text().isEmpty() ||
                action->menu() != nullptr) {
                continue;
            }
            ++total;
            if (!action->isEnabled() || worksWithNothingLoaded(action->text())) {
                continue;
            }
            // Un comando encendido que no puede hacer nada. Se apunta con su
            // texto para que el fallo diga CUÁL, no cuántos.
            promising << action->text();
        }
    }

    std::printf("  [muertos] %d comandos en los menús, %d encendidos sin nada que hacer\n",
                total, static_cast<int>(promising.size()));
    for (const QString& text : promising) {
        std::printf("  [muertos]    %s\n", text.toStdString().c_str());
    }
    ASSERT_GT(total, 20) << "apenas se leen comandos: esta prueba no está mirando el menú";

    // EL TOPE ES EL DE HOY. Baja cuando se apaga uno; no sube nunca.
    //
    // Cada uno de estos es una promesa que la aplicación no puede cumplir: el
    // operador pulsa, no pasa nada visible o pasa algo a medias, y concluye que
    // el programa está roto.
    constexpr int kToday = 0;
    EXPECT_LE(promising.size(), kToday)
        << "hay " << promising.size()
        << " comandos encendidos que no pueden hacer nada: " << promising.join(", ").toStdString()
        << ". Apágalos con su motivo en el tooltip, como ya hace la auto-inspección.";
    if (static_cast<int>(promising.size()) < kToday) {
        ADD_FAILURE() << "quedan " << promising.size() << " y el tope sigue en " << kToday
                      << ": bájalo. Un trinquete que no se aprieta deja de serlo.";
    }
}

// Y AL REVÉS: CON UNA IMAGEN DELANTE, LO QUE PEDÍA IMAGEN SE ENCIENDE.
//
// Apagar se hacía bien; encender, no. Los comandos que piden imagen —«Medir
// pieza», calibrar la escala, el editor de plantilla— se recalculaban al montar
// la fuente, y en ese instante todavía no hay ningún frame: el primero llega
// justo después. Nada los volvía a mirar, así que con la imagen abierta y su
// contorno en pantalla «Medir pieza» seguía apagado, con el tooltip diciendo
// «no hay ninguna imagen delante». Con la cámara, igual.
//
// Es el mismo fallo que ya tuvo el pincel de corregir el borde, y se arregla en
// el mismo sitio: donde llega cada frame.
TEST(DeadControls, AnOpenedImageTurnsOnWhatNeedsAnImage) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(140, 100, 120, 100), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("pieza.png"));
    ASSERT_TRUE(photo.save(path));

    ui::MainWindow window;
    window.resize(1400, 800);
    window.show();
    auto* measure = window.findChild<QPushButton*>(QStringLiteral("measurePieceButton"));
    ASSERT_NE(measure, nullptr);
    ASSERT_FALSE(measure->isEnabled()) << "sin imagen tiene que empezar apagado";

    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000 && canvas->livePieceCount() < 1) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    ASSERT_GE(canvas->livePieceCount(), 1) << "la imagen no llega a enseñar la pieza";

    EXPECT_TRUE(measure->isEnabled())
        << "con la imagen abierta y la pieza en pantalla, «Medir pieza» sigue apagado: «"
        << measure->toolTip().toStdString() << "»";
}
