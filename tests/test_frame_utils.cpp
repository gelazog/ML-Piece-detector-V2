#include "camera/camera_controls.h"
#include "camera/frame_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

#include <QColor>
#include <QImage>

using pci::camera::matToQImage;
using pci::camera::qImageToMat;

TEST(FrameUtils, EmptyMatGivesNullImage) {
    const cv::Mat empty;
    EXPECT_TRUE(matToQImage(empty).isNull());
}

TEST(FrameUtils, UnsupportedTypeGivesNullImage) {
    const cv::Mat floats(4, 4, CV_32FC1, cv::Scalar(0.5));
    EXPECT_TRUE(matToQImage(floats).isNull());
}

TEST(FrameUtils, BgrPixelsMapToCorrectColors) {
    cv::Mat mat(2, 2, CV_8UC3, cv::Scalar(0, 0, 0));
    mat.at<cv::Vec3b>(0, 0) = {255, 0, 0};  // azul en BGR
    mat.at<cv::Vec3b>(0, 1) = {0, 255, 0};  // verde
    mat.at<cv::Vec3b>(1, 0) = {0, 0, 255};  // rojo

    const QImage image = matToQImage(mat);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(2, 2));
    EXPECT_EQ(image.pixelColor(0, 0), QColor(0, 0, 255));
    EXPECT_EQ(image.pixelColor(1, 0), QColor(0, 255, 0));
    EXPECT_EQ(image.pixelColor(0, 1), QColor(255, 0, 0));
    EXPECT_EQ(image.pixelColor(1, 1), QColor(0, 0, 0));
}

TEST(FrameUtils, ImageOwnsItsBuffer) {
    cv::Mat mat(2, 2, CV_8UC3, cv::Scalar(10, 20, 30));
    QImage image = matToQImage(mat);

    // Al reutilizar el Mat (como hace el hilo de captura), la QImage no cambia.
    mat.setTo(cv::Scalar(200, 200, 200));
    EXPECT_EQ(image.pixelColor(0, 0), QColor(30, 20, 10));
}

TEST(FrameUtils, GrayscaleSupported) {
    const cv::Mat gray(3, 3, CV_8UC1, cv::Scalar(128));
    const QImage image = matToQImage(gray);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.format(), QImage::Format_Grayscale8);
    EXPECT_EQ(image.pixelColor(1, 1), QColor(128, 128, 128));
}

TEST(FrameUtils, QImageToMatNullGivesEmpty) {
    EXPECT_TRUE(qImageToMat(QImage()).empty());
}

TEST(FrameUtils, QImageToMatRoundTrip) {
    cv::Mat mat(2, 2, CV_8UC3, cv::Scalar(0, 0, 0));
    mat.at<cv::Vec3b>(0, 0) = {255, 0, 0};
    mat.at<cv::Vec3b>(1, 1) = {10, 20, 30};

    const cv::Mat back = qImageToMat(matToQImage(mat));
    ASSERT_EQ(back.type(), CV_8UC3);
    ASSERT_EQ(back.size(), mat.size());
    EXPECT_EQ(back.at<cv::Vec3b>(0, 0), cv::Vec3b(255, 0, 0));
    EXPECT_EQ(back.at<cv::Vec3b>(1, 1), cv::Vec3b(10, 20, 30));
}

TEST(FrameUtils, QImageToMatConvertsForeignFormats) {
    QImage rgb(2, 2, QImage::Format_RGB32);
    rgb.fill(QColor(10, 20, 30));  // R=10, G=20, B=30

    const cv::Mat mat = qImageToMat(rgb);
    ASSERT_EQ(mat.type(), CV_8UC3);
    EXPECT_EQ(mat.at<cv::Vec3b>(0, 0), cv::Vec3b(30, 20, 10));  // BGR
}

// --- Controles de la fuente (O2) ---

TEST(CameraControls, KeysAndLabelsAreUniqueAndStable) {
    std::vector<std::string> keys;
    for (const auto property : pci::camera::allCameraProperties()) {
        const std::string key(pci::camera::propertyKey(property));
        EXPECT_FALSE(key.empty());
        EXPECT_EQ(std::count(keys.begin(), keys.end(), key), 0) << "clave repetida: " << key;
        keys.push_back(key);
        EXPECT_NE(std::string(pci::camera::propertyLabel(property)), std::string());
        EXPECT_NE(std::string(pci::camera::propertyHelp(property)), std::string());
    }
    // Las claves persisten en Settings: fijarlas evita perder ajustes al
    // renombrar el enum.
    EXPECT_EQ(std::string(pci::camera::propertyKey(pci::camera::CameraProperty::Exposure)),
              "cam_exposure");
}

