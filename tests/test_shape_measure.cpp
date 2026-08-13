// Banco de la MEDICIÓN AUTOMÁTICA POR FORMA: que lo que se propone dependa de
// qué figura es la pieza y no solo de su envolvente.
//
// Existe porque la medición automática miraba el tamaño y nunca la forma. A un
// disco le proponía el largo y el ancho de su rectángulo envolvente —dos
// números que sobre una pieza redonda son el mismo y no significan nada— y a un
// hexágono no le proponía ni un lado.
//
// Las figuras son sintéticas y de geometría CONOCIDA a propósito: así cada test
// compara contra el número exacto que debería salir en vez de contra «que salga
// algo». Un test que solo comprueba que la lista no está vacía pasa también
// cuando las propuestas son las equivocadas.
#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "vision/geometry_features.h"
#include "vision/shape_class.h"

using pci::inspection::AutoProposal;
using pci::inspection::proposeTools;
using pci::inspection::ProposeOptions;
using pci::inspection::ToolType;
using pci::vision::classifyShape;
using pci::vision::ShapeKind;

namespace {

struct Scene {
    cv::Mat gray;
    cv::Mat mask;
};

// Pieza clara sobre fondo oscuro: el montaje a contraluz que estas medidas
// piden.
Scene sceneFrom(const cv::Mat& mask) {
    Scene scene;
    scene.mask = mask;
    scene.gray = cv::Mat(mask.size(), CV_8UC1, cv::Scalar(30));
    scene.gray.setTo(cv::Scalar(220), mask);
    return scene;
}

cv::Mat disc(int size = 500, int radius = 150) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    cv::circle(mask, {size / 2, size / 2}, radius, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    return mask;
}

// Polígono regular inscrito en un círculo de radio `radius`. El radio es el
// CIRCUNRADIO, así que el lado vale 2·R·sen(π/n) y los tests comparan contra esa
// fórmula en vez de contra un número apuntado a mano.
cv::Mat regularPolygon(int sides, int size = 500, int radius = 160, double rotationDeg = 0.0) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> points;
    for (int i = 0; i < sides; ++i) {
        const double a = rotationDeg * CV_PI / 180.0 + 2.0 * CV_PI * i / sides;
        points.emplace_back(cv::Point(static_cast<int>(size / 2 + radius * std::cos(a)),
                                      static_cast<int>(size / 2 + radius * std::sin(a))));
    }
    cv::fillPoly(mask, points, cv::Scalar(255), cv::LINE_AA);
    return mask;
}

double sideOfRegularPolygon(int sides, double circumradius) {
    return 2.0 * circumradius * std::sin(CV_PI / sides);
}

cv::Mat star(int points, int size = 500, int outer = 170, double innerRatio = 0.45) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> pts;
    for (int i = 0; i < points * 2; ++i) {
        const double a = CV_PI * i / points - CV_PI / 2.0;
        const double r = (i % 2 == 0) ? outer : outer * innerRatio;
        pts.emplace_back(cv::Point(static_cast<int>(size / 2 + r * std::cos(a)),
                                   static_cast<int>(size / 2 + r * std::sin(a))));
    }
    cv::fillPoly(mask, pts, cv::Scalar(255), cv::LINE_AA);
    return mask;
}

cv::Mat ellipseMask(int size = 500, int a = 180, int b = 90) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    cv::ellipse(mask, {size / 2, size / 2}, {a, b}, 0.0, 0, 360, cv::Scalar(255), cv::FILLED,
                cv::LINE_AA);
    return mask;
}

cv::Mat washer(int size = 500, int outer = 160, int inner = 70, int offsetX = 0) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    cv::circle(mask, {size / 2, size / 2}, outer, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::circle(mask, {size / 2 + offsetX, size / 2}, inner, cv::Scalar(0), cv::FILLED,
               cv::LINE_AA);
    return mask;
}

