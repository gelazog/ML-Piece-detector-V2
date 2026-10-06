// El proceso hijo del banco ExitDoesNotHang.BenchManyShortProcessesAllExit.
//
// Hace lo mínimo que dejaba el proceso colgado al salir: un `parallel_for_` de
// OpenCV y volver de `main`. Es un ejecutable aparte, y no pci_tests, para que
// cada hijo cargue solo OpenCV y el banco se pueda repetir miles de veces
// deprisa. El banco lo lanza con OMP_NUM_THREADS=1 por la memoria (ver allí).
//
// Con `--sin-arreglo` no suelta el grupo de hilos y se ve el fallo original.

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>

#include <atomic>
#include <cstring>

#include "vision/parallel_pool.h"

int main(int argc, char** argv) {
    std::atomic<long> sum{0};
    cv::parallel_for_(cv::Range(0, 256), [&](const cv::Range& range) {
        long partial = 0;
        for (int i = range.start; i < range.end; ++i) {
            partial += i;
        }
        sum += partial;
    });

    const bool withoutFix = argc > 1 && std::strcmp(argv[1], "--sin-arreglo") == 0;
    if (!withoutFix) {
        pci::vision::releaseParallelPoolBeforeExit();
    }
    return sum.load() == 32640 ? 0 : 1;
}
