// Banco de lo que la aplicación RECUERDA entre sesiones.
//
// La regla que gobierna todo esto: lo que el operador coloca una vez tiene que
// seguir colocado mañana. Lo contrario no se percibe como un ajuste que falta,
// sino como que el programa no se acuerda de nada — y a los dos días se deja de
// colocar.
//
// Aquí van las piezas probables sin ventana: el nombre con el que se persiste
// cada fuente, y el tamaño de diálogo con sus dos casos difíciles (que no haya
// nada guardado, y que lo guardado ya no quepa en la pantalla de hoy).
#include <gtest/gtest.h>

#include <QDialog>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "camera/frame_source.h"
#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/dialog_geometry.h"

namespace {

// Base en disco temporal: los ajustes viven en SQLite, y probar con un doble
// dejaría sin comprobar justamente el ida y vuelta que interesa.
class SettingsFixture {
public:
    SettingsFixture() {
        path_ = std::filesystem::temp_directory_path() /
                ("pci_session_" + std::to_string(::testing::UnitTest::GetInstance()
                                                     ->random_seed()) +
                 std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".db");
        auto opened = pci::database::Db::open(path_.string());
        EXPECT_TRUE(opened.isOk()) << (opened.isOk() ? "" : opened.error().message);
        if (!opened.isOk()) {
            return;
        }
        db_ = std::move(opened.value());
        const auto migrated = pci::database::migrate(*db_);
        EXPECT_TRUE(migrated.isOk()) << (migrated.isOk() ? "" : migrated.error().message);
        settings_ = std::make_unique<pci::repositories::SettingsRepository>(*db_);
    }

    ~SettingsFixture() {
        settings_.reset();
        db_.reset();
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] pci::repositories::SettingsRepository* settings() const {
        return settings_.get();
    }

private:
    std::filesystem::path path_;
    std::unique_ptr<pci::database::Db> db_;
    std::unique_ptr<pci::repositories::SettingsRepository> settings_;
};

}  // namespace

TEST(SessionState, EverySourceSurvivesBeingSavedAndRead) {
    using pci::camera::SourceKind;
    using pci::camera::sourceKindFromKey;
    using pci::camera::sourceKindKey;

    for (const auto kind : {SourceKind::Camera, SourceKind::Photo, SourceKind::Image,
                            SourceKind::Video}) {
        EXPECT_EQ(sourceKindFromKey(sourceKindKey(kind)), kind);
    }
    // Se persiste por NOMBRE justo para que añadir una fuente en el futuro no
    // convierta lo guardado en otra cosa. Con números, insertar `Photo` en
    // medio del enum habría hecho que los «2» guardados pasaran a ser vídeos.
    EXPECT_STREQ(sourceKindKey(SourceKind::Image), "image");
    EXPECT_STREQ(sourceKindKey(SourceKind::Video), "video");

    // Lo que no se reconoce cae a la cámara: es la única fuente que existe
    // siempre, y equivocarse hacia ella no deja el programa apuntando a un
    // fichero que quizá ya no está.
    EXPECT_EQ(sourceKindFromKey("imagen"), SourceKind::Camera);
    EXPECT_EQ(sourceKindFromKey(""), SourceKind::Camera);
    EXPECT_EQ(sourceKindFromKey(nullptr), SourceKind::Camera);
}

TEST(SessionState, ADialogWithNothingSavedOpensAtItsFactorySize) {
    SettingsFixture fixture;
    QDialog dialog;
    pci::ui::restoreDialogSize(dialog, fixture.settings(), "prueba", 640, 460);
    EXPECT_EQ(dialog.width(), 640);
    EXPECT_EQ(dialog.height(), 460);
}

TEST(SessionState, ADialogRemembersTheSizeItWasGiven) {
    SettingsFixture fixture;
    // El tamaño elegido cabe en cualquier pantalla a propósito. El primer
    // intento usaba 880 px de ancho y fallaba: la plataforma sin pantalla de
    // los tests mide 800, así que el acotado de más abajo hacía su trabajo y
    // devolvía 800. El test estaba mal, no el acotado — pero conviene que se
    // vea escrito, porque volver a subirlo lo rompería igual.
    {
        QDialog dialog;
        pci::ui::restoreDialogSize(dialog, fixture.settings(), "prueba", 640, 460);
        dialog.resize(560, 420);  // el operador lo cambia de tamaño
        pci::ui::rememberDialogSize(dialog, fixture.settings(), "prueba");
    }
    QDialog reopened;
    pci::ui::restoreDialogSize(reopened, fixture.settings(), "prueba", 640, 460);
    EXPECT_EQ(reopened.width(), 560);
    EXPECT_EQ(reopened.height(), 420);

    // Y cada diálogo lleva el suyo: guardar por nombre existe para que agrandar
    // el historial no agrande también la calibración.
    QDialog other;
    pci::ui::restoreDialogSize(other, fixture.settings(), "otro", 300, 300);
    EXPECT_EQ(other.width(), 300);
    EXPECT_EQ(other.height(), 300);
}

TEST(SessionState, ASizeFromAScreenThatIsNoLongerThereGetsClamped) {
    // El caso que convierte un ajuste cómodo en un ajuste que rompe: la sesión
    // anterior corrió en un monitor grande y hoy la máquina de línea tiene uno
    // pequeño. Sin acotar, el diálogo abriría con los botones fuera de la
    // pantalla y sin forma de alcanzarlos.
    SettingsFixture fixture;
    fixture.settings()->setInt("dlg_prueba_w", 30000);
    fixture.settings()->setInt("dlg_prueba_h", 30000);

    QDialog dialog;
    pci::ui::restoreDialogSize(dialog, fixture.settings(), "prueba", 640, 460);
    EXPECT_LE(dialog.width(), 4096);
    EXPECT_LE(dialog.height(), 4096);

    // Y por abajo: un tamaño ridículo guardado por accidente dejaría un diálogo
    // ilegible que además es dificilísimo de volver a agrandar.
    fixture.settings()->setInt("dlg_prueba_w", 5);
    fixture.settings()->setInt("dlg_prueba_h", 5);
    QDialog tiny;
    pci::ui::restoreDialogSize(tiny, fixture.settings(), "prueba", 640, 460);
    EXPECT_GE(tiny.width(), 240);
    EXPECT_GE(tiny.height(), 240);
}

TEST(SessionState, WithoutAPlaceToSaveNothingBreaks) {
    // La aplicación funciona sin persistencia si la base no pudo abrirse. Un
    // ajuste que no se puede guardar no puede impedir que el diálogo se abra.
    QDialog dialog;
    pci::ui::restoreDialogSize(dialog, nullptr, "prueba", 700, 500);
    EXPECT_EQ(dialog.width(), 700);
    EXPECT_EQ(dialog.height(), 500);
    EXPECT_NO_THROW(pci::ui::rememberDialogSize(dialog, nullptr, "prueba"));
}
