// QUITAR UN ACABADO REGISTRADO POR ERROR.
//
// Una pieza es buena si alguno de sus acabados la reconoce. Si alguien registra
// como acabado una pieza mala, desde ese momento las piezas malas como ella
// pasan por buenas, y no había forma de quitarlo desde la ventana:
// `PieceRepository::deleteVariant` existía, pero ninguna pantalla lo llamaba.
//
// Esto fija que el gestor de piezas enseña los acabados de la pieza elegida,
// que quitar uno pregunta antes (con Cancelar en el Enter, porque no se puede
// deshacer) y lo borra de la base de datos dejando los demás, y que la
// «principal» se ve pero no se puede quitar.

#include <gtest/gtest.h>

#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database/db.h"
#include "database/schema.h"
#include "ml/reference.h"
#include "repositories/piece_repository.h"
#include "ui/piece_manager_dialog.h"

using namespace pci;

namespace {

QStringList textsOf(const QListWidget& list) {
    QStringList texts;
    for (int row = 0; row < list.count(); ++row) {
        texts << list.item(row)->text();
    }
    return texts;
}

void select(QListWidget& list, const QString& text) {
    list.setCurrentItem(list.findItems(text, Qt::MatchExactly).value(0));
}

struct Asked {
    bool shown = false;
    bool cancelByDefault = false;
    QString later;  // un segundo aviso (un error), que se cierra para no colgar
};

// Pulsa el botón y contesta `answer` en la confirmación, que es modal.
Asked clickAndAnswer(QPushButton& button, QMessageBox::StandardButton answer) {
    Asked asked;
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr) {
            return;
        }
        if (asked.shown) {
            asked.later = box->text();
            box->reject();
            return;
        }
        asked.shown = true;
        asked.cancelByDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
        if (auto* reply = box->button(answer)) {
            reply->click();
        } else {
            box->reject();
        }
    });
    poll.start();
    button.click();
    return asked;
}

// «brida» con tres acabados y «tuerca» solo con la principal, en una BD temporal.
class RemoveFinish : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(dir_.isValid());
        auto opened = database::Db::open(dir_.filePath(QStringLiteral("pci.db")).toStdString());
        ASSERT_TRUE(opened.isOk()) << opened.error().message;
        db_ = std::move(opened.value());
        ASSERT_TRUE(database::migrate(*db_).isOk());
        pieces_ = std::make_unique<repositories::PieceRepository>(*db_);

        ml::Reference reference;
        reference.mean = {1.0F, 2.0F};
        reference.stddev = {0.1F, 0.1F};
        reference.sampleCount = 30;
        auto brida = pieces_->createPiece("brida");
        ASSERT_TRUE(brida.isOk());
        brida_ = brida.value();
        for (const char* finish : {"principal", "pulido", "mate"}) {
            ASSERT_TRUE(pieces_->saveReference(brida_, reference, finish).isOk());
        }
        auto tuerca = pieces_->createPiece("tuerca");
        ASSERT_TRUE(tuerca.isOk());
        ASSERT_TRUE(pieces_->saveReference(tuerca.value(), reference).isOk());
    }

    std::vector<std::string> finishesInDatabase() {
        auto listed = pieces_->listVariants(brida_);
        return listed.isOk() ? listed.value() : std::vector<std::string>{};
    }

    QTemporaryDir dir_;  // el primero, para borrarse después de cerrar la BD
    std::unique_ptr<database::Db> db_;
    std::unique_ptr<repositories::PieceRepository> pieces_;
    std::int64_t brida_ = -1;
};

}  // namespace

TEST_F(RemoveFinish, TheListShowsTheFinishesOfTheSelectedPiece) {
    ui::PieceManagerDialog dialog(pieces_.get(), nullptr);
    auto* finishes = dialog.findChild<QListWidget*>(QStringLiteral("variantList"));
    auto* pieceList = dialog.findChild<QListWidget*>(QStringLiteral("pieceList"));
    ASSERT_NE(finishes, nullptr) << "el gestor de piezas no enseña los acabados";
    ASSERT_NE(pieceList, nullptr);

    select(*pieceList, QStringLiteral("brida"));
    EXPECT_EQ(textsOf(*finishes),
              (QStringList{QStringLiteral("mate"), QStringLiteral("principal"),
                           QStringLiteral("pulido")}));

    select(*pieceList, QStringLiteral("tuerca"));
    EXPECT_EQ(textsOf(*finishes), QStringList{QStringLiteral("principal")})
        << "al cambiar de pieza siguen los acabados de la anterior";
}

TEST_F(RemoveFinish, TheMainOneIsShownButCannotBeRemoved) {
    ui::PieceManagerDialog dialog(pieces_.get(), nullptr);
    auto* finishes = dialog.findChild<QListWidget*>(QStringLiteral("variantList"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("removeVariantButton"));
    ASSERT_NE(finishes, nullptr);
    ASSERT_NE(remove, nullptr) << "no hay forma de quitar un acabado";
    EXPECT_FALSE(remove->autoDefault()) << "tabular hasta él y pulsar Enter lo dispara";

    select(*finishes, QStringLiteral("principal"));
    EXPECT_FALSE(remove->isEnabled()) << "deja quitar la principal";
    EXPECT_TRUE(remove->toolTip().contains(QStringLiteral("principal")))
        << "apagado sin decir por qué: " << remove->toolTip().toStdString();
    EXPECT_FALSE(remove->toolTip().contains(QLatin1Char('\n')));

    select(*finishes, QStringLiteral("pulido"));
    EXPECT_TRUE(remove->isEnabled());
    EXPECT_FALSE(remove->toolTip().contains(QLatin1Char('\n')));
}

TEST_F(RemoveFinish, RemovingOneDeletesItFromTheDatabaseAndKeepsTheOthers) {
    ui::PieceManagerDialog dialog(pieces_.get(), nullptr);
    auto* finishes = dialog.findChild<QListWidget*>(QStringLiteral("variantList"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("removeVariantButton"));
    ASSERT_NE(finishes, nullptr);
    ASSERT_NE(remove, nullptr);
    select(*finishes, QStringLiteral("pulido"));

    // Cancelar no toca nada.
    Asked asked = clickAndAnswer(*remove, QMessageBox::Cancel);
    EXPECT_TRUE(asked.shown) << "se quita sin preguntar";
    EXPECT_TRUE(asked.cancelByDefault) << "un Enter en la confirmación lo quita";
    EXPECT_EQ(finishesInDatabase().size(), 3U) << "cancelar también lo quitó";
    EXPECT_FALSE(dialog.changed());

    asked = clickAndAnswer(*remove, QMessageBox::Yes);
    EXPECT_TRUE(asked.later.isEmpty()) << asked.later.toStdString();
    EXPECT_EQ(finishesInDatabase(), (std::vector<std::string>{"mate", "principal"}));
    EXPECT_EQ(textsOf(*finishes),
              (QStringList{QStringLiteral("mate"), QStringLiteral("principal")}));
    EXPECT_TRUE(dialog.changed())
        << "la ventana no se entera de que la pieza cambió";
}
