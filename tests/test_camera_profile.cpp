// Banco de pruebas del PERFIL DE MEDICIÓN de la cámara con una cámara falsa:
// el camino entero —apagar automáticos, barrer exposiciones, juzgar si
// compensa— sin hardware.
//
// Existe porque en la cámara de esta máquina el perfil siempre se RECHAZA (poca
// luz), así que el camino de aceptación no se ha visto correr nunca.
#include <gtest/gtest.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "camera/camera_controls.h"

using pci::camera::ExposureProfileOutcome;
using pci::camera::ExposureProfileResult;
using pci::camera::ExposureSweepCamera;
using pci::camera::runExposureProfile;
using pci::camera::SceneObservation;

namespace {

// El rango que `probeControls` midió en la cámara real de esta máquina. La
// escala es log2 de segundos —un paso entero DUPLICA el tiempo—, así que −11 es
// la exposición más corta y −3 la más larga.
constexpr double kShortest = -11.0;
constexpr double kLongest = -3.0;

// Cámara de mentira. Su única verdad es una función exposición → (fps,
// contraste); todo lo demás —qué se le escribió y cuántas veces se la miró—
// queda anotado, porque de este barrido importa tanto lo que elige como lo que
// cuesta y como deja la cámara al salir.
struct FakeCamera {
    std::function<SceneObservation(double)> response;  // con la exposición fija
    SceneObservation automatic;                        // con el automático puesto

    // La cámara sorda dice que sí a todo y no hace nada. No es un capricho: es
    // lo que hace la real con CAP_PROP_AUTO_EXPOSURE, que acepta el set() y
    // devuelve −1 pase lo que pase.
    bool deaf = false;

    // Diario de lo que le hicieron.
    std::vector<double> exposureWrites;
    std::vector<bool> autoWrites;
    int looks = 0;

    // Lo que de verdad tiene puesto el sensor.
    bool autoOn = true;
    double exposure = 0.0;

    SceneObservation look() {
        ++looks;
        return autoOn ? automatic : response(exposure);
    }

    [[nodiscard]] ExposureSweepCamera seam() {
        ExposureSweepCamera seam;
        seam.setExposure = [this](double value) {
            exposureWrites.push_back(value);
            if (!deaf) {
                exposure = value;
            }
        };
        seam.setAutoExposure = [this](bool on) {
            autoWrites.push_back(on);
            if (!deaf) {
                autoOn = on;
            }
        };
        seam.observe = [this] { return look(); };
        return seam;
    }
};

// La forma de los fps medida en la cámara real: NO bajan poco a poco con la
// exposición. Se mantienen planos de −11 a −5 y se desploman en el extremo
// largo, cuando el tiempo de integración pasa a ser mayor que el periodo del
// frame. Es un acantilado, no una rampa, y por eso hay un codo que elegir.
double cliffFps(double exposure, double plateau) {
    if (exposure <= -5.0) {
        return exposure <= -11.0 ? plateau + 0.1 : plateau;
    }
    if (exposure <= -4.0) {
        return 16.0;
    }
    return 8.0;
}

// Taller iluminado: hay luz sobre la pieza, así que fijar la exposición corta
// no la oscurece apenas. El automático, en cambio, se pone conservador y elige
// tiempos largos: imagen buena a 8 fps.
SceneObservation litWorkshop(double exposure) {
    return {cliffFps(exposure, 30.2), 40.0 + (exposure + 11.0) * 3.0};
}

// Habitación oscura, que es lo que hay en esta máquina: el automático gobierna
// también la GANANCIA y con ella sostiene el contraste a plena velocidad. Al
// fijar la exposición ese refuerzo se pierde y no hay con qué reponerlo,
// porque la ganancia aquí no es ajustable.
SceneObservation darkRoom(double exposure) {
    return {cliffFps(exposure, 30.5), 18.0 + (exposure + 11.0) * 1.0};
}

void printSweep(const char* scenario, const ExposureProfileResult& result) {
    std::printf("[%s] barrido:", scenario);
    for (const auto& sample : result.sweep) {
        std::printf(" %.0f->%.1ffps", sample.exposure, sample.fps);
    }
    std::printf("\n[%s] automatico %.1f fps / %.1f contraste ; fijada %.1f fps / %.1f "
                "contraste -> velocidad x%.2f, contraste x%.2f\n",
                scenario, result.automatic.fps, result.automatic.contrast, result.fixed.fps,
                result.fixed.contrast,
                result.automatic.fps > 0.0 ? result.fixed.fps / result.automatic.fps : 0.0,
                result.automatic.contrast > 0.0
                    ? result.fixed.contrast / result.automatic.contrast
                    : 0.0);
    if (!result.reason.empty()) {
        std::printf("[%s] motivo: %s\n", scenario, result.reason.c_str());
    }
}

}  // namespace

