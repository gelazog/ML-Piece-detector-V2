// ¿CUÁNTO DEFORMA LA LENTE UNA MEDIDA?
//
// ARQUITECTURA lleva tiempo diciendo que corregir la distorsión de la lente es
// «la mejora de exactitud más seria que queda». Puede que sea verdad y puede que
// no: es una afirmación sin ninguna cifra detrás, y antes de construir un
// asistente de calibración con su tablero de ajedrez conviene saber qué se gana.
//
// Esto no calibra nada. Mide el problema.
//
// La escena se dibuja EN EL PLANO IDEAL y se rasteriza como la vería una cámara
// con una lente conocida: para cada píxel de la imagen que sale de la cámara se
// calcula de qué punto del plano viene y se pregunta si ese punto está dentro de
// la pieza. Así el borde es exacto y no hay remuestreo que confunda la medida
// con el error de interpolación.
//
// La misma pieza se pone en varios sitios del encuadre y se mide con el pipeline
// de la aplicación. Si la lente importa, la misma pieza medirá distinto en el
// centro que en una esquina — y ese desacuerdo es exactamente el error que un
// operador no puede ver ni explicar.

#include <gtest/gtest.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "vision/lens_calibration.h"
#include "vision/pipeline.h"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 960;

// Una lente de gama de consumo: k1 = -0,25 es barril moderado, del orden de lo
// que trae una webcam corriente. No es un caso extremo elegido para que salgan
// números grandes.
cv::Mat cameraMatrix() {
    cv::Mat k = cv::Mat::eye(3, 3, CV_64F);
    k.at<double>(0, 0) = 900.0;
    k.at<double>(1, 1) = 900.0;
    k.at<double>(0, 2) = kWidth / 2.0;
    k.at<double>(1, 2) = kHeight / 2.0;
    return k;
}

cv::Mat distortion(double k1) {
    return (cv::Mat_<double>(5, 1) << k1, 0.08, 0.0, 0.0, 0.0);
}

// La imagen tal como la vería la cámara: para cada píxel de salida se busca de
// qué punto del plano ideal viene, y se pregunta si cae dentro del disco.
cv::Mat renderThroughLens(const cv::Point2d& centre, double radius, const cv::Mat& k,
                          const cv::Mat& d) {
    std::vector<cv::Point2f> grid;
    grid.reserve(static_cast<std::size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            grid.emplace_back(static_cast<float>(x), static_cast<float>(y));
        }
    }
    std::vector<cv::Point2f> ideal;
    cv::undistortPoints(grid, ideal, k, d, cv::noArray(), k);

    cv::Mat scene(kHeight, kWidth, CV_8UC1, cv::Scalar(30));
    std::size_t i = 0;
    for (int y = 0; y < kHeight; ++y) {
        auto* row = scene.ptr<unsigned char>(y);
        for (int x = 0; x < kWidth; ++x, ++i) {
            const double dx = ideal[i].x - centre.x;
            const double dy = ideal[i].y - centre.y;
            if (dx * dx + dy * dy <= radius * radius) {
                row[x] = 220;
            }
        }
    }
    return scene;
}

// Lo mismo sin lente, para tener la referencia de lo que mide el pipeline
// cuando no hay nada que corregir.
cv::Mat renderIdeal(const cv::Point2d& centre, double radius) {
    cv::Mat scene(kHeight, kWidth, CV_8UC1, cv::Scalar(30));
    // Sin antialiasing: LINE_AA agranda el disco ~1,4 px por lado y ese sesgo
    // constante se confundiría con el efecto de la lente. Es la misma decisión,
    // y por el mismo motivo medido, que en `test_calibration_images.cpp`.
    cv::circle(scene, cv::Point(cvRound(centre.x), cvRound(centre.y)),
               static_cast<int>(std::lround(radius)), cv::Scalar(220), cv::FILLED,
               cv::LINE_8);
    return scene;
}

