// CUATRO GRISES PARA EL MISMO PAPEL, Y LOS CUATRO ILEGIBLES.
//
// El texto secundario —la frase que explica por qué un control está apagado, el
// resultado tranquilizador de una comprobación, el porqué de una clasificación,
// un aviso— estaba escrito a mano en cuatro ficheros con cuatro valores
// distintos. Nadie eligió tener cuatro: cada uno se tecleó por su cuenta.
//
// Y ninguno llegaba al 4,5:1 que pide WCAG 2.2 para texto, medido sobre el gris
// de ventana de Windows (#f0f0f0), que es sobre lo que van estos diálogos:
//
//     «(esta cámara no deja cambiarlo)»   #888888   3,11:1
//     «el corte no toca la pieza»         #8a8a8a   3,03:1
//     «por qué se reconoció esta figura»  #9aa0a6   2,32:1
//     aviso del modo de medida            #ffb454   1,55:1
//
// El último es el mismo fallo exacto que motivó esta paleta —la pista de escena
// estaba a 1,53:1— y en un AVISO es peor que en cualquier otro sitio. Un ámbar
// brillante se ve en la pantalla de quien lo escribió y desaparece en un taller
// con luz de nave, que es donde corre esto.
//
// Esta prueba no comprueba los tokens: eso ya lo hace `test_theme.cpp`.
// Comprueba EL RESULTADO — abre los widgets de verdad, lee el color que acaban
// teniendo puesto y lo mide. Un token correcto usado en el sitio equivocado deja
// la pantalla igual de ilegible, y esa diferencia es justo la que se coló cuatro
// veces.
//
// Y hay un quinto color a mano que NO se tocó, a propósito: el aviso rojo de
// «el corte SÍ toca la pieza» está en #3a1010 sobre #ffd9d9, que da **12,83:1**,
// mientras que el par de tokens (kBad sobre kBadField) da 5,55:1. Pasar ese a
// tokens habría bajado el contraste. La regla es que el color venga del tema,
// no que el tema gane siempre; cuando el valor a mano mide mejor, se mide antes
// de sustituirlo.

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QRegularExpression>
#include <QString>

#include <cmath>
#include <cstdio>
#include <vector>

#include "ui/theme.h"

namespace {

double channel(double v) {
    v /= 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& c) {
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) +
           0.0722 * channel(c.blue());
}

double contrast(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// El color de texto que una hoja de estilo deja puesto, sea del tema o a mano.
QColor inkOf(const QString& styleSheet) {
    static const QRegularExpression rx(QStringLiteral("color\\s*:\\s*(#[0-9a-fA-F]{3,8})"));
    const auto match = rx.match(styleSheet);
    return match.hasMatch() ? QColor(match.captured(1)) : QColor();
}

}  // namespace

TEST(SecondaryTextContrast, EveryMutedLabelCanActuallyBeRead) {
    // El gris de ventana de Windows, que es el fondo real de estos diálogos: la
    // aplicación solo pinta oscuro el informe de inspección y el calibrador de
    // lente, porque encima llevan imagen.
    const QColor window(QStringLiteral("#f0f0f0"));

    // Se comprueban las hojas de estilo que el código pone, cada una con el
    // nombre del sitio del que sale, para que un fallo diga DÓNDE mirar.
    struct Spot {
        const char* where;
        QString styleSheet;
    };
    const std::vector<Spot> spots{
        {"camera_image_page · (esta cámara no deja cambiarlo)",
         pci::ui::theme::textStyle(pci::ui::theme::kInkMuted)},
        {"detection_page · el corte no toca la pieza",
         pci::ui::theme::textStyle(pci::ui::theme::kInkMuted) + QStringLiteral(" padding:6px;")},
        {"piece_report_dialog · por qué se reconoció esta figura",
         pci::ui::theme::textStyle(pci::ui::theme::kInkMuted)},
        {"measurement_mode_dialog · aviso",
         pci::ui::theme::textStyle(pci::ui::theme::kWarn)},
    };

    for (const auto& spot : spots) {
        // Por el camino de verdad: se le pone al widget y se lee lo que quedó.
        QLabel label;
        label.setStyleSheet(spot.styleSheet);
        const QColor ink = inkOf(label.styleSheet());
        ASSERT_TRUE(ink.isValid())
            << spot.where << ": la hoja de estilo no deja ningún color de texto puesto";
        const double ratio = contrast(ink, window);
        std::printf("  [contraste] %-52s %s -> %.2f:1\n", spot.where,
                    qPrintable(ink.name()), ratio);
        EXPECT_GE(ratio, 4.5)
            << spot.where << ": el texto está a " << ratio
            << ":1 sobre el gris de ventana y WCAG pide 4,5:1. Se lee en la pantalla del "
               "que lo escribió y no en un taller con luz de nave";
    }
}

