#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "database/blob_codec.h"
#include "database/db.h"
#include "database/schema.h"
#include "database/statement.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "ml/reference.h"
#include "domain/measurement_mode.h"
#include "repositories/config_io.h"
#include "repositories/detection_profile_repository.h"
#include "repositories/piece_repository.h"
#include "repositories/settings_repository.h"
#include "repositories/tool_repository.h"

using namespace pci;

namespace {

// Cada test trabaja sobre un archivo temporal propio que se limpia al final
// (incluidos los ficheros -wal/-shm del modo WAL).
class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = (std::filesystem::temp_directory_path() /
                 (std::string("pci_test_") + info->test_suite_name() + "_" + info->name() +
                  ".db"))
                    .string();
        std::filesystem::remove(path_);
    }

    void TearDown() override {
        db_.reset();
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::filesystem::remove(path_ + suffix);
        }
    }

    database::Db& openAndMigrate() {
        auto opened = database::Db::open(path_);
        EXPECT_TRUE(opened.isOk()) << (opened.isOk() ? "" : opened.error().message);
        db_ = std::move(opened.value());
        auto migrated = database::migrate(*db_);
        EXPECT_TRUE(migrated.isOk()) << (migrated.isOk() ? "" : migrated.error().message);
        return *db_;
    }

    std::string path_;
    std::unique_ptr<database::Db> db_;
};

ml::Reference makeReference(float seed) {
    ml::Reference reference;
    reference.mean = {seed, seed + 0.5F, seed - 0.25F};
    reference.stddev = {0.01F, 0.02F, 0.03F};
    reference.simMean = 0.98;
    reference.simStd = 0.005;
    reference.simMin = 0.97;
    reference.sampleCount = 30;
    return reference;
}

}  // namespace

// --- Codec de blobs ---

TEST(BlobCodec, RoundTripPreservesFloatsExactly) {
    const std::vector<float> values{1.5F, -2.25F, 0.0F, 3.14159F};
    const auto decoded = database::blobToFloats(database::floatsToBlob(values));
    ASSERT_TRUE(decoded.isOk());
    EXPECT_EQ(decoded.value(), values);
}

TEST(BlobCodec, RejectsCorruptSize) {
    EXPECT_FALSE(database::blobToFloats({0x01, 0x02, 0x03}).isOk());
}

// --- Esquema ---

TEST_F(DatabaseTest, MigratesEmptyDatabaseToCurrentVersion) {
    auto& db = openAndMigrate();

    auto stmt = db.prepare("PRAGMA user_version;");
    ASSERT_TRUE(stmt.isOk());
    ASSERT_TRUE(stmt.value().step().value());
    EXPECT_EQ(stmt.value().columnInt(0), database::kSchemaVersion);

    auto tables = db.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
        "('Pieces','Embeddings','InspectionTools','ToolResults','Measurements',"
        "'InspectionHistory','InspectionResults','Settings','Statistics');");
    ASSERT_TRUE(tables.isOk());
    ASSERT_TRUE(tables.value().step().value());
    EXPECT_EQ(tables.value().columnInt(0), 9);
}

TEST_F(DatabaseTest, MigrateIsIdempotentAcrossReopen) {
    openAndMigrate();
    db_.reset();
    openAndMigrate();  // no debe fallar ni duplicar nada
}

TEST_F(DatabaseTest, CorruptFileFailsControlled) {
    {
        std::ofstream garbage(path_);
        garbage << "esto no es una base de datos sqlite, es un archivo roto";
    }
    auto opened = database::Db::open(path_);
    ASSERT_FALSE(opened.isOk());
    EXPECT_NE(opened.error().message.find("corrupta"), std::string::npos);
}

// --- PieceRepository ---

TEST_F(DatabaseTest, CreatesAndListsPieces) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);

    const auto idA = pieces.createPiece("Engranaje A");
    const auto idB = pieces.createPiece("Arandela B");
    ASSERT_TRUE(idA.isOk());
    ASSERT_TRUE(idB.isOk());
    EXPECT_NE(idA.value(), idB.value());

    const auto list = pieces.listPieces();
    ASSERT_TRUE(list.isOk());
    ASSERT_EQ(list.value().size(), 2U);
    EXPECT_EQ(list.value()[0].name, "Arandela B");  // orden alfabético
    EXPECT_EQ(list.value()[1].name, "Engranaje A");
    EXPECT_FALSE(list.value()[0].createdAt.empty());
}

