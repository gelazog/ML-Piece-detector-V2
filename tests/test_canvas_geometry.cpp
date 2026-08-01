// Pruebas del TRAZADO: la aritmética que decide dónde cae cada cosa en pantalla
// y qué está tocando el operador. Antes vivía dentro del widget y no se podía
// probar sin abrir una ventana, que es justo la razón por la que los fallos de
// aquí llegaban al usuario.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "inspection_editor/canvas/canvas_geometry.h"
#include "vision/position_fixture.h"

using namespace pci::inspection;

namespace {

// Fixture identidad: coords de pieza == coords de imagen. Aísla los fallos de
// trazado de los del sistema de referencia.
pci::vision::Fixture identity() {
    pci::vision::Fixture f;
    f.origin = {0.0F, 0.0F};
    f.angleDeg = 0.0;
    return f;
}

pci::vision::Fixture fixtureAt(cv::Point2f origin, double angleDeg) {
    pci::vision::Fixture f;
    f.origin = origin;
    f.angleDeg = angleDeg;
    return f;
}

constexpr double kEps = 1e-6;

}  // namespace

// ---------------------------------------------------------------------------
// ViewTransform: encuadre
// ---------------------------------------------------------------------------

TEST(ViewTransform, FitKeepsAspectAndCentersOnTheLimitingAxis) {
    // Imagen 4:3 en una ventana 2:1 -> sobra a los lados, no arriba.
    const ViewTransform v({640, 480}, {1000, 500}, 1.0, {0.0, 0.0});
    const ViewRect fit = v.fitRect();
    EXPECT_NEAR(fit.height, 500.0, kEps);          // el alto manda
    EXPECT_NEAR(fit.width, 500.0 * 640 / 480, kEps);
    EXPECT_NEAR(fit.centerX(), 500.0, kEps);       // centrado en la ventana
    EXPECT_NEAR(fit.centerY(), 250.0, kEps);
    EXPECT_NEAR(fit.width / fit.height, 640.0 / 480.0, kEps);  // proporción intacta
}

TEST(ViewTransform, FitScalesUpWhenTheWidgetIsBiggerThanTheImage) {
    const ViewTransform v({100, 100}, {800, 400}, 1.0, {0.0, 0.0});
    const ViewRect fit = v.fitRect();
    EXPECT_NEAR(fit.width, 400.0, kEps);
    EXPECT_NEAR(fit.height, 400.0, kEps);
}

TEST(ViewTransform, DegenerateSizesGiveAnEmptyViewInsteadOfNaN) {
    for (const auto& [image, widget] :
         std::vector<std::pair<cv::Size, cv::Size>>{{{0, 0}, {800, 600}},
                                                    {{640, 480}, {0, 600}},
                                                    {{640, 480}, {800, 0}},
                                                    {{-4, 480}, {800, 600}}}) {
        const ViewTransform v(image, widget, 1.0, {10.0, 10.0});
        EXPECT_TRUE(v.fitRect().empty());
        EXPECT_TRUE(v.targetRect().empty());
        EXPECT_EQ(v.displayScale(), 0.0);
        // Y las conversiones no deben devolver NaN aunque no haya vista.
        const cv::Point2d w = v.imageToWidget({10.0F, 10.0F});
        EXPECT_FALSE(std::isnan(w.x) || std::isnan(w.y));
        const cv::Point2f i = v.widgetToImage({10.0, 10.0});
        EXPECT_FALSE(std::isnan(i.x) || std::isnan(i.y));
    }
}

// ---------------------------------------------------------------------------
// ViewTransform: límite del desplazamiento
// ---------------------------------------------------------------------------

TEST(ViewTransform, PanIsIgnoredWhileTheImageStillFits) {
    // Con zoom 1 la imagen cabe entera: no hay nada que desplazar, así que
    // cualquier arrastre debe quedar en cero (si no, se descubriría el fondo).
    const ViewTransform v({640, 480}, {800, 600}, 1.0, {0.0, 0.0});
    const cv::Point2d clamped = v.clampedPan({500.0, -500.0});
    EXPECT_NEAR(clamped.x, 0.0, kEps);
    EXPECT_NEAR(clamped.y, 0.0, kEps);
}

TEST(ViewTransform, PanIsLimitedPerAxisSoNoBackgroundShowsThrough) {
    // Imagen 4:3 en ventana cuadrada: al ampliar, primero sobra ancho.
    const ViewTransform v({640, 480}, {600, 600}, 2.0, {0.0, 0.0});
    const ViewRect fit = v.fitRect();  // 600 x 450
    const double maxX = (fit.width * 2.0 - 600) / 2.0;
    const double maxY = (fit.height * 2.0 - 600) / 2.0;
    ASSERT_GT(maxX, 0.0);
    ASSERT_GT(maxY, 0.0);
    EXPECT_NEAR(v.clampedPan({1e6, 1e6}).x, maxX, kEps);
    EXPECT_NEAR(v.clampedPan({1e6, 1e6}).y, maxY, kEps);
    EXPECT_NEAR(v.clampedPan({-1e6, -1e6}).x, -maxX, kEps);
    EXPECT_NEAR(v.clampedPan({-1e6, -1e6}).y, -maxY, kEps);
}

