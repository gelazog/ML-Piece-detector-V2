#pragma once

// SOLTAR LOS HILOS DE OPENCV ANTES DE SALIR DEL PROCESO.
//
// OpenCV de MSYS2 reparte `cv::parallel_for_` con TBB, y guarda el grupo de
// hilos en un `tbb::task_arena` ESTÁTICO dentro de libopencv_core. Ese objeto se
// destruye al descargar la DLL, en `LdrShutdownProcess`, y para entonces
// `ExitProcess` ya ha matado todos los hilos menos el principal. Su destructor
// llama a `tbb::detail::r1::terminate`, que a veces se queda esperando
// (`WaitForSingleObjectEx`) algo que solo un hilo de trabajo podía avisar. Ese
// hilo ya no existe, así que la espera no acaba nunca y el proceso no termina.
//
// Pasa poco. En un banco de procesos que hacen un `parallel_for_` y salen, 48 a
// la vez, se colgaron 9 de 16000 sin este arreglo y 0 de 8000 con él. Con
// `ctest -j` y varias suites a la vez basta para que una prueba cualquiera, que
// ya había dicho «OK», agote los 300 s. Se colgaron así cuatro pruebas
// distintas y ninguna tenía nada que ver con el fallo.
//
// El arreglo es terminar ese `task_arena` mientras los hilos siguen vivos, al
// final de `main`. `cv::setNumThreads(0)` hace exactamente eso
// (`tbbArena.terminate()` sin volver a inicializarlo), y el destructor estático
// ya no tiene nada que hacer cuando le llega su turno. Comprobado con gdb y un
// punto de parada en `r1::terminate`: sin el arreglo se llama desde
// `LdrShutdownProcess`; con él, una sola vez y desde `main`.
//
// Después de soltarlo, OpenCV sigue funcionando, pero en un solo hilo: con cero
// hilos `parallel_for_` ejecuta el cuerpo en el hilo que llama y no vuelve a
// crear el grupo. Por eso solo se llama al salir.

namespace pci::vision {

// Termina el grupo de hilos de OpenCV. Llamarla con los hilos de la aplicación
// ya parados (cámara, vídeo): lo que use OpenCV después irá en serie.
void releaseParallelPoolBeforeExit();

// Lo mismo, como objeto: se declara el PRIMERO en `main` y al destruirse —el
// último, después de la ventana y de los hilos de cámara— suelta el grupo.
// Cubre también los `return` tempranos y las excepciones capturadas en `main`.
class ParallelPoolExitGuard {
public:
    ParallelPoolExitGuard() = default;
    ParallelPoolExitGuard(const ParallelPoolExitGuard&) = delete;
    ParallelPoolExitGuard& operator=(const ParallelPoolExitGuard&) = delete;
    ParallelPoolExitGuard(ParallelPoolExitGuard&&) = delete;
    ParallelPoolExitGuard& operator=(ParallelPoolExitGuard&&) = delete;
    ~ParallelPoolExitGuard() { releaseParallelPoolBeforeExit(); }
};

}  // namespace pci::vision