TEST(CameraControls, TogglesAreOnlyTheAutomaticOnes) {
    EXPECT_TRUE(pci::camera::isToggle(pci::camera::CameraProperty::AutoExposure));
    EXPECT_TRUE(pci::camera::isToggle(pci::camera::CameraProperty::AutoFocus));
    EXPECT_FALSE(pci::camera::isToggle(pci::camera::CameraProperty::Brightness));
    EXPECT_FALSE(pci::camera::isToggle(pci::camera::CameraProperty::Exposure));
}

// El rango ya no se adivina del valor: se MIDE al abrir la camara (probeControls)
// y rangeFor solo decide un paso de ajuste usable para ese recorrido.
//
// Motivo del cambio: sondeando una camara real, get(BRIGHTNESS) devolvia 91
// pero set() lo rechazaba, y la exposicion aceptaba solo [-11, -3] en vez del
// [-15, 5] que se suponia. Adivinar producia deslizadores muertos o con topes
// falsos.
TEST(CameraControls, RangeUsesTheMeasuredSpan) {
    using pci::camera::CameraProperty;
    using pci::camera::rangeFor;

    // Recorrido en unidades (0..255 de DirectShow): paso entero.
    const auto wide = rangeFor(CameraProperty::Brightness, 0.0, 255.0);
    EXPECT_DOUBLE_EQ(wide.min, 0.0);
    EXPECT_DOUBLE_EQ(wide.max, 255.0);
    EXPECT_DOUBLE_EQ(wide.step, 1.0);

    // Rango real medido en la camara de pruebas: exposicion de -11 a -3.
    const auto exposure = rangeFor(CameraProperty::Exposure, -11.0, -3.0);
    EXPECT_DOUBLE_EQ(exposure.min, -11.0);
    EXPECT_DOUBLE_EQ(exposure.max, -3.0);
    EXPECT_DOUBLE_EQ(exposure.step, 1.0);

    // Escala normalizada (0..1 de MSMF): hace falta paso decimal o el
    // deslizador solo tendria dos posiciones.
    const auto normalized = rangeFor(CameraProperty::Contrast, 0.0, 1.0);
    EXPECT_LT(normalized.step, 1.0);
    EXPECT_GT(normalized.step, 0.0);

    // Los extremos pueden llegar invertidos (se empuja primero al alto): se
    // ordenan solos.
    const auto swapped = rangeFor(CameraProperty::Gain, 128.0, 8.0);
    EXPECT_DOUBLE_EQ(swapped.min, 8.0);
    EXPECT_DOUBLE_EQ(swapped.max, 128.0);

    // Rango degenerado (la camara no dijo nada util): nunca un rango vacio que
    // deje el deslizador atascado.
    const auto degenerate = rangeFor(CameraProperty::Gain, 5.0, 5.0);
    EXPECT_GT(degenerate.max, degenerate.min);
    EXPECT_GT(degenerate.step, 0.0);

    // Las casillas son binarias pase lo que pase.
    const auto toggle = rangeFor(CameraProperty::AutoFocus, -50.0, 900.0);
    EXPECT_DOUBLE_EQ(toggle.min, 0.0);
    EXPECT_DOUBLE_EQ(toggle.max, 1.0);
    EXPECT_DOUBLE_EQ(toggle.step, 1.0);
}

