#include "core/crash_guard.h"

#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/logging.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pci::core {

namespace {

// La miga de pan se lee desde el manejador de fallos, que corre en un contexto
// delicado (el proceso se está muriendo). Guardarla en un buffer fijo evita
// tomar mutex o allocar dentro del manejador: una lectura a medio escribir da
// texto truncado, nunca un fallo. La escritura sí se serializa.
constexpr std::size_t kBreadcrumbMax = 256;
char g_breadcrumb[kBreadcrumbMax] = "(ninguna)";
std::mutex g_breadcrumbMutex;
std::string g_crashLogPath;

#ifdef _WIN32

// GCC/MinGW no soporta __try/__except (es una extensión de MSVC), así que el
// blindaje se hace con un Vectored Exception Handler que, ante un fallo "duro"
// del SO dentro de la región protegida, hace longjmp de vuelta a runProtected.
// El estado es thread_local: el VEH es de proceso, pero solo actúa en el hilo
// que está dentro de runProtected.
thread_local std::jmp_buf g_guardJmp;
thread_local bool g_guardActive = false;
thread_local unsigned long g_guardCode = 0;

bool isHardFault(DWORD code) {
    switch (code) {
        case EXCEPTION_INT_DIVIDE_BY_ZERO:   // el fallo típico de kswdmcap.ax
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
            return true;
        default:
            // Las excepciones de C++ (0xE06D7363) y los breakpoints del
            // depurador NO se interceptan: siguen su curso normal.
            return false;
    }
}

LONG CALLBACK guardVeh(EXCEPTION_POINTERS* info) {
    if (g_guardActive && info != nullptr && info->ExceptionRecord != nullptr &&
        isHardFault(info->ExceptionRecord->ExceptionCode)) {
        g_guardCode = static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode);
        g_guardActive = false;
        std::longjmp(g_guardJmp, 1);  // no retorna: desenrolla hasta runProtected
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

const char* exceptionName(unsigned long code) {
    switch (code) {
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
        default: return "DESCONOCIDA";
    }
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info) {
    const unsigned long code =
        (info != nullptr && info->ExceptionRecord != nullptr)
            ? static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode)
            : 0;

    // Best-effort: C stdio, sin allocaciones de C++, porque el proceso agoniza.
    if (std::FILE* f = std::fopen(g_crashLogPath.c_str(), "a")) {
        std::fprintf(
            f,
            "==== CRASH a nivel del sistema operativo ====\n"
            "  excepcion: 0x%08lX (%s)\n"
            "  ultima operacion: %s\n"
            "  Causa probable: un driver de captura (p. ej. kswdmcap.ax con una\n"
            "  camara virtual no lista, como AndroidCam sin el celular conectado)\n"
            "  fallo al negociar formato y dividio por cero. Es un fallo del SO,\n"
            "  no del codigo C++, por eso ningun try/catch pudo atraparlo.\n\n",
            code, exceptionName(code), g_breadcrumb);
        std::fclose(f);
    }

    // Handled: el SO termina el proceso sin el diálogo de error, ya registrado.
    return EXCEPTION_EXECUTE_HANDLER;
}

// Aísla el setjmp en su propia función sin variables locales vivas a través del
// salto, para no disparar -Werror=clobbered.
bool runWithJump(void (*fn)(void*), void* ctx) {
    if (setjmp(g_guardJmp) != 0) {
        return false;  // regresamos aquí por longjmp desde el VEH
    }
    g_guardActive = true;
    fn(ctx);
    g_guardActive = false;
    return true;
}

#endif  // _WIN32

}  // namespace

void setBreadcrumb(const std::string& operation) {
    {
        std::lock_guard lock(g_breadcrumbMutex);
        std::strncpy(g_breadcrumb, operation.c_str(), kBreadcrumbMax - 1);
        g_breadcrumb[kBreadcrumbMax - 1] = '\0';
    }
    logDebug("» " + operation);
}

void installCrashHandler(const std::string& crashLogPath) {
    g_crashLogPath = crashLogPath;
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledFilter);
    // Sin diálogos modales de fallo: una app sin consola quedaría congelada.
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    logInfo("Manejador de fallos del SO instalado (crash log: " + crashLogPath + ")");
#else
    (void)crashLogPath;
    logInfo("Manejador de fallos del SO no disponible en esta plataforma");
#endif
}

bool runProtected(void (*fn)(void*), void* ctx, unsigned long* outCode) {
#ifdef _WIN32
    // Prioridad 1 = se llama antes que otros manejadores; así interceptamos el
    // fallo del driver antes de que escale a terminación del proceso.
    PVOID veh = AddVectoredExceptionHandler(1, guardVeh);
    g_guardCode = 0;
    const bool ok = runWithJump(fn, ctx);
    g_guardActive = false;
    if (veh != nullptr) {
        RemoveVectoredExceptionHandler(veh);
    }
    if (!ok && outCode != nullptr) {
        *outCode = g_guardCode;
    }
    return ok;
#else
    (void)outCode;
    fn(ctx);
    return true;
#endif
}

}  // namespace pci::core