TEST(ViewTransform, ClampedViewNeverLeavesAGapAtAnyZoom) {
    // Invariante de verdad: con la imagen ampliada, el encuadre pintado debe
    // cubrir la ventana entera; con la imagen más chica que la ventana, debe
    // quedar centrada. Se comprueba empujando el desplazamiento al extremo.
    const cv::Size image(1920, 1080);
    const cv::Size widget(900, 640);
    for (const double zoom : {1.0, 1.25, 2.0, 3.7, 8.0}) {
        for (const cv::Point2d pan : {cv::Point2d{1e6, 1e6}, cv::Point2d{-1e6, -1e6},
                                      cv::Point2d{1e6, -1e6}, cv::Point2d{-1e6, 1e6}}) {
            const ViewTransform v(image, widget, zoom, pan);
            const ViewRect t = v.targetRect();
            const ViewRect fit = v.fitRect();
            if (fit.width * zoom >= widget.width) {
                EXPECT_LE(t.left(), 1e-6) << "zoom " << zoom;
                EXPECT_GE(t.left() + t.width, widget.width - 1e-6) << "zoom " << zoom;
            } else {
                EXPECT_NEAR(t.centerX(), widget.width / 2.0, kEps);
            }
            if (fit.height * zoom >= widget.height) {
                EXPECT_LE(t.top(), 1e-6) << "zoom " << zoom;
                EXPECT_GE(t.top() + t.height, widget.height - 1e-6) << "zoom " << zoom;
            } else {
                EXPECT_NEAR(t.centerY(), widget.height / 2.0, kEps);
            }
        }
    }
}

TEST(ViewTransform, ClampingIsIdempotent) {
    const ViewTransform v({1920, 1080}, {800, 600}, 4.0, {0.0, 0.0});
    const cv::Point2d once = v.clampedPan({4000.0, -3000.0});
    const cv::Point2d twice = v.clampedPan(once);
    EXPECT_NEAR(once.x, twice.x, kEps);
    EXPECT_NEAR(once.y, twice.y, kEps);
}

// ---------------------------------------------------------------------------
// ViewTransform: ida y vuelta pantalla <-> imagen
// ---------------------------------------------------------------------------

TEST(ViewTransform, ImageAndWidgetRoundTripAtEveryZoomAndPan) {
    // Si esta ida y vuelta no cierra, todo lo demás falla: dibujar donde se
    // hizo clic, medir donde se ve la herramienta y arrastrar manijas.
    std::mt19937 rng(20260731);
    std::uniform_real_distribution<double> zoomDist(1.0, 8.0);
    std::uniform_real_distribution<double> panDist(-3000.0, 3000.0);
    std::uniform_real_distribution<float> pxDist(0.0F, 1919.0F);
    std::uniform_real_distribution<float> pyDist(0.0F, 1079.0F);

    for (int i = 0; i < 2000; ++i) {
        const ViewTransform v({1920, 1080}, {933, 617}, zoomDist(rng),
                              {panDist(rng), panDist(rng)});
        const cv::Point2f p(pxDist(rng), pyDist(rng));
        const cv::Point2f back = v.widgetToImage(v.imageToWidget(p));
        EXPECT_NEAR(back.x, p.x, 1e-2) << "iteración " << i;
        EXPECT_NEAR(back.y, p.y, 1e-2) << "iteración " << i;
    }
}

TEST(ViewTransform, CornersOfTheImageLandOnTheCornersOfTheTarget) {
    const ViewTransform v({640, 480}, {800, 600}, 2.5, {40.0, -25.0});
    const ViewRect t = v.targetRect();
    const cv::Point2d topLeft = v.imageToWidget({0.0F, 0.0F});
    const cv::Point2d bottomRight = v.imageToWidget({640.0F, 480.0F});
    EXPECT_NEAR(topLeft.x, t.left(), kEps);
    EXPECT_NEAR(topLeft.y, t.top(), kEps);
    EXPECT_NEAR(bottomRight.x, t.left() + t.width, kEps);
    EXPECT_NEAR(bottomRight.y, t.top() + t.height, kEps);
}

TEST(ViewTransform, DisplayScaleMatchesTheZoomTimesTheFit) {
    const ViewTransform v({640, 480}, {800, 600}, 3.0, {0.0, 0.0});
    const ViewRect fit = v.fitRect();
    EXPECT_NEAR(v.displayScale(), fit.width * 3.0 / 640.0, kEps);
    // 800x600 con imagen 640x480: cabe justo por ancho -> ajuste 1.25.
    EXPECT_NEAR(v.displayScale(), 1.25 * 3.0, kEps);
}

TEST(ViewTransform, ZoomingAtAPointKeepsThatPointUnderTheCursor) {
    // Reproduce lo que hace la rueda del ratón: anclar el punto de imagen bajo
    // el cursor y corregir el desplazamiento. Si esto deriva, al ampliar se
    // pierde lo que se estaba mirando.
    const cv::Size image(1920, 1080);
    const cv::Size widget(900, 640);
    const cv::Point2d cursor(710.0, 180.0);  // deliberadamente descentrado
    double zoom = 1.0;
    cv::Point2d pan(0.0, 0.0);

    for (int step = 0; step < 12; ++step) {
        const ViewTransform before(image, widget, zoom, pan);
        const cv::Point2f anchor = before.widgetToImage(cursor);
        const double next = std::clamp(zoom * 1.25, 1.0, 8.0);
        if (std::abs(next - zoom) < 1e-9) {
            break;
        }
        zoom = next;
        const ViewTransform moved(image, widget, zoom, pan);
        const cv::Point2d drift = cursor - moved.imageToWidget(anchor);
        const cv::Point2d desired = pan + drift;
        pan = moved.clampedPan(desired);

        // El ancla se queda bajo el cursor en cada eje donde el desplazamiento
        // cabía. Donde el límite lo recortó no puede quedarse, y es correcto: en
        // ese eje la imagen todavía entra en la ventana, así que tiene que estar
        // centrada; mantener el ancla ahí significaría descubrir fondo.
        const ViewTransform after(image, widget, zoom, pan);
        const cv::Point2d landed = after.imageToWidget(anchor);
        if (std::abs(pan.x - desired.x) < 1e-9) {
            EXPECT_NEAR(landed.x, cursor.x, 1.0) << "paso " << step;
        }
        if (std::abs(pan.y - desired.y) < 1e-9) {
            EXPECT_NEAR(landed.y, cursor.y, 1.0) << "paso " << step;
        }
    }
    EXPECT_NEAR(zoom, 8.0, 1e-9);  // llega al tope sin atascarse
}

