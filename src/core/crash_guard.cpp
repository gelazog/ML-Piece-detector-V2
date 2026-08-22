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

// El modulo al que pertenece una direccion, o "" si no se sabe.
//
// Esto es lo que convierte el informe en una PISTA en vez de una conjetura: si
// la direccion que fallo cae dentro de kswdmcap.ax o de la DLL de una camara
// virtual, la causa deja de ser una hipotesis y pasa a estar demostrada; y si
// cae dentro de pc_inspector.exe, la culpa es nuestra y no de ningun driver.
//
// Sin CRT y sin reservar memoria: el proceso se esta muriendo.
void moduleOfAddress(void* address, char* out, unsigned long outSize) {
    out[0] = '\0';
    if (address == nullptr) {
        return;
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) == 0 ||
        module == nullptr) {
        return;
    }
    char full[MAX_PATH] = {0};
    if (GetModuleFileNameA(module, full, MAX_PATH) == 0) {
        return;
    }
    // Solo el nombre del fichero: la ruta completa no aporta y ensucia el log.
    const char* name = full;
    for (const char* c = full; *c != '\0'; ++c) {
        if (*c == '\\' || *c == '/') {
            name = c + 1;
        }
    }
    unsigned long i = 0;
    while (name[i] != '\0' && i + 1 < outSize) {
        out[i] = name[i];
        ++i;
    }
    out[i] = '\0';
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = (info != nullptr) ? info->ExceptionRecord : nullptr;

    CrashFacts facts;
    facts.breadcrumb = g_breadcrumb;
    char module[MAX_PATH] = {0};
    if (record != nullptr) {
        facts.code = static_cast<unsigned long>(record->ExceptionCode);
        facts.address = record->ExceptionAddress;
        moduleOfAddress(record->ExceptionAddress, module, MAX_PATH);
        facts.module = module;
        if (facts.code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            facts.hasAccessInfo = true;
            facts.accessKind =
                static_cast<unsigned long long>(record->ExceptionInformation[0]);
            facts.accessAddress =
                static_cast<unsigned long long>(record->ExceptionInformation[1]);
        }
    }

    // Buffer en la pila: el proceso agoniza y reservar memoria puede fallar o
    // colgarse si lo que reventó fue el propio montón.
    char report[2048] = {0};
    describeCrash(report, sizeof(report), facts);

    // Best-effort: C stdio, sin allocaciones de C++.
    if (std::FILE* f = std::fopen(g_crashLogPath.c_str(), "a")) {
        std::fputs(report, f);
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
// La redacción del informe, separada de los hechos para poder probarla.
//
// Toda la gracia está en que la causa se DEDUCE de lo que hay, en vez de
// afirmar siempre la misma. La versión anterior decía «dividió por cero»
// incluso cuando el código era ACCESS_VIOLATION —que es otra cosa— y mandaba a
// quien leyera el log a buscar una división que nunca ocurrió. Hay tres cierres
// registrados así en este proyecto.
void describeCrash(char* out, unsigned long outSize, const CrashFacts& facts) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    out[0] = 0;
    unsigned long used = 0;
    const auto append = [&](const char* format, auto... args) {
        if (used + 1 >= outSize) {
            return;
        }
        const int written = std::snprintf(out + used, outSize - used, format, args...);
        if (written > 0) {
            used += static_cast<unsigned long>(written);
            if (used >= outSize) {
                used = outSize - 1;
            }
        }
    };

    append("==== CRASH a nivel del sistema operativo ====\n");
    append("  excepcion: 0x%08lX (%s)\n", facts.code, exceptionName(facts.code));
    append("  ultima operacion: %s\n",
           (facts.breadcrumb != nullptr && facts.breadcrumb[0] != 0) ? facts.breadcrumb
                                                                    : "(ninguna)");
    if (facts.address != nullptr) {
        append("  direccion: %p\n", facts.address);
    }
    if (facts.module != nullptr && facts.module[0] != 0) {
        // Lo más útil del informe: si la dirección cae en un .ax/.dll de
        // captura, la culpa del driver deja de ser una hipótesis; si cae en el
        // propio ejecutable, es de este código y no de ningún driver.
        append("  en el modulo: %s\n", facts.module);
    }
    if (facts.hasAccessInfo) {
        const char* verb = facts.accessKind == 0   ? "leyendo"
                           : facts.accessKind == 1 ? "escribiendo"
                           : facts.accessKind == 8 ? "ejecutando"
                                                   : "accediendo a";
        append("  %s la direccion 0x%016llX%s\n", verb, facts.accessAddress,
               facts.accessAddress < 0x10000
                   ? "  (puntero nulo o casi: fallo de codigo, no de hardware)"
                   : "");
    }

    if (facts.code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        facts.code == EXCEPTION_FLT_DIVIDE_BY_ZERO) {
        append("  Encaja con el fallo conocido de kswdmcap.ax: un driver de captura\n"
               "  negociando formato con una camara virtual no lista (p. ej.\n"
               "  AndroidCam sin el celular conectado) divide por cero.\n");
    } else if (facts.code == EXCEPTION_ACCESS_VIOLATION) {
        append("  Una violacion de acceso NO es el fallo conocido de division por\n"
               "  cero de los drivers de captura: es otra cosa. Mira el modulo de\n"
               "  arriba para saber de quien es.\n");
    }
    append("  Es un fallo a nivel del SO: ningun try/catch de C++ puede atraparlo,\n"
           "  por eso el proceso termina aqui.\n\n");
}




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
