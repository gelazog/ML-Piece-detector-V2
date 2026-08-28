// REGISTRAR OTRO ACABADO NO PUEDE PARECER «REGISTRAR UNA PIEZA NUEVA».
//
// Es el mismo asistente y el mismo flujo de capturas, así que la confusión es
// natural — y equivocarse no da un error: da una pieza DUPLICADA, con sus
// herramientas, sus tolerancias y su historial aparte. Eso no se descubre hasta
// que alguien se pregunta por qué hay dos «brida» en la lista.
//
// Lo que se comprueba aquí es que la ventana lo diga por sí sola: el título, la
// etiqueta del campo y el texto de encabezado. Lo que hace al guardar está
// cubierto en `tests/test_database.cpp` (que la variante no pisa a la principal)
// y en `tests/test_engine.cpp` (que el motor juzga contra todas).
//
// LO QUE ESTE FICHERO NO CUBRE, dicho para que no se dé por cubierto: el camino
// de guardar del propio asistente. Las capturas entran por la cámara y la sesión
// de registro se construye dentro, así que sin refactorizarlo no hay forma de
// llevarlo hasta el final desde una prueba.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>

#include <cstdio>
#include <filesystem>
#include <memory>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/piece_repository.h"
#include "ui/registration_wizard.h"

namespace {

pci::core::Result<std::vector<float>> noEmbed(const cv::Mat&) {
    return pci::core::Result<std::vector<float>>::ok({1.0F, 0.0F});
}

// Las dos etiquetas que distinguen los modos del asistente, por su NOMBRE.
//
// `nameCaption` existe siempre y cambia de texto; `variantIntro` sólo existe en
// modo acabado. Esa diferencia —una habla, la otra ni está— es justo lo que
// comprueba este fichero, y buscarlas por su texto la confundía con «el rótulo
// se reescribió».
QLabel* namedLabel(QWidget& widget, const char* name) {
    return widget.findChild<QLabel*>(QString::fromLatin1(name));
}

}  // namespace

TEST(VariantWizard, TheVariantModeSaysItIsNotANewPiece) {
    const auto path = (std::filesystem::temp_directory_path() / "pci_variant_wizard.db")
                          .string();
    std::filesystem::remove(path);
    auto opened = pci::database::Db::open(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::PieceRepository pieces(*db);
    const auto pieceId = pieces.createPiece("brida");
    ASSERT_TRUE(pieceId.isOk());

    pci::ui::RegistrationWizard variant(nullptr, noEmbed, &pieces, pieceId.value(),
                                        QStringLiteral("brida"));

    // 1) El título nombra la pieza y el acabado, no «pieza nueva».
    std::printf("  [acabado] titulo: %s\n", variant.windowTitle().toStdString().c_str());
    EXPECT_TRUE(variant.windowTitle().contains(QStringLiteral("acabado")))
        << "el título dice: " << variant.windowTitle().toStdString();
    EXPECT_TRUE(variant.windowTitle().contains(QStringLiteral("brida")))
        << "el título no dice a qué pieza se le está añadiendo el acabado";
    EXPECT_FALSE(variant.windowTitle().contains(QStringLiteral("pieza nueva")));

    // 2) El campo pide el nombre del ACABADO, no el de la pieza.
    auto* caption = namedLabel(variant, "nameCaption");
    ASSERT_NE(caption, nullptr) << "el campo del nombre no lleva rótulo";
    EXPECT_TRUE(caption->text().contains(QStringLiteral("acabado")))
        << "el campo sigue pidiendo el nombre de la pieza: quien lo rellene creerá que "
           "está registrando una pieza. Dice: "
        << caption->text().toStdString();

    // Y el ejemplo del campo enseña qué clase de nombre se espera.
    auto* field = variant.findChild<QLineEdit*>();
    ASSERT_NE(field, nullptr);
    std::printf("  [acabado] ejemplo del campo: %s\n",
                field->placeholderText().toStdString().c_str());
    EXPECT_TRUE(field->placeholderText().contains(QStringLiteral("pulido")) ||
                field->placeholderText().contains(QStringLiteral("proveedor")))
        << "el ejemplo del campo no ayuda a entender qué se pide: "
        << field->placeholderText().toStdString();

    // 3) Y se explica POR QUÉ hay que registrarlo aparte, que es la parte que
    //    evita que alguien los mezcle «para simplificar».
    auto* intro = namedLabel(variant, "variantIntro");
    ASSERT_NE(intro, nullptr) << "no se explica que esto no crea una pieza";
    EXPECT_TRUE(intro->text().contains(QStringLiteral("no una pieza nueva")))
        << "la explicación no dice lo único que hay que entender aquí: "
        << intro->text().toStdString();
    EXPECT_TRUE(intro->wordWrap()) << "la explicación se corta en vez de leerse entera";
    EXPECT_TRUE(intro->text().contains(QStringLiteral("dejan de vigilar")))
        << "no se dice qué pasa si se mezclan, que es el motivo de que esto exista";
}

// Y el asistente normal sigue siendo el de siempre: añadir el modo variante no
// puede cambiar el flujo que ya funcionaba.
TEST(VariantWizard, TheOrdinaryWizardIsUnchanged) {
    const auto path = (std::filesystem::temp_directory_path() / "pci_variant_wizard2.db")
                          .string();
    std::filesystem::remove(path);
    auto opened = pci::database::Db::open(path);
    ASSERT_TRUE(opened.isOk());
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::PieceRepository pieces(*db);

    pci::ui::RegistrationWizard normal(nullptr, noEmbed, &pieces);
    EXPECT_TRUE(normal.windowTitle().contains(QStringLiteral("pieza nueva")))
        << "el título dice: " << normal.windowTitle().toStdString();
    auto* caption = namedLabel(normal, "nameCaption");
    ASSERT_NE(caption, nullptr);
    EXPECT_TRUE(caption->text().contains(QStringLiteral("pieza")))
        << "el asistente normal no pide el nombre de la pieza: "
        << caption->text().toStdString();
    EXPECT_EQ(namedLabel(normal, "variantIntro"), nullptr)
        << "el asistente normal enseña la explicación del modo acabado";
    EXPECT_TRUE(normal.savedVariant().isEmpty());
}