TEST_F(DatabaseTest, RejectsDuplicateWithFriendlyMessage) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);

    ASSERT_TRUE(pieces.createPiece("Pieza X").isOk());
    const auto duplicate = pieces.createPiece("Pieza X");
    ASSERT_FALSE(duplicate.isOk());
    // Mensaje accionable, no el error críptico de SQLite.
    EXPECT_NE(duplicate.error().message.find("Ya existe"), std::string::npos);
    EXPECT_FALSE(pieces.createPiece("").isOk());

    EXPECT_TRUE(pieces.nameExists("Pieza X").value());
    EXPECT_FALSE(pieces.nameExists("Pieza Y").value());
}

TEST_F(DatabaseTest, RenameAndRemovePiece) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    repositories::ToolRepository tools(db);

    const auto pieceId = pieces.createPiece("Original");
    ASSERT_TRUE(pieceId.isOk());
    ASSERT_TRUE(pieces.createPiece("Ocupado").isOk());

    // Renombrar: nombre ocupado rechazado con mensaje claro; libre funciona.
    auto clash = pieces.renamePiece(pieceId.value(), "Ocupado");
    ASSERT_FALSE(clash.isOk());
    EXPECT_NE(clash.error().message.find("Ya existe"), std::string::npos);
    ASSERT_TRUE(pieces.renamePiece(pieceId.value(), "Renombrada").isOk());
    EXPECT_TRUE(pieces.nameExists("Renombrada").value());
    EXPECT_FALSE(pieces.nameExists("Original").value());
    EXPECT_FALSE(pieces.renamePiece(9999, "Nadie").isOk());

    // Eliminar arrastra sus herramientas (FK en cascada).
    inspection::ToolConfig config;
    config.type = inspection::ToolType::Caliper;
    config.name = "Suya";
    config.geometryJson = inspection::toJson(
        inspection::ToolGeometry(inspection::CaliperGeometry{{0, 0}, {10, 0}, 5.0F}));
    ASSERT_TRUE(tools.save(pieceId.value(), config).isOk());

    ASSERT_TRUE(pieces.removePiece(pieceId.value()).isOk());
    EXPECT_FALSE(pieces.nameExists("Renombrada").value());
    const auto orphaned = tools.listForPiece(pieceId.value());
    ASSERT_TRUE(orphaned.isOk());
    EXPECT_TRUE(orphaned.value().empty());
}

TEST_F(DatabaseTest, OrientationOffsetRoundTrip) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    const auto pieceId = pieces.createPiece("Con offset");
    ASSERT_TRUE(pieceId.isOk());

    EXPECT_DOUBLE_EQ(pieces.loadOrientationOffset(pieceId.value()).value(), 0.0);
    ASSERT_TRUE(pieces.saveOrientationOffset(pieceId.value(), 90.0).isOk());
    EXPECT_DOUBLE_EQ(pieces.loadOrientationOffset(pieceId.value()).value(), 90.0);
    EXPECT_FALSE(pieces.loadOrientationOffset(9999).isOk());
}

TEST_F(DatabaseTest, AnchorRoundTrip) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);

    const auto pieceId = pieces.createPiece("Con rasgo");
    ASSERT_TRUE(pieceId.isOk());

    // Sin rasgo guardado: nullopt, no error.
    auto none = pieces.loadAnchor(pieceId.value());
    ASSERT_TRUE(none.isOk());
    EXPECT_FALSE(none.value().has_value());

    vision::OrientationAnchor anchor;
    anchor.piecePoint = {12.5F, -30.25F};
    anchor.intensity = 42.75;
    ASSERT_TRUE(pieces.saveAnchor(pieceId.value(), anchor).isOk());

    auto loaded = pieces.loadAnchor(pieceId.value());
    ASSERT_TRUE(loaded.isOk());
    ASSERT_TRUE(loaded.value().has_value());
    EXPECT_FLOAT_EQ(loaded.value()->piecePoint.x, 12.5F);
    EXPECT_FLOAT_EQ(loaded.value()->piecePoint.y, -30.25F);
    EXPECT_DOUBLE_EQ(loaded.value()->intensity, 42.75);

    EXPECT_FALSE(pieces.loadAnchor(9999).isOk());
}

