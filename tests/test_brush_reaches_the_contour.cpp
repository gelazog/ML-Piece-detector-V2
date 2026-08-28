// LO QUE EL PINCEL MARCA COMO FONDO TIENE QUE SALIR DEL CONTORNO.
//
// Queja del taller: «si ya pasé los pinceles, se sigue remarcando la zona que le
// dije que no».
//
// Leyendo el código no se reproduce: la corrección se guarda en
// `PipelineConfig::forceBackground`, se aplica DESPUÉS de segmentar —así que la
// recuperación de brillos no la puede deshacer— y solo se descarta si cambia el
// tamaño del frame. Todo correcto sobre el papel.
//
// Por eso esta prueba: recorre el camino entero con números, y si pasa deja
// acotado que el fallo no está aquí sino en cómo el lienzo construye la máscara
// —coordenadas, escala del zoom— que es otro sitio y otro arreglo.
//
// Leer código y concluir «esto funciona» no es una comprobación. Esto sí.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>

#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;

namespace {

// Una pieza con una SOMBRA pegada por un lado, que es el caso del taller: la
// sombra se pega al borde y entra en la silueta.
cv::Mat pieceWithAShadow() {
    cv::Mat frame(300, 400, CV_8UC1, cv::Scalar(245));
    // La sombra, a la derecha de la pieza y tocándola.
    cv::rectangle(frame, cv::Rect(200, 110, 70, 80), cv::Scalar(120), cv::FILLED);
    // La pieza.
    cv::rectangle(frame, cv::Rect(120, 100, 80, 100), cv::Scalar(30), cv::FILLED);
    return frame;
}

double widthOfTheBiggest(const cv::Mat& frame, const vision::PipelineConfig& config) {
    auto all = vision::analyzeFrames(frame, config);
    if (!all.isOk() || all.value().empty()) {
        return 0.0;
    }
    const auto& piece = all.value()[vision::largestPieceIndex(all.value())];
    return std::max(piece.contour.rotatedRect.size.width,
                    piece.contour.rotatedRect.size.height);
}

}  // namespace

TEST(BrushReachesTheContour, PaintingBackgroundOverAShadowTakesItOutOfThePiece) {
    const cv::Mat frame = pieceWithAShadow();
    vision::PipelineConfig config;

    // Sin corregir: la sombra entra y la pieza sale más ancha de lo que es.
    const double withShadow = widthOfTheBiggest(frame, config);
    std::printf("  [pincel] sin corregir, la pieza mide %.0f px\n", withShadow);
    ASSERT_GT(withShadow, 120.0)
        << "la escena no reproduce el problema: la sombra no está entrando en la "
           "silueta, así que corregirla no probaría nada";

    // Y ahora la pincelada de «quitar de la pieza» sobre la sombra.
    cv::Mat forceBackground(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::rectangle(forceBackground, cv::Rect(200, 105, 80, 90), cv::Scalar(255), cv::FILLED);
    config.forceBackground = forceBackground;

    const double corrected = widthOfTheBiggest(frame, config);
    std::printf("  [pincel] con la sombra marcada como fondo, mide %.0f px\n", corrected);
    EXPECT_LT(corrected, withShadow - 20.0)
        << "lo marcado como fondo sigue dentro del contorno: la pincelada no llega al "
           "análisis, que es exactamente lo que se ve como «sigue remarcando la zona "
           "que le dije que no»";
    // Y la pieza de verdad sigue entera: quitar la sombra no puede comerse parte
    // de lo que sí es pieza.
    EXPECT_GT(corrected, 90.0)
        << "la corrección se ha llevado por delante parte de la pieza";
}

TEST(BrushReachesTheContour, PaintingPieceOverAGapPutsItBackIn) {
    // El sentido contrario, que es la otra mitad del pincel: lo que el umbral
    // dejó fuera y el operador marca como pieza tiene que volver a entrar.
    cv::Mat frame(300, 400, CV_8UC1, cv::Scalar(245));
    cv::rectangle(frame, cv::Rect(120, 100, 80, 100), cv::Scalar(30), cv::FILLED);
    // Un mordisco claro en el borde: un reflejo que sube al nivel del fondo.
    cv::rectangle(frame, cv::Rect(180, 130, 20, 40), cv::Scalar(243), cv::FILLED);

    vision::PipelineConfig config;
    auto before = vision::analyzeFrames(frame, config);
    ASSERT_TRUE(before.isOk());
    const double bitten = before.value()[0].contour.area;

    cv::Mat forcePiece(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::rectangle(forcePiece, cv::Rect(178, 128, 24, 44), cv::Scalar(255), cv::FILLED);
    config.forcePiece = forcePiece;

    auto after = vision::analyzeFrames(frame, config);
    ASSERT_TRUE(after.isOk());
    const double repaired = after.value()[0].contour.area;
    std::printf("  [pincel] el mordisco: área %.0f -> %.0f px²\n", bitten, repaired);
    EXPECT_GT(repaired, bitten)
        << "marcar como pieza no devuelve el trozo que el umbral se había comido";
}

TEST(BrushReachesTheContour, ACorrectionOfAnotherSizeIsIgnoredAndNotAppliedShifted) {
    // La red de seguridad: una corrección hecha sobre una imagen de otro tamaño
    // no se puede aplicar, y aplicarla desplazada sería peor que no aplicarla —
    // borraría un trozo cualquiera de la pieza.
    const cv::Mat frame = pieceWithAShadow();
    vision::PipelineConfig config;
    const double untouched = widthOfTheBiggest(frame, config);

    config.forceBackground = cv::Mat(cv::Size(200, 150), CV_8UC1, cv::Scalar(255));
    const double withStale = widthOfTheBiggest(frame, config);
    std::printf("  [pincel] corrección de otro tamaño: %.0f -> %.0f px (sin cambio)\n",
                untouched, withStale);
    EXPECT_DOUBLE_EQ(withStale, untouched)
        << "una corrección de otra imagen se está aplicando igualmente, y desplazada: "
           "borraría un trozo cualquiera de la pieza";
}
