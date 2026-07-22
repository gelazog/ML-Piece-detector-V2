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

// Ejecuta fn(ctx) protegido frente a EXCEPCIONES ESTRUCTURADAS del SO (división
// entera por cero, acceso inválido, instrucción ilegal) que un try/catch de C++
// NO atrapa. Devuelve true si fn terminó con normalidad; false si el SO lanzó
// una excepción estructurada, dejando su código en *outCode (si no es nullptr).
// Las excepciones de C++ deben capturarse DENTRO de fn: no cruzan esta barrera.
// En plataformas sin SEH ejecuta fn directamente y siempre devuelve true.
bool runProtected(void (*fn)(void*), void* ctx, unsigned long* outCode);

}  // namespace pci::core
