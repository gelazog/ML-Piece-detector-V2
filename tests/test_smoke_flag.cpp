// LA COMPROBACIÓN QUE SALÍA EN VERDE PASARA LO QUE PASARA.
//
// Cerrar un trabajo en este proyecto pide tres cosas: compilación limpia, banco
// verde, y que la aplicación arranque. La tercera se comprobaba lanzando
// `pc_inspector.exe --smoke`.
//
// `--smoke` NO EXISTÍA. Qt ignora en silencio un argumento que no conoce, así
// que aquello abría la ventana y la dejaba abierta para siempre; el «0» que se
// leía después salía de MATAR el proceso, no de la aplicación. Meses de
// «la app arranca» apoyados en un proceso colgado.
//
// Es la peor clase de prueba: no es que no comprobara nada, es que además daba
// confianza. Y se descubrió por accidente —el proceso colgado bloqueó el
// enlazado del siguiente build— no porque nadie la mirara.
//
// Esta guardia no puede lanzar la aplicación (necesita pantalla y una cámara
// del sistema), así que hace lo siguiente mejor: leer `src/main.cpp` y exigir
// que la bandera SIGA MANEJÁNDOSE y siga terminando en una salida. Si alguien
// la quita, el arranque en seco vuelve a ser un proceso colgado — y esto lo
// dice en vez de dejar que se descubra otra vez de rebote.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// Se ancla en un fichero conocido para dar con la raíz: el árbol de compilación
// crea directorios con los mismos nombres, y este proyecto ya ha tropezado con
// eso antes.
std::string readMain() {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        const std::filesystem::path candidate = dir / "src" / "main.cpp";
        if (std::filesystem::exists(candidate)) {
            std::ifstream file(candidate);
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }
        dir = dir.parent_path();
    }
    return {};
}

}  // namespace

TEST(SmokeFlag, TheDryRunFlagIsActuallyHandledAndActuallyQuits) {
    const std::string main = readMain();
    ASSERT_FALSE(main.empty()) << "no se encuentra src/main.cpp: esta guardia no está "
                                  "comprobando nada, que es exactamente el fallo que narra";

    // Se busca la BANDERA COMPILADA y no la cadena «--smoke» a secas: esa sale
    // también en los comentarios —incluido el de arriba, que cuenta la
    // historia—, y entonces bastaría con dejar el comentario para que esta
    // guardia siguiera en verde. Sería el mismo fallo otra vez, dentro de la
    // prueba que lo denuncia.
    EXPECT_NE(main.find("QStringLiteral(\"--smoke\")"), std::string::npos)
        << "`--smoke` ha dejado de manejarse en main.cpp. La aplicación seguirá "
           "aceptándolo sin rechistar —Qt ignora lo que no conoce— y el arranque en seco "
           "volverá a ser una ventana abierta para siempre que da un 0 al matarla.";

    // Y que lleve a SALIR. Reconocer la bandera y no terminar sería el mismo
    // proceso colgado con más letras.
    EXPECT_NE(main.find("QCoreApplication::quit"), std::string::npos)
        << "se reconoce `--smoke` pero nada cierra la aplicación: el arranque en seco no "
           "termina solo, así que su código de salida sigue viniendo de matarlo";

    std::printf("  [smoke] la bandera se maneja en main.cpp y termina en una salida\n");
}