// Arrastrar un deslizador encola decenas de valores por segundo; aplicarlos
// todos bloqueaba el hilo de captura (cada set() cuesta milisegundos) y de los
// intermedios no queda nada visible.
TEST(CameraControls, CoalescingKeepsOnlyTheLastValuePerProperty) {
    using pci::camera::CameraProperty;
    using pci::camera::CameraControlValue;

    std::vector<CameraControlValue> pending;
    // Simula un arrastre de 200 pasos sobre el brillo mientras la exposicion se
    // toca dos veces.
    for (int i = 0; i < 200; ++i) {
        pci::camera::coalesceControls(pending,
                                      {{CameraProperty::Brightness, static_cast<double>(i)}});
    }
    pci::camera::coalesceControls(pending, {{CameraProperty::Exposure, -7.0}});
    pci::camera::coalesceControls(pending, {{CameraProperty::Exposure, -5.0}});

    ASSERT_EQ(pending.size(), 2U) << "la cola debe tener una entrada por propiedad";
    EXPECT_EQ(pending[0].property, CameraProperty::Brightness);
    EXPECT_DOUBLE_EQ(pending[0].value, 199.0);  // el ultimo valor del arrastre
    EXPECT_EQ(pending[1].property, CameraProperty::Exposure);
    EXPECT_DOUBLE_EQ(pending[1].value, -5.0);

    // El orden de llegada de propiedades distintas se conserva.
    std::vector<CameraControlValue> other;
    pci::camera::coalesceControls(other, {{CameraProperty::AutoFocus, 1.0},
                                          {CameraProperty::Focus, 30.0}});
    ASSERT_EQ(other.size(), 2U);
    EXPECT_EQ(other[0].property, CameraProperty::AutoFocus);
    EXPECT_EQ(other[1].property, CameraProperty::Focus);
}

// --- Resoluciones (O2) ---

TEST(CameraControls, CandidateResolutionsAreSaneAndOrdered) {
    const auto candidates = pci::camera::candidateResolutions();
    ASSERT_FALSE(candidates.empty());
    for (const auto& resolution : candidates) {
        EXPECT_TRUE(resolution.valid());
        EXPECT_GE(resolution.width, resolution.height) << "todas son apaisadas";
    }
    // De menor a mayor: el desplegable se lee mejor y el sondeo acaba en la
    // mas grande, que es la mas cara de fijar.
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const int previous = candidates[i - 1].width * candidates[i - 1].height;
        const int current = candidates[i].width * candidates[i].height;
        EXPECT_LT(previous, current);
    }
    // Las clasicas de webcam tienen que estar.
    const auto has = [&candidates](int w, int h) {
        return std::any_of(candidates.begin(), candidates.end(),
                           [w, h](const pci::camera::CameraResolution& r) {
                               return r.width == w && r.height == h;
                           });
    };
    EXPECT_TRUE(has(640, 480));
    EXPECT_TRUE(has(1280, 720));
    EXPECT_TRUE(has(1920, 1080));
}

TEST(CameraControls, ResolutionValidityAndEquality) {
    using pci::camera::CameraResolution;
    EXPECT_FALSE(CameraResolution{}.valid());
    EXPECT_FALSE((CameraResolution{640, 0}).valid());
    EXPECT_FALSE((CameraResolution{-1, 480}).valid());
    EXPECT_TRUE((CameraResolution{640, 480}).valid());
    EXPECT_TRUE((CameraResolution{640, 480}) == (CameraResolution{640, 480}));
    EXPECT_FALSE((CameraResolution{640, 480}) == (CameraResolution{640, 481}));
}

// ---------------------------------------------------------------------------
// Perfil de medicion de la camara (C1)
// ---------------------------------------------------------------------------

namespace {

using pci::camera::CameraControlState;
using pci::camera::CameraControlValue;
using pci::camera::CameraProperty;
using pci::camera::measurementDefaults;

CameraControlState probed(CameraProperty property, bool supported, double value,
                          double min = 0.0, double max = 1.0) {
    CameraControlState state;
    state.property = property;
    state.supported = supported;
    state.value = value;
    state.min = min;
    state.max = max;
    return state;
}

std::optional<double> appliedTo(const std::vector<CameraControlValue>& values,
                                CameraProperty property) {
    for (const auto& value : values) {
        if (value.property == property) {
            return value.value;
        }
    }
    return std::nullopt;
}

// Lo que sondeo una camara tipica de portatil: exposicion ajustable en [-11,-3]
// y los dos interruptores presentes. Son los numeros medidos de verdad sobre la
// camara de esta maquina, no inventados.
std::vector<CameraControlState> typicalWebcam() {
    return {probed(CameraProperty::Brightness, false, 91.0, 91.0, 91.0),
            probed(CameraProperty::Contrast, false, 28.0, 28.0, 28.0),
            probed(CameraProperty::Gain, false, -1.0, -1.0, -1.0),
            probed(CameraProperty::Exposure, true, -7.0, -11.0, -3.0),
            probed(CameraProperty::AutoExposure, true, 0.0),
            probed(CameraProperty::Focus, true, 35.0, 0.0, 250.0),
            probed(CameraProperty::AutoFocus, true, 1.0)};
}

}  // namespace

