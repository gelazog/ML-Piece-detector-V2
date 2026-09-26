// LOS INDICADORES DE ESTACIÓN SOLO CAMBIABAN DE COLOR.
//
// `updateStatusIndicators` (src/ui/main_window.cpp) pinta Cám / BD / Modelo
// como un punto de color con una leyenda. Antes de este arreglo la leyenda
// era LA MISMA en los dos estados -«BD» en rojo y «BD» en verde-, y lo único
// que decía «conectada» o «no disponible» era el tooltip, que no se ve sin
// pasar el ratón por encima.
//
// Eso incumple WCAG 1.4.1 (el color no puede ser el único portador de la
// información) y falla en concreto para un operador daltónico deutan, que es
// el tipo de daltonismo más común: ve el mismo punto gris apagado en los dos
// casos y el mismo texto «BD» al lado. No hay forma de que sepa si la base de
// datos está conectada mirando la pantalla.
//
// Esta prueba abre la ventana dos veces -una con el repositorio de piezas y
// la función de embeddings disponibles, otra sin ninguno de los dos, que es
// justo como arranca la app cuando la BD no abre o el modelo ONNX no está- y
// comprueba que el TEXTO VISIBLE del indicador (no el tooltip) cambia entre
// los dos casos, y que cada indicador tiene un nombre accesible no vacío.
//
// UNA TRAMPA QUE ESTA PRUEBA YA SE COMIÓ UNA VEZ: `label->text()` completo
// lleva `<span style='color:#rrggbb'>` delante, y ese hexadecimal YA cambia
// entre estados. Comparar la cadena entera daba una prueba en verde sin el
// arreglo -el color por sí solo bastaba para que las dos cadenas fueran
// distintas- que es justo el defecto que se quiere cazar. `visibleWord()`
// quita el marcado antes de comparar, y con eso la prueba SÍ falla si se
// quita la palabra de estado (se comprobó apagándola antes de escribir esto).

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include <cstdio>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "database/schema.h"
#include "repositories/piece_repository.h"
#include "ui/main_window.h"

using namespace pci;

namespace {

// EL TEXTO QUE UN OPERADOR LEE, SIN EL MARCADO DE COLOR.
//
// `label->text()` completo lleva un `<span style='color:#rrggbb'>` delante:
// comparar esa cadena entera es la primera trampa de esta prueba, porque el
// color por sí solo YA la hace distinta entre estados (`#b3261e` frente a
// `#14532d`) aunque la palabra visible -lo que de verdad lee un operador o un
// lector de pantalla- sea idéntica. Eso es exactamente el defecto que esta
// prueba tiene que cazar, así que hay que quitar el color antes de comparar.
QString visibleWord(const QLabel* label) {
    const QString text = label->text();
    const int end = text.indexOf(QStringLiteral("</span>"));
    if (end < 0) {
        return text;
    }
    return text.mid(end + 7).trimmed();
}

}  // namespace

TEST(StatusIndicatorWords, DatabaseIndicatorTextDiffersByState) {
    // Estado "caída": repositorios vacíos, como arranca la app si la BD falla
    // al abrir o migrar (ver el comentario de `main.cpp` sobre degradar sin
    // persistencia en vez de crashear).
    pci::ui::AppRepositories down;
    pci::ui::MainWindow downWindow(down);
    auto* downLabel = downWindow.findChild<QLabel*>(QStringLiteral("dbIndicator"));
    ASSERT_NE(downLabel, nullptr);

    // Estado "conectada": con un repositorio de piezas de verdad, sobre una
    // BD nueva en un directorio temporal.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("indicadores.db")).toStdString();
    auto opened = database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(database::migrate(*db).isOk());
    repositories::PieceRepository pieces(*db);

    pci::ui::AppRepositories up;
    up.pieces = &pieces;
    pci::ui::MainWindow upWindow(up);
    auto* upLabel = upWindow.findChild<QLabel*>(QStringLiteral("dbIndicator"));
    ASSERT_NE(upLabel, nullptr);

    const QString downWord = visibleWord(downLabel);
    const QString upWord = visibleWord(upLabel);
    std::printf("  [indicador] BD caida:      \"%s\" (texto completo \"%s\")\n",
                downWord.toUtf8().constData(), downLabel->text().toUtf8().constData());
    std::printf("  [indicador] BD conectada:  \"%s\" (texto completo \"%s\")\n",
                upWord.toUtf8().constData(), upLabel->text().toUtf8().constData());

    EXPECT_NE(downWord, upWord)
        << "el indicador de BD dice lo mismo caída que conectada -quitado el marcado de "
           "color-: solo cambiaba el color, y el color por sí solo es justo lo que un "
           "daltónico no distingue";
    EXPECT_FALSE(downLabel->accessibleName().isEmpty())
        << "el indicador de BD no tiene nombre accesible: un lector de pantalla no dice "
           "nada de él";
    EXPECT_NE(downLabel->accessibleName(), upLabel->accessibleName())
        << "el nombre accesible tampoco distingue el estado";
}

TEST(StatusIndicatorWords, ModelIndicatorTextDiffersByState) {
    // Sin función de embeddings: el modelo ONNX no está disponible.
    pci::ui::AppRepositories without;
    pci::ui::MainWindow withoutWindow(without);
    auto* withoutLabel = withoutWindow.findChild<QLabel*>(QStringLiteral("modelIndicator"));
    ASSERT_NE(withoutLabel, nullptr);

    // Con una función de embeddings de mentira: basta con que no esté vacía,
    // igual que comprueba `updateStatusIndicators` (`static_cast<bool>(embedFn)`).
    pci::ui::AppRepositories with;
    with.embedFn = [](const cv::Mat&) { return core::Result<std::vector<float>>::ok({}); };
    pci::ui::MainWindow withWindow(with);
    auto* withLabel = withWindow.findChild<QLabel*>(QStringLiteral("modelIndicator"));
    ASSERT_NE(withLabel, nullptr);

    const QString withoutWord = visibleWord(withoutLabel);
    const QString withWord = visibleWord(withLabel);
    std::printf("  [indicador] Modelo sin cargar: \"%s\" (texto completo \"%s\")\n",
                withoutWord.toUtf8().constData(), withoutLabel->text().toUtf8().constData());
    std::printf("  [indicador] Modelo cargado:    \"%s\" (texto completo \"%s\")\n",
                withWord.toUtf8().constData(), withLabel->text().toUtf8().constData());

    EXPECT_NE(withoutWord, withWord)
        << "el indicador del modelo ONNX dice lo mismo cargado que sin cargar -quitado el "
           "marcado de color-";
    EXPECT_FALSE(withLabel->accessibleName().isEmpty())
        << "el indicador del modelo no tiene nombre accesible";
}