// Diámetro equivalente que mide la aplicación, en píxeles. 0 si no ve la pieza.
double measuredDiameter(const cv::Mat& scene) {
    const auto analysis = pci::vision::analyzeFrame(scene, {});
    if (!analysis.isOk()) {
        return 0.0;
    }
    return 2.0 * std::sqrt(analysis.value().contour.area / CV_PI);
}

}  // namespace

// LA CIFRA QUE DECIDE: la misma pieza, en distintos sitios del encuadre.
TEST(LensDistortion, TheSamePieceMeasuresDifferentlyDependingOnWhereItSits) {
    const cv::Mat k = cameraMatrix();
    const cv::Mat d = distortion(-0.25);
    constexpr double kRadius = 90.0;

    // Del centro hacia la esquina inferior derecha.
    const cv::Point2d places[] = {
        {640.0, 480.0}, {800.0, 560.0}, {960.0, 640.0}, {1080.0, 740.0}, {1150.0, 820.0},
    };

    double worstError = 0.0;
    double centreDiameter = 0.0;
    std::printf("  [lente] radio real %.0f px (diámetro %.0f)\n", kRadius, 2.0 * kRadius);
    for (const auto& place : places) {
        const double ideal = measuredDiameter(renderIdeal(place, kRadius));
        const double through = measuredDiameter(renderThroughLens(place, kRadius, k, d));
        if (ideal <= 0.0 || through <= 0.0) {
            continue;
        }
        const double radial = std::hypot(place.x - kWidth / 2.0, place.y - kHeight / 2.0);
        const double error = 100.0 * (through - ideal) / ideal;
        if (place.x == 640.0) {
            centreDiameter = through;
        }
        worstError = std::max(worstError, std::abs(error));
        std::printf("  [lente] a %4.0f px del centro: sin lente %6.2f, con lente %6.2f "
                    "(%+.2f %%)\n",
                    radial, ideal, through, error);
    }

    EXPECT_GT(centreDiameter, 0.0) << "no se detectó la pieza ni en el centro";
    std::printf("  [lente] peor error por posición: %.2f %%\n", worstError);

    // No se afirma un umbral: lo que se quiere de esta prueba es la CIFRA, y que
    // quede registrada para poder decidir con ella. Lo único que se exige es que
    // el montaje funcione, o el número no significaría nada.
    EXPECT_GT(worstError, 0.0) << "la lente no cambió ninguna medida: el montaje no está "
                                  "aplicando distorsión";
}

// Y cuánto mueve la lente un punto, que es la cifra que se le puede enseñar a un
// operador: «tu lente desplaza hasta N píxeles en las esquinas» se entiende, y
// «k1 = -0,2478» no.
TEST(LensDistortion, HowFarTheLensMovesAPoint) {
    const cv::Mat k = cameraMatrix();
    for (const double k1 : {-0.10, -0.25, -0.40}) {
        const cv::Mat d = distortion(k1);
        std::vector<cv::Point2f> corners = {{0.0F, 0.0F},
                                            {static_cast<float>(kWidth - 1), 0.0F},
                                            {0.0F, static_cast<float>(kHeight - 1)},
                                            {static_cast<float>(kWidth - 1),
                                             static_cast<float>(kHeight - 1)}};
        std::vector<cv::Point2f> ideal;
        cv::undistortPoints(corners, ideal, k, d, cv::noArray(), k);
        double worst = 0.0;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            worst = std::max(worst, cv::norm(cv::Point2f(corners[i] - ideal[i])));
        }
        std::printf("  [lente] con k1 = %.2f, la esquina se desplaza %.1f px\n", k1, worst);
        EXPECT_GT(worst, 0.0);
    }
}

// ---------------------------------------------------------------------------
// Calibrar la lente y comprobar que el error se derrumba
// ---------------------------------------------------------------------------

