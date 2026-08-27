// LOS ACELERADORES `Alt+letra`, Y SOBRE TODO QUE NO CHOQUEN.
//
// En Qt, un `&` dentro del texto de un botón o de un menú crea un mnemónico:
// Alt+esa letra lo activa. La barra de menús ya los usa —&Archivo, &Fuente,
// &Medida…— y los botones de la ventana no tenían ninguno.
//
// Añadirlos es una mejora; el motivo de que esto sea una PRUEBA y no solo un
// cambio es lo otro: **dos mnemónicos iguales en la misma ventana no dan un
// error, dan un ciclo**. Alt+P deja de activar y pasa a ir saltando entre los
// dos candidatos, que desde fuera se vive como «a veces hace otra cosa». Es la
// misma familia que `ambiguousActivate` con los atajos, y este proyecto ya se
// comió aquella con Ctrl+1 y Ctrl+2.
//
// Así que lo que se fija aquí no es «cuántos hay» sino «ninguno pisa a otro»,
// contando también los de la barra de menús, que compiten por las mismas
// teclas.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QChar>
#include <QDialog>
#include <QLineEdit>
#include <QMenuBar>
#include <QString>

#include <cstdio>
#include <map>

#include "ui/history_dialog.h"
#include "ui/lens_calibration_dialog.h"
#include "ui/main_window.h"
#include "ui/measurement_mode_dialog.h"
#include "ui/piece_manager_dialog.h"
#include "ui/template_manager_dialog.h"

using namespace pci;

namespace {

// La letra que sigue a un `&` suelto. Vacío si el texto no tiene mnemónico.
// «&&» es un ampersand literal y no cuenta.
QChar mnemonicOf(const QString& text) {
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != QLatin1Char('&')) {
            continue;
        }
        if (text[i + 1] == QLatin1Char('&')) {
            ++i;  // «&&» literal: se salta el par entero
            continue;
        }
        return text[i + 1].toUpper();
    }
    return {};
}

}  // namespace

TEST(Mnemonics, NoTwoOfThemClaimTheSameLetter) {
    ui::MainWindow window;

    // Quién reclama cada letra. La barra de menús entra en el mismo saco: un
    // botón con Alt+P compite con el menú «&Pieza» por la misma tecla.
    std::map<QChar, QStringList> owners;

    for (auto* action : window.menuBar()->actions()) {
        const QChar key = mnemonicOf(action->text());
        if (!key.isNull()) {
            owners[key] << (QStringLiteral("menú ") + action->text());
        }
    }
    for (auto* button : window.findChildren<QAbstractButton*>()) {
        const QChar key = mnemonicOf(button->text());
        if (!key.isNull()) {
            owners[key] << (QStringLiteral("botón ") + button->text());
        }
    }

    int claimed = 0;
    int clashes = 0;
    for (const auto& [key, who] : owners) {
        ++claimed;
        if (who.size() > 1) {
            ++clashes;
            ADD_FAILURE() << "Alt+" << QString(key).toStdString() << " lo reclaman "
                          << who.size() << ": " << who.join(QStringLiteral(", ")).toStdString()
                          << ". Qt no activa ninguno — va ciclando entre ellos, y desde "
                             "fuera eso se vive como «a veces hace otra cosa».";
        }
    }
    std::printf("  [alt] %d letras reclamadas, %d con choque\n", claimed, clashes);
    EXPECT_GT(claimed, 5)
        << "apenas hay mnemónicos: esta prueba no está comprobando nada. La barra de "
           "menús sola ya trae siete.";
}

TEST(Mnemonics, TheDialogsAreWhereAltLetterWouldEarnItsPlace) {
    // C2 decía «ningún botón tiene acelerador Alt+letra, 0 de unos 40» y lo
    // contaba sobre la ventana principal. Ahí la falta NO es un defecto: los
    // botones que se usan a diario ya tienen TECLA SUELTA —I inspecciona, C
    // calibra, P abre el editor, F1 la guía— y una tecla suelta es mejor que
    // Alt+letra: se pulsa con una mano y no compite con la barra de menús, que
    // ya se ha quedado con A, F, I, M, P, V e Y.
    //
    // Donde sí haría falta es en los DIÁLOGOS, y por una razón concreta: allí
    // hay campos de texto, así que una tecla suelta es imposible —se la come el
    // campo— y Alt+letra es el único mecanismo que queda. Y ahí no hay ninguno.
    //
    // Esta prueba mide eso y no exige nada todavía: pone el número delante para
    // que la decisión de añadirlos se tome sabiendo cuántos y dónde, en vez de
    // por cumplir una nota.
    struct Seen {
        int buttons = 0;
        int withMnemonic = 0;
        int clashes = 0;
    };
    const auto look = [](const QDialog& dialog, const char* name) {
        Seen seen;
        std::map<QChar, QStringList> owners;
        for (auto* button : dialog.findChildren<QAbstractButton*>()) {
            if (button->text().isEmpty()) {
                continue;
            }
            ++seen.buttons;
            const QChar key = mnemonicOf(button->text());
            if (!key.isNull()) {
                ++seen.withMnemonic;
                owners[key] << button->text();
            }
        }
        // DENTRO DE UN DIÁLOGO, y no contra la barra de menús: un diálogo modal
        // bloquea la ventana de detrás, así que sus letras solo compiten entre
        // ellas. Por eso «&Eliminar» puede convivir con el menú «&Inspección».
        for (const auto& [key, who] : owners) {
            if (who.size() > 1) {
                ++seen.clashes;
                ADD_FAILURE() << name << ": Alt+" << QString(key).toStdString()
                              << " lo reclaman "
                              << who.join(QStringLiteral(", ")).toStdString()
                              << ". Qt no activa ninguno: va ciclando entre ellos.";
            }
        }
        const int fields = static_cast<int>(dialog.findChildren<QLineEdit*>().size());
        std::printf("  [alt] %-24s %2d botones, %2d con Alt+letra, %d campos\n",
                    name, seen.buttons, seen.withMnemonic, fields);
        return seen;
    };

    repositories::PieceMeasurement measurement;
    ui::MeasurementModeDialog mode(measurement, QStringLiteral("pieza"));
    ui::TemplateManagerDialog templates(nullptr, 0, QStringLiteral("principal"));
    ui::PieceManagerDialog pieces(nullptr, nullptr);
    ui::HistoryDialog history(nullptr, nullptr, 0);
    ui::LensCalibrationDialog lens;

    int buttons = 0;
    int covered = 0;
    for (const auto& seen : {look(mode, "Modo de medición"),
                             look(templates, "Plantillas"), look(pieces, "Piezas"),
                             look(history, "Historial"),
                             look(lens, "Calibración de lente")}) {
        buttons += seen.buttons;
        covered += seen.withMnemonic;
    }
    std::printf("  [alt] %d de %d botones de diálogo con acelerador\n", covered,
                buttons);

    EXPECT_GT(buttons, 10) << "apenas se encuentran botones en los diálogos: el barrido "
                              "ya no está mirando donde cree";
    // Un trinquete al revés que los demás: aquí lo que no puede es BAJAR. Los
    // dos gestores están hechos —donde más se teclea, porque son los que borran
    // cosas— y los otros tres van cuando toque.
    EXPECT_GE(covered, 10) << "se han perdido aceleradores de los diálogos que ya los "
                              "tenían";
}