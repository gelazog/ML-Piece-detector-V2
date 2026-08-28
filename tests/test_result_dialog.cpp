// EL DIÁLOGO DE RESULTADO ENSEÑA DÓNDE, NO SÓLO CUÁNTO.
//
// Hasta ahora el diálogo daba dos miniaturas —«Registrada» y «Actual»— y un
// número de similitud, y encontrar qué le pasa a la pieza era cosa del ojo del
// operador. Con piezas pequeñas o defectos finos eso no se puede hacer, y lo que
// acaba pasando es que se acepta el veredicto sin entenderlo.
//
// Lo que se comprueba aquí son las dos mitades del trato: que con un defecto
// aparece el mapa y se dice en palabras dónde mirar, y que con una pieza limpia
// NO aparece. Un hueco que a veces trae imagen se entiende peor que no tener el
// hueco, y un mapa que siempre enseña algo enseña a ignorarlo.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>

#include <opencv2/imgproc.hpp>

#include <cstdio>

#include "camera/frame_utils.h"
#include "engine/inspection_engine.h"
#include "ui/inspection_result_dialog.h"

namespace {

constexpr int kSide = 256;

cv::Mat referenceCrop() {
    cv::Mat crop = cv::Mat::zeros(kSide, kSide, CV_8UC1);
    cv::circle(crop, {kSide / 2, kSide / 2}, 95, cv::Scalar(180), cv::FILLED, cv::LINE_8);
    cv::circle(crop, {kSide / 2, kSide / 2}, 30, cv::Scalar(120), cv::FILLED, cv::LINE_8);
    cv::Mat colour;
    cv::cvtColor(crop, colour, cv::COLOR_GRAY2BGR);
    return colour;
}

pci::engine::InspectionEngine::Outcome outcomeWith(const cv::Mat& normalized) {
    pci::engine::InspectionEngine::Outcome outcome;
    outcome.analysis.normalized = normalized;
    outcome.verdict.embedding.evaluated = true;
    outcome.verdict.embedding.similarity = 0.91;
    outcome.verdict.embedding.threshold = 0.95;
    outcome.verdict.ok = false;
    return outcome;
}

// El rótulo de una miniatura, por su NOMBRE.
//
// La mitad de lo que comprueba este fichero es si el mapa de diferencias
// APARECE o no —sólo sale cuando hay algo que señalar—, y eso se estaba
// resolviendo buscando la frase «Dónde difiere». El día que alguien mejore ese
// rótulo, el test dirá «con un defecto no aparece el mapa» y el mapa estará ahí.
QLabel* namedLabel(QWidget& widget, const char* name) {
    return widget.findChild<QLabel*>(QString::fromLatin1(name));
}

}  // namespace

TEST(ResultDialog, WithADefectItShowsWhereAndSaysItInWords) {
    const cv::Mat reference = referenceCrop();

    // El sitio del defecto y lo que hay que leer. El recorte se parte en nueve,
    // así que el tercio central no lleva apellido de izquierda ni de derecha —y
    // esta prueba lo fija, porque la primera versión ponía el defecto en x=90 de
    // 256, lo llamaba «a la izquierda» y 90 cae en el tercio de en medio.
    struct Case {
        cv::Point where;
        const char* mustSay;
        const char* alsoSay;
    };
    const Case cases[] = {
        {{55, 80}, "arriba", "izquierda"},
        {{200, 195}, "abajo", "derecha"},
        {{110, 80}, "arriba", nullptr},  // tercio central: sin apellido
    };

    for (const auto& one : cases) {
        cv::Mat damaged = reference.clone();
        cv::circle(damaged, one.where, 16, cv::Scalar(0, 0, 0), cv::FILLED, cv::LINE_8);

        QImage frame(640, 480, QImage::Format_RGB888);
        frame.fill(QColor(30, 30, 30));
        pci::ui::InspectionResultDialog dialog(frame, outcomeWith(damaged), nullptr, 1,
                                               pci::camera::matToQImage(reference).copy());
        dialog.resize(1100, 700);

        auto* title = namedLabel(dialog, "thumbDifference");
        ASSERT_NE(title, nullptr)
            << "con un defecto no aparece el mapa: el operador se queda con un número y "
               "a buscar a ojo";

        auto* note = namedLabel(dialog, "differenceNote");
        ASSERT_NE(note, nullptr) << "el mapa está y no se dice en palabras dónde mirar";
        std::printf("  [resultado] defecto en (%d,%d): %s\n", one.where.x, one.where.y,
                    note->text().toStdString().c_str());
        EXPECT_TRUE(note->text().contains(QString::fromUtf8(one.mustSay)))
            << "dice: " << note->text().toStdString();
        if (one.alsoSay != nullptr) {
            EXPECT_TRUE(note->text().contains(QString::fromUtf8(one.alsoSay)))
                << "dice: " << note->text().toStdString();
        } else {
            EXPECT_FALSE(note->text().contains(QStringLiteral("izquierda")))
                << "le pone apellido a un defecto que está en el tercio central";
            EXPECT_FALSE(note->text().contains(QStringLiteral("derecha")))
                << "le pone apellido a un defecto que está en el tercio central";
        }
        EXPECT_TRUE(note->wordWrap()) << "el aviso se corta en vez de leerse entero";
    }
}

TEST(ResultDialog, WithACleanPieceThereIsNoMapAtAll) {
    const cv::Mat reference = referenceCrop();
    cv::Mat same = reference.clone();
    cv::Mat noise(same.size(), CV_8UC3);
    cv::randn(noise, 0, 2);
    cv::add(same, noise, same);
    same.setTo(cv::Scalar(0, 0, 0), reference == 0);

    QImage frame(640, 480, QImage::Format_RGB888);
    frame.fill(QColor(30, 30, 30));
    pci::ui::InspectionResultDialog dialog(frame, outcomeWith(same), nullptr, 1,
                                           pci::camera::matToQImage(reference).copy());
    dialog.resize(1100, 700);

    EXPECT_EQ(namedLabel(dialog, "thumbDifference"), nullptr)
        << "se enseña un mapa de una pieza que no tiene nada: el operador aprendería a "
           "no hacerle caso";
    EXPECT_EQ(namedLabel(dialog, "differenceNote"), nullptr);
}

// Sin miniatura de referencia no hay nada que comparar, y eso no puede romper el
// diálogo: es el caso de una pieza registrada antes de que se guardaran las
// miniaturas.
TEST(ResultDialog, WithNoReferenceThumbnailItSimplyDoesNotOffer) {
    const cv::Mat reference = referenceCrop();
    QImage frame(640, 480, QImage::Format_RGB888);
    frame.fill(QColor(30, 30, 30));

    pci::ui::InspectionResultDialog dialog(frame, outcomeWith(reference), nullptr, 1, QImage());
    dialog.resize(1100, 700);

    EXPECT_EQ(namedLabel(dialog, "thumbDifference"), nullptr);
    // Y las dos miniaturas de siempre siguen ahí: quitar el mapa no puede
    // llevarse por delante lo que ya había.
    EXPECT_NE(namedLabel(dialog, "thumbCurrent"), nullptr);
}
