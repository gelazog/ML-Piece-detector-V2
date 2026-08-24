// APAGAR UNA AYUDA DEL PINCEL Y QUE VUELVA ENCENDIDA.
//
// Las tres ayudas —pulso estable, trazo recto, ceñir al borde— se guardan al
// pulsarlas y se recuperan al arrancar. Eso está escrito y parece correcto.
//
// Pero el «recuperar» es `action->setChecked(guardado)`, y una QAction empieza
// SIN MARCAR. Si lo guardado es «apagado», `setChecked(false)` sobre algo que ya
// está en false NO EMITE `toggled` — y esa señal es la única que le dice al
// lienzo qué hacer. El lienzo se queda con su valor de fábrica.
//
// «Pulso estable» viene de fábrica ENCENDIDO. Así que el operador lo apaga,
// reinicia, y el menú se lo enseña apagado mientras el pincel lo sigue
// aplicando. Es la peor forma de este fallo: no es que se olvide el ajuste, es
// que la pantalla afirma una cosa y el programa hace otra, y el operador no
// tiene forma de descubrirlo salvo notando que el trazo no le obedece.
//
// Es la misma queja de siempre —«no se guarda la anterior configuración»—
// escondida detrás de un código que, leído, parece que sí la guarda.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include "database/db.h"
#include "database/schema.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "repositories/settings_repository.h"
#include "ui/main_window.h"

namespace {

// La acción del menú por su rótulo. Los nombres llevan sangría a propósito
// —cuelgan del modo de pincel— así que se busca por contenido.
QAction* assistAction(const pci::ui::MainWindow& window, const QString& label) {
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text().contains(label)) {
            return action;
        }
    }
    return nullptr;
}

}  // namespace

TEST(BrushPersistence, TurningAnAssistOffKeepsItOffAfterRestarting) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("brush.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    pci::ui::AppRepositories repos;
    repos.settings = &settings;

    // --- Sesión 1: el operador apaga «Pulso estable» -----------------------
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        auto* steady = assistAction(window, QStringLiteral("Pulso estable"));
        ASSERT_NE(steady, nullptr) << "no está la ayuda de pulso estable";

        // De fábrica viene encendido: si esto cambiara, el test dejaría de
        // probar el caso que importa y hay que enterarse.
        ASSERT_TRUE(steady->isChecked()) << "el pulso estable ya no viene encendido";
        ASSERT_TRUE(canvas->brushSteady());

        steady->setChecked(false);
        EXPECT_FALSE(canvas->brushSteady()) << "apagarlo no llega al lienzo";
    }

    // --- Sesión 2: se vuelve a abrir ----------------------------------------
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        auto* steady = assistAction(window, QStringLiteral("Pulso estable"));
        ASSERT_NE(steady, nullptr);

        EXPECT_FALSE(steady->isChecked()) << "el menú se olvidó de que estaba apagado";
        // ESTA es la que fallaba: el menú lo enseñaba apagado y el pincel lo
        // seguía aplicando.
        EXPECT_FALSE(canvas->brushSteady())
            << "el menú dice que el pulso estable está apagado y el pincel lo sigue "
               "aplicando: la pantalla afirma una cosa y el programa hace otra";
    }
}

TEST(BrushPersistence, TurningAnAssistOnAlsoSurvivesARestart) {
    // El sentido contrario, que es el que sí funcionaba: «trazo recto» viene
    // de fábrica apagado, así que encenderlo sí emite la señal. Se comprueba
    // para que el arreglo del otro caso no rompa este.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("brush2.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    pci::ui::AppRepositories repos;
    repos.settings = &settings;

    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        auto* straight = assistAction(window, QStringLiteral("Trazo recto"));
        ASSERT_NE(straight, nullptr);
        ASSERT_FALSE(straight->isChecked());
        straight->setChecked(true);
    }
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        auto* straight = assistAction(window, QStringLiteral("Trazo recto"));
        ASSERT_NE(straight, nullptr);
        EXPECT_TRUE(straight->isChecked());
        EXPECT_TRUE(canvas->brushStraight());
    }
}

// EL MISMO FALLO, EN EL CONTORNO EN VIVO.
//
// «Mostrar contorno» tiene un comentario que dice que era la única capa del
// menú Ver que no se recordaba, y que ya se arregló. El menú sí lo recuerda; lo
// que no llega es el lienzo, por dos motivos encadenados:
//
//   · `setChecked(false)` sobre una acción que ya está en false no emite nada.
//   · Y el `connect` que lleva ese valor al lienzo se hace DESPUÉS del
//     `setChecked`, así que aunque emitiera, no habría nadie escuchando.
//
// El lienzo trae el contorno visible de fábrica. Resultado: quien lo apaga para
// inspeccionar con la pieza congelada se encuentra el menú diciendo «apagado» y
// el contorno pintado encima del vídeo.
//
// Es exactamente el mismo fallo que el del pulso estable, en otro sitio, y por
// eso está en el mismo archivo: la regla que se saltan los dos es la misma.
TEST(BrushPersistence, HidingTheLiveContourAlsoSurvivesARestart) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("contorno.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    pci::ui::AppRepositories repos;
    repos.settings = &settings;

    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        auto* contour = assistAction(window, QStringLiteral("contorno"));
        ASSERT_NE(contour, nullptr) << "no está la acción de mostrar contorno";
        ASSERT_TRUE(contour->isChecked()) << "el contorno ya no viene visible de fábrica";
        ASSERT_TRUE(canvas->liveContourVisible());

        contour->setChecked(false);
        EXPECT_FALSE(canvas->liveContourVisible()) << "apagarlo no llega al lienzo";
    }
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        auto* contour = assistAction(window, QStringLiteral("contorno"));
        ASSERT_NE(contour, nullptr);

        EXPECT_FALSE(contour->isChecked()) << "el menú se olvidó de que estaba apagado";
        EXPECT_FALSE(canvas->liveContourVisible())
            << "el menú dice que el contorno está oculto y se sigue pintando encima "
               "del vídeo";
    }
}
