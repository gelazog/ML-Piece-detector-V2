// LOS CONTROLES DE DETECCIÓN, AGRUPADOS POR LO QUE HACEN.
//
// La pestaña apilaba DIECINUEVE filas de formulario seguidas, sin ninguna
// división. Y el orden no ayudaba: el aviso de «tu mesa tiene color» salía en la
// cuarta fila y el desplegable que lo arregla estaba en la undécima — siete
// filas más abajo, con diez controles distintos por en medio. Lo mismo con el
// aviso del método por canto y su control.
//
// Nadie lo hizo mal a propósito: cada control se añadió al final el día que
// nació, y diecinueve días después la pestaña era una lista. Es la misma forma
// de erosionarse que la de los colores escritos a mano, y se para igual: con
// una prueba que no deja que crezca sin agrupar.
//
// Los cuatro grupos son las cuatro preguntas que se hacen aquí, en el orden en
// que se hacen: cómo se separa la pieza, dónde se pone el corte, cómo se corrige
// la silueta que sale, y qué cuenta como pieza.

#include <gtest/gtest.h>

#include <QFormLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>

#include <cstdio>
#include <memory>

#include "ui/detection_page.h"

using namespace pci;

namespace {

ui::DetectionPage* freshPage() {
    return new ui::DetectionPage(vision::SegmentationOptions{}, nullptr, nullptr, 0, 0.005,
                                 0.9, false);
}

}  // namespace

TEST(DetectionGroups, EveryControlLivesInsideAGroupAndNoneIsLooseAtTheTop) {
    const std::unique_ptr<ui::DetectionPage> page(freshPage());

    const auto boxes = page->findChildren<QGroupBox*>();
    ASSERT_GE(boxes.size(), 4U)
        << "la pestaña de detección tiene menos de cuatro grupos: o se quitó alguno, o "
           "alguien volvió a apilar las filas seguidas";

    int inGroups = 0;
    for (auto* box : boxes) {
        auto* form = box->findChild<QFormLayout*>();
        const int rows = form != nullptr ? form->rowCount() : 0;
        inGroups += rows;
        std::printf("  [detección] «%s»: %d filas\n", box->title().toStdString().c_str(),
                    rows);
        EXPECT_GT(rows, 0) << "el grupo «" << box->title().toStdString()
                           << "» está vacío: un título sin nada debajo es peor que "
                              "ningún título";
    }
    std::printf("  [detección] %d filas repartidas en %lld grupos\n", inGroups,
                static_cast<long long>(boxes.size()));

    // Las diecinueve que había, y las que vengan.
    EXPECT_GE(inGroups, 19)
        << "hay menos filas dentro de grupos de las que la pestaña tenía: alguna se ha "
           "quedado suelta fuera, que es exactamente como empezó el desorden";
}

TEST(DetectionGroups, TheColourAdviceSitsWithTheControlItTalksAbout) {
    // Lo que motivó agrupar. El aviso dice «enciende la clave de color» y el
    // desplegable que la enciende estaba siete filas más abajo: quien leía el
    // aviso tenía que buscar de qué hablaba.
    const std::unique_ptr<ui::DetectionPage> page(freshPage());

    QGroupBox* withAdvice = nullptr;
    QGroupBox* withControl = nullptr;
    for (auto* box : page->findChildren<QGroupBox*>()) {
        for (auto* button : box->findChildren<QPushButton*>()) {
            if (button->text().contains(QStringLiteral("color del fondo"))) {
                withAdvice = box;
            }
        }
        for (auto* combo : box->findChildren<QComboBox*>()) {
            if (combo->count() == 3 &&
                combo->itemText(0).contains(QStringLiteral("claridad"))) {
                withControl = box;
            }
        }
    }
    ASSERT_NE(withAdvice, nullptr) << "no se encuentra el botón de la sugerencia de color";
    ASSERT_NE(withControl, nullptr) << "no se encuentra el desplegable de clave de color";
    std::printf("  [detección] la sugerencia y su control: «%s» / «%s»\n",
                withAdvice->title().toStdString().c_str(),
                withControl->title().toStdString().c_str());
    EXPECT_EQ(withAdvice, withControl)
        << "el aviso que propone encender la clave de color y el control que la enciende "
           "están en grupos distintos. Quien lea el aviso tendrá que buscar de qué habla.";
}