// ---------------------------------------------------------------------------
// Distancia a segmento
// ---------------------------------------------------------------------------

TEST(DistanceToSegment, MeasuresToTheSegmentNotToTheInfiniteLine) {
    const cv::Point2f a(0.0F, 0.0F);
    const cv::Point2f b(10.0F, 0.0F);
    EXPECT_NEAR(distanceToSegment({5.0F, 3.0F}, a, b), 3.0, kEps);   // perpendicular
    EXPECT_NEAR(distanceToSegment({-4.0F, 0.0F}, a, b), 4.0, kEps);  // fuera, no 0
    EXPECT_NEAR(distanceToSegment({14.0F, 0.0F}, a, b), 4.0, kEps);
    EXPECT_NEAR(distanceToSegment({0.0F, 0.0F}, a, b), 0.0, kEps);
}

TEST(DistanceToSegment, DegenerateSegmentBehavesLikeAPoint) {
    const cv::Point2f a(5.0F, 5.0F);
    EXPECT_NEAR(distanceToSegment({5.0F, 8.0F}, a, a), 3.0, kEps);
    EXPECT_NEAR(distanceToSegment({5.0F, 8.0F}, a, {5.0F, 5.0F + 1e-6F}), 3.0, 1e-3);
}

// ---------------------------------------------------------------------------
// Manijas: una por cada tipo de herramienta
// ---------------------------------------------------------------------------

TEST(Handles, EveryGeometryTypeExposesHandlesThatCanBeMoved) {
    // Coherencia por tipo: mover la manija i y volver a leerla debe devolver
    // justo lo que se pidió (salvo los dos casos derivados, que se prueban
    // aparte). Un desajuste aquí sería una manija que "salta" al arrastrarla.
    const std::vector<ToolGeometry> geometries = {
        CaliperGeometry{{10.0F, 20.0F}, {90.0F, 40.0F}, 10.0F},
        PointToLineGeometry{{0.0F, 0.0F}, {50.0F, 0.0F}, {25.0F, -20.0F}, {25.0F, 20.0F}},
        EdgeFlawGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 20},
        RulerGeometry{{1.0F, 2.0F}, {3.0F, 4.0F}},
        LineToLineGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, {0.0F, 30.0F}, {40.0F, 30.0F}},
        AngleGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, {0.0F, 40.0F}},
        PositionGeometry{{12.0F, 34.0F}, PositionAxis::Radial},
        PolyBlobGeometry{{{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}},
                         20.0F, true},
    };

    for (const auto& base : geometries) {
        const auto handles = handlePoints(base);
        ASSERT_FALSE(handles.empty());
        for (int h = 0; h < static_cast<int>(handles.size()); ++h) {
            ToolGeometry g = base;
            const cv::Point2f target(123.5F + h, -47.25F - h);
            setHandlePoint(g, h, target);
            const auto moved = handlePoints(g);
            ASSERT_EQ(moved.size(), handles.size());
            EXPECT_NEAR(moved[static_cast<std::size_t>(h)].x, target.x, 1e-3);
            EXPECT_NEAR(moved[static_cast<std::size_t>(h)].y, target.y, 1e-3);
            // Y las demás manijas no se mueven solas.
            for (std::size_t k = 0; k < handles.size(); ++k) {
                if (static_cast<int>(k) == h) {
                    continue;
                }
                EXPECT_NEAR(moved[k].x, handles[k].x, 1e-3);
                EXPECT_NEAR(moved[k].y, handles[k].y, 1e-3);
            }
        }
    }
}

TEST(Handles, CircleSecondHandleIsTheRadiusAndKeepsAMinimum) {
    ToolGeometry g = CircleGeometry{{100.0F, 100.0F}, 50.0F, 12.0F, 36};
    ASSERT_EQ(handlePoints(g).size(), 2U);
    EXPECT_NEAR(handlePoints(g)[1].x, 150.0F, kEps);  // centro + (r, 0)

    setHandlePoint(g, 1, {100.0F, 30.0F});  // arrastre en vertical: cuenta la distancia
    EXPECT_NEAR(std::get<CircleGeometry>(g).radius, 70.0F, 1e-3);

    setHandlePoint(g, 1, {100.5F, 100.0F});  // pegado al centro
    EXPECT_NEAR(std::get<CircleGeometry>(g).radius, 4.0F, kEps);  // mínimo, no cero

    // Mover el centro conserva el radio (arrastrar el círculo no lo encoge).
    setHandlePoint(g, 0, {400.0F, 400.0F});
    EXPECT_NEAR(std::get<CircleGeometry>(g).radius, 4.0F, kEps);
}

TEST(Handles, BlobSecondHandleResizesSymmetricallyWithAMinimum) {
    ToolGeometry g = BlobGeometry{{100.0F, 100.0F}, 80.0F, 60.0F, 20.0F, true};
    EXPECT_NEAR(handlePoints(g)[1].x, 140.0F, kEps);  // esquina = centro + (w/2, h/2)
    EXPECT_NEAR(handlePoints(g)[1].y, 130.0F, kEps);

    setHandlePoint(g, 1, {160.0F, 90.0F});  // esquina arrastrada, incluso "hacia atrás"
    EXPECT_NEAR(std::get<BlobGeometry>(g).width, 120.0F, kEps);
    EXPECT_NEAR(std::get<BlobGeometry>(g).height, 20.0F, kEps);  // |90-100| * 2

    setHandlePoint(g, 1, {100.0F, 100.0F});  // colapsado sobre el centro
    EXPECT_NEAR(std::get<BlobGeometry>(g).width, 8.0F, kEps);   // mínimo utilizable
    EXPECT_NEAR(std::get<BlobGeometry>(g).height, 8.0F, kEps);
}