TEST_F(DatabaseTest, ThumbnailRoundTrip) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);

    const auto pieceId = pieces.createPiece("Con miniatura");
    ASSERT_TRUE(pieceId.isOk());

    // Sin miniatura guardada: blob vacío, no error.
    auto empty = pieces.loadThumbnail(pieceId.value());
    ASSERT_TRUE(empty.isOk());
    EXPECT_TRUE(empty.value().empty());

    const std::vector<unsigned char> jpeg{0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
    ASSERT_TRUE(pieces.saveThumbnail(pieceId.value(), jpeg).isOk());

    auto loaded = pieces.loadThumbnail(pieceId.value());
    ASSERT_TRUE(loaded.isOk());
    EXPECT_EQ(loaded.value(), jpeg);

    EXPECT_FALSE(pieces.loadThumbnail(9999).isOk());
}

TEST_F(DatabaseTest, ReferenceVersioningNeverDeletes) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);

    const auto pieceId = pieces.createPiece("Pieza versionada");
    ASSERT_TRUE(pieceId.isOk());

    const auto v1 = pieces.saveReference(pieceId.value(), makeReference(1.0F));
    const auto v2 = pieces.saveReference(pieceId.value(), makeReference(2.0F));
    ASSERT_TRUE(v1.isOk());
    ASSERT_TRUE(v2.isOk());
    EXPECT_EQ(v1.value(), 1);
    EXPECT_EQ(v2.value(), 2);

    const auto versions = pieces.listReferenceVersions(pieceId.value());
    ASSERT_TRUE(versions.isOk());
    EXPECT_EQ(versions.value(), (std::vector<int>{1, 2}));

    const auto latest = pieces.loadLatestReference(pieceId.value());
    ASSERT_TRUE(latest.isOk());
    EXPECT_EQ(latest.value().version, 2);
    // Roundtrip exacto de floats (el codec no redondea).
    EXPECT_EQ(latest.value().reference.mean, makeReference(2.0F).mean);
    EXPECT_EQ(latest.value().reference.stddev, makeReference(2.0F).stddev);
    EXPECT_EQ(latest.value().reference.sampleCount, 30);
    EXPECT_DOUBLE_EQ(latest.value().reference.simMean, 0.98);
}

TEST_F(DatabaseTest, LoadReferenceFailsForUnknownPiece) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    EXPECT_FALSE(pieces.loadLatestReference(9999).isOk());
}

TEST_F(DatabaseTest, RejectsInvalidReference) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    const auto pieceId = pieces.createPiece("Pieza");
    ASSERT_TRUE(pieceId.isOk());

    ml::Reference empty;
    EXPECT_FALSE(pieces.saveReference(pieceId.value(), empty).isOk());
}

// --- ToolRepository ---

TEST_F(DatabaseTest, ToolCrudRoundTrip) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    repositories::ToolRepository tools(db);

    const auto pieceId = pieces.createPiece("Pieza con herramientas");
    ASSERT_TRUE(pieceId.isOk());

    inspection::ToolConfig caliper;
    caliper.type = inspection::ToolType::Caliper;
    caliper.name = "Ancho brazo";
    caliper.geometryJson = inspection::toJson(
        inspection::ToolGeometry(inspection::CaliperGeometry{{0, 0}, {40, 0}, 6.0F}));
    caliper.toleranceMin = 35.0;
    caliper.toleranceMax = 45.0;

    const auto savedId = tools.save(pieceId.value(), caliper);
    ASSERT_TRUE(savedId.isOk()) << savedId.error().message;

    auto listed = tools.listForPiece(pieceId.value());
    ASSERT_TRUE(listed.isOk());
    ASSERT_EQ(listed.value().size(), 1U);
    EXPECT_EQ(listed.value()[0].name, "Ancho brazo");
    EXPECT_EQ(listed.value()[0].type, inspection::ToolType::Caliper);
    EXPECT_DOUBLE_EQ(listed.value()[0].toleranceMax, 45.0);

    // La geometría sobrevive el roundtrip por la BD.
    const auto geometry = inspection::geometryFromJson(inspection::ToolType::Caliper,
                                                       listed.value()[0].geometryJson);
    ASSERT_TRUE(geometry.isOk());
    EXPECT_FLOAT_EQ(std::get<inspection::CaliperGeometry>(geometry.value()).p1.x, 40.0F);

    // Update.
    auto updated = listed.value()[0];
    updated.name = "Ancho brazo v2";
    updated.toleranceMax = 50.0;
    ASSERT_TRUE(tools.save(pieceId.value(), updated).isOk());
    listed = tools.listForPiece(pieceId.value());
    ASSERT_TRUE(listed.isOk());
    ASSERT_EQ(listed.value().size(), 1U);
    EXPECT_EQ(listed.value()[0].name, "Ancho brazo v2");

    // Delete.
    ASSERT_TRUE(tools.remove(listed.value()[0].id).isOk());
    listed = tools.listForPiece(pieceId.value());
    ASSERT_TRUE(listed.isOk());
    EXPECT_TRUE(listed.value().empty());
}