TEST(SecondaryTextContrast, TheHandWrittenValuesItReplacedReallyDidFail) {
    // Que esta prueba no sea una que pasa porque sí. Los cuatro valores que
    // había antes tienen que SUSPENDER con la misma fórmula: si pasaran, es que
    // la fórmula o el fondo de referencia están mal, y entonces el aprobado de
    // arriba tampoco significa nada.
    const QColor window(QStringLiteral("#f0f0f0"));
    struct Old {
        const char* what;
        const char* hex;
        double measured;
    };
    const Old previously[] = {
        {"camera_image_page", "#888888", 3.11},
        {"detection_page", "#8a8a8a", 3.03},
        {"piece_report_dialog", "#9aa0a6", 2.32},
        {"measurement_mode_dialog", "#ffb454", 1.55},
    };
    for (const auto& old : previously) {
        const double ratio = contrast(QColor(QString::fromLatin1(old.hex)), window);
        std::printf("  [contraste] antes %-24s %s -> %.2f:1\n", old.what, old.hex, ratio);
        EXPECT_LT(ratio, 4.5) << old.what << ": el valor que había antes SÍ pasaba. "
                                 "Entonces esta prueba no comprueba lo que cree";
        EXPECT_NEAR(ratio, old.measured, 0.02)
            << old.what << ": la fórmula ya no da lo que se midió al arreglarlo";
    }
}

TEST(SecondaryTextContrast, TheRedNoticeWasLeftAloneBecauseTheThemeMeasuresWorse) {
    // La excepción, con su número. Sustituir a ciegas «lo escrito a mano» por
    // «el token» habría empeorado esta, y una regla que se aplica sin medir es
    // la misma clase de error que vino a arreglar.
    const double byHand = contrast(QColor(QStringLiteral("#3a1010")),
                                   QColor(QStringLiteral("#ffd9d9")));
    const double withTokens = contrast(QColor(QString(pci::ui::theme::kBad)),
                                       QColor(QString(pci::ui::theme::kBadField)));
    std::printf("  [contraste] aviso rojo: a mano %.2f:1, con tokens %.2f:1\n", byHand,
                withTokens);
    EXPECT_GE(byHand, 4.5) << "el aviso rojo escrito a mano tampoco se lee: entonces hay "
                              "que cambiarlo, y el motivo para dejarlo era falso";
    EXPECT_GT(byHand, withTokens)
        << "el par de tokens ya mide mejor que el escrito a mano. Entonces la razón para "
           "dejar este a mano ha caducado: pásalo a tokens y borra esta prueba";
}

TEST(SecondaryTextContrast, TheMosaicBadgeSaysWhichPieceIsMeasuredAndCanBeRead) {
    // EL MISMO SIGNIFICADO CON DOS COLORES, y de momento se queda así.
    //
    // El mosaico marca en verde la pieza que se está midiendo; la pastilla de
    // estado de la ventana marca ESA MISMA COSA en azul (`kChipChosen`). Eso es
    // el desorden que motivó la paleta, otra vez. Unificarlos cambia lo que el
    // operador ve todo el día en la pantalla que más usa, así que se decide con
    // la pantalla delante y no de paso.
    //
    // Lo que sí se arregló ahora es que el verde dejara de estar tecleado: el
    // MISMO valor estaba escrito dos veces en el mismo fichero y en dos formatos
    // —`QColor(0, 190, 0)` para la chapa del número y `#00be00` para el marco—.
    // Así es como se acaba teniendo tres verdes que casi coinciden.
    //
    // Y ya que tiene nombre, se mide: el número de la baldosa es lo que dice CUÁL
    // es, y sobre noventa píxeles de foto no puede quedarse en un verde sobre
    // verde.
    const double measured = contrast(QColor(QString(pci::ui::theme::kInkOnTileMeasured)),
                                     QColor(QString(pci::ui::theme::kTileMeasured)));
    std::printf("  [contraste] baldosa medida: %s sobre %s -> %.2f:1\n",
                pci::ui::theme::kInkOnTileMeasured, pci::ui::theme::kTileMeasured, measured);
    EXPECT_GE(measured, 4.5)
        << "el número de la baldosa que se está midiendo no se lee sobre su propia chapa";

    // Y el marco tiene que distinguirse del de las demás: es lo único que dice
    // cuál está elegida cuando hay cien baldosas. WCAG pide 3:1 para un elemento
    // gráfico, y aquí se compara contra el marco en reposo, que es su vecino.
    const double frame = contrast(QColor(QString(pci::ui::theme::kTileMeasured)),
                                  QColor(QString(pci::ui::theme::kChipRest)));
    std::printf("  [contraste] marco elegido contra marco en reposo -> %.2f:1\n", frame);
    EXPECT_GE(frame, 3.0)
        << "el marco de la baldosa elegida no se distingue del de las demás, y entonces "
           "no dice nada";
}
