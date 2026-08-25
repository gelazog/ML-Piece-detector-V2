// EL AVISO QUE NUNCA APARECÍA.
//
// Petición de uso: «si puedes mejorar la detección de bordes, debido a
// reflejos, fondo, etc.».
//
// Resultó que casi todo estaba construido: el método por borde, el consejero
// que dice cuándo conviene, el aviso y hasta un botón para cambiarse. Lo que
// fallaba era la puerta. El aviso se enseñaba solo si las piezas CABALGABAN el
// fondo —partes más claras y más oscuras a la vez—, y «más claro que el fondo»
// se cuenta por encima de `fondo + 12`. Con la mesa en 255 ese techo cae en
// 267, que ningún píxel de 8 bits alcanza.
//
// O sea: sobre fondo blanco, que es el montaje industrial normal, la condición
// era FALSA POR CONSTRUCCIÓN y el botón no aparecía jamás. En las ocho imágenes
// reales del usuario el fondo va de 244 a 255.
//
// Esta prueba mira la pantalla, no el cálculo: que el aviso y el botón salgan
// cuando hay algo que hacer, y que no salgan cuando no.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>

#include <cstdio>

#include "ui/detection_page.h"

using namespace pci;

namespace {

// Lo que la lectura diría de una escena con la pieza recortada por el corte.
vision::SceneReading clippedScene() {
    vision::SceneReading reading;
    reading.backgroundLevel = 255.0;
    reading.brightSideIsUnmeasurable = true;
    reading.brighterThanBackground = 0.0;  // no se pudo mirar
    reading.darkerThanBackground = 0.41;
    reading.piecesStraddleTheBackground = false;
    reading.thresholdSwing = 0.368;  // el de tornillos-1.png
    reading.thresholdCutsThePiece = true;
    reading.aSingleCutCannotDoIt = true;
    return reading;
}

vision::SceneReading calmScene() {
    vision::SceneReading reading;
    reading.backgroundLevel = 255.0;
    reading.brightSideIsUnmeasurable = true;
    reading.darkerThanBackground = 0.53;
    reading.thresholdSwing = 0.046;  // el de la bandeja de cien tuercas
    return reading;
}

QPushButton* edgeButtonOf(const ui::DetectionPage& page) {
    for (auto* button : page.findChildren<QPushButton*>()) {
        if (button->text().contains(QStringLiteral("canto"), Qt::CaseInsensitive)) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST(EdgeAdviceReachesUI, AClippedPieceOnAWhiteTableOffersTheOtherMethod) {
    ui::DetectionPage page{vision::SegmentationOptions{}};
    page.setSceneReading(clippedScene());

    QPushButton* offer = edgeButtonOf(page);
    ASSERT_NE(offer, nullptr) << "no existe el botón que ofrece el método por borde";
    EXPECT_TRUE(offer->isVisible() || !offer->isHidden())
        << "con la pieza recortada sobre mesa blanca el botón sigue escondido: es "
           "exactamente el caso que no aparecía nunca";

    QString said;
    for (auto* label : page.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("silueta")) ||
            label->text().contains(QStringLiteral("por dentro de la pieza"))) {
            said = label->text();
        }
    }
    std::printf("  [aviso] %s\n", said.toStdString().c_str());
    ASSERT_FALSE(said.isEmpty()) << "no dice por qué conviene cambiar de método";
    // CON SU CIFRA: «prueba el otro método» es una corazonada; «cambia la
    // silueta un 36,8 %» es un motivo que el operador puede comprobar.
    EXPECT_TRUE(said.contains(QStringLiteral("36.8")) ||
                said.contains(QStringLiteral("36,8")))
        << "avisa sin la cifra que lo justifica: " << said.toStdString();
}

TEST(EdgeAdviceReachesUI, ACalmSceneIsLeftAlone) {
    // La bandeja de cien tuercas: el nivel las cuenta bien y el borde funde diez.
    // Un aviso que sale también aquí se aprende a ignorar, y encima empujaría al
    // operador al método que en ESA escena falla.
    ui::DetectionPage page{vision::SegmentationOptions{}};
    page.setSceneReading(calmScene());

    QPushButton* offer = edgeButtonOf(page);
    ASSERT_NE(offer, nullptr);
    EXPECT_TRUE(offer->isHidden())
        << "ofrece el método por borde en una escena donde el nivel acierta";
}