TEST_F(DatabaseTest, ToolSaveReinsertsWhenRowIsGone) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    repositories::ToolRepository tools(db);
    const auto pieceId = pieces.createPiece("Pieza undo");
    ASSERT_TRUE(pieceId.isOk());

    // Herramienta con id de una fila que ya no existe (borrado + Ctrl+Z):
    // guardar debe reinsertarla, no perderla en silencio.
    inspection::ToolConfig ghost;
    ghost.id = 12345;
    ghost.type = inspection::ToolType::Caliper;
    ghost.name = "Resucitada";
    ghost.geometryJson = inspection::toJson(
        inspection::ToolGeometry(inspection::CaliperGeometry{{0, 0}, {10, 0}, 5.0F}));

    const auto saved = tools.save(pieceId.value(), ghost);
    ASSERT_TRUE(saved.isOk()) << saved.error().message;
    EXPECT_NE(saved.value(), 12345);

    const auto listed = tools.listForPiece(pieceId.value());
    ASSERT_TRUE(listed.isOk());
    ASSERT_EQ(listed.value().size(), 1U);
    EXPECT_EQ(listed.value()[0].name, "Resucitada");
}

TEST_F(DatabaseTest, MultipleTemplatesPerPiece) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    repositories::ToolRepository tools(db);
    const auto pieceId = pieces.createPiece("Multi-plantilla");
    ASSERT_TRUE(pieceId.isOk());

    auto makeTool = [](const std::string& name) {
        inspection::ToolConfig c;
        c.type = inspection::ToolType::Ruler;
        c.name = name;
        c.geometryJson = inspection::toJson(
            inspection::ToolGeometry(inspection::RulerGeometry{{0, 0}, {10, 0}}));
        return c;
    };

    ASSERT_TRUE(tools.save(pieceId.value(), makeTool("A"), "principal").isOk());
    ASSERT_TRUE(tools.save(pieceId.value(), makeTool("B"), "cara-2").isOk());
    ASSERT_TRUE(tools.save(pieceId.value(), makeTool("C"), "cara-2").isOk());

    // Cada plantilla lista solo sus herramientas.
    EXPECT_EQ(tools.listForPiece(pieceId.value(), "principal").value().size(), 1U);
    EXPECT_EQ(tools.listForPiece(pieceId.value(), "cara-2").value().size(), 2U);

    auto templates = tools.listTemplates(pieceId.value());
    ASSERT_TRUE(templates.isOk());
    EXPECT_EQ(templates.value(), (std::vector<std::string>{"cara-2", "principal"}));
}

