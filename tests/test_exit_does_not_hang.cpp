// UNA PRUEBA QUE YA HABÍA DICHO «OK» SE QUEDABA COLGADA 300 s AL SALIR.
//
// Con `ctest -j 4..6 --timeout 300`, de vez en cuando una prueba cualquiera de
// pci_tests acababa en Timeout: Segmentation.LightPieceOnDarkBackground,
// AutoMeasure.ProposesTheOverallDimensionsFirst,
// SyntheticMeasures.TheGearAreaSubtractsItsBoreInsteadOfFillingIt y
// Holes.FindsTheInternalContoursAndIgnoresTheOutline. Sueltas tardaban menos de
// 1 s. La prueba no tenía la culpa: el proceso se colgaba DESPUÉS de `main`.
//
// gdb sobre tres procesos colgados dio la misma pila, con un solo hilo vivo:
//
//   ntdll!ZwWaitForSingleObject
//   KernelBase!WaitForSingleObjectEx
//   libtbb12.dll  (x4)  <- tbb::detail::r1::terminate(task_arena_base&)
//   libopencv_core-413.dll  (destructor estático del task_arena de OpenCV)
//   ucrtbase!_execute_onexit_table
//   ntdll!LdrShutdownProcess / RtlExitUserProcess
//   ucrtbase!exit  <- __tmainCRTStartup, es decir, main ya había vuelto
//
// `ExitProcess` mata primero los hilos de trabajo de TBB y luego descarga las
// DLL. Al descargar libopencv_core, su `task_arena` estático llama a
// `terminate`, que espera algo que solo podía avisar uno de esos hilos ya
// muertos. Un programa de quince líneas (un `parallel_for_` y salir) se colgó
// igual, con la misma pila: 9 de 16000 procesos, 48 a la vez; soltando el grupo
// antes de salir, 0 de 8000. No interviene nada del proyecto (ni el log, ni
// SQLite, ni ONNX Runtime).
//
// El arreglo, en vision/parallel_pool.h, es terminar ese grupo de hilos al
// final de `main`, cuando los hilos aún viven. Aquí se registra en cada proceso
// de pci_tests con `atexit`. La primera prueba comprueba lo que
// el arreglo necesita: que tras soltarlo OpenCV deja de usar el grupo y no lo
// vuelve a crear. Sin el arreglo falla: el bucle lo siguen corriendo 8 hilos.
//
// La carrera no se puede provocar a voluntad dentro de un proceso, así que la
// medida de verdad es un banco aparte (la segunda prueba). No corre en el banco
// diario porque necesita miles de procesos:
//
//   PCI_BANCO_SALIDA=8000 pci_tests.exe --gtest_filter=ExitDoesNotHang.Bench*
//
// Lanza `pci_exit_bench_child.exe` (tests/exit_bench_child.cpp). Con
// PCI_BANCO_SALIDA_SIN_ARREGLO=1 los hijos no sueltan el grupo y se ve el fallo
// original.

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "vision/parallel_pool.h"

namespace {

// Cada proceso de pci_tests suelta el grupo de hilos de OpenCV al salir, igual
// que pc_inspector al cerrar. Con `atexit` y no con un `TearDown` de gtest:
// `--gtest_list_tests`, que es lo primero que hace ctest con cada ejecutable,
// no llega a los entornos de gtest, y un listado colgado al salir hizo fallar
// el descubrimiento de pruebas entero. `atexit` corre en cualquier salida
// normal y antes de que `ExitProcess` mate los hilos.
[[maybe_unused]] const int kReleaseAtExit =
    std::atexit(pci::vision::releaseParallelPoolBeforeExit);

// Los hilos que reparten un `parallel_for_` de muchas franjas lentas.
std::set<std::thread::id> threadsThatRanTheLoop() {
    std::mutex mutex;
    std::set<std::thread::id> ids;
    cv::parallel_for_(cv::Range(0, 64), [&](const cv::Range& range) {
        for (int i = range.start; i < range.end; ++i) {
            // Una espera corta: sin ella el hilo que llama se come todas las
            // franjas antes de que los demás despierten.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const std::lock_guard lock(mutex);
            ids.insert(std::this_thread::get_id());
        }
    });
    return ids;
}

// El hijo del banco, junto a este ejecutable.
QString benchChild() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH] = {0};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const QFileInfo self(QString::fromWCharArray(buffer, static_cast<int>(length)));
    const QString child = self.dir().filePath(QStringLiteral("pci_exit_bench_child.exe"));
    return QFileInfo::exists(child) ? child : QString();
#else
    return {};
#endif
}

}  // namespace