// EL CAMINO QUE NUNCA SE HA VISTO CORRER. Con luz encima de la pieza, fijar la
// exposición en el codo multiplica la velocidad por casi cuatro y el contraste
// aguanta: el perfil se lo ha ganado y se queda puesto.
TEST(CameraProfile, LitWorkshopKeepsTheFixedExposure) {
    FakeCamera camera;
    camera.response = litWorkshop;
    camera.automatic = {8.0, 62.0};  // el automático: se ve bien, pero va lento

    const ExposureProfileResult result =
        runExposureProfile(camera.seam(), kShortest, kLongest);
    printSweep("taller", result);

    EXPECT_EQ(result.outcome, ExposureProfileOutcome::Applied);
    ASSERT_TRUE(result.exposure.has_value());
    EXPECT_DOUBLE_EQ(*result.exposure, -5.0);
    EXPECT_TRUE(result.reason.empty());

    // Y la cámara queda como dice el veredicto: exposición fija puesta y el
    // automático apagado. Si el veredicto dijera una cosa y la cámara acabara
    // en otra, el perfil no serviría de nada.
    EXPECT_FALSE(camera.autoOn);
    EXPECT_DOUBLE_EQ(camera.exposure, -5.0);
    ASSERT_FALSE(camera.autoWrites.empty());
    EXPECT_FALSE(camera.autoWrites.back());

    // Los márgenes con los que se acepta, para que se vea que no está en el
    // filo: 3,8x de velocidad (hace falta 1,25x) con el 93 % del contraste
    // (hace falta un 60 %).
    EXPECT_GT(result.fixed.fps / result.automatic.fps, 1.25);
    EXPECT_GT(result.fixed.contrast / result.automatic.contrast, 0.6);
}

// Poca luz: fijar la exposición gana un 3 % de velocidad y se lleva por delante
// la mitad del contraste. Cambiar una imagen buena por un 3 % es un mal
// negocio, así que se deshace y se dice por qué.
TEST(CameraProfile, DarkRoomFallsBackToAutomatic) {
    FakeCamera camera;
    camera.response = darkRoom;
    camera.automatic = {29.7, 48.0};  // lo medido en esta máquina

    const ExposureProfileResult result =
        runExposureProfile(camera.seam(), kShortest, kLongest);
    printSweep("oscuro", result);

    EXPECT_EQ(result.outcome, ExposureProfileOutcome::Reverted);
    EXPECT_FALSE(result.exposure.has_value());

    // Deshacer de verdad: la cámara vuelve al automático, no se queda a medias
    // con la exposición fija de una prueba que no salió.
    EXPECT_TRUE(camera.autoOn);
    ASSERT_FALSE(camera.autoWrites.empty());
    EXPECT_TRUE(camera.autoWrites.back());

    // El motivo tiene que ser accionable: aquí no hay nada roto, hace falta más
    // luz sobre la pieza. Sin esa frase el operador solo sabe que algo falló.
    EXPECT_NE(result.reason.find("mas luz"), std::string::npos);
}

// La cámara que dice que sí a todo y no hace nada. Se parece muchísimo a un
// éxito —los fps salen planos, el contraste no se mueve, el veredicto no tiene
// nada que reprochar— y es justo lo contrario: no se le ha cambiado nada, así
// que no puede darse por configurada. Decir que sí aquí sería vender una
// repetibilidad que no existe.
TEST(CameraProfile, DeafCameraIsNeverReportedAsConfigured) {
    FakeCamera camera;
    camera.deaf = true;
    // Una respuesta que sería estupenda si la cámara escuchara: así queda claro
    // que el veredicto sale de lo que la cámara HIZO y no del guion.
    camera.response = litWorkshop;
    camera.automatic = {30.0, 50.0};

    const ExposureProfileResult result =
        runExposureProfile(camera.seam(), kShortest, kLongest);
    printSweep("sorda", result);

    EXPECT_NE(result.outcome, ExposureProfileOutcome::Applied);
    EXPECT_EQ(result.outcome, ExposureProfileOutcome::Ignored);
    EXPECT_FALSE(result.exposure.has_value());
    EXPECT_NE(result.reason.find("no reacciona"), std::string::npos);

    // Se intentó escribir (no hay forma de saber que es sorda sin intentarlo) y
    // se acaba pidiendo el automático de vuelta.
    EXPECT_FALSE(camera.exposureWrites.empty());
    ASSERT_FALSE(camera.autoWrites.empty());
    EXPECT_TRUE(camera.autoWrites.back());

    // De dónde venía el agujero: el veredicto de imagen, por sí solo, se la
    // habría quedado. Y con razón dentro de su terreno —no perdió ni contraste
    // ni velocidad—, porque no es asunto suyo saber si la cámara escuchó.
    EXPECT_TRUE(pci::camera::judgeProfile(result.automatic.fps, result.automatic.contrast,
                                          result.fixed.fps, result.fixed.contrast)
                    .keep);
}