namespace {

// Un tablero de ajedrez visto a través de la lente, con los cuadros CURVADOS.
//
// Cada lado de cada cuadro se subdivide antes de proyectar: si se proyectaran
// solo las cuatro esquinas y se unieran con rectas, la imagen tendría las
// esquinas en el sitio correcto y los bordes rectos, o sea un tablero al que le
// falta justo la deformación que se quiere medir.
cv::Mat renderBoardThroughLens(const pci::vision::BoardSpec& spec, const cv::Vec3d& rvec,
                               const cv::Vec3d& tvec, const cv::Mat& k, const cv::Mat& d) {
    cv::Mat image(kHeight, kWidth, CV_8UC1, cv::Scalar(235));
    const auto side = static_cast<float>(spec.squareMm);
    constexpr int kSteps = 6;  // puntos por lado de cuadro

    for (int row = 0; row <= spec.innerRows; ++row) {
        for (int col = 0; col <= spec.innerCols; ++col) {
            if ((row + col) % 2 != 0) {
                continue;  // solo los cuadros oscuros
            }
            const float x0 = static_cast<float>(col - 1) * side;
            const float y0 = static_cast<float>(row - 1) * side;
            std::vector<cv::Point3f> outline;
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0 + side * i / kSteps, y0, 0.0F);
            }
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0 + side, y0 + side * i / kSteps, 0.0F);
            }
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0 + side - side * i / kSteps, y0 + side, 0.0F);
            }
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0, y0 + side - side * i / kSteps, 0.0F);
            }
            std::vector<cv::Point2f> projected;
            cv::projectPoints(outline, rvec, tvec, k, d, projected);
            std::vector<cv::Point> polygon;
            polygon.reserve(projected.size());
            for (const auto& p : projected) {
                polygon.emplace_back(cvRound(p.x), cvRound(p.y));
            }
            cv::fillConvexPoly(image, polygon, cv::Scalar(25), cv::LINE_8);
        }
    }
    return image;
}

// Doce tomas del tablero: distintas posiciones del encuadre, distintas
// inclinaciones y distintas distancias.
//
// La variedad NO es adorno. Con doce tomas del tablero en el mismo sitio, el
// ajuste solo ve una zona del encuadre y el modelo que sale describe esa zona.
// La distorsión es justo lo que cambia de una zona a otra.
std::vector<cv::Vec3d> boardRotations() {
    return {{0.05, 0.02, 0.0},   {0.35, -0.20, 0.05}, {-0.30, 0.25, -0.05},
            {0.20, 0.30, 0.10},  {-0.25, -0.30, 0.0}, {0.40, 0.05, -0.15},
            {-0.10, 0.40, 0.08}, {0.15, -0.35, 0.12}, {-0.35, -0.10, -0.10},
            {0.28, 0.18, 0.20},  {-0.18, 0.32, 0.15}, {0.10, -0.10, -0.20}};
}

// EL TABLERO TIENE QUE LLEGAR A LAS ESQUINAS, y esto no es un detalle del
// montaje: es la lección de producto que hay que trasladar al operador.
//
// La primera versión de esta prueba dejaba el tablero rondando el centro. La
// calibración salía bien —fx 901,8 de 900— pero k1 salía -0,2329 en vez de
// -0,2500, y con ese 7 % de error el residuo en el borde se quedaba en 3,4 %.
// El motivo es que la distorsión radial CRECE con el radio: si no se le enseña
// nunca el borde, el ajuste lo extrapola.
//
// Con las tomas repartidas por los cuatro rincones, ese mismo residuo baja al
// entorno del uno por ciento. Quien haga el asistente de calibración tiene que
// pedir esto explícitamente, y comprobarlo.
std::vector<cv::Vec3d> boardTranslations() {
    // Puestas para que el centro del tablero caiga en las cuatro esquinas, en
    // los cuatro lados y en el centro del encuadre, a distintas distancias.
    return {{80.0, 70.0, 400.0},    {-240.0, 70.0, 400.0},  {80.0, -170.0, 400.0},
            {-240.0, -170.0, 400.0}, {-80.0, 70.0, 400.0},  {-80.0, -170.0, 400.0},
            {80.0, -50.0, 400.0},   {-240.0, -50.0, 400.0}, {-80.0, -50.0, 400.0},
            {40.0, 30.0, 460.0},    {-200.0, -130.0, 460.0}, {-80.0, -50.0, 340.0}};
}