TEST(MeasurementProfile, TurnsOffTheAutomaticsAndTouchesNothingElse) {
    // El perfil apaga los automaticos y ya. El valor de exposicion NO sale de
    // aqui: sale de `chooseExposure`, que lo elige midiendo.
    const auto defaults = measurementDefaults(typicalWebcam(), {});

    EXPECT_EQ(appliedTo(defaults, CameraProperty::AutoFocus), 0.0);
    // La exposicion automatica NO sale de aqui: apagarla puede costar mas de lo
    // que da, y eso solo se sabe con la imagen delante. Lo decide el barrido.
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::AutoExposure).has_value());

    // Y nada mas. Que aqui NO aparezca la exposicion es el punto del test, no un
    // olvido: la primera version la ponia, congelandola en el valor que la
    // camara reportaba, y sobre la camara real eso hizo la captura 3,7 veces
    // mas lenta. Con el automatico puesto, el valor reportado no es el que el
    // sensor usa.
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::Exposure).has_value());
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::Focus).has_value());
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::Brightness).has_value());
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::Contrast).has_value());
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::Gain).has_value());
}

TEST(MeasurementProfile, WhatTheOperatorSavedAlwaysWins) {
    // El perfil es para una camara sin configurar, no una opinion que se impone
    // en cada arranque. Si alguien dejo el autofoco puesto a proposito —una
    // estacion sin calibrar donde es comodo— no se le quita cada vez que abre.
    const std::vector<CameraControlValue> saved{{CameraProperty::AutoFocus, 1.0}};
    EXPECT_TRUE(measurementDefaults(typicalWebcam(), saved).empty());

    // Y lo contrario: haber tocado OTRA cosa no le quita el autofoco al perfil.
    // Respetar un ajuste no es rendirse con los demas.
    const std::vector<CameraControlValue> elsewhere{{CameraProperty::Brightness, 120.0}};
    EXPECT_EQ(appliedTo(measurementDefaults(typicalWebcam(), elsewhere),
                        CameraProperty::AutoFocus),
              0.0);
}

TEST(MeasurementProfile, ItDoesNotTurnOffAnAutomaticItCannotReplace) {
    // Medido, y contundente: escribir solo `auto_exposure = 0` dejo la camara
    // en 8,0 fps viniendo de 29,7. Al quitarle el automatico se cae a su valor
    // manual, que era el mas largo del rango. Apagar un automatico sin poder
    // elegir el valor no es neutral: es elegir el peor.
    std::vector<CameraControlState> noManualExposure = typicalWebcam();
    for (auto& state : noManualExposure) {
        if (state.property == CameraProperty::Exposure) {
            state.supported = false;  // la camara no deja fijarla
        }
    }
    const auto defaults = measurementDefaults(noManualExposure, {});
    EXPECT_FALSE(appliedTo(defaults, CameraProperty::AutoExposure).has_value());
    // El autofoco si se apaga: su manual esta disponible.
    EXPECT_EQ(appliedTo(defaults, CameraProperty::AutoFocus), 0.0);

    // Y el caso simetrico, que es el que prueba la regla de verdad: sin foco
    // manual, el autofoco tampoco se toca.
    std::vector<CameraControlState> noManualFocus = typicalWebcam();
    for (auto& state : noManualFocus) {
        if (state.property == CameraProperty::Focus) {
            state.supported = false;
        }
    }
    EXPECT_FALSE(appliedTo(measurementDefaults(noManualFocus, {}),
                           CameraProperty::AutoFocus)
                     .has_value())
        << "apago el autofoco sin tener con que sustituirlo";
}

TEST(MeasurementProfile, ACameraThatDoesNotLetYouChangeAnythingGetsNothing) {
    // Medido en la camara de esta maquina: ganancia, foco y autofoco salieron
    // NO ajustables (las tres escrituras del sondeo rechazadas). Escribirles de
    // todas formas no arregla nada y ensucia el log de cada arranque.
    std::vector<CameraControlState> deaf;
    for (const auto property : pci::camera::allCameraProperties()) {
        deaf.push_back(probed(property, false, 0.0));
    }
    EXPECT_TRUE(measurementDefaults(deaf, {}).empty());
}