TEST(Handles, OutOfRangeIndexNeverCorruptsTheGeometry) {
    // Desgaste: índices imposibles (de un arrastre que perdió la selección).
    const std::vector<ToolGeometry> geometries = {
        CaliperGeometry{{0.0F, 0.0F}, {10.0F, 0.0F}, 10.0F},
        CircleGeometry{{5.0F, 5.0F}, 20.0F, 12.0F, 36},
        PolyBlobGeometry{{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 9.0F}}, 20.0F, true},
        PositionGeometry{{7.0F, 7.0F}, PositionAxis::X},
    };
    for (const auto& base : geometries) {
        for (const int bad : {-5, -1, 99, 12345}) {
            ToolGeometry g = base;
            setHandlePoint(g, bad, {1e4F, -1e4F});
            for (const auto& p : handlePoints(g)) {
                EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y));
                EXPECT_LT(std::abs(p.x), 1e3F);  // nada se fue al infinito
                EXPECT_LT(std::abs(p.y), 1e3F);
            }
        }
    }
}

TEST(Handles, PolyBlobHandlesFollowItsVertices) {
    ToolGeometry g = PolyBlobGeometry{
        {{0.0F, 0.0F}, {30.0F, 0.0F}, {30.0F, 30.0F}, {0.0F, 30.0F}, {-10.0F, 15.0F}},
        20.0F, true};
    EXPECT_EQ(handlePoints(g).size(), 5U);
    setHandlePoint(g, 4, {-50.0F, 15.0F});
    EXPECT_NEAR(std::get<PolyBlobGeometry>(g).vertices[4].x, -50.0F, kEps);
    EXPECT_EQ(std::get<PolyBlobGeometry>(g).vertices.size(), 5U);  // no añade ni quita
}

// Regla del marco de selección: queda seleccionada la herramienta con ALGÚN
// punto de referencia dentro. Se comprueba tal cual, porque es lo que hace el
// widget al soltar el marco.
namespace {

bool marqueeSelects(const ToolGeometry& geometry, const pci::vision::Fixture& fixture,
                    cv::Point2f corner0, cv::Point2f corner1) {
    const float left = std::min(corner0.x, corner1.x);
    const float right = std::max(corner0.x, corner1.x);
    const float top = std::min(corner0.y, corner1.y);
    const float bottom = std::max(corner0.y, corner1.y);
    for (const auto& piecePoint : referencePoints(geometry)) {
        const cv::Point2f q = pci::vision::toImageCoords(fixture, piecePoint);
        if (q.x >= left && q.x <= right && q.y >= top && q.y <= bottom) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(Marquee, ASquareDrawnOverWhatYouSeeSelectsTheTool) {
    // El fallo real: con solo el centro como punto de referencia, un marco
    // trazado sobre el anillo del círculo o sobre un lado del blob —justo donde
    // el operador ve la herramienta— no seleccionaba nada.
    const ToolGeometry circle = CircleGeometry{{500.0F, 500.0F}, 200.0F, 12.0F, 36};
    EXPECT_TRUE(marqueeSelects(circle, identity(), {650.0F, 450.0F}, {750.0F, 550.0F}))
        << "marco sobre el borde derecho del anillo";
    EXPECT_TRUE(marqueeSelects(circle, identity(), {450.0F, 250.0F}, {550.0F, 350.0F}))
        << "marco sobre el borde superior del anillo";
    EXPECT_TRUE(marqueeSelects(circle, identity(), {400.0F, 400.0F}, {600.0F, 600.0F}))
        << "marco sobre el centro (seguía funcionando)";
    // Y un marco lejos del círculo sigue sin seleccionarlo.
    EXPECT_FALSE(marqueeSelects(circle, identity(), {900.0F, 900.0F}, {1000.0F, 1000.0F}));
    // Tampoco el hueco interior, donde no hay nada dibujado.
    EXPECT_FALSE(marqueeSelects(circle, identity(), {560.0F, 560.0F}, {620.0F, 620.0F}));

    const ToolGeometry blob = BlobGeometry{{300.0F, 300.0F}, 200.0F, 100.0F, 20.0F, true};
    EXPECT_TRUE(marqueeSelects(blob, identity(), {380.0F, 330.0F}, {420.0F, 370.0F}))
        << "marco sobre la esquina inferior derecha";
    EXPECT_FALSE(marqueeSelects(blob, identity(), {700.0F, 700.0F}, {800.0F, 800.0F}));

    const ToolGeometry pointToLine = PointToLineGeometry{
        {0.0F, 0.0F}, {100.0F, 0.0F}, {50.0F, 60.0F}, {50.0F, 120.0F}};
    EXPECT_TRUE(marqueeSelects(pointToLine, identity(), {40.0F, 100.0F}, {60.0F, 140.0F}))
        << "marco sobre el segmento de escaneo, que también se dibuja";
}

TEST(Marquee, TheSelectionFrameFollowsTheRotatedPiece) {
    const ToolGeometry circle = CircleGeometry{{100.0F, 0.0F}, 40.0F, 12.0F, 36};
    const auto f = fixtureAt({640.0F, 360.0F}, 90.0);
    // Con la pieza girada 90°, el borde derecho del círculo en coords de pieza
    // aparece en otro sitio de la imagen; el marco debe seguirlo.
    const cv::Point2f edge = pci::vision::toImageCoords(f, {140.0F, 0.0F});
    EXPECT_TRUE(marqueeSelects(circle, f, edge - cv::Point2f(10.0F, 10.0F),
                               edge + cv::Point2f(10.0F, 10.0F)));
}

TEST(Marquee, EveryReferencePointIsAPointYouCanSee) {
    // Coherencia: los puntos de referencia deben caer SOBRE la herramienta
    // dibujada (distancia ~0 a su geometría) o ser su centro. Si alguno cayera
    // fuera, el marco seleccionaría cosas que el operador no está encuadrando.
    const std::vector<std::pair<ToolGeometry, cv::Point2f>> withCenters = {
        {CircleGeometry{{500.0F, 500.0F}, 200.0F, 12.0F, 36}, {500.0F, 500.0F}},
        {BlobGeometry{{300.0F, 300.0F}, 200.0F, 100.0F, 20.0F, true}, {300.0F, 300.0F}},
        {CaliperGeometry{{10.0F, 10.0F}, {90.0F, 10.0F}, 10.0F}, {10.0F, 10.0F}},
        {AngleGeometry{{0.0F, 0.0F}, {50.0F, 0.0F}, {0.0F, 50.0F}}, {0.0F, 0.0F}},
        {PointToLineGeometry{{0.0F, 0.0F}, {100.0F, 0.0F}, {50.0F, 60.0F}, {50.0F, 120.0F}},
         {0.0F, 0.0F}},
    };
    for (const auto& [geometry, center] : withCenters) {
        for (const auto& p : referencePoints(geometry)) {
            const bool onTheShape = distanceToGeometry(geometry, identity(), p) < 1e-3;
            const bool isTheCenter = cv::norm(p - center) < 1e-3;
            EXPECT_TRUE(onTheShape || isTheCenter)
                << "punto (" << p.x << ", " << p.y << ") ni está sobre la forma ni es su ancla";
        }
    }
}

TEST(Handles, ReferencePointsCoverEveryTypeWithoutBeingEmpty) {
    const std::vector<ToolGeometry> geometries = {
        CaliperGeometry{}, CircleGeometry{}, PointToLineGeometry{}, EdgeFlawGeometry{},
        BlobGeometry{},    RulerGeometry{},  LineToLineGeometry{},  AngleGeometry{},
        PositionGeometry{},
        PolyBlobGeometry{{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}}, 20.0F, true},
    };
    for (const auto& g : geometries) {
        EXPECT_FALSE(referencePoints(g).empty());
        for (const auto& p : referencePoints(g)) {
            EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y));
        }
    }
}