cv::Mat roundedRect(int size = 500, int w = 300, int h = 200, int radius = 40) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    const cv::Point c(size / 2, size / 2);
    cv::rectangle(mask, {c.x - w / 2 + radius, c.y - h / 2}, {c.x + w / 2 - radius, c.y + h / 2},
                  cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, {c.x - w / 2, c.y - h / 2 + radius}, {c.x + w / 2, c.y + h / 2 - radius},
                  cv::Scalar(255), cv::FILLED);
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            cv::circle(mask, {c.x + sx * (w / 2 - radius), c.y + sy * (h / 2 - radius)}, radius,
                       cv::Scalar(255), cv::FILLED, cv::LINE_AA);
        }
    }
    return mask;
}

cv::Mat lShape(int size = 500) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    const std::vector<cv::Point> pts{{120, 120}, {380, 120}, {380, 200},
                                     {200, 200}, {200, 380}, {120, 380}};
    cv::fillPoly(mask, pts, cv::Scalar(255));
    return mask;
}

pci::vision::ShapeClass classOf(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> found;
    cv::findContours(mask, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (found.empty()) {
        return {};
    }
    const auto& biggest = *std::max_element(
        found.begin(), found.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    return classifyShape(biggest, mask);
}

int countType(const std::vector<AutoProposal>& proposals, ToolType type) {
    return static_cast<int>(std::count_if(
        proposals.begin(), proposals.end(),
        [type](const AutoProposal& p) { return p.config.type == type; }));
}

int countNamed(const std::vector<AutoProposal>& proposals, const std::string& needle) {
    return static_cast<int>(
        std::count_if(proposals.begin(), proposals.end(), [&needle](const AutoProposal& p) {
            return p.config.name.find(needle) != std::string::npos;
        }));
}

const AutoProposal* findNamed(const std::vector<AutoProposal>& proposals,
                              const std::string& needle) {
    for (const auto& p : proposals) {
        if (p.config.name.find(needle) != std::string::npos) {
            return &p;
        }
    }
    return nullptr;
}

// Sin tope: varios tests cuentan propuestas por tipo, y el recorte por defecto
// a doce las truncaría — el test estaría midiendo el tope y no la lógica.
ProposeOptions everything() {
    ProposeOptions options;
    options.maxProposals = 100;
    return options;
}

}  // namespace

// ---------------------------------------------------------------------------
// Qué figura es
// ---------------------------------------------------------------------------

TEST(ShapeClassBasics, EachFigureIsRecognisedAsWhatItIs) {
    EXPECT_EQ(classOf(disc()).kind, ShapeKind::Circle);
    EXPECT_EQ(classOf(disc(500, 40)).kind, ShapeKind::Circle)
        << "un disco pequeño sigue siendo un disco";
    EXPECT_EQ(classOf(washer()).kind, ShapeKind::Ring);
    EXPECT_EQ(classOf(roundedRect()).kind, ShapeKind::Rounded);
    EXPECT_EQ(classOf(lShape()).kind, ShapeKind::Polygon);
    EXPECT_EQ(classOf(lShape()).sides, 6);

    // De tres a doce lados se cuentan uno a uno.
    for (int n : {3, 4, 5, 6, 8, 10, 12}) {
        const auto shape = classOf(regularPolygon(n));
        EXPECT_EQ(shape.kind, ShapeKind::Polygon) << n << " lados";
        EXPECT_EQ(shape.sides, n) << "contó " << shape.sides << " lados en un polígono de " << n;
    }

    // Una estrella de cinco puntas ES un decágono, y llamarla así no es un
    // fallo del clasificador: es la respuesta correcta.
    EXPECT_EQ(classOf(star(5)).sides, 10);
}

TEST(ShapeClassBasics, TheAnswerDoesNotDependOnHowThePieceIsTurned) {
    // Una clasificación que cambia al girar la pieza es inservible: la pieza
    // llega a la mesa como llega. Se barre el giro entero de un hexágono, y 60°
    // ya es una vuelta completa por su simetría.
    for (double degrees = 0.0; degrees <= 60.0; degrees += 5.0) {
        const auto shape = classOf(regularPolygon(6, 500, 160, degrees));
        EXPECT_EQ(shape.kind, ShapeKind::Polygon) << "girado " << degrees << "°";
        EXPECT_EQ(shape.sides, 6) << "girado " << degrees << "°: contó " << shape.sides;
    }
}

TEST(ShapeClassBasics, ManySidedContoursAreMeasuredAsRoundAndSayWhy) {
    // La tierra de nadie entre «polígono» y «círculo». Un contorno de catorce o
    // dieciséis lados tiene más caras de las que merece la pena medir una a una
    // y todavía no cae dentro del ruido de una circunferencia. Antes salía
    // «irregular», que es la peor respuesta: ni lados ni diámetro.
    for (int n : {14, 16, 20, 32}) {
        const auto shape = classOf(regularPolygon(n));
        EXPECT_EQ(shape.kind, ShapeKind::Circle) << n << " lados";
        EXPECT_GT(shape.outerDiameter, 0.0) << n << " lados: sin diámetro no hay qué medir";
    }

    // Y una estrella no cuela por muchos lados que tenga: se separa muchísimo
    // de su circunferencia, así que no se mide como redonda.
    EXPECT_NE(classOf(star(8)).kind, ShapeKind::Circle);
    EXPECT_NE(classOf(star(20, 500, 170, 0.85)).kind, ShapeKind::Circle);
}

TEST(ShapeClassBasics, AnOffCentreHoleDoesNotMakeItAWasher) {
    // Llamar arandela a un disco con un agujero descentrado mentiría sobre lo
    // que se mide: en una corona el Ø interior es una cota de la pieza, y aquí
    // sería un agujero más que además está donde no debe.
    EXPECT_EQ(classOf(washer(500, 160, 70, 0)).kind, ShapeKind::Ring);
    EXPECT_EQ(classOf(washer(500, 160, 70, 60)).kind, ShapeKind::Circle)
        << "un agujero a 60 px del centro ya no es el de una corona";

    // Y sin máscara no hay agujeros que ver, así que sale disco. No es falso:
    // es incompleto, y es lo correcto con la información que hay.
    std::vector<std::vector<cv::Point>> found;
    const cv::Mat mask = washer();
    cv::findContours(mask, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(classifyShape(found.front()).kind, ShapeKind::Circle);
}

TEST(ShapeClassBasics, NothingDegenerateThrowsAndTheReasonAlwaysSaysSomething) {
    // Estas funciones se llaman con lo que salga de la segmentación, que un mal
    // día es basura. Lo correcto es «irregular» con un motivo legible, nunca una
    // excepción ni un número inventado.
    const std::vector<std::vector<cv::Point>> rubbish{
        {},
        {{10, 10}, {11, 11}, {12, 12}},
        {{5, 5}, {5, 5}, {5, 5}, {5, 5}, {5, 5}, {5, 5}, {5, 5}, {5, 5}},
        {{0, 0}, {10, 0}, {20, 0}, {30, 0}, {40, 0}, {50, 0}, {60, 0}, {70, 0}},
    };
    for (const auto& contour : rubbish) {
        pci::vision::ShapeClass shape;
        ASSERT_NO_THROW(shape = classifyShape(contour));
        EXPECT_FALSE(shape.reason.empty()) << "una clasificación sin motivo no se puede revisar";
    }
}

// ---------------------------------------------------------------------------
// Qué se propone según la figura
// ---------------------------------------------------------------------------

TEST(ShapeProposals, ARoundPieceGetsItsDiameterAndItsRoundnessAndNothingElse) {
    // El caso que motivó todo esto. Sobre un disco, el largo y el ancho de la
    // envolvente son el MISMO número que el diámetro, y el radio del arco es su
    // mitad: tres nombres para una cota. Una lista así no se revisa.
    const Scene scene = sceneFrom(disc(500, 150));
    const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());

    const auto* diameter = findNamed(proposals, "Ø");
    ASSERT_NE(diameter, nullptr) << "a una pieza redonda hay que proponerle su diámetro";
    EXPECT_EQ(diameter->config.type, ToolType::Circle);
    // Ø nominal 300 px; el contorno con antialias sale un pelo mayor (302,6).
    EXPECT_NEAR(diameter->measured, 300.0, 6.0);

    const auto* roundness = findNamed(proposals, "Redondez");
    ASSERT_NE(roundness, nullptr)
        << "el diámetro dice el tamaño; sin redondez no se sabe si la forma está en plano";
    EXPECT_EQ(roundness->config.type, ToolType::Roundness);

    EXPECT_EQ(countNamed(proposals, "total"), 0) << "el largo/ancho de un disco es su diámetro";
    EXPECT_EQ(countType(proposals, ToolType::Arc), 0)
        << "el arco de un disco es el propio disco: su radio ya está en el diámetro";
    std::printf("  [disco] %zu propuestas: Ø=%.2f, redondez=%.2f\n", proposals.size(),
                diameter->measured, roundness->measured);
}

TEST(ShapeProposals, AWasherGetsBothDiameters) {
    const Scene scene = sceneFrom(washer(500, 160, 70));
    const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());

    const auto* outer = findNamed(proposals, "Ø exterior");
    const auto* inner = findNamed(proposals, "Ø interior");
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr) << "en una corona el agujero central es una cota, no «un agujero»";
    EXPECT_NEAR(outer->measured, 320.0, 8.0);
    EXPECT_NEAR(inner->measured, 140.0, 8.0);
    EXPECT_GT(outer->measured, inner->measured);
    EXPECT_NE(findNamed(proposals, "Redondez"), nullptr);
    std::printf("  [arandela] Ø ext=%.2f, Ø int=%.2f\n", outer->measured, inner->measured);
}

TEST(ShapeProposals, EveryStraightSideGetsMeasuredWithItsOwnLength) {
    // «Medir los lados» de verdad: una regla por cada cara, con su propia
    // tolerancia y su propio veredicto. La herramienta `Lados` NO sirve para
    // esto — su medida es el RECUENTO de caras, no una longitud.
    for (int n : {3, 4, 5, 6, 8}) {
        const int radius = 160;
        const Scene scene = sceneFrom(regularPolygon(n, 500, radius));
        const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());

        EXPECT_EQ(countNamed(proposals, "Lado "), n)
            << "un polígono de " << n << " lados tiene que traer " << n << " reglas";

        // Y que cada una mida el lado de verdad: 2·R·sen(π/n), no un número
        // cualquiera. Sin esto el test pasaría con n reglas mal trazadas.
        const double expected = sideOfRegularPolygon(n, radius);
        double worst = 0.0;
        for (const auto& p : proposals) {
            if (p.config.name.rfind("Lado ", 0) != 0) {
                continue;
            }
            worst = std::max(worst, std::abs(p.measured - expected) / expected);
        }
        std::printf("  [%d lados] lado esperado %.1f px, peor error relativo %.2f %%\n", n,
                    expected, worst * 100.0);
        EXPECT_LT(worst, 0.05) << n << " lados: alguna regla no mide el lado";
    }
}

TEST(ShapeProposals, TheSideCountIsProposedAsItsOwnCheck) {
    // `Lados (n)` vigila una avería distinta de la de cada longitud: que
    // aparezca o falte una cara. Su medida es el recuento, no una distancia.
    const Scene scene = sceneFrom(regularPolygon(6));
    const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());
    const auto* count = findNamed(proposals, "Lados (");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->config.type, ToolType::Polygon);
    EXPECT_DOUBLE_EQ(count->measured, 6.0) << "su medida es el recuento de caras";
}

TEST(ShapeProposals, EveryCornerGetsItsAngleIncludingTheLastOne) {
    // El contorno es CERRADO: la última cara hace esquina con la primera. El
    // bucle se paraba en `size()-1` y perdía siempre esa esquina, así que a un
    // hexágono le proponía cinco ángulos y a un triángulo dos.
    //
    // Un contador que siempre se queda uno corto es de los peores fallos que
    // hay: cuadra con la pieza casi siempre y falla justo cuando cuentas.
    for (int n : {3, 4, 5, 6}) {
        const Scene scene = sceneFrom(regularPolygon(n));
        const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());
        EXPECT_EQ(countType(proposals, ToolType::Angle), n)
            << "un polígono de " << n << " esquinas tiene " << n << " ángulos";

        // Y que sean los ángulos interiores del polígono regular, no cualquier
        // número: (n−2)·180/n.
        const double expected = (n - 2) * 180.0 / n;
        for (const auto& p : proposals) {
            if (p.config.type == ToolType::Angle) {
                EXPECT_NEAR(p.measured, expected, 2.0) << n << " lados";
            }
        }
    }
}

TEST(ShapeProposals, ARoundedRectangleGetsItsFlatsAndItsFilletsButNoSideCount) {
    const Scene scene = sceneFrom(roundedRect(500, 300, 200, 40));
    const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());

    EXPECT_EQ(countNamed(proposals, "Lado "), 4) << "cuatro tramos rectos";
    EXPECT_EQ(countType(proposals, ToolType::Arc), 4) << "cuatro redondeos";

    // Los radios son la cota del redondeo y tienen que dar los 40 px dibujados.
    for (const auto& p : proposals) {
        if (p.config.type == ToolType::Arc) {
            EXPECT_NEAR(p.measured, 40.0, 4.0);
        }
    }

    // Y NO se propone el recuento de lados, a propósito: la herramienta exige
    // que el recuento no cambie al mitad y al doble de epsilon, y al afinar
    // epsilon las esquinas redondeadas aparecen como vértices nuevos. Sería una
    // propuesta que nace muerta.
    EXPECT_EQ(countNamed(proposals, "Lados ("), 0);
}

TEST(ShapeProposals, AnIrregularPieceKeepsBeingMeasuredAsBefore) {
    // La forma decide qué se AÑADE, no borra lo que ya funcionaba. Una pieza que
    // no encaja en ninguna figura conocida tiene que seguir recibiendo su
    // envolvente, que en una elipse es justo la medida útil: sus dos ejes.
    const Scene scene = sceneFrom(ellipseMask(500, 180, 90));
    const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());
    ASSERT_FALSE(proposals.empty());
    EXPECT_GT(countNamed(proposals, "total"), 0)
        << "sin figura reconocida, la envolvente es lo único que hay";

    const auto* longest = findNamed(proposals, "Largo total");
    ASSERT_NE(longest, nullptr);
    EXPECT_NEAR(longest->measured, 360.0, 8.0) << "eje mayor de la elipse 180x90";
}

TEST(ShapeProposals, NoProposalPromisesOneThingAndMeasuresAnother) {
    // Apareció un caso real: un «Espesor» que decía «dos caras paralelas a
    // ≈ 260 px» y medía 81, porque el calíper se topaba por el camino con una
    // pared más cercana. Son dos fallos en uno —un motivo que miente y una cota
    // repetida con otro nombre— y ahora esa propuesta se descarta.
    //
    // El test barre todas las figuras y, cuando el motivo lleva un número de
    // píxeles dentro, exige que se parezca a lo medido.
    const std::vector<std::pair<std::string, cv::Mat>> figures{
        {"disco", disc()},
        {"arandela", washer()},
        {"hexagono", regularPolygon(6)},
        {"triangulo", regularPolygon(3)},
        {"L", lShape()},
        {"rect redondeado", roundedRect()},
    };
    int checked = 0;
    for (const auto& [name, mask] : figures) {
        const Scene scene = sceneFrom(mask);
        for (const auto& p : proposeTools(scene.gray, scene.mask, {}, everything())) {
            EXPECT_FALSE(p.config.name.empty()) << name;
            EXPECT_FALSE(p.reason.empty()) << name << ": una propuesta sin motivo no se revisa";

            const std::string marker = "≈ ";
            const std::size_t at = p.reason.find(marker);
            if (at == std::string::npos) {
                continue;
            }
            const double announced = std::atof(p.reason.c_str() + at + marker.size());
            if (announced <= 0.0) {
                continue;
            }
            ++checked;
            EXPECT_LT(std::abs(p.measured - announced) / announced, 0.15)
                << name << ": «" << p.config.name << "» anuncia " << announced << " y mide "
                << p.measured;
        }
    }
    // Sin esto el test pasaría también si ningún motivo llevara número, que es
    // como no haber comprobado nada.
    EXPECT_GT(checked, 0) << "no se ha contrastado ni un motivo contra su medida";
    std::printf("  [coherencia] %d motivos con número contrastados contra su medida\n", checked);
}