std::vector<pci::vision::BoardView> shootTheBoard(const pci::vision::BoardSpec& spec,
                                                  const cv::Mat& k, const cv::Mat& d) {
    std::vector<pci::vision::BoardView> views;
    const auto rotations = boardRotations();
    const auto translations = boardTranslations();
    for (std::size_t i = 0; i < rotations.size(); ++i) {
        const cv::Mat shot = renderBoardThroughLens(spec, rotations[i], translations[i], k, d);
        if (auto view = pci::vision::findBoard(shot, spec); view.has_value()) {
            views.push_back(*view);
        }
    }
    return views;
}

}  // namespace

TEST(LensDistortion, CalibratingFromBoardsRecoversTheLens) {
    const pci::vision::BoardSpec spec;  // 9x6 esquinas interiores, cuadro de 20 mm
    const cv::Mat trueK = cameraMatrix();
    const cv::Mat trueD = distortion(-0.25);

    const auto views = shootTheBoard(spec, trueK, trueD);
    std::printf("  [lente] tableros encontrados: %d de 12\n", static_cast<int>(views.size()));
    ASSERT_GE(views.size(), static_cast<std::size_t>(pci::vision::kMinimumViews))
        << "no se detectaron bastantes tableros: la prueba no llega a calibrar nada";

    const auto calibration = pci::vision::calibrateLens(views, spec);
    ASSERT_TRUE(calibration.isOk()) << calibration.error().message;
    const auto& model = calibration.value();
    EXPECT_TRUE(model.isValid());

    const double fx = model.cameraMatrix.at<double>(0, 0);
    const double cx = model.cameraMatrix.at<double>(0, 2);
    const double k1 = model.distortion.at<double>(0);
    std::printf("  [lente] verdad  fx 900,0  cx 640,0  k1 -0,2500\n");
    std::printf("  [lente] hallado fx %.1f  cx %.1f  k1 %.4f  (reproyeccion %.3f px)\n", fx,
                cx, k1, model.reprojectionError);

    EXPECT_NEAR(fx, 900.0, 15.0) << "la distancia focal recuperada se aleja de la real";
    EXPECT_NEAR(cx, kWidth / 2.0, 15.0) << "el centro óptico recuperado se aleja del real";
    EXPECT_NEAR(k1, -0.25, 0.03) << "el coeficiente de barril recuperado no es el real";
    EXPECT_LT(model.reprojectionError, pci::vision::kMaxUsableReprojectionError);

    std::printf("  [lente] desplazamiento peor del modelo hallado: %.1f px\n",
                pci::vision::worstDisplacementPx(model));
    EXPECT_FALSE(pci::vision::distortionIsNegligible(model));
}