// ---------------------------------------------------------------------------
// Selección por clic
// ---------------------------------------------------------------------------

TEST(HitTesting, DistanceIsZeroOnTheGeometryAndGrowsAwayFromIt) {
    const ToolGeometry caliper = CaliperGeometry{{10.0F, 10.0F}, {110.0F, 10.0F}, 10.0F};
    EXPECT_NEAR(distanceToGeometry(caliper, identity(), {60.0F, 10.0F}), 0.0, kEps);
    EXPECT_NEAR(distanceToGeometry(caliper, identity(), {60.0F, 15.0F}), 5.0, kEps);

    // El círculo se agarra por su TRAZO, no por su interior: el centro está a
    // un radio de distancia, o el operador no podría seleccionar lo que hay
    // debajo del círculo.
    const ToolGeometry circle = CircleGeometry{{100.0F, 100.0F}, 40.0F, 12.0F, 36};
    EXPECT_NEAR(distanceToGeometry(circle, identity(), {140.0F, 100.0F}), 0.0, kEps);
    EXPECT_NEAR(distanceToGeometry(circle, identity(), {100.0F, 100.0F}), 40.0, kEps);

    // La Posición sí es un punto: distancia directa.
    const ToolGeometry position = PositionGeometry{{50.0F, 50.0F}, PositionAxis::Radial};
    EXPECT_NEAR(distanceToGeometry(position, identity(), {53.0F, 54.0F}), 5.0, kEps);
}

TEST(HitTesting, DistanceTravelsWithThePieceThroughTheFixture) {
    // La geometría está en coords de pieza: si la pieza gira, el clic debe
    // seguir cayendo sobre la herramienta dibujada, no sobre su sitio viejo.
    const ToolGeometry ruler = RulerGeometry{{0.0F, 0.0F}, {100.0F, 0.0F}};
    const auto rotated = fixtureAt({300.0F, 200.0F}, 90.0);
    const cv::Point2f mid = pci::vision::toImageCoords(rotated, {50.0F, 0.0F});
    EXPECT_NEAR(distanceToGeometry(ruler, rotated, mid), 0.0, 1e-3);
    // Y a 7 px del trazo sigue midiendo 7, sea cual sea el giro.
    for (const double angle : {0.0, 33.0, 90.0, 147.0, -120.0}) {
        const auto f = fixtureAt({300.0F, 200.0F}, angle);
        const cv::Point2f on = pci::vision::toImageCoords(f, {50.0F, 0.0F});
        const cv::Point2f normal(static_cast<float>(-std::sin(angle * CV_PI / 180.0)),
                                 static_cast<float>(std::cos(angle * CV_PI / 180.0)));
        EXPECT_NEAR(distanceToGeometry(ruler, f, on + normal * 7.0F), 7.0, 1e-2);
    }
}

TEST(HitTesting, ClosedShapesAreReachableFromEverySide) {
    const ToolGeometry blob = BlobGeometry{{100.0F, 100.0F}, 80.0F, 60.0F, 20.0F, true};
    // Los cuatro lados del rectángulo, tocados desde fuera a 3 px.
    EXPECT_NEAR(distanceToGeometry(blob, identity(), {100.0F, 67.0F}), 3.0, 1e-3);
    EXPECT_NEAR(distanceToGeometry(blob, identity(), {100.0F, 133.0F}), 3.0, 1e-3);
    EXPECT_NEAR(distanceToGeometry(blob, identity(), {57.0F, 100.0F}), 3.0, 1e-3);
    EXPECT_NEAR(distanceToGeometry(blob, identity(), {143.0F, 100.0F}), 3.0, 1e-3);

    const ToolGeometry poly = PolyBlobGeometry{
        {{0.0F, 0.0F}, {100.0F, 0.0F}, {100.0F, 100.0F}, {0.0F, 100.0F}}, 20.0F, true};
    EXPECT_NEAR(distanceToGeometry(poly, identity(), {50.0F, -4.0F}), 4.0, 1e-3);
    EXPECT_NEAR(distanceToGeometry(poly, identity(), {50.0F, 104.0F}), 4.0, 1e-3);
    // El polígono se cierra solo: el lado último->primero también cuenta.
    const ToolGeometry openLooking =
        PolyBlobGeometry{{{0.0F, 0.0F}, {100.0F, 0.0F}, {100.0F, 100.0F}}, 20.0F, true};
    EXPECT_NEAR(distanceToGeometry(openLooking, identity(), {50.0F, 50.0F}), 0.0, 1e-3);
}