TEST(ShapeProposals, TheSuggestedToleranceAlwaysContainsTheMeasurement) {
    // Una propuesta que nace fuera de tolerancia es una que el operador tiene
    // que corregir antes de poder usarla, y entonces no ha ahorrado nada.
    for (const auto& mask : {disc(), washer(), regularPolygon(6), roundedRect(), lShape()}) {
        const Scene scene = sceneFrom(mask);
        for (const auto& p : proposeTools(scene.gray, scene.mask, {}, everything())) {
            EXPECT_GE(p.measured, p.config.toleranceMin) << p.config.name;
            EXPECT_LE(p.measured, p.config.toleranceMax) << p.config.name;
        }
    }
}


// La escala es la invariancia que más costó, y la que más se rompe sola: la
// pieza se ve del tamaño que la cámara la vea. Un hexágono es un hexágono a 80
// px de ancho y a 640.
//
// Falló de verdad: con el paso de remuestreo fijo en 2 px que traía la
// descomposición por defecto, el mismo hexágono salía de 6 lados a r=160 y de
// «4 rectas y 2 arcos» a r=40 — 128 muestras no dan para resolver seis
// esquinas. Se arregló manteniendo constante el NÚMERO de muestras
// (`decomposeOptionsFor`), no el paso.
TEST(ShapeClassBasics, TheAnswerDoesNotDependOnHowBigThePieceLooks) {
    for (int radius : {40, 60, 80, 120, 160, 320}) {
        const int size = std::max(500, radius * 4);

        const auto hex = classOf(regularPolygon(6, size, radius));
        EXPECT_EQ(hex.kind, ShapeKind::Polygon) << "hexagono de radio " << radius;
        EXPECT_EQ(hex.sides, 6) << "hexagono de radio " << radius << ": contó " << hex.sides;

        const auto round = classOf(disc(size, radius));
        EXPECT_EQ(round.kind, ShapeKind::Circle) << "disco de radio " << radius;

        // Y las propuestas van detrás de la clase: seis reglas y seis ángulos
        // para el hexágono a cualquier tamaño.
        const Scene scene = sceneFrom(regularPolygon(6, size, radius));
        const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());
        EXPECT_EQ(countNamed(proposals, "Lado "), 6) << "radio " << radius;
        EXPECT_EQ(countType(proposals, ToolType::Angle), 6) << "radio " << radius;
    }
}

// Y las MEDIDAS escalan como la pieza: el lado de un hexágono es 2·R·sen(π/6),
// que es R. Si la clase aguantara pero el número no, el banco pasaría dando
// medidas falsas.
TEST(ShapeProposals, TheMeasurementsScaleWithThePiece) {
    for (int radius : {60, 120, 240}) {
        const int size = std::max(500, radius * 4);
        const Scene scene = sceneFrom(regularPolygon(6, size, radius));
        const auto proposals = proposeTools(scene.gray, scene.mask, {}, everything());
        const double expected = sideOfRegularPolygon(6, radius);
        double worst = 0.0;
        for (const auto& p : proposals) {
            if (p.config.name.rfind("Lado ", 0) == 0) {
                worst = std::max(worst, std::abs(p.measured - expected) / expected);
            }
        }
        std::printf("  [escala r=%d] lado esperado %.1f px, peor error %.2f %%\n", radius,
                    expected, worst * 100.0);
        EXPECT_LT(worst, 0.05) << "radio " << radius;
    }
}
