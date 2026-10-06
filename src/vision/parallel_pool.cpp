#include "vision/parallel_pool.h"

#include <opencv2/core/utility.hpp>

namespace pci::vision {

void releaseParallelPoolBeforeExit() {
    // Cero hilos: OpenCV termina su `task_arena` y no lo vuelve a crear (ver la
    // cabecera). Repetirla no hace daño: si ya está terminado, no hace nada.
    cv::setNumThreads(0);
}

}  // namespace pci::vision