TEST_F(DatabaseTest, TemplateManagementOps) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    repositories::ToolRepository tools(db);
    const auto pieceId = pieces.createPiece("Gestión de plantillas");
    ASSERT_TRUE(pieceId.isOk());

    auto makeTool = [](const std::string& name) {
        inspection::ToolConfig c;
        c.type = inspection::ToolType::Ruler;
        c.name = name;
        c.geometryJson = inspection::toJson(
            inspection::ToolGeometry(inspection::RulerGeometry{{0, 0}, {10, 0}}));
        return c;
    };
    ASSERT_TRUE(tools.save(pieceId.value(), makeTool("a"), "principal").isOk());
    ASSERT_TRUE(tools.save(pieceId.value(), makeTool("b"), "cara-2").isOk());

    // Duplicar copia todas las herramientas de la plantilla origen.
    ASSERT_TRUE(tools.duplicateTemplate(pieceId.value(), "principal", "copia").isOk());
    EXPECT_EQ(tools.listForPiece(pieceId.value(), "copia").value().size(), 1U);
    // Duplicar a un nombre que ya existe falla.
    EXPECT_FALSE(tools.duplicateTemplate(pieceId.value(), "principal", "cara-2").isOk());

    // Renombrar mueve las herramientas al nuevo nombre.
    ASSERT_TRUE(tools.renameTemplate(pieceId.value(), "cara-2", "cara-B").isOk());
    EXPECT_TRUE(tools.listForPiece(pieceId.value(), "cara-2").value().empty());
    EXPECT_EQ(tools.listForPiece(pieceId.value(), "cara-B").value().size(), 1U);
    // Renombrar a un nombre existente falla.
    EXPECT_FALSE(tools.renameTemplate(pieceId.value(), "cara-B", "principal").isOk());

    // Eliminar borra todas las herramientas de esa plantilla.
    ASSERT_TRUE(tools.deleteTemplate(pieceId.value(), "copia").isOk());
    EXPECT_TRUE(tools.listForPiece(pieceId.value(), "copia").value().empty());

    // Quedan principal + cara-B.
    auto templates = tools.listTemplates(pieceId.value());
    ASSERT_TRUE(templates.isOk());
    EXPECT_EQ(templates.value(), (std::vector<std::string>{"cara-B", "principal"}));
}

TEST_F(DatabaseTest, ClearAnchorRemovesIt) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    const auto pieceId = pieces.createPiece("Con y sin rasgo");
    ASSERT_TRUE(pieceId.isOk());

    vision::OrientationAnchor anchor;
    anchor.piecePoint = {5.0F, 5.0F};
    anchor.intensity = 30.0;
    ASSERT_TRUE(pieces.saveAnchor(pieceId.value(), anchor).isOk());
    ASSERT_TRUE(pieces.loadAnchor(pieceId.value()).value().has_value());

    ASSERT_TRUE(pieces.clearAnchor(pieceId.value()).isOk());
    EXPECT_FALSE(pieces.loadAnchor(pieceId.value()).value().has_value());
}

TEST_F(DatabaseTest, ToolSaveRejectsInvalid) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    repositories::ToolRepository tools(db);
    const auto pieceId = pieces.createPiece("Pieza");
    ASSERT_TRUE(pieceId.isOk());

    inspection::ToolConfig noName;
    noName.geometryJson = "{}";
    EXPECT_FALSE(tools.save(pieceId.value(), noName).isOk());

    inspection::ToolConfig noGeometry;
    noGeometry.name = "sin geometría";
    noGeometry.geometryJson.clear();
    EXPECT_FALSE(tools.save(pieceId.value(), noGeometry).isOk());
}

// --- SettingsRepository ---

TEST_F(DatabaseTest, SettingsSetGetOverwriteAndDefaults) {
    auto& db = openAndMigrate();
    repositories::SettingsRepository settings(db);

    EXPECT_EQ(settings.getInt("camera_index", -1).value(), -1);

    ASSERT_TRUE(settings.setInt("camera_index", 2).isOk());
    EXPECT_EQ(settings.getInt("camera_index", -1).value(), 2);

    ASSERT_TRUE(settings.setInt("camera_index", 0).isOk());
    EXPECT_EQ(settings.getInt("camera_index", -1).value(), 0);

    ASSERT_TRUE(settings.setString("modo", "estricto").isOk());
    EXPECT_EQ(settings.getString("modo").value(), "estricto");
    EXPECT_EQ(settings.getString("inexistente", "def").value(), "def");

    // Dobles (calibración de escala).
    EXPECT_DOUBLE_EQ(settings.getDouble("calib_mm_per_px", 0.0).value(), 0.0);
    ASSERT_TRUE(settings.setDouble("calib_mm_per_px", 0.253).isOk());
    EXPECT_NEAR(settings.getDouble("calib_mm_per_px", 0.0).value(), 0.253, 1e-9);
}

