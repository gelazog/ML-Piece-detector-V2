#include <gtest/gtest.h>

#include "camera/native_cameras.h"
#include "core/crash_guard.h"

namespace {

void doNothing(void* /*ctx*/) {}

// Sumidero volátil para que el optimizador no elimine las operaciones que
// provocan los fallos del SO.
volatile int g_sink = 0;

void divideByZero(void* ctx) {
    const volatile int zero = *static_cast<int*>(ctx);  // 0 en tiempo de ejecución
    const volatile int one = 1;
    g_sink = one / zero;  // #DE -> EXCEPTION_INT_DIVIDE_BY_ZERO
}

void nullDereference(void* /*ctx*/) {
    volatile int* const p = nullptr;
    g_sink = *p;  // EXCEPTION_ACCESS_VIOLATION
}

// Valores NTSTATUS estables de Windows (evita incluir windows.h en el test).
constexpr unsigned long kIntDivideByZero = 0xC0000094UL;
constexpr unsigned long kAccessViolation = 0xC0000005UL;

}  // namespace

TEST(CrashGuard, RunsNormalFunctionAndReportsSuccess) {
    unsigned long code = 0;
    EXPECT_TRUE(pci::core::runProtected(&doNothing, nullptr, &code));
}

#ifdef _WIN32

TEST(CrashGuard, SurvivesIntegerDivideByZero) {
    int zero = 0;
    unsigned long code = 0;
    const bool survived = pci::core::runProtected(&divideByZero, &zero, &code);
    // Que este test siga corriendo demuestra que el proceso NO murió pese a la
    // división entera por cero — justo el fallo que tumbaba la app con drivers
    // de captura defectuosos.
    EXPECT_FALSE(survived);
    EXPECT_EQ(code, kIntDivideByZero);
}

TEST(CrashGuard, SurvivesAccessViolation) {
    unsigned long code = 0;
    EXPECT_FALSE(pci::core::runProtected(&nullDereference, nullptr, &code));
    EXPECT_EQ(code, kAccessViolation);
}

TEST(CrashGuard, GuardIsReusableAfterFault) {
    int zero = 0;
    unsigned long code = 0;
    EXPECT_FALSE(pci::core::runProtected(&divideByZero, &zero, &code));
    // Tras recuperarse de un fallo, el guard debe seguir operativo.
    EXPECT_TRUE(pci::core::runProtected(&doNothing, nullptr, &code));
}

#endif  // _WIN32

TEST(CrashGuard, BreadcrumbAndHandlerAreSafe) {
    pci::core::setBreadcrumb("prueba de miga de pan");
    pci::core::installCrashHandler("");  // ruta vacía: no debe lanzar ni crashear
    SUCCEED();
}

TEST(NativeCameras, EnumerationIsSafeAndWellFormed) {
    // No abre dispositivos, así que es seguro incluso sin cámara; en CI puede
    // devolver vacío. Solo verificamos que no lanza y que los datos son válidos.
    const std::vector<pci::camera::NativeCamera> cameras =
        pci::camera::enumerateNativeCameras();
    for (const auto& cam : cameras) {
        EXPECT_GE(cam.index, 0);
        EXPECT_FALSE(cam.friendlyName.empty());
    }
    SUCCEED();
}
