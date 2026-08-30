#include "inspection_editor/piece_report.h"
#include "vision/quality_metrics.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include "vision/geometry_features.h"

namespace pci::inspection {

namespace {

// Una fila de HECHO del contorno: no la mide una herramienta, así que no tiene
// ni estado ni tolerancia. Se construye pasando por `ToolRunResult` para que la
// conversión de unidades sea LA MISMA que la de las cotas — dos caminos
// distintos hacia el mismo milímetro acabarían dando dos milímetros distintos.
MeasurementRow fact(const std::string& name, double value, MeasuredKind kind,
                    double mmPerPixel, LengthUnit unit, const std::string& detail = {}) {
    ToolRunResult reading;
    reading.name = name;
    reading.measured = value;
    reading.kind = kind;
    reading.ok = true;
    reading.informative = true;  // informa, no juzga
    reading.detail = detail;
    auto rows = measurementRows({reading}, mmPerPixel, unit);
    rows.front().group = kGroupContour;
    return rows.front();
}

}  // namespace

std::string describeShape(const vision::ShapeClass& shape) {
    switch (shape.kind) {
        case vision::ShapeKind::Circle: return "Pieza redonda";
        case vision::ShapeKind::Ring: return "Arandela";
        case vision::ShapeKind::Polygon:
            return "Polígono de " + std::to_string(shape.sides) + " lados";
        case vision::ShapeKind::Rounded:
            return "Polígono redondeado de " + std::to_string(shape.sides) + " lados";
        case vision::ShapeKind::Irregular: return "Pieza de contorno libre";
    }
    return "Pieza";
}

std::size_t PieceReport::contourFactCount() const {
    return static_cast<std::size_t>(
        std::count_if(rows.begin(), rows.end(), [](const MeasurementRow& row) {
            return row.group == kGroupContour;
        }));
}

PieceReport measureWholePiece(const cv::Mat& gray, const cv::Mat& mask,
                              const vision::Fixture& fixture, double mmPerPixel,
                              LengthUnit unit, const cv::Size& frame) {
    PieceReport report;
    if (gray.empty() || mask.empty()) {
        report.problem = "No hay imagen que medir.";
        return report;
    }

    const vision::ContourReport contour = vision::describeContour(mask);
    if (!contour.valid || contour.outer.size() < 3) {
        report.problem =
            "No se distingue el contorno de la pieza: ajusta la luz o el umbral de "
            "detección antes de medir.";
        return report;
    }

    report.shape = vision::classifyShape(contour.outer, mask);
    report.headline = describeShape(report.shape);

    // La unidad se resuelve UNA VEZ para el informe entero.
    //
    // En automático, cada medida elige mm o cm según su propio tamaño, y para
    // una etiqueta suelta sobre la pieza está bien: se lee sola. En una TABLA
    // no, porque una tabla existe para comparar filas, y un perímetro en cm
    // junto a un lado en mm obliga a convertir de cabeza en cada renglón. Se
    // decide con la medida MAYOR de la pieza, que es la que fija su escala.
    if (unit == LengthUnit::Auto && mmPerPixel > 0.0) {
        const double longestMm =
            std::max(contour.perimeter,
                     static_cast<double>(std::max(contour.minRect.size.width,
                                                  contour.minRect.size.height))) *
            mmPerPixel;
        unit = longestMm >= 100.0 ? LengthUnit::Centimeters : LengthUnit::Millimeters;
    }

    // --- Los hechos del contorno -------------------------------------------
    //
    // Estos van SIEMPRE, sea cual sea la figura, porque no dependen de haberla
    // reconocido: son lo que el contorno es. Y son justo los que hasta ahora no
    // se podían leer en ningún sitio salvo un rótulo en una esquina del editor.
    report.rows.push_back(fact("Perímetro", contour.perimeter, MeasuredKind::Length,
                               mmPerPixel, unit,
                               "contorno exterior cerrado, sin los agujeros"));
    report.rows.push_back(fact("Área", contour.area, MeasuredKind::Area, mmPerPixel, unit,
                               "con los agujeros ya descontados"));

    // Envolvente GIRADA y no la recta: el largo y el ancho reales de la pieza
    // no dependen de cómo esté puesta en la mesa, y con la envolvente recta sí
    // — una pieza girada 45° daría un rectángulo mucho mayor que ella.
    const double longSide = std::max(contour.minRect.size.width, contour.minRect.size.height);
    const double shortSide = std::min(contour.minRect.size.width, contour.minRect.size.height);
    report.rows.push_back(fact("Largo total", longSide, MeasuredKind::Length, mmPerPixel,
                               unit, "envolvente mínima girada"));
    report.rows.push_back(fact("Ancho total", shortSide, MeasuredKind::Length, mmPerPixel,
                               unit, "envolvente mínima girada"));

    report.rows.push_back(fact("Agujeros", static_cast<double>(contour.holes.size()),
                               MeasuredKind::Count, mmPerPixel, unit));

    // Circularidad 4πA/P². Vale 1 en un círculo perfecto y baja según la pieza
    // se aparta de él. Se da siempre porque es la forma más rápida de ver que
    // una pieza que debería ser redonda ha dejado de serlo.
    if (contour.perimeter > 1e-6) {
        const double circularity =
            4.0 * CV_PI * contour.area / (contour.perimeter * contour.perimeter);
        report.rows.push_back(fact("Circularidad", std::min(circularity, 1.0),
                                   MeasuredKind::Fraction, mmPerPixel, unit,
                                   "4·pi·area/perimetro^2; 1 es un circulo perfecto"));
    }

    // Cuántos tramos rectos y cuántos arcos: es el resumen de la FORMA, y lo
    // que explica de dónde salen las cotas de abajo.
    int lines = 0;
    int arcs = 0;
    for (const auto& primitive : contour.primitives) {
        (primitive.kind == vision::PrimitiveKind::Line ? lines : arcs)++;
    }
    report.rows.push_back(fact("Tramos rectos", lines, MeasuredKind::Count, mmPerPixel, unit,
                               report.shape.reason));
    report.rows.push_back(fact("Arcos", arcs, MeasuredKind::Count, mmPerPixel, unit));

    // --- Las cotas ----------------------------------------------------------
    //
    // Sin tope, y esa es la diferencia con el diálogo de propuestas: aquello es
    // una lista que hay que revisar a mano y por eso se corta en doce; esto es
    // un informe, y un informe cortado contesta a medias. El número sigue
    // acotado por la propia pieza — salen las cotas que su contorno tiene.
    ProposeOptions everything;
    everything.maxProposals = 200;
    report.watchable = proposeTools(gray, mask, fixture, everything, mmPerPixel);

    std::vector<ToolRunResult> asResults;
    std::vector<ToolConfig> asTools;
    asResults.reserve(report.watchable.size());
    asTools.reserve(report.watchable.size());
    for (const auto& proposal : report.watchable) {
        ToolRunResult reading;
        reading.toolId = static_cast<std::int64_t>(asResults.size());
        reading.name = proposal.config.name;
        reading.type = proposal.config.type;
        reading.measured = proposal.measured;
        reading.kind = proposal.kind;
        reading.detail = proposal.reason;
        // Una cota recién medida sobre esta pieza está dentro de su propia
        // tolerancia por construcción: la banda se sugirió A PARTIR de ella.
        // Marcarla «OK» sería dar por comprobado algo que nadie ha comprobado
        // todavía, así que se deja informativa hasta que haya una inspección de
        // verdad.
        reading.ok = true;
        reading.informative = true;
        ToolConfig config = proposal.config;
        config.id = reading.toolId;
        asResults.push_back(std::move(reading));
        asTools.push_back(std::move(config));
    }
    auto dimensionRows = measurementRows(asResults, mmPerPixel, unit, &asTools);
    for (auto& row : dimensionRows) {
        row.group = kGroupDimension;
        report.rows.push_back(std::move(row));
    }

    // LOS AVISOS, al final: hacen falta el contorno y el area ya medidos.
    //
    // Van dentro del informe y no en la barra de estado porque el informe se
    // EXPORTA. Un aviso que se queda en la ventana llega a la mitad de la gente
    // que lo necesita; la otra mitad se lleva las cifras sin el.
    if (frame.width > 0 && frame.height > 0) {
        const auto contact = vision::pieceTouchesFrame(contour.outer, frame);
        if (std::string warning = vision::frameContactWarning(contact); !warning.empty()) {
            report.warnings.push_back(std::move(warning));
        }
    }
    if (vision::contourLooksRagged(contour.area, contour.perimeter)) {
        report.warnings.push_back(
            "El contorno mide " +
            std::to_string(static_cast<int>(std::lround(
                100.0 * vision::contourRaggedness(contour.area, contour.perimeter)))) +
            " % del perimetro de un circulo de la misma area: o la pieza es muy "
            "dentada, o la deteccion esta siguiendo el dibujo de la superficie en vez "
            "del borde. En ese caso el perimetro no es de fiar.");
    }

    // Y EL AVISO DE QUE ESTE INFORME NO PUEDE RECHAZAR NADA.
    //
    // Las cotas son de dos clases y cada una lo dice en su motivo: las que
    // COMPRUEBAN —un diámetro, la redondez, el paso de una rosca— vuelven a
    // medir en cada inspección, y las de REFERENCIA repiten el valor de hoy
    // porque salen de la descomposición de ESTE contorno («Lado 4» es el cuarto
    // tramo en que se cortó esta pieza, y en la de al lado el cuarto tramo es
    // otra cosa).
    //
    // Una pieza a la que solo le salen de las segundas se puede guardar como
    // plantilla entera y no rechazará nunca nada. Antes no pasaba nunca, pero
    // porque el hueco lo tapaba el área de la Región — y sobre trece de las
    // diecisiete fotos del banco esa área no se parecía a la de la pieza, así
    // que la garantía se cumplía con un número que no valía. Ahora esa cota no
    // se publica cuando no reproduce el contorno, y donde no quede ninguna
    // comprobación hay que DECIRLO: es la mitad que faltaba de la misma regla.
    const bool nothingRechecks =
        !report.watchable.empty() &&
        std::none_of(report.watchable.begin(), report.watchable.end(),
                     [](const AutoProposal& proposal) {
                         return proposal.reason.find("no como comprobación") ==
                                std::string::npos;
                     });
    if (nothingRechecks) {
        report.warnings.push_back(
            "Ninguna de estas cotas vuelve a medir: todas salen de la descomposicion "
            "de ESTE contorno y guardadas repetirian el valor de hoy. Sirven para "
            "apuntar la pieza, no para rechazar la siguiente. Si hace falta vigilarla, "
            "dibuja a mano la cota que la juzgue.");
    }

    report.ok = true;
    return report;
}

}  // namespace pci::inspection