// --- v5: modo de medición y tablero por pieza (M1) ---

TEST_F(DatabaseTest, MeasurementModeDefaultsToRealAndRoundTrips) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);
    const auto id = pieces.createPiece("pieza-modo");
    ASSERT_TRUE(id.isOk());

    // Una pieza recién creada se comporta como siempre: modo Real y tablero
    // centrado en la propia pieza.
    auto initial = pieces.loadMeasurement(id.value());
    ASSERT_TRUE(initial.isOk());
    EXPECT_EQ(initial.value().mode, domain::MeasurementMode::Real);
    EXPECT_EQ(initial.value().board.origin, vision::BoardOrigin::PieceBounds);
    EXPECT_FLOAT_EQ(initial.value().board.manualOffset.x, 0.0F);
    EXPECT_FLOAT_EQ(initial.value().board.manualOffset.y, 0.0F);
    // Sin reglas de posición: una pieza nueva no puede dar NG por colocación.
    EXPECT_DOUBLE_EQ(initial.value().maxOffsetPx, 0.0);
    EXPECT_DOUBLE_EQ(initial.value().maxAngleDeg, 0.0);
    EXPECT_FALSE(initial.value().board.followPieceAngle);

    repositories::PieceMeasurement special;
    special.mode = domain::MeasurementMode::Special;
    special.board.origin = vision::BoardOrigin::FixedPoint;
    special.board.fixedPoint = {123.5F, 77.25F};
    special.board.manualOffset = {-3.5F, 2.25F};
    special.board.followPieceAngle = true;
    special.maxOffsetPx = 12.5;
    special.maxAngleDeg = 3.0;
    ASSERT_TRUE(pieces.saveMeasurement(id.value(), special).isOk());

    auto loaded = pieces.loadMeasurement(id.value());
    ASSERT_TRUE(loaded.isOk());
    EXPECT_EQ(loaded.value().mode, domain::MeasurementMode::Special);
    EXPECT_EQ(loaded.value().board.origin, vision::BoardOrigin::FixedPoint);
    EXPECT_FLOAT_EQ(loaded.value().board.fixedPoint.x, 123.5F);
    EXPECT_FLOAT_EQ(loaded.value().board.fixedPoint.y, 77.25F);
    EXPECT_FLOAT_EQ(loaded.value().board.manualOffset.x, -3.5F);
    EXPECT_FLOAT_EQ(loaded.value().board.manualOffset.y, 2.25F);
    EXPECT_TRUE(loaded.value().board.followPieceAngle);
    EXPECT_DOUBLE_EQ(loaded.value().maxOffsetPx, 12.5);
    EXPECT_DOUBLE_EQ(loaded.value().maxAngleDeg, 3.0);

    // Volver al modo Real no borra el tablero configurado.
    repositories::PieceMeasurement back = loaded.value();
    back.mode = domain::MeasurementMode::Real;
    ASSERT_TRUE(pieces.saveMeasurement(id.value(), back).isOk());
    auto again = pieces.loadMeasurement(id.value());
    ASSERT_TRUE(again.isOk());
    EXPECT_EQ(again.value().mode, domain::MeasurementMode::Real);
    EXPECT_EQ(again.value().board.origin, vision::BoardOrigin::FixedPoint);

    EXPECT_FALSE(pieces.loadMeasurement(9999).isOk());
}

TEST(MeasurementMode, KeysRoundTripAndLabelsExist) {
    for (const auto mode : {domain::MeasurementMode::Real, domain::MeasurementMode::Special}) {
        EXPECT_EQ(domain::modeFromKey(domain::modeKey(mode)), mode);
        EXPECT_NE(std::string(domain::modeLabel(mode)), std::string());
        EXPECT_NE(std::string(domain::modeDescription(mode)), std::string());
    }
    // Clave desconocida (BD de otra versión o corrupta): se cae al modo de
    // siempre en vez de fallar.
    EXPECT_EQ(domain::modeFromKey("lo-que-sea"), domain::MeasurementMode::Real);
}

// --- Perfiles de detección (O3) ---