TEST(ExposureChoice, PicksTheLongestExposureThatStillRunsAtFullSpeed) {
    // Los fps MEDIDOS en la camara de esta maquina, uno por uno. No es un
    // ejemplo inventado: es la forma real de la curva, y esa forma es la que
    // justifica la regla.
    //
    // Fijate en que NO baja poco a poco: es plana y luego se cae por un
    // acantilado. Con esa forma, "la mas corta" tiraria luz a cambio de nada.
    const std::vector<pci::camera::ExposureFpsSample> measured{
        {-3.0, 8.0},   {-4.0, 16.0},  {-5.0, 30.3},  {-6.0, 30.5},
        {-7.0, 30.2},  {-8.0, 30.3},  {-9.0, 30.3},  {-11.0, 30.2}};
    const auto chosen = pci::camera::chooseExposure(measured);
    ASSERT_TRUE(chosen.has_value());
    // −5 es el codo: toda la luz que no cuesta velocidad.
    EXPECT_DOUBLE_EQ(*chosen, -5.0);
}

TEST(ExposureChoice, NoiseInTheFpsMeasurementDoesNotPushItToTheShortestOne) {
    // Dos medidas de fps sobre una camara real nunca salen identicas. Si la
    // regla exigiera el maximo exacto, el ruido elegiria casi siempre la mas
    // corta y se tiraria luz por nada. Aqui la mejor es la mas corta por 0,3
    // fps de ruido, y aun asi gana la larga.
    const std::vector<pci::camera::ExposureFpsSample> noisy{
        {-5.0, 30.0}, {-6.0, 30.1}, {-7.0, 30.3}, {-8.0, 29.9}};
    const auto chosen = pci::camera::chooseExposure(noisy);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_DOUBLE_EQ(*chosen, -5.0);
}

TEST(ExposureChoice, ItGivesUpInsteadOfGuessingWhenThereIsNothingToDecideWith) {
    // Sin barrido no hay decision, y no tocar la exposicion es mejor que
    // elegirla a ciegas: a ciegas fue exactamente como se perdieron los 3,7x.
    EXPECT_FALSE(pci::camera::chooseExposure({}).has_value());
    EXPECT_FALSE(pci::camera::chooseExposure({{-5.0, 0.0}, {-7.0, 0.0}}).has_value());
}

TEST(ExposureChoice, TheCandidatesCoverTheMeasuredRangeFromLongToShort) {
    // De la mas larga a la mas corta, porque el barrido se puede parar en
    // cuanto encuentra el codo y el codo esta por el lado largo.
    const auto candidates = pci::camera::exposureCandidates(-11.0, -3.0);
    ASSERT_FALSE(candidates.empty());
    EXPECT_DOUBLE_EQ(candidates.front(), -3.0);
    EXPECT_DOUBLE_EQ(candidates.back(), -11.0);
    EXPECT_TRUE(std::is_sorted(candidates.begin(), candidates.end(), std::greater<>()));
    // Un rango degenerado no genera candidatos que no significan nada.
    EXPECT_TRUE(pci::camera::exposureCandidates(-1.0, -1.0).empty());
}

TEST(ExposureChoice, AFlatCameraKeepsTheLongestExposure) {
    // Si los fps no dependen de la exposicion —camara que ya va al maximo—, la
    // regla se queda con la mas larga, que es toda la luz disponible. Es el
    // caso de una camara industrial con obturador rapido, y no hay motivo para
    // acortarle nada.
    const std::vector<pci::camera::ExposureFpsSample> flat{
        {-3.0, 60.0}, {-5.0, 60.0}, {-9.0, 60.0}};
    const auto chosen = pci::camera::chooseExposure(flat);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_DOUBLE_EQ(*chosen, -3.0);
}