TEST(ExitDoesNotHang, ReleasingThePoolStopsOpenCvFromUsingIt) {
    if (cv::getNumberOfCPUs() < 2) {
        GTEST_SKIP() << "con un solo núcleo OpenCV no reparte y no hay grupo que soltar";
    }

    // Antes: el grupo de TBB existe y reparte. Es justo lo que se cuelga al salir.
    const auto before = threadsThatRanTheLoop();
    std::printf("  hilos que corrieron el bucle antes de soltar: %zu\n", before.size());
    ASSERT_GT(before.size(), 1U) << "OpenCV no está repartiendo: la prueba no mira nada";

    pci::vision::releaseParallelPoolBeforeExit();

    // Después: todo en el hilo que llama, y el grupo no se ha vuelto a crear
    // (si `parallel_for_` lo recreara, el destructor estático volvería a tener
    // trabajo y el cuelgue volvería con él). No se mira `cv::getNumThreads()`:
    // en este OpenCV sigue diciendo 8 después de soltar, aunque ya no reparta.
    const auto after = threadsThatRanTheLoop();
    std::printf("  hilos que corrieron el bucle después de soltar: %zu\n", after.size());
    EXPECT_EQ(after.size(), 1U);
    EXPECT_EQ(after.count(std::this_thread::get_id()), 1U);

    // Repetirla no rompe nada.
    pci::vision::releaseParallelPoolBeforeExit();
    EXPECT_EQ(threadsThatRanTheLoop().size(), 1U);

    // El resto de pruebas de este mismo proceso vuelven a tener hilos.
    cv::setNumThreads(-1);
    EXPECT_GT(threadsThatRanTheLoop().size(), 1U);
}

TEST(ExitDoesNotHang, BenchManyShortProcessesAllExit) {
    const char* requested = std::getenv("PCI_BANCO_SALIDA");
    if (requested == nullptr || std::atoi(requested) <= 0) {
        GTEST_SKIP() << "banco de miles de procesos: PCI_BANCO_SALIDA=<n> para correrlo";
    }
    const QString child = benchChild();
    if (child.isEmpty()) {
        GTEST_SKIP() << "no está pci_exit_bench_child.exe junto a pci_tests.exe";
    }

    const int total = std::atoi(requested);
    // Muchos a la vez: el cuelgue necesita que un hilo de TBB pierda la CPU en
    // el peor momento, y eso pasa más cuanto más cargada está la máquina.
    const int parallel = 48;
    // Cargar libopenblas.dll (la arrastra libopencv_core) compromete 1027 MB
    // de memoria por proceso; con OMP_NUM_THREADS=1, 130 MB. 48 hijos sin esto
    // podían pedir ~49 GB y dejar sin memoria a toda la máquina. A TBB, que es
    // lo que se mide aquí, no le afecta.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    const int deadlineMs = 20000;  // sueltos tardan décimas; colgados, siempre
    const char* withoutFixEnv = std::getenv("PCI_BANCO_SALIDA_SIN_ARREGLO");
    const bool withoutFix = withoutFixEnv != nullptr && withoutFixEnv[0] != '\0';
    const QStringList args =
        withoutFix ? QStringList{QStringLiteral("--sin-arreglo")} : QStringList{};

    int launched = 0;
    int hung = 0;
    int failed = 0;
    const auto start = std::chrono::steady_clock::now();
    while (launched < total) {
        std::vector<std::unique_ptr<QProcess>> batch;
        const int count = std::min(parallel, total - launched);
        for (int i = 0; i < count; ++i) {
            auto process = std::make_unique<QProcess>();
            process->setProcessEnvironment(env);
            process->start(child, args);
            batch.push_back(std::move(process));
        }
        launched += count;
        const auto batchStart = std::chrono::steady_clock::now();
        for (auto& process : batch) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - batchStart)
                                     .count();
            const int left = std::max(1, deadlineMs - static_cast<int>(elapsed));
            if (!process->waitForFinished(left)) {
                ++hung;
                // Solo este hijo, por su propio identificador: nunca por nombre.
                process->kill();
                process->waitForFinished(5000);
            } else if (process->exitStatus() != QProcess::NormalExit ||
                       process->exitCode() != 0) {
                ++failed;
            }
        }
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  [banco salida] %s: %d procesos, %d a la vez, %d colgados (>%d ms), "
                "%d fallidos, %.0f s\n",
                withoutFix ? "SIN arreglo" : "con arreglo", launched, parallel, hung,
                deadlineMs, failed, seconds);

    EXPECT_EQ(failed, 0);
    if (!withoutFix) {
        EXPECT_EQ(hung, 0) << "algún proceso no terminó después de soltar el grupo de hilos";
    }
}
