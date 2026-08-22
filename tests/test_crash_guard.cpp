#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
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

#ifdef _WIN32

// El informe de un cierre es lo ÚNICO que queda cuando el proceso muere, y
// hasta ahora afirmaba siempre la misma causa: «un driver de captura falló al
// negociar formato y dividió por cero». Lo decía también cuando la excepción
// era una violación de acceso, que es otra cosa.
//
// En el registro de este proyecto hay tres cierres con ese texto y el código
// 0xC0000005. Cualquiera que los leyera se pondría a buscar una división por
// cero que nunca ocurrió.
TEST(CrashReport, ItDoesNotBlameADivisionWhenThereWasNone) {
    char report[2048] = {0};
    pci::core::CrashFacts facts;
    facts.code = kAccessViolation;
    facts.breadcrumb = "enumerando las camaras del sistema";
    facts.hasAccessInfo = true;
    facts.accessKind = 0;  // leyendo
    facts.accessAddress = 0;
    pci::core::describeCrash(report, sizeof(report), facts);

    const std::string text(report);
    std::printf("  [informe]\n%s", report);

    EXPECT_EQ(text.find("dividio por cero"), std::string::npos)
        << "sigue culpando a una division por cero con una violacion de acceso";
    EXPECT_NE(text.find("ACCESS_VIOLATION"), std::string::npos);
    // Y los datos que la excepcion TRAE y que antes se tiraban.
    EXPECT_NE(text.find("leyendo"), std::string::npos)
        << "no dice si fue leyendo o escribiendo, que es la mitad del diagnostico";
    EXPECT_NE(text.find("puntero nulo"), std::string::npos)
        << "la direccion 0 es un puntero nulo y el informe no lo dice";
    EXPECT_NE(text.find("enumerando las camaras"), std::string::npos)
        << "se perdio la ultima operacion";
}

// Y cuando SÍ es el fallo conocido, se dice: la explicación no desaparece, se
// condiciona a que encaje con la evidencia.
TEST(CrashReport, ItStillNamesTheKnownDriverBugWhenItFits) {
    char report[2048] = {0};
    pci::core::CrashFacts facts;
    facts.code = kIntDivideByZero;
    facts.breadcrumb = "abriendo camara 'AndroidCam'";
    facts.module = "kswdmcap.ax";
    pci::core::describeCrash(report, sizeof(report), facts);

    const std::string text(report);
    EXPECT_NE(text.find("kswdmcap.ax"), std::string::npos);
    EXPECT_NE(text.find("divide por cero"), std::string::npos)
        << "con una division por cero de verdad, la causa conocida tiene que salir";
    EXPECT_NE(text.find("en el modulo: kswdmcap.ax"), std::string::npos)
        << "el modulo culpable es lo que convierte la hipotesis en prueba";
}

// Sin espacio no se desborda ni se queda sin terminador: el informe se escribe
// con el proceso muriendose, y un desbordamiento ahí se lleva por delante lo
// poco que quedaba por registrar.
TEST(CrashReport, ItNeverOverrunsTheBuffer) {
    pci::core::CrashFacts facts;
    facts.code = kAccessViolation;
    facts.breadcrumb = "una operacion con un nombre larguisimo que no cabe de ninguna manera";
    facts.hasAccessInfo = true;
    for (unsigned long size = 1; size <= 200; size += 7) {
        std::vector<char> buffer(size + 8, '\x7f');
        pci::core::describeCrash(buffer.data(), size, facts);
        // Terminado en cero dentro del tamaño dado, y ni un byte más tocado.
        const auto end = std::find(buffer.begin(), buffer.begin() + size, '\0');
        EXPECT_NE(end, buffer.begin() + size) << "sin terminador con size=" << size;
        for (unsigned long i = size; i < size + 8; ++i) {
            EXPECT_EQ(buffer[i], '\x7f') << "escribio fuera del buffer con size=" << size;
        }
    }
    std::printf("  [informe] sin desbordar con 29 tamanos entre 1 y 200 bytes\n");
}

#endif  // _WIN32