TEST(HitTesting, DegeneratePolygonsDoNotCrashOrReturnNaN) {
    // Desgaste: polígonos a medio dibujar o rotos por una importación.
    for (const std::vector<cv::Point2f>& verts :
         std::vector<std::vector<cv::Point2f>>{{},
                                               {{10.0F, 10.0F}},
                                               {{10.0F, 10.0F}, {10.0F, 10.0F}},
                                               {{0.0F, 0.0F}, {50.0F, 0.0F}}}) {
        const ToolGeometry g = PolyBlobGeometry{verts, 20.0F, true};
        const double d = distanceToGeometry(g, identity(), {25.0F, 25.0F});
        EXPECT_FALSE(std::isnan(d));
        EXPECT_GE(d, 0.0);
    }
}

TEST(HitTesting, MultiSegmentToolsPickTheirNearestPart) {
    const ToolGeometry lineToLine =
        LineToLineGeometry{{0.0F, 0.0F}, {100.0F, 0.0F}, {0.0F, 80.0F}, {100.0F, 80.0F}};
    EXPECT_NEAR(distanceToGeometry(lineToLine, identity(), {50.0F, 6.0F}), 6.0, kEps);
    EXPECT_NEAR(distanceToGeometry(lineToLine, identity(), {50.0F, 74.0F}), 6.0, kEps);
    EXPECT_NEAR(distanceToGeometry(lineToLine, identity(), {50.0F, 40.0F}), 40.0, kEps);

    const ToolGeometry angle = AngleGeometry{{0.0F, 0.0F}, {60.0F, 0.0F}, {0.0F, 60.0F}};
    EXPECT_NEAR(distanceToGeometry(angle, identity(), {30.0F, 2.0F}), 2.0, kEps);
    EXPECT_NEAR(distanceToGeometry(angle, identity(), {2.0F, 30.0F}), 2.0, kEps);

    const ToolGeometry pointToLine = PointToLineGeometry{
        {0.0F, 0.0F}, {100.0F, 0.0F}, {50.0F, -30.0F}, {50.0F, 30.0F}};
    EXPECT_NEAR(distanceToGeometry(pointToLine, identity(), {80.0F, 3.0F}), 3.0, kEps);
    EXPECT_NEAR(distanceToGeometry(pointToLine, identity(), {53.0F, 25.0F}), 3.0, kEps);
}

// ---------------------------------------------------------------------------
// Tolerancia de clic: el defecto que motivó estas pruebas
// ---------------------------------------------------------------------------

TEST(PickTolerance, GrabZoneStaysConstantOnScreenAcrossZoom) {
    // La manija se dibuja siempre del mismo tamaño en pantalla, así que la zona
    // de agarre medida en pantalla no puede depender del zoom.
    const cv::Size image(1920, 1080);
    const cv::Size widget(900, 640);
    for (const double zoom : {1.0, 2.0, 4.0, 8.0}) {
        const ViewTransform v(image, widget, zoom, {0.0, 0.0});
        const double toleranceInImagePx = pickTolerance(9.0, v.displayScale());
        // De vuelta a pantalla: siempre 9 px, con cualquier zoom.
        EXPECT_NEAR(toleranceInImagePx * v.displayScale(), 9.0, 1e-9) << "zoom " << zoom;
    }
}

TEST(PickTolerance, FallsBackToScreenPixelsWhenThereIsNoView) {
    EXPECT_NEAR(pickTolerance(14.0, 0.0), 14.0, kEps);
    EXPECT_NEAR(pickTolerance(14.0, -1.0), 14.0, kEps);
    EXPECT_TRUE(std::isfinite(pickTolerance(14.0, 1e-12)));
}

TEST(PickTolerance, ZoomedInAClickFarFromAHandleNoLongerGrabsIt) {
    // Regresión del fallo real: con la tolerancia fijada en píxeles de imagen,
    // al 800 % una manija se agarraba desde ~70 px de pantalla, y un clic en un
    // sitio claramente vacío deformaba la herramienta.
    const ViewTransform v({1920, 1080}, {900, 640}, 8.0, {0.0, 0.0});
    const ToolGeometry g = CaliperGeometry{{500.0F, 500.0F}, {600.0F, 500.0F}, 10.0F};

    // A 8× la escala es 3.75, así que los 9 px de imagen del criterio viejo
    // eran ~34 px de pantalla. Un clic a 20 px de la manija está claramente
    // fuera de lo que se ve, y sin embargo la agarraba.
    ASSERT_NEAR(v.displayScale(), 3.75, 1e-9);
    const cv::Point2d handleOnScreen = v.imageToWidget({500.0F, 500.0F});
    const cv::Point2f clickFar = v.widgetToImage(handleOnScreen + cv::Point2d(20.0, 0.0));
    EXPECT_EQ(pickHandle(g, identity(), clickFar, pickTolerance(9.0, v.displayScale())), -1);
    // Con el criterio viejo (9 px de imagen) sí lo habría agarrado.
    EXPECT_EQ(pickHandle(g, identity(), clickFar, 9.0), 0);

    // Y lo que se ve encima de la manija sí se agarra.
    const cv::Point2f clickOn = v.widgetToImage(handleOnScreen + cv::Point2d(4.0, 3.0));
    EXPECT_EQ(pickHandle(g, identity(), clickOn, pickTolerance(9.0, v.displayScale())), 0);
}

