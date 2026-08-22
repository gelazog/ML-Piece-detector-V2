#pragma once

#include <string>

namespace pci::core {

// Blindaje frente a fallos a nivel del sistema operativo — pensado sobre todo
// para drivers de captura de vídeo rotos (p. ej. kswdmcap.ax intentando
// negociar resolución/fps con una cámara virtual no lista, como AndroidCam sin
// el celular conectado, que divide por cero y tumba el proceso sin lanzar
// ninguna excepción de C++ que un try/catch pueda atrapar).
//
// El API es portable: en plataformas sin soporte (Linux, microcontroladores)
// las funciones degradan a un comportamiento seguro y trivial.

// Deja una "miga de pan" con la operación de riesgo en curso (p. ej. "abriendo
// cámara DroidCam"). Si el proceso muere a nivel del SO, el manejador de fallos
// la vuelca al log de crash para que la causa quede registrada. Thread-safe.
void setBreadcrumb(const std::string& operation);

// Instala un manejador de último recurso que, justo antes de que el proceso
// muera por una excepción estructurada del SO, escribe el código de excepción y
// la última miga de pan a `crashLogPath`. En Windows además desactiva el diálogo
// de Windows Error Reporting que congelaría una app sin consola. Llamar una sola
// vez al arrancar. En plataformas sin soporte no hace nada.
void installCrashHandler(const std::string& crashLogPath);

// Los hechos de un fallo del SO, tal como los entrega Windows.
//
// Existe para poder PROBAR el informe. El texto que se escribe al morir es lo
// unico que queda de un cierre inesperado, y la version anterior afirmaba
// siempre la misma causa —"un driver dividio por cero"— incluso cuando la
// excepcion era una violacion de acceso, que es otra cosa. Quien leyera ese log
// se pondria a buscar una division que nunca ocurrio.
//
// Separando los HECHOS de su REDACCION, la redaccion se puede comprobar sin
// tener que matar un proceso.
struct CrashFacts {
    unsigned long code = 0;        // codigo de excepcion del SO
    const char* breadcrumb = "";   // ultima operacion marcada
    const void* address = nullptr; // donde fallo
    const char* module = "";       // modulo que contiene esa direccion, si se supo
    // Solo para ACCESS_VIOLATION: 0 leyendo, 1 escribiendo, 8 ejecutando.
    bool hasAccessInfo = false;
    unsigned long long accessKind = 0;
    unsigned long long accessAddress = 0;
};

// Redacta el informe de un fallo en el buffer dado (siempre terminado en cero).
// Sin reservar memoria: se llama con el proceso muriendose.
void describeCrash(char* out, unsigned long outSize, const CrashFacts& facts);

// Ejecuta fn(ctx) protegido frente a EXCEPCIONES ESTRUCTURADAS del SO (división
// entera por cero, acceso inválido, instrucción ilegal) que un try/catch de C++
// NO atrapa. Devuelve true si fn terminó con normalidad; false si el SO lanzó
// una excepción estructurada, dejando su código en *outCode (si no es nullptr).
// Las excepciones de C++ deben capturarse DENTRO de fn: no cruzan esta barrera.
// En plataformas sin SEH ejecuta fn directamente y siempre devuelve true.
bool runProtected(void (*fn)(void*), void* ctx, unsigned long* outCode);

}  // namespace pci::core
