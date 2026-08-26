// ENTER NO PUEDE DISPARAR UN BOTÓN QUE DESTRUYE.
//
// En un `QDialog`, si nadie declara un botón por defecto, Qt elige el primer
// `QPushButton` con `autoDefault` — o sea, el primero que se construyó. Eso hace
// que el orden en que alguien escribió tres líneas de C++ decida qué pasa al
// pulsar Enter, y nadie lo revisa nunca porque no se ve en la pantalla.
//
// En la guía de atajos el orden era: «Restaurar por defecto», «Guardar»,
// «Cancelar». Restaurar borra TODA la tabla de teclas que el operador acaba de
// editar. Pulsar Enter creyendo que guardas y perder el trabajo es el peor
// desenlace posible de un diálogo, y no hace falta ningún fallo de programación
// para llegar a él: basta con teclear.
//
// Esta prueba no arregla un diálogo: arregla la CLASE de fallo. Recorre los
// diálogos, mira quién es el botón por defecto y se niega si es destructivo.

#include <gtest/gtest.h>

#include <QApplication>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>
#include <vector>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/shortcuts_dialog.h"

namespace {

// Las palabras que delatan a un botón que quita algo. Se buscan por prefijo
// porque «Restaurar», «Restablecer» y «Borrar» aparecen conjugados de varias
// formas y la lista de textos exactos envejecería a la primera reescritura.
const QStringList& destructiveWords() {
    static const QStringList words{
        QStringLiteral("Restaurar"), QStringLiteral("Restablecer"),
        QStringLiteral("Borrar"),    QStringLiteral("Eliminar"),
        QStringLiteral("Quitar"),    QStringLiteral("Descartar"),
    };
    return words;
}

bool looksDestructive(const QString& text) {
    for (const auto& word : destructiveWords()) {
        if (text.contains(word, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// Quién se lleva el Enter. Se pregunta a los propios botones, no se deduce del
// orden de construcción: lo que importa es lo que Qt hará, no lo que uno cree
// que hará.
QPushButton* defaultButton(const QDialog& dialog) {
    const auto buttons = dialog.findChildren<QPushButton*>();
    for (auto* button : buttons) {
        if (button->isDefault()) {
            return button;
        }
    }
    // Sin ninguno declarado, Qt se queda con el primero que tenga autoDefault.
    for (auto* button : buttons) {
        if (button->autoDefault()) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST(DefaultButtons, TheShortcutsDialogDoesNotWipeTheTableWhenYouPressEnter) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto opened = pci::database::Db::open(
        QDir(dir.path()).filePath(QStringLiteral("k.db")).toStdString());
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);

    // Un par de atajos cualesquiera: el diálogo necesita algo que enseñar.
    QAction undo(QStringLiteral("Deshacer"), nullptr);
    undo.setShortcut(QKeySequence::Undo);
    QAction redo(QStringLiteral("Rehacer"), nullptr);
    redo.setShortcut(QKeySequence::Redo);
    std::vector<pci::ui::ShortcutSpec> shortcuts{
        {QStringLiteral("undo"), QStringLiteral("Deshacer"), QKeySequence::Undo, &undo},
        {QStringLiteral("redo"), QStringLiteral("Rehacer"), QKeySequence::Redo, &redo},
    };

    pci::ui::ShortcutsDialog dialog(&shortcuts, &settings);
    auto* chosen = defaultButton(dialog);
    ASSERT_NE(chosen, nullptr) << "ningún botón se lleva el Enter: entonces el diálogo no "
                                 "responde a la tecla más usada que hay";
    std::printf("  [Enter] guía de atajos -> «%s»\n", chosen->text().toStdString().c_str());

    EXPECT_FALSE(looksDestructive(chosen->text()))
        << "pulsar Enter en la guía de atajos dispara «" << chosen->text().toStdString()
        << "», que borra la tabla de teclas que el operador acaba de editar. Nadie "
           "escribió eso a propósito: en un QDialog sin botón por defecto declarado, Qt "
           "coge el primero que se construyó, así que lo decide el orden de tres líneas "
           "de C++ que nadie vuelve a mirar.";
}

TEST(DefaultButtons, NoDestructiveButtonKeepsAutoDefaultInTheShortcutsDialog) {
    // No basta con declarar otro por defecto. `autoDefault` hace que un botón se
    // convierta en el del Enter EN CUANTO RECIBE EL FOCO, así que un «Restaurar»
    // con autoDefault sigue siendo una trampa: se llega a él tabulando.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto opened = pci::database::Db::open(
        QDir(dir.path()).filePath(QStringLiteral("k.db")).toStdString());
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);

    QAction undo(QStringLiteral("Deshacer"), nullptr);
    std::vector<pci::ui::ShortcutSpec> shortcuts{
        {QStringLiteral("undo"), QStringLiteral("Deshacer"), QKeySequence::Undo, &undo},
    };
    pci::ui::ShortcutsDialog dialog(&shortcuts, &settings);

    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (!looksDestructive(button->text())) {
            continue;
        }
        std::printf("  [Enter] botón destructivo: «%s» (autoDefault=%d)\n",
                    button->text().toStdString().c_str(),
                    static_cast<int>(button->autoDefault()));
        EXPECT_FALSE(button->autoDefault())
            << "«" << button->text().toStdString()
            << "» conserva autoDefault: basta con tabular hasta él para que el Enter "
               "siguiente destruya la tabla.";
        EXPECT_FALSE(button->isDefault())
            << "«" << button->text().toStdString() << "» es el botón por defecto.";
    }
}