TEST_F(DatabaseTest, DetectionProfilesSaveListAndAssign) {
    auto& db = openAndMigrate();
    repositories::DetectionProfileRepository profiles(db);
    repositories::PieceRepository pieces(db);

    vision::SegmentationOptions bright;
    bright.manualThreshold = 180;
    bright.polarity = vision::SegmentationPolarity::DarkPiece;
    bright.blurKernel = 7;
    bright.morphKernel = 3;
    const auto brightId = profiles.save("luz brillante", bright);
    ASSERT_TRUE(brightId.isOk());

    vision::SegmentationOptions backlit;
    backlit.manualThreshold = -1;  // Otsu
    backlit.polarity = vision::SegmentationPolarity::LightPiece;
    ASSERT_TRUE(profiles.save("contraluz", backlit).isOk());

    auto listed = profiles.list();
    ASSERT_TRUE(listed.isOk());
    ASSERT_EQ(listed.value().size(), 2U);
    // Orden alfabético: "contraluz" antes que "luz brillante".
    EXPECT_EQ(listed.value()[0].name, "contraluz");

    auto loaded = profiles.load(brightId.value());
    ASSERT_TRUE(loaded.isOk());
    EXPECT_EQ(loaded.value().options.manualThreshold, 180);
    EXPECT_EQ(loaded.value().options.polarity, vision::SegmentationPolarity::DarkPiece);
    EXPECT_EQ(loaded.value().options.blurKernel, 7);

    // Guardar con el mismo nombre sobrescribe en vez de duplicar.
    bright.manualThreshold = 200;
    const auto again = profiles.save("luz brillante", bright);
    ASSERT_TRUE(again.isOk());
    EXPECT_EQ(again.value(), brightId.value());
    EXPECT_EQ(profiles.list().value().size(), 2U);
    EXPECT_EQ(profiles.load(brightId.value()).value().options.manualThreshold, 200);

    // Asignación por pieza; sin asignar es 0 (ajustes globales).
    const auto pieceId = pieces.createPiece("con perfil");
    ASSERT_TRUE(pieceId.isOk());
    EXPECT_EQ(profiles.profileForPiece(pieceId.value()).value(), 0);
    ASSERT_TRUE(profiles.assignToPiece(pieceId.value(), brightId.value()).isOk());
    EXPECT_EQ(profiles.profileForPiece(pieceId.value()).value(), brightId.value());
}

// Borrar un perfil no puede dejar piezas apuntando a una fila inexistente.
TEST_F(DatabaseTest, DeletingProfileResetsPiecesToGlobalSettings) {
    auto& db = openAndMigrate();
    repositories::DetectionProfileRepository profiles(db);
    repositories::PieceRepository pieces(db);

    const auto profileId = profiles.save("temporal", vision::SegmentationOptions{});
    ASSERT_TRUE(profileId.isOk());
    const auto pieceId = pieces.createPiece("huérfana");
    ASSERT_TRUE(pieceId.isOk());
    ASSERT_TRUE(profiles.assignToPiece(pieceId.value(), profileId.value()).isOk());

    ASSERT_TRUE(profiles.remove(profileId.value()).isOk());
    EXPECT_EQ(profiles.profileForPiece(pieceId.value()).value(), 0);
    EXPECT_TRUE(profiles.list().value().empty());
    EXPECT_FALSE(profiles.load(profileId.value()).isOk());
}

TEST_F(DatabaseTest, ProfileNeedsANameAndRenameRejectsDuplicates) {
    auto& db = openAndMigrate();
    repositories::DetectionProfileRepository profiles(db);

    EXPECT_FALSE(profiles.save("", vision::SegmentationOptions{}).isOk());
    const auto first = profiles.save("uno", vision::SegmentationOptions{});
    ASSERT_TRUE(first.isOk());
    ASSERT_TRUE(profiles.save("dos", vision::SegmentationOptions{}).isOk());

    EXPECT_FALSE(profiles.rename(first.value(), "").isOk());
    EXPECT_FALSE(profiles.rename(first.value(), "dos").isOk());  // nombre repetido
    ASSERT_TRUE(profiles.rename(first.value(), "uno bis").isOk());
    EXPECT_EQ(profiles.load(first.value()).value().name, "uno bis");
}

