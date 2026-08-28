// LAS BANDAS SOBRE EL VÍDEO, Y UNA SOSPECHA QUE RESULTÓ FALSA.
//
// Son tres: el aviso de puesta en marcha, la lectura continua del tablero y su
// estado de fuera de tolerancia. Van encima de la imagen, así que llevan fondo
// propio y oscuro.
//
// Estaban escritas a mano y con el desorden de siempre. El estilo de la lectura
// estaba tecleado DOS VECES palabra por palabra —donde se crea la banda y donde
// se restaura al salir de la alarma— y había dos azules de fondo, `#1b2b38` y
// `#10222b`, que se distinguen 1,13:1: o sea, el mismo color. Nadie eligió tener
// dos, y con un tercero en el calibrador (`#22333a`) ya eran tres.
//
// LA SOSPECHA, QUE ERA LO IMPORTANTE Y RESULTÓ FALSA. Antes de tocar nada se
// midió si el estado de alarma se distinguía solo por TONO, que es el error que
// esta paleta lleva escrito en su cabecera: dos veredictos con la misma
// luminancia son el mismo gris para un daltónico deutan, y en una ALARMA eso es
// lo más caro que puede pasar.
//
// No lo era: los dos fondos se separan 1,59:1, por encima del 1,26 que la propia
// paleta ya acepta entre «no cumple» y «aviso». Así que no se cambió el aspecto
// —solo se le puso nombre—, y esta prueba guarda la medida para que el día que
// alguien retoque esos fondos se entere si los acerca.
//
// La alarma lleva además NEGRITA, que es la señal que no depende del color.

#include <gtest/gtest.h>

#include <QColor>
#include <QString>

#include <cmath>
#include <cstdio>

#include "ui/theme.h"

namespace {

double channel(double v) {
    v /= 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(const char* hex) {
    const QColor c(QString::fromLatin1(hex));
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) +
           0.0722 * channel(c.blue());
}

double contrast(const char* a, const char* b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace

TEST(VideoBands, EveryBandCanBeRead) {
    using namespace pci::ui::theme;
    struct Row {
        const char* what;
        const char* ink;
        const char* field;
    };
    const Row rows[] = {
        {"aviso de puesta en marcha", kProseOnBand, kBandField},
        {"lectura del tablero", kInkOnBand, kBandField},
        {"lectura fuera de tolerancia", kInkOnBandAlarm, kBandAlarm},
    };
    for (const auto& row : rows) {
        const double ratio = contrast(row.ink, row.field);
        std::printf("  [banda] %-30s %s sobre %s -> %.2f:1\n", row.what, row.ink, row.field,
                    ratio);
        EXPECT_GE(ratio, 4.5) << row.what
                              << ": la banda va encima del vídeo, donde el operador mira "
                                 "todo el día, y no llega al contraste de texto";
    }
}

TEST(VideoBands, TheAlarmIsNotJustAnotherHue) {
    // La comprobación que motivó todo esto. Un cambio de estado que solo cambia
    // el TONO no lo ve un daltónico deutan —el más común— ni nadie de reojo, y
    // esta paleta ya se comió ese fallo una vez con «no cumple» y «aviso».
    //
    // El listón es el que la propia paleta aceptó entonces: 1,26 entre dos
    // veredictos. Aquí hay 1,59.
    using namespace pci::ui::theme;
    const double apart = contrast(kBandField, kBandAlarm);
    std::printf("  [banda] los dos estados se separan %.2f:1 en luminancia\n", apart);
    EXPECT_GE(apart, 1.26)
        << "los dos estados de la lectura tienen casi la misma luminancia: cambian de "
           "tono y no de claridad, así que de reojo —o para un daltónico— son la misma "
           "banda. Es el fallo que esta paleta ya arregló una vez entre veredictos";
}

TEST(VideoBands, TheThreeDarkBluesWereOneColour) {
    // Por qué se unificaron, con el número: no era una preferencia. Los tres
    // fondos que había se distinguen tan poco entre sí que llamarlos colores
    // distintos era darle nombre a un error de tecleo.
    //
    // Si algún día hiciera falta un segundo fondo de banda de verdad, esta
    // prueba dice cuánto tendría que separarse para que se note.
    const char* kWereThere[] = {"#1b2b38", "#10222b", "#22333a"};
    for (const auto* other : kWereThere) {
        const double apart = contrast(pci::ui::theme::kBandField, other);
        std::printf("  [banda] el fondo de hoy contra el viejo %s -> %.2f:1\n", other,
                    apart);
        EXPECT_LT(apart, 1.26)
            << "el azul " << other
            << " SÍ se distinguía del que ha quedado: entonces unificarlos cambió algo "
               "que el operador ve, y eso había que decidirlo con la pantalla delante";
    }
}