TEST(ProfileVerdict, ThreePercentMoreSpeedDoesNotPayForLosingTheImage) {
    // El caso medido en la camara de esta maquina, numero por numero: apagar el
    // automatico subio los fps de 29,7 a 30,5 —un 3 %— y hundio el contraste,
    // porque en automatico la camara gobierna tambien la GANANCIA y aqui la
    // ganancia no es ajustable, asi que ese refuerzo se pierde sin repuesto.
    //
    // Cambiar una imagen buena por un 3 % es un mal negocio. Hacerlo en
    // silencio es peor: el operador acabaria con una estacion que no ve la
    // pieza y sin saber por que.
    const auto verdict = pci::camera::judgeProfile(29.7, 46.0, 30.5, 12.0);
    EXPECT_FALSE(verdict.keep);
    EXPECT_FALSE(verdict.reason.empty());
    // Y dice que hacer, que es lo unico accionable: mas luz.
    EXPECT_NE(verdict.reason.find("luz"), std::string::npos) << verdict.reason;
}

TEST(ProfileVerdict, RealSpeedIsWorthADarkerImage) {
    // El otro caso medido: una camara que arranca con la exposicion larga da
    // 8,0 fps, y fijarla da 30,5 — 3,8x. Ahi la imagen mas oscura si se
    // compensa con luz, y renunciar a 3,8x por no querer encender una lampara
    // seria la decision equivocada.
    const auto verdict = pci::camera::judgeProfile(8.0, 46.0, 30.5, 12.0);
    EXPECT_TRUE(verdict.keep) << verdict.reason;
}

TEST(ProfileVerdict, KeepingTheImageIsEnoughOnItsOwn) {
    // Si el contraste aguanta, no hace falta justificar nada mas: la
    // repetibilidad es la razon de ser del perfil y sale gratis.
    EXPECT_TRUE(pci::camera::judgeProfile(29.7, 46.0, 29.8, 44.0).keep);
}

TEST(ProfileVerdict, WithoutAReferenceItDoesNotSecondGuessItself) {
    // Sin medida previa no hay comparacion posible, y deshacer lo hecho por si
    // acaso seria tan arbitrario como mantenerlo por si acaso.
    EXPECT_TRUE(pci::camera::judgeProfile(0.0, 46.0, 30.0, 5.0).keep);
    EXPECT_TRUE(pci::camera::judgeProfile(29.7, 0.0, 30.0, 5.0).keep);
}

// ---------------------------------------------------------------------------
// El aviso de «escala calibrada + automatico encendido» (C4)
// ---------------------------------------------------------------------------

TEST(AutomaticsWarning, ItOnlyFiresOnTheOneQuadrantThatIsActuallyDangerous) {
    // Los cuatro cuadrantes, y el aviso en UNO. Que se calle en los otros tres
    // no es tacaneria: sin calibrar, el autofoco es una comodidad legitima
    // —las medidas van en pixeles y nadie ha prometido milimetros—, y un aviso
    // que salta siempre se aprende a ignorar, con lo que tampoco serviria donde
    // de verdad importa.
    using pci::camera::automaticsWarning;

    EXPECT_TRUE(automaticsWarning(false, false, false).empty());
    EXPECT_TRUE(automaticsWarning(false, true, true).empty())
        << "sin calibrar no hay milimetros que estropear";
    EXPECT_TRUE(automaticsWarning(true, false, false).empty())
        << "calibrado y sin automaticos es justo el estado bueno";
    EXPECT_FALSE(automaticsWarning(true, true, false).empty());
    EXPECT_FALSE(automaticsWarning(true, false, true).empty());
    EXPECT_FALSE(automaticsWarning(true, true, true).empty());
}

TEST(AutomaticsWarning, ItNamesWhichAutomaticBecauseTheDamageIsDifferent) {
    using pci::camera::automaticsWarning;

    // El autofoco cambia la MAGNIFICACION: todas las cotas a la vez y
    // proporcionalmente, que es la forma de estar equivocado que no se nota.
    const std::string focus = automaticsWarning(true, false, true);
    EXPECT_NE(focus.find("enfoque"), std::string::npos) << focus;
    EXPECT_NE(focus.find("TODAS"), std::string::npos) << focus;

    // La exposicion mueve el umbral aparente del borde: la pieza sale mas
    // gorda o mas fina. Es otro dano y se dice con otras palabras.
    const std::string exposure = automaticsWarning(true, true, false);
    EXPECT_NE(exposure.find("exposicion"), std::string::npos) << exposure;
    EXPECT_NE(exposure.find("borde"), std::string::npos) << exposure;

    // Con los dos encendidos manda el peor, que es el enfoque.
    const std::string both = automaticsWarning(true, true, true);
    EXPECT_NE(both.find("enfoque"), std::string::npos) << both;
}
