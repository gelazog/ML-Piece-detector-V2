// PASAR A GRIS, UNA SOLA VEZ Y DE UNA SOLA MANERA.
//
// El módulo `vision/` tenía CUATRO copias de esta función y tres de ellas
// contestaban distinto a la misma pregunta:
//
//   difference_map.cpp     con un canal devolvía una COPIA
//   edge_segmentation.cpp  con un canal devolvía la misma imagen, compartida
//   orientation_anchor.cpp convertía si había tres canales y devolvía la
//                          imagen TAL CUAL en cualquier otro caso
//   segmentation.cpp       devolvía error para todo lo que no fuera 1 ó 3
//
// Ninguna estaba mal escrita: cada una era razonable donde nació. El problema
// es que juntas no describen un comportamiento, describen cuatro.
//
// La tercera es la que hacía daño de verdad. Una imagen BGRA —cuatro canales, y
// es lo que entrega más de una fuente— salía de ahí sin convertir, y el resto
// del código la trataba como si fuera gris. Eso no falla: da números.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>

#include "vision/gray.h"

using namespace pci;

TEST(ToGray, AFourChannelImageIsConvertedAndNotWavedThrough) {
    // El caso que motivó juntarlas. Se construye un BGRA cuyo gris es conocido:
    // azul puro opaco, que en la fórmula de OpenCV pesa 0,114.
    cv::Mat bgra(4, 4, CV_8UC4, cv::Scalar(255, 0, 0, 255));
    const cv::Mat gray = vision::toGray(bgra);

    ASSERT_FALSE(gray.empty()) << "un BGRA no se puede convertir: es lo que entrega más de "
                                  "una fuente de imagen";
    EXPECT_EQ(gray.channels(), 1)
        << "el BGRA sale con " << gray.channels()
        << " canales: se está devolviendo la entrada sin convertir, y quien la reciba la "
           "va a tratar como si fuera gris. No falla — da números.";
    std::printf("  [gris] BGRA azul puro -> %d\n", static_cast<int>(gray.at<unsigned char>(0, 0)));
    EXPECT_NEAR(gray.at<unsigned char>(0, 0), 29, 2)
        << "el azul puro debería pesar 0,114 en el gris";
}

TEST(ToGray, AnUnknownFormatComesBackEmptyInsteadOfComingBackUnchanged) {
    // Devolver la propia entrada cuando no se sabe qué hacer es contestar que sí
    // a todo. Vacío obliga a quien llama a decidir qué hace con el «no sé», y
    // eso es exactamente lo que hace `segmentPiece`: convertirlo en un error con
    // su mensaje.
    cv::Mat twoChannels(4, 4, CV_8UC2, cv::Scalar(10, 20));
    EXPECT_TRUE(vision::toGray(twoChannels).empty())
        << "una imagen de dos canales vuelve como si fuera gris";
    EXPECT_TRUE(vision::toGray(cv::Mat()).empty());
}

TEST(ToGray, AGreyImageIsSharedAndNotCopied) {
    // No clonar es una decisión y conviene que esté fijada: `segmentByEdges`
    // corre en CADA frame del vídeo, y una copia de la imagen completa por frame
    // se paga en el sitio más caliente del programa.
    //
    // La contrapartida está dicha en `vision/gray.h`: quien necesite modificar
    // el resultado tiene que escribir en otra matriz. `difference_map` lo hace
    // así desde que se unificaron.
    cv::Mat grey(8, 8, CV_8UC1, cv::Scalar(128));
    const cv::Mat same = vision::toGray(grey);
    EXPECT_EQ(same.data, grey.data)
        << "se está clonando la imagen: eso es una copia del frame entero en el camino que "
           "corre sesenta veces por segundo";
}

TEST(ToGray, AThreeChannelImageStillConvertsLikeItAlwaysDid) {
    cv::Mat bgr(4, 4, CV_8UC3, cv::Scalar(0, 255, 0));  // verde puro
    const cv::Mat gray = vision::toGray(bgr);
    ASSERT_FALSE(gray.empty());
    EXPECT_EQ(gray.channels(), 1);
    EXPECT_NEAR(gray.at<unsigned char>(0, 0), 150, 3)
        << "el verde puro debería pesar 0,587 en el gris";
}
