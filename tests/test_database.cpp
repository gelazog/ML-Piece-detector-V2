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

TEST_F(DatabaseTest, RejectsDuplicateAndEmptyNames) {
    auto& db = openAndMigrate();
    repositories::PieceRepository pieces(db);

    ASSERT_TRUE(pieces.createPiece("Pieza X").isOk());
    EXPECT_FALSE(pieces.createPiece("Pieza X").isOk());
    EXPECT_FALSE(pieces.createPiece("").isOk());
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
}