// Sin recorrido de exposición no hay nada que elegir, y sin nada que elegir NO
// SE APAGA EL AUTOMÁTICO: apagarlo a secas dejó la cámara real en 8,0 fps
// viniendo de 29,7, porque se cae a su valor manual, que era el más largo del
// rango. Apagar un automático que no se puede sustituir no es neutral: es
// elegir el peor valor.
TEST(CameraProfile, WithoutAdjustableExposureNothingIsWritten) {
    FakeCamera camera;
    camera.response = litWorkshop;
    camera.automatic = {30.0, 50.0};

    // Rango degenerado: es lo que reporta `probeControls` de una cámara que se
    // queda en el mismo valor la empujes a donde la empujes.
    const ExposureProfileResult result = runExposureProfile(camera.seam(), -3.0, -3.0);

    EXPECT_EQ(result.outcome, ExposureProfileOutcome::Untouched);
    EXPECT_FALSE(result.exposure.has_value());
    EXPECT_TRUE(result.sweep.empty());

    // Ni una sola escritura, ni siquiera una medida: se sale sin tocar nada.
    EXPECT_TRUE(camera.autoWrites.empty());
    EXPECT_TRUE(camera.exposureWrites.empty());
    EXPECT_EQ(camera.looks, 0);
    EXPECT_TRUE(camera.autoOn);
    std::printf("[sin rango] escrituras=%zu medidas=%d\n",
                camera.autoWrites.size() + camera.exposureWrites.size(), camera.looks);
}

// El codo del acantilado. Con los fps planos de −11 a −5, la exposición buena
// no es ni la más corta —tiraría luz a cambio de nada— ni la que trae la
// cámara: es la MÁS LARGA que todavía da la velocidad máxima.
TEST(CameraProfile, PicksTheKneeOfTheCliff) {
    FakeCamera camera;
    // Los números exactos medidos en la cámara real: 30,2 fps de −11 a −5,
    // 16,0 en −4 y 8,0 en −3.
    camera.response = [](double exposure) -> SceneObservation {
        return {cliffFps(exposure, 30.2), 40.0 + (exposure + 11.0) * 3.0};
    };
    camera.automatic = {8.0, 62.0};

    const ExposureProfileResult result =
        runExposureProfile(camera.seam(), kShortest, kLongest);
    printSweep("acantilado", result);

    ASSERT_TRUE(result.exposure.has_value());
    EXPECT_DOUBLE_EQ(*result.exposure, -5.0);

    // Y que el codo sea el codo: un paso más largo ya cuesta la mitad de la
    // velocidad, y uno más corto no da ni un fps más.
    EXPECT_DOUBLE_EQ(cliffFps(-4.0, 30.2), 16.0);
    EXPECT_DOUBLE_EQ(cliffFps(-6.0, 30.2), cliffFps(-5.0, 30.2));
}

// La salida temprana no es una optimización elegante: son segundos de arranque.
// Cada medida cuesta una ventana de 400 ms más los frames de asentamiento, así
// que barrer las nueve candidatas en vez de parar en el codo doblaría el
// tiempo que el operador pasa mirando una cámara que aún no da imagen.
TEST(CameraProfile, SweepStopsAtTheKnee) {
    FakeCamera camera;
    camera.response = litWorkshop;
    camera.automatic = {8.0, 62.0};

    const ExposureProfileResult result =
        runExposureProfile(camera.seam(), kShortest, kLongest);

    // Se mide la más corta (el techo) y se baja desde la más larga: −3, −4 y
    // −5, que ya alcanza el techo y corta el barrido. Cuatro medidas de las
    // nueve candidatas.
    ASSERT_EQ(result.sweep.size(), 4U);
    EXPECT_DOUBLE_EQ(result.sweep.front().exposure, -11.0);
    EXPECT_DOUBLE_EQ(result.sweep.back().exposure, -5.0);

    // Y el coste total: la referencia en automático, las cuatro del barrido y
    // la comprobación final. Seis ventanas, no once.
    EXPECT_EQ(camera.looks, 6);
    std::printf("[coste] %d medidas (%d del barrido de 9 candidatas) ~ %.1f s a 400 ms\n",
                camera.looks, static_cast<int>(result.sweep.size()), camera.looks * 0.4);
}