TEST(PickTolerance, ZoomedOutTheHandleYouSeeIsStillGrabbable) {
    // El otro lado del mismo fallo: una imagen grande en una ventana pequeña
    // hace que 9 px de imagen sean ~2 px de pantalla, puntería imposible.
    const ViewTransform v({4000, 3000}, {600, 400}, 1.0, {0.0, 0.0});
    ASSERT_LT(v.displayScale(), 0.2);
    const ToolGeometry g = RulerGeometry{{2000.0F, 1500.0F}, {2400.0F, 1500.0F}};

    const cv::Point2d handleOnScreen = v.imageToWidget({2000.0F, 1500.0F});
    const cv::Point2f clickNear = v.widgetToImage(handleOnScreen + cv::Point2d(5.0, 0.0));
    EXPECT_EQ(pickHandle(g, identity(), clickNear, pickTolerance(9.0, v.displayScale())), 0);
    EXPECT_EQ(pickHandle(g, identity(), clickNear, 9.0), -1);  // criterio viejo: fallaba
}

TEST(PickTolerance, ClosestHandleWinsWhenTwoAreNear) {
    const ToolGeometry g =
        LineToLineGeometry{{0.0F, 0.0F}, {100.0F, 0.0F}, {0.0F, 20.0F}, {100.0F, 20.0F}};
    EXPECT_EQ(pickHandle(g, identity(), {2.0F, 1.0F}, 30.0), 0);
    EXPECT_EQ(pickHandle(g, identity(), {98.0F, 1.0F}, 30.0), 1);
    EXPECT_EQ(pickHandle(g, identity(), {1.0F, 19.0F}, 30.0), 2);
    EXPECT_EQ(pickHandle(g, identity(), {99.0F, 21.0F}, 30.0), 3);
    EXPECT_EQ(pickHandle(g, identity(), {50.0F, 10.0F}, 5.0), -1);  // en medio: nada
}

TEST(PickTolerance, HandlesAreFoundThroughARotatedFixture) {
    const ToolGeometry g = CaliperGeometry{{0.0F, 0.0F}, {100.0F, 0.0F}, 10.0F};
    const auto f = fixtureAt({640.0F, 360.0F}, 37.0);
    const cv::Point2f secondHandle = pci::vision::toImageCoords(f, {100.0F, 0.0F});
    EXPECT_EQ(pickHandle(g, f, secondHandle, 9.0), 1);
    EXPECT_EQ(pickHandle(g, f, secondHandle + cv::Point2f(30.0F, 30.0F), 9.0), -1);
}

// ---------------------------------------------------------------------------
// Desgaste
// ---------------------------------------------------------------------------