// --- Exportar/importar configuración (O4) ---

TEST_F(DatabaseTest, ConfigExportImportRoundTrip) {
    const std::string jsonPath =
        (std::filesystem::temp_directory_path() / "pci_config_roundtrip.json").string();
    std::filesystem::remove(jsonPath);

    {
        auto& db = openAndMigrate();
        repositories::SettingsRepository settings(db);
        repositories::DetectionProfileRepository profiles(db);
        ASSERT_TRUE(settings.setDouble("calib_mm_per_px", 0.125).isOk());
        ASSERT_TRUE(settings.setString("key_undo", "Ctrl+Z").isOk());
        ASSERT_TRUE(settings.setInt("pref_auto_interval_ms", 750).isOk());

        vision::SegmentationOptions options;
        options.manualThreshold = 173;
        options.polarity = vision::SegmentationPolarity::LightPiece;
        options.blurKernel = 9;
        ASSERT_TRUE(profiles.save("contraluz", options).isOk());

        auto exported = repositories::exportConfig(jsonPath, settings, profiles);
        ASSERT_TRUE(exported.isOk()) << exported.error().message;
        EXPECT_GE(exported.value().settings, 3);
        EXPECT_EQ(exported.value().profiles, 1);
    }

    // Otra "PC de la línea": base de datos nueva y vacía.
    const std::string otherPath =
        (std::filesystem::temp_directory_path() / "pci_config_other.db").string();
    std::filesystem::remove(otherPath);
    {
        auto opened = database::Db::open(otherPath);
        ASSERT_TRUE(opened.isOk());
        ASSERT_TRUE(database::migrate(*opened.value()).isOk());
        repositories::SettingsRepository settings(*opened.value());
        repositories::DetectionProfileRepository profiles(*opened.value());

        auto imported = repositories::importConfig(jsonPath, settings, profiles);
        ASSERT_TRUE(imported.isOk()) << imported.error().message;
        EXPECT_GE(imported.value().settings, 3);
        EXPECT_EQ(imported.value().profiles, 1);

        EXPECT_DOUBLE_EQ(settings.getDouble("calib_mm_per_px", 0.0).value(), 0.125);
        EXPECT_EQ(settings.getString("key_undo", "").value(), "Ctrl+Z");
        EXPECT_EQ(settings.getInt("pref_auto_interval_ms", 0).value(), 750);

        auto listed = profiles.list();
        ASSERT_TRUE(listed.isOk());
        ASSERT_EQ(listed.value().size(), 1U);
        EXPECT_EQ(listed.value()[0].name, "contraluz");
        EXPECT_EQ(listed.value()[0].options.manualThreshold, 173);
        EXPECT_EQ(listed.value()[0].options.blurKernel, 9);
    }

    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::filesystem::remove(otherPath + suffix);
    }
    std::filesystem::remove(jsonPath);
}

TEST_F(DatabaseTest, ConfigImportRejectsForeignOrBrokenFiles) {
    auto& db = openAndMigrate();
    repositories::SettingsRepository settings(db);
    repositories::DetectionProfileRepository profiles(db);

    const auto write = [](const std::string& path, const std::string& content) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    };
    const std::string path =
        (std::filesystem::temp_directory_path() / "pci_config_bad.json").string();

    write(path, "esto no es json");
    EXPECT_FALSE(repositories::importConfig(path, settings, profiles).isOk());

    // JSON válido pero de otra aplicación: no se aplica nada.
    write(path, R"({"app":"otra-cosa","settings":{"calib_mm_per_px":"9.0"}})");
    EXPECT_FALSE(repositories::importConfig(path, settings, profiles).isOk());
    EXPECT_DOUBLE_EQ(settings.getDouble("calib_mm_per_px", -1.0).value(), -1.0);

    // De una versión futura: mejor no aplicar a medias.
    write(path, R"({"app":"pc-inspector","config_version":99,"settings":{}})");
    EXPECT_FALSE(repositories::importConfig(path, settings, profiles).isOk());

    // Un archivo que no existe falla de forma controlada.
    EXPECT_FALSE(repositories::importConfig(path + ".ausente", settings, profiles).isOk());

    std::filesystem::remove(path);
}