// LA PRUEBA QUE JUSTIFICA EL MÓDULO ENTERO: con la lente corregida, la misma
// pieza mide lo mismo esté donde esté del encuadre.
TEST(LensDistortion, OnceCorrectedThePieceMeasuresTheSameEverywhere) {
    const pci::vision::BoardSpec spec;
    const cv::Mat trueK = cameraMatrix();
    const cv::Mat trueD = distortion(-0.25);

    const auto views = shootTheBoard(spec, trueK, trueD);
    ASSERT_GE(views.size(), static_cast<std::size_t>(pci::vision::kMinimumViews));
    const auto calibration = pci::vision::calibrateLens(views, spec);
    ASSERT_TRUE(calibration.isOk()) << calibration.error().message;

    const pci::vision::LensCorrector corrector(calibration.value());
    ASSERT_TRUE(corrector.isReady());

    constexpr double kRadius = 90.0;
    const cv::Point2d places[] = {{640.0, 480.0}, {960.0, 640.0}, {1080.0, 740.0}};

    double worstRaw = 0.0;
    double worstFixed = 0.0;
    const double reference = measuredDiameter(renderIdeal({640.0, 480.0}, kRadius));
    ASSERT_GT(reference, 0.0);
    for (const auto& place : places) {
        const cv::Mat seen = renderThroughLens(place, kRadius, trueK, trueD);
        const double raw = measuredDiameter(seen);
        const double fixed = measuredDiameter(corrector.apply(seen));
        const double radial = std::hypot(place.x - kWidth / 2.0, place.y - kHeight / 2.0);
        const double rawError = 100.0 * (raw - reference) / reference;
        const double fixedError = 100.0 * (fixed - reference) / reference;
        worstRaw = std::max(worstRaw, std::abs(rawError));
        worstFixed = std::max(worstFixed, std::abs(fixedError));
        std::printf("  [lente] a %4.0f px: sin corregir %+6.2f %%, corregido %+6.2f %%\n",
                    radial, rawError, fixedError);
    }

    std::printf("  [lente] peor error: %.2f %% sin corregir, %.2f %% corregido\n", worstRaw,
                worstFixed);
    EXPECT_GT(worstRaw, 10.0) << "sin corregir el error ya era pequeño: entonces no hay "
                                 "nada que justifique este módulo";
    EXPECT_LT(worstFixed, 2.0)
        << "corregir la lente no arregla el desacuerdo por posición, que es lo único "
           "para lo que existe";
    EXPECT_LT(worstFixed, worstRaw / 4.0);
}

// El modelo se guarda y se recupera TAL CUAL: si al releerlo cambiara aunque
// fuera poco, las medidas de mañana no serían las de hoy.
TEST(LensDistortion, TheModelSurvivesBeingSavedAndRead) {
    pci::vision::LensCalibration model;
    model.cameraMatrix = cameraMatrix();
    model.distortion = distortion(-0.25);
    model.imageSize = {kWidth, kHeight};
    model.reprojectionError = 0.234567891;
    model.views = 12;

    const std::string text = pci::vision::serializeCalibration(model);
    ASSERT_FALSE(text.empty());
    const auto back = pci::vision::parseCalibration(text);
    ASSERT_TRUE(back.has_value());

    EXPECT_EQ(back->imageSize, model.imageSize);
    EXPECT_EQ(back->views, model.views);
    EXPECT_DOUBLE_EQ(back->reprojectionError, model.reprojectionError);
    for (int i = 0; i < 9; ++i) {
        EXPECT_DOUBLE_EQ(back->cameraMatrix.at<double>(i / 3, i % 3),
                         model.cameraMatrix.at<double>(i / 3, i % 3));
    }
    ASSERT_EQ(back->distortion.total(), model.distortion.total());
    for (std::size_t i = 0; i < model.distortion.total(); ++i) {
        EXPECT_DOUBLE_EQ(back->distortion.at<double>(static_cast<int>(i)),
                         model.distortion.at<double>(static_cast<int>(i)));
    }

    // Y basura no se acepta calladamente: un ajuste corrupto tiene que costar la
    // corrección, no una aplicación midiendo con un modelo inventado.
    EXPECT_FALSE(pci::vision::parseCalibration("").has_value());
    EXPECT_FALSE(pci::vision::parseCalibration("esto no es un modelo").has_value());
    EXPECT_FALSE(pci::vision::parseCalibration("0 0 0.1 8").has_value());
}