TEST(CanvasStress, SelectingAmongManyToolsAlwaysPicksTheNearest) {
    // Una plantilla cargada de herramientas: el clic debe caer siempre en la
    // que está debajo, sin que el orden de la lista influya.
    std::vector<ToolGeometry> tools;
    tools.reserve(400);
    for (int i = 0; i < 400; ++i) {
        const float x = static_cast<float>((i % 20) * 90 + 30);
        const float y = static_cast<float>((i / 20) * 90 + 30);
        tools.emplace_back(RulerGeometry{{x, y}, {x + 40.0F, y}});
    }

    std::mt19937 rng(4242);
    std::uniform_int_distribution<int> pick(0, 399);
    for (int trial = 0; trial < 400; ++trial) {
        const int expected = pick(rng);
        const auto& target = std::get<RulerGeometry>(tools[static_cast<std::size_t>(expected)]);
        const cv::Point2f click((target.p0.x + target.p1.x) / 2.0F, target.p0.y + 1.0F);

        int best = -1;
        double bestDistance = 14.0;
        for (int i = 0; i < static_cast<int>(tools.size()); ++i) {
            const double d = distanceToGeometry(tools[static_cast<std::size_t>(i)],
                                                identity(), click);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        EXPECT_EQ(best, expected) << "intento " << trial;
    }
}

TEST(CanvasStress, ExtremeCoordinatesNeverProduceNaN) {
    // Geometrías con valores absurdos (importadas, o de un arrastre disparado).
    const std::vector<ToolGeometry> hostile = {
        CaliperGeometry{{-1e6F, -1e6F}, {1e6F, 1e6F}, 10.0F},
        CircleGeometry{{0.0F, 0.0F}, 1e6F, 12.0F, 36},
        CircleGeometry{{0.0F, 0.0F}, 0.0F, 12.0F, 36},
        BlobGeometry{{0.0F, 0.0F}, 0.0F, 0.0F, 20.0F, true},
        AngleGeometry{{1.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 1.0F}},
        PolyBlobGeometry{{{1e5F, 1e5F}, {-1e5F, 1e5F}, {0.0F, -1e5F}}, 20.0F, true},
    };
    for (const auto& g : hostile) {
        for (const auto& p : std::vector<cv::Point2f>{{0.0F, 0.0F}, {1e6F, -1e6F}, {7.0F, 7.0F}}) {
            const double d = distanceToGeometry(g, fixtureAt({320.0F, 240.0F}, 61.0), p);
            EXPECT_FALSE(std::isnan(d));
            EXPECT_GE(d, 0.0);
        }
        for (const auto& hp : handlePoints(g)) {
            EXPECT_TRUE(std::isfinite(hp.x) && std::isfinite(hp.y));
        }
    }
}

TEST(CanvasStress, ARandomWalkOfViewChangesKeepsTheViewValid) {
    // Simula una sesión larga: rueda arriba y abajo, arrastres al límite y
    // redimensionados de ventana. En ningún momento la vista puede degenerar.
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> factorDist(0.5, 2.0);
    std::uniform_real_distribution<double> dragDist(-800.0, 800.0);
    std::uniform_int_distribution<int> widthDist(120, 1600);
    std::uniform_int_distribution<int> heightDist(90, 1200);

    const cv::Size image(1280, 720);
    cv::Size widget(900, 600);
    double zoom = 1.0;
    cv::Point2d pan(0.0, 0.0);

    for (int step = 0; step < 5000; ++step) {
        switch (step % 3) {
            case 0:
                zoom = std::clamp(zoom * factorDist(rng), 1.0, 8.0);
                break;
            case 1:
                pan = ViewTransform(image, widget, zoom, pan)
                          .clampedPan(pan + cv::Point2d(dragDist(rng), dragDist(rng)));
                break;
            default:
                widget = cv::Size(widthDist(rng), heightDist(rng));
                pan = ViewTransform(image, widget, zoom, pan).clampedPan(pan);
                break;
        }

        const ViewTransform v(image, widget, zoom, pan);
        const ViewRect t = v.targetRect();
        ASSERT_TRUE(std::isfinite(t.x) && std::isfinite(t.y)) << "paso " << step;
        ASSERT_GT(t.width, 0.0) << "paso " << step;
        ASSERT_GT(t.height, 0.0) << "paso " << step;
        ASSERT_NEAR(t.width / t.height, 1280.0 / 720.0, 1e-6) << "paso " << step;
        ASSERT_GT(v.displayScale(), 0.0) << "paso " << step;

        // El centro de la ventana siempre señala un punto DENTRO de la imagen:
        // es la garantía de que el operador no puede perder la pieza de vista.
        const cv::Point2f center = v.widgetToImage({widget.width / 2.0, widget.height / 2.0});
        ASSERT_GE(center.x, -0.5F) << "paso " << step;
        ASSERT_LE(center.x, image.width + 0.5F) << "paso " << step;
        ASSERT_GE(center.y, -0.5F) << "paso " << step;
        ASSERT_LE(center.y, image.height + 0.5F) << "paso " << step;
    }
}

TEST(CanvasStress, MovingAToolNeverDeformsIt) {
    // Invariante que vale para los diez tipos: arrastrar una herramienta
    // desplaza TODAS sus manijas por el mismo delta y no cambia nada más. Si un
    // tipo olvidara un campo (el segmento de escaneo, un vértice), la
    // herramienta se deformaría al moverla y mediría otra cosa sin avisar.
    const std::vector<ToolGeometry> geometries = {
        CaliperGeometry{{10.0F, 20.0F}, {90.0F, 40.0F}, 10.0F},
        CircleGeometry{{100.0F, 100.0F}, 40.0F, 12.0F, 36},
        PointToLineGeometry{{0.0F, 0.0F}, {50.0F, 0.0F}, {25.0F, -20.0F}, {25.0F, 20.0F}},
        EdgeFlawGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 20},
        BlobGeometry{{100.0F, 100.0F}, 80.0F, 60.0F, 20.0F, true},
        RulerGeometry{{1.0F, 2.0F}, {3.0F, 4.0F}},
        LineToLineGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, {0.0F, 30.0F}, {40.0F, 30.0F}},
        AngleGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, {0.0F, 40.0F}},
        PolyBlobGeometry{{{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}},
                         20.0F, true},
        PositionGeometry{{12.0F, 34.0F}, PositionAxis::Radial},
    };
    const cv::Point2f delta(37.5F, -12.25F);

    for (const auto& base : geometries) {
        const auto before = handlePoints(base);
        ToolGeometry moved = base;
        translateGeometry(moved, delta);
        const auto after = handlePoints(moved);
        ASSERT_EQ(before.size(), after.size());
        for (std::size_t i = 0; i < before.size(); ++i) {
            EXPECT_NEAR(after[i].x, before[i].x + delta.x, 1e-3);
            EXPECT_NEAR(after[i].y, before[i].y + delta.y, 1e-3);
        }
        // Y los puntos del marco de selección se mueven igual (mismo recuento).
        EXPECT_EQ(referencePoints(moved).size(), referencePoints(base).size());
    }
}

TEST(CanvasStress, MovingAToolBackAndForthLeavesItWhereItStarted) {
    // Un arrastre largo aplica cientos de deltas pequeños: si acumularan error,
    // la herramienta quedaría desplazada tras pasearla y devolverla.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> stepDist(-9.0F, 9.0F);
    ToolGeometry g =
        PolyBlobGeometry{{{100.0F, 100.0F}, {180.0F, 110.0F}, {150.0F, 190.0F}}, 20.0F, true};
    const auto start = handlePoints(g);

    cv::Point2f total(0.0F, 0.0F);
    for (int i = 0; i < 2000; ++i) {
        const cv::Point2f d(stepDist(rng), stepDist(rng));
        translateGeometry(g, d);
        total += d;
    }
    translateGeometry(g, -total);  // vuelta exacta al punto de partida

    const auto end = handlePoints(g);
    for (std::size_t i = 0; i < start.size(); ++i) {
        EXPECT_NEAR(end[i].x, start[i].x, 0.05F);  // deriva de coma flotante acotada
        EXPECT_NEAR(end[i].y, start[i].y, 0.05F);
    }
}

TEST(CanvasStress, DraggingAHandleManyTimesConvergesWhereItIsDropped) {
    // Arrastre continuo: cada paso lee la manija, la mueve y la vuelve a leer.
    // Si hubiera deriva acumulada, la manija se separaría del cursor.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> step(-25.0F, 25.0F);
    ToolGeometry g = AngleGeometry{{100.0F, 100.0F}, {200.0F, 100.0F}, {100.0F, 200.0F}};
    cv::Point2f cursor = handlePoints(g)[1];

    for (int i = 0; i < 3000; ++i) {
        cursor += cv::Point2f(step(rng), step(rng));
        setHandlePoint(g, 1, cursor);
        const cv::Point2f read = handlePoints(g)[1];
        ASSERT_NEAR(read.x, cursor.x, 1e-3F) << "paso " << i;
        ASSERT_NEAR(read.y, cursor.y, 1e-3F) << "paso " << i;
    }
}
