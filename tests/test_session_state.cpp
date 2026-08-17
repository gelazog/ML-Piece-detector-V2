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

// ---------------------------------------------------------------------------
// Restablecer: OLVIDAR, no escribir valores de fábrica
// ---------------------------------------------------------------------------

TEST(SessionState, ResettingForgetsInsteadOfWritingDefaults) {
    // La diferencia parece de matiz y es la que impide que esto se
    // desincronice. Cada sitio que LEE un ajuste lleva su valor por defecto en
    // la propia llamada, así que borrando la clave el programa vuelve
    // exactamente a lo que hace recién instalado. Escribir aqui una segunda
    // copia de esos valores crearia dos listas que mantener a la vez.
    SettingsFixture fixture;
    auto* settings = fixture.settings();
    ASSERT_NE(settings, nullptr);

    settings->setInt("det_blur", 9);
    settings->setInt("det_morph", 11);
    settings->setString("work_zone_mode", "fixed");

    const auto forgotten = settings->forget();
    ASSERT_TRUE(forgotten.isOk()) << forgotten.error().message;
    EXPECT_EQ(forgotten.value(), 3) << "no dijo cuantos ajustes se llevo";

    // La clave no existe: la lectura devuelve el valor por defecto de QUIEN LEE.
    // Dos lectores con defectos distintos siguen obteniendo el suyo, que es
    // justo lo que se perderia escribiendo un valor.
    EXPECT_EQ(settings->getInt("det_blur", 5).value(), 5);
    EXPECT_EQ(settings->getInt("det_blur", 7).value(), 7);
    EXPECT_EQ(settings->getString("work_zone_mode", "auto").value(), "auto");

    auto rows = settings->listAll();
    ASSERT_TRUE(rows.isOk());
    EXPECT_TRUE(rows.value().empty()) << "quedaron ajustes despues de restablecer";
}

TEST(SessionState, ResettingByFamilyLeavesTheOthersAlone) {
    // Restablecer una pestaña no puede llevarse la calibracion de la maquina.
    SettingsFixture fixture;
    auto* settings = fixture.settings();
    settings->setInt("det_blur", 9);
    settings->setInt("det_morph", 11);
    settings->setDouble("calib_mm_per_px", 0.25);
    settings->setInt("pref_auto_interval_ms", 2000);

    const auto forgotten = settings->forget("det_");
    ASSERT_TRUE(forgotten.isOk()) << forgotten.error().message;
    EXPECT_EQ(forgotten.value(), 2);

    EXPECT_EQ(settings->getInt("det_blur", 5).value(), 5);
    EXPECT_DOUBLE_EQ(settings->getDouble("calib_mm_per_px", 0.0).value(), 0.25)
        << "restablecer la deteccion se llevo la calibracion";
    EXPECT_EQ(settings->getInt("pref_auto_interval_ms", 1000).value(), 2000);
}

TEST(SessionState, ResettingNothingIsNotAnError) {
    // «No habia nada que restablecer» es una respuesta distinta de un fallo, y
    // el operador tiene que poder distinguirlas.
    SettingsFixture fixture;
    const auto forgotten = fixture.settings()->forget();
    ASSERT_TRUE(forgotten.isOk()) << forgotten.error().message;
    EXPECT_EQ(forgotten.value(), 0);

    // Y un prefijo que no casa con nada tampoco lo es.
    fixture.settings()->setInt("det_blur", 9);
    const auto none = fixture.settings()->forget("no_existe_");
    ASSERT_TRUE(none.isOk());
    EXPECT_EQ(none.value(), 0);
    EXPECT_EQ(fixture.settings()->getInt("det_blur", 5).value(), 9)
        << "un prefijo que no casa se llevo algo por delante";
}