// Menos tomas de las que hacen falta se rechaza, y se dice por qué.
TEST(LensDistortion, TooFewViewsAreRefusedWithAReason) {
    const pci::vision::BoardSpec spec;
    std::vector<pci::vision::BoardView> three(3);
    for (auto& view : three) {
        view.imageSize = {kWidth, kHeight};
        view.corners.resize(static_cast<std::size_t>(spec.cornerCount()));
    }
    const auto refused = pci::vision::calibrateLens(three, spec);
    ASSERT_FALSE(refused.isOk());
    std::printf("  [lente] con 3 tomas dice: %s\n", refused.error().message.c_str());
    EXPECT_NE(refused.error().message.find("8"), std::string::npos)
        << "no dice cuántas tomas hacen falta";

    // Y un tablero mal descrito también: contar cuadros en vez de esquinas
    // interiores es el error más común al calibrar.
    pci::vision::BoardSpec broken;
    broken.innerCols = 2;
    std::vector<pci::vision::BoardView> plenty(10);
    const auto rejected = pci::vision::calibrateLens(plenty, broken);
    ASSERT_FALSE(rejected.isOk());
    EXPECT_NE(rejected.error().message.find("INTERIORES"), std::string::npos);
}

// LA TRAMPA DE LA CALIBRACIÓN, y por qué hay que mirar la cobertura aparte.
//
// Con las tomas rondando el centro, el ajuste sale con un aspecto estupendo: la
// distancia focal se recupera casi clavada y el error de reproyección es igual
// de bueno que con las tomas bien repartidas. Y sin embargo k1 sale mal, porque
// la distorsión radial CRECE con el radio y al ajuste no se le ha enseñado nunca
// el borde.
//
// Lo peligroso es que NO SE QUEJA. El error de reproyección mide lo bien que el
// modelo explica las tomas que se le dieron, así que un modelo que solo ha visto
// el centro lo explica perfectamente. Un operador miraría esa cifra, la vería
// buena, y se llevaría una corrección que falla justo donde más falta hacía.
TEST(LensDistortion, GoodReprojectionDoesNotMeanGoodCoverage) {
    const pci::vision::BoardSpec spec;
    const cv::Mat trueK = cameraMatrix();
    const cv::Mat trueD = distortion(-0.25);

    // Las mismas doce tomas, pero apiñadas alrededor del centro.
    const std::vector<cv::Vec3d> huddled = {
        {-80.0, -50.0, 400.0},  {-100.0, -60.0, 430.0}, {-70.0, -40.0, 420.0},
        {-90.0, -55.0, 380.0},  {-85.0, -45.0, 450.0},  {-75.0, -65.0, 410.0},
        {-95.0, -50.0, 395.0},  {-80.0, -35.0, 440.0},  {-105.0, -55.0, 405.0},
        {-65.0, -60.0, 425.0},  {-88.0, -48.0, 415.0},  {-78.0, -52.0, 435.0}};
    const auto rotations = boardRotations();

    std::vector<pci::vision::BoardView> views;
    for (std::size_t i = 0; i < huddled.size(); ++i) {
        const cv::Mat shot =
            renderBoardThroughLens(spec, rotations[i], huddled[i], trueK, trueD);
        if (auto view = pci::vision::findBoard(shot, spec); view.has_value()) {
            views.push_back(*view);
        }
    }
    ASSERT_GE(views.size(), static_cast<std::size_t>(pci::vision::kMinimumViews));

    // El ajuste se hace AQUÍ a mano, con OpenCV directamente, porque la API de
    // este proyecto lo rechaza — que es justo lo que se quiere comprobar. Lo que
    // se demuestra después es que ese rechazo protege de algo real.
    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints;
    for (const auto& view : views) {
        std::vector<cv::Point3f> board;
        for (int row = 0; row < spec.innerRows; ++row) {
            for (int col = 0; col < spec.innerCols; ++col) {
                board.emplace_back(static_cast<float>(col * spec.squareMm),
                                   static_cast<float>(row * spec.squareMm), 0.0F);
            }
        }
        objectPoints.push_back(board);
        imagePoints.push_back(view.corners);
    }
    pci::vision::LensCalibration huddledModel;
    huddledModel.imageSize = views.front().imageSize;
    std::vector<cv::Mat> rotationsOut;
    std::vector<cv::Mat> translationsOut;
    huddledModel.reprojectionError = cv::calibrateCamera(
        objectPoints, imagePoints, huddledModel.imageSize, huddledModel.cameraMatrix,
        huddledModel.distortion, rotationsOut, translationsOut);
    huddledModel.views = static_cast<int>(views.size());

    const auto coverage = pci::vision::coverageOf(views);
    std::printf("  [lente] apiñadas: reproyeccion %.3f px, esquinas cubiertas %d/4, "
                "zonas %d/9\n",
                huddledModel.reprojectionError, coverage.cornersTouched,
                coverage.cellsTouched);
    std::printf("  [lente] aviso: %s\n", coverage.advice().c_str());

    // 1) El error de reproyección NO delata el problema: es tan bueno como el de
    //    una calibración bien repartida.
    EXPECT_LT(huddledModel.reprojectionError, pci::vision::kMaxUsableReprojectionError)
        << "esta prueba supone que el ajuste apiñado parece bueno; si ya se quejaba "
           "solo, la cobertura no haria falta";

    // 1b) Y LA API LO RECHAZA, que es la consecuencia de todo esto: una
    //     calibración mal cubierta deja las medidas PEOR que no corregir nada,
    //     así que no se entrega con un aviso — no se entrega.
    const auto refused = pci::vision::calibrateLens(views, spec);
    ASSERT_FALSE(refused.isOk())
        << "se devuelve una calibración mal cubierta: quien la use medirá peor que sin "
           "corregir nada, y con una cifra de calidad tranquilizadora delante";
    EXPECT_NE(refused.error().message.find("esquinas"), std::string::npos);

    // 2) La cobertura sí lo delata, y dice qué falta.
    EXPECT_FALSE(coverage.goodEnough())
        << "doce tomas alrededor del centro se dan por bien repartidas: entonces el "
           "aviso no protege de nada";
    EXPECT_FALSE(coverage.advice().empty());
    EXPECT_NE(coverage.advice().find("esquinas"), std::string::npos)
        << "el aviso no dice lo unico que hay que hacer: llevar el tablero a las "
           "esquinas";

    // 3) Y la corrección que sale es peor de verdad. Es la consecuencia que
    //    justifica el aviso: sin esto, «cobertura» seria una regla inventada.
    const pci::vision::LensCorrector corrector(huddledModel);
    ASSERT_TRUE(corrector.isReady());
    constexpr double kRadius = 90.0;
    const cv::Point2d edge{1080.0, 740.0};
    const double reference = measuredDiameter(renderIdeal({640.0, 480.0}, kRadius));
    const double fixed =
        measuredDiameter(corrector.apply(renderThroughLens(edge, kRadius, trueK, trueD)));
    const double residue = 100.0 * std::abs(fixed - reference) / reference;
    std::printf("  [lente] residuo en el borde con las tomas apiñadas: %.2f %%\n", residue);
    // Y no solo peor: PEOR QUE NO CORREGIR. Sin corregir, ese mismo borde falla
    // un 14 %. Es la cifra que convierte el aviso en un rechazo.
    EXPECT_GT(residue, 14.0)
        << "la calibración apiñada corrige mejor que no hacer nada: entonces rechazarla "
           "es excesivo y bastaría con avisar";
    EXPECT_GT(residue, 1.0)
        << "la calibracion apiñada corrige igual de bien que la repartida: entonces "
           "repartir las tomas no importaba y este aviso sobra";
}

// Tomas bien repartidas: la cobertura no protesta.
TEST(LensDistortion, SpreadOutViewsPassTheCoverageCheck) {
    const pci::vision::BoardSpec spec;
    const auto views = shootTheBoard(spec, cameraMatrix(), distortion(-0.25));
    const auto coverage = pci::vision::coverageOf(views);
    std::printf("  [lente] repartidas: esquinas %d/4, zonas %d/9\n", coverage.cornersTouched,
                coverage.cellsTouched);
    EXPECT_TRUE(coverage.goodEnough()) << coverage.advice();
    EXPECT_TRUE(coverage.advice().empty());
    EXPECT_EQ(coverage.cornersTouched, 4);
}