// Aplicar el perfil dos veces seguidas no puede ir moviendo la cámara. Importa
// porque el perfil se pide al abrir y también desde el botón «perfil de
// medición», y una segunda pasada que eligiera otra cosa querría decir que la
// primera no era una decisión, era una casualidad.
TEST(CameraProfile, ApplyingTheProfileTwiceLeavesTheSameCamera) {
    FakeCamera camera;
    camera.response = litWorkshop;
    camera.automatic = {8.0, 62.0};

    const ExposureProfileResult first =
        runExposureProfile(camera.seam(), kShortest, kLongest);
    const std::vector<double> firstWrites = camera.exposureWrites;
    const bool firstAutoOn = camera.autoOn;
    const double firstExposure = camera.exposure;
    camera.exposureWrites.clear();
    camera.autoWrites.clear();

    const ExposureProfileResult second =
        runExposureProfile(camera.seam(), kShortest, kLongest);

    EXPECT_EQ(second.outcome, first.outcome);
    ASSERT_TRUE(second.exposure.has_value());
    EXPECT_DOUBLE_EQ(*second.exposure, *first.exposure);
    // Mismo estado final y, además, el mismo camino: si la segunda pasada
    // probara otras candidatas sería que arrastra estado de la primera.
    EXPECT_EQ(camera.autoOn, firstAutoOn);
    EXPECT_DOUBLE_EQ(camera.exposure, firstExposure);
    EXPECT_EQ(camera.exposureWrites, firstWrites);
    std::printf("[idempotencia] 1a %.0f, 2a %.0f, la camara queda en %.0f\n", *first.exposure,
                *second.exposure, camera.exposure);
}

// El techo mal medido. La primera ventana es la que fija la velocidad máxima
// alcanzable; si esa medida se pierde —un tropiezo del driver, un frame que no
// llega— el techo sale 0, la salida temprana se dispara con la PRIMERA
// candidata y la elegida acaba siendo la más larga, que es la peor. Con eso el
// perfil dejaría la cámara en 8 fps viniendo de 29,7: exactamente el desastre
// de 3,7x que este código existe para evitar.
TEST(CameraProfile, ABadCeilingDoesNotShipASlowerCamera) {
    FakeCamera camera;
    int measured = 0;
    camera.response = [&measured](double exposure) -> SceneObservation {
        ++measured;
        if (measured == 1) {
            return {0.0, 0.0};  // la ventana del techo se pierde
        }
        return litWorkshop(exposure);
    };
    camera.automatic = {29.7, 55.0};

    const ExposureProfileResult result =
        runExposureProfile(camera.seam(), kShortest, kLongest);
    printSweep("techo perdido", result);

    // Lo que NO puede pasar: quedarse con una exposición más lenta que el
    // automático que había.
    EXPECT_NE(result.outcome, ExposureProfileOutcome::Applied);
    EXPECT_FALSE(result.exposure.has_value());
    EXPECT_TRUE(camera.autoOn);
    ASSERT_FALSE(camera.autoWrites.empty());
    EXPECT_TRUE(camera.autoWrites.back());
    EXPECT_LT(result.fixed.fps, result.automatic.fps);

    // Y el agujero, otra vez: el veredicto de imagen se lo habría quedado, y
    // encima con buena nota, porque una exposición larga da MÁS contraste. Lo
    // que no mira —no puede— es que el precio fue 3,7x de velocidad.
    EXPECT_TRUE(pci::camera::judgeProfile(result.automatic.fps, result.automatic.contrast,
                                          result.fixed.fps, result.fixed.contrast)
                    .keep);
    std::printf("[techo perdido] sin la red de la orquestacion se habria quedado en %.1f "
                "fps, %.1fx mas lento\n",
                result.fixed.fps, result.automatic.fps / result.fixed.fps);
}
