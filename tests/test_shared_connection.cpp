#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"

using namespace pci;

// POR QUÉ EXISTE: el programa abre UNA sola conexión SQLite y la comparte
// entre el hilo de inspección y la GUI. El hilo de inspección guarda cada
// resultado dentro de BEGIN…COMMIT, y mientras tanto la GUI puede escribir un
// ajuste por la MISMA conexión. SQLite no sabe de hilos: para él todo lo que
// llega por esa conexión forma parte de la transacción abierta. Resultado
// medido antes del arreglo:
//   - el ajuste escrito por la GUI caía dentro de la transacción ajena y, si
//     esta acababa en ROLLBACK, desaparecía sin ningún error (setString
//     devolvía ok y la clave no existía después);
//   - si la GUI abría su propia transacción, fallaba con «cannot start a
//     transaction within a transaction».
// El arreglo hace que una transacción retenga el cerrojo de la conexión desde
// BEGIN hasta COMMIT/ROLLBACK: el otro hilo espera y escribe DESPUÉS.

namespace {

class SharedConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = (std::filesystem::temp_directory_path() /
                 (std::string("pci_test_") + info->test_suite_name() + "_" + info->name() +
                  ".db"))
                    .string();
        std::filesystem::remove(path_);
        auto opened = database::Db::open(path_);
        ASSERT_TRUE(opened.isOk()) << (opened.isOk() ? "" : opened.error().message);
        db_ = std::move(opened.value());
        auto migrated = database::migrate(*db_);
        ASSERT_TRUE(migrated.isOk()) << (migrated.isOk() ? "" : migrated.error().message);
    }

    void TearDown() override {
        db_.reset();
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::filesystem::remove(path_ + suffix);
        }
    }

    // Hilo «de inspección»: abre una transacción, avisa de que está dentro, da
    // al otro hilo hasta 300 ms para terminar su escritura y la deshace. Antes
    // del arreglo el otro hilo termina enseguida (su escritura se cuela dentro);
    // después, está bloqueado y el plazo vence sin que termine.
    void runLongTransactionThatRollsBack(std::function<void()> otherThreadWork,
                                         bool& otherFinishedInsideTransaction) {
        std::mutex m;
        std::condition_variable cv;
        bool entered = false;
        bool otherDone = false;

        std::thread other([&] {
            {
                std::unique_lock lock(m);
                cv.wait(lock, [&] { return entered; });
            }
            otherThreadWork();
            {
                std::lock_guard lock(m);
                otherDone = true;
            }
            cv.notify_all();
        });

        auto result = db_->transaction([&]() -> core::Result<void> {
            {
                std::lock_guard lock(m);
                entered = true;
            }
            cv.notify_all();
            std::unique_lock lock(m);
            otherFinishedInsideTransaction =
                cv.wait_for(lock, std::chrono::milliseconds(300), [&] { return otherDone; });
            return core::Result<void>::err("fallo simulado de la inspección");
        });
        EXPECT_FALSE(result.isOk());
        other.join();
    }

    std::string path_;
    std::unique_ptr<database::Db> db_;
};

TEST_F(SharedConnectionTest, UnAjusteEscritoDuranteUnaTransaccionAjenaNoSePierde) {
    repositories::SettingsRepository settings(*db_);
    bool setOk = false;
    bool finishedInside = true;

    runLongTransactionThatRollsBack(
        [&] { setOk = settings.setString("cam_index", "2").isOk(); }, finishedInside);

    EXPECT_TRUE(setOk);
    EXPECT_FALSE(finishedInside)
        << "la escritura de la GUI se ejecutó DENTRO de la transacción del otro hilo";
    auto stored = settings.getString("cam_index", "<ausente>");
    ASSERT_TRUE(stored.isOk());
    EXPECT_EQ(stored.value(), "2") << "el ROLLBACK ajeno se llevó el ajuste de la GUI";
}

TEST_F(SharedConnectionTest, OtraTransaccionEsperaEnVezDeFallar) {
    repositories::SettingsRepository settings(*db_);
    std::string error;
    bool finishedInside = true;

    runLongTransactionThatRollsBack(
        [&] {
            auto r = db_->transaction([&]() -> core::Result<void> {
                return settings.setString("det_blur", "7");
            });
            if (!r.isOk()) {
                error = r.error().message;
            }
        },
        finishedInside);

    EXPECT_EQ(error, "");
    EXPECT_FALSE(finishedInside);
    auto stored = settings.getString("det_blur", "<ausente>");
    ASSERT_TRUE(stored.isOk());
    EXPECT_EQ(stored.value(), "7");
}

// La transacción RAII: sin commit() explícito, al salir del ámbito se deshace.
TEST_F(SharedConnectionTest, TransaccionRaiiSinCommitSeDeshace) {
    repositories::SettingsRepository settings(*db_);
    {
        auto tx = db_->begin();
        ASSERT_TRUE(tx.isOk()) << tx.error().message;
        ASSERT_TRUE(settings.setString("tmp", "1").isOk());
    }
    auto stored = settings.getString("tmp", "<ausente>");
    ASSERT_TRUE(stored.isOk());
    EXPECT_EQ(stored.value(), "<ausente>");
}

}  // namespace
