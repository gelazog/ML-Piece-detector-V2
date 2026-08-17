#include "inspection_editor/execution/measurement_report.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace pci::inspection {

namespace {

// A qué unidad se lleva cada clase de medida, y con qué factor desde lo que
// midió la herramienta.
//
// Es la MISMA decisión que toma `formatMeasure` para la pantalla, y por eso el
// criterio de mm/cm se repite tal cual: si el informe y la etiqueta que hay
// encima de la pieza dijeran unidades distintas para el mismo número, el
// operador tendría que averiguar cuál de los dos se cree.
struct Converted {
    double value = 0.0;
    std::string unit;
};

Converted convert(const ToolRunResult& result, double mmPerPixel, LengthUnit unit) {
    switch (result.kind) {
        case MeasuredKind::Angle: return {result.measured, "°"};
        case MeasuredKind::Count: return {result.measured, "n"};
        case MeasuredKind::Fraction: return {result.measured, "—"};
        case MeasuredKind::Area: {
            if (mmPerPixel <= 0.0 || unit == LengthUnit::Pixels) {
                return {result.measured, "px²"};
            }
            const double mm2 = result.measured * mmPerPixel * mmPerPixel;
            const bool useCm =
                unit == LengthUnit::Centimeters || (unit == LengthUnit::Auto && mm2 >= 10000.0);
            return useCm ? Converted{mm2 / 100.0, "cm²"} : Converted{mm2, "mm²"};
        }
        case MeasuredKind::Length: {
            if (mmPerPixel <= 0.0 || unit == LengthUnit::Pixels) {
                return {result.measured, "px"};
            }
            const double mm = result.measured * mmPerPixel;
            const bool useCm =
                unit == LengthUnit::Centimeters || (unit == LengthUnit::Auto && mm >= 100.0);
            return useCm ? Converted{mm / 10.0, "cm"} : Converted{mm, "mm"};
        }
    }
    return {result.measured, "px"};
}

// Un campo de CSV que puede llevar comas o comillas —el detalle las lleva— se
// entrecomilla y sus comillas se duplican. Sin esto, una sola coma en un texto
// desplaza todas las columnas siguientes de esa fila, y el fallo no se ve hasta
// que alguien abre la hoja y encuentra la tolerancia en la columna del estado.
std::string quoted(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

}  // namespace

std::vector<MeasurementRow> measurementRows(const std::vector<ToolRunResult>& results,
                                            double mmPerPixel, LengthUnit unit,
                                            const std::vector<ToolConfig>* tolerances) {
    std::vector<MeasurementRow> rows;
    rows.reserve(results.size());
    for (const auto& result : results) {
        MeasurementRow row;
        row.tool = result.name;
        const Converted converted = convert(result, mmPerPixel, unit);
        row.value = converted.value;
        row.unit = converted.unit;
        row.pixels = result.measured;
        // Una construcción geométrica que salió bien no es un OK: no ha juzgado
        // nada, solo ha calculado un datum. Escribir «OK» sería dar por
        // comprobado lo que nadie comprobó.
        row.state = (result.informative && result.ok) ? "—" : (result.ok ? "OK" : "NG");
        row.pieceIndex = result.pieceIndex;
        row.detail = result.detail;

        if (tolerances != nullptr && !result.informative) {
            const auto found = std::find_if(
                tolerances->begin(), tolerances->end(), [&result](const ToolConfig& config) {
                    return config.id == result.toolId && config.name == result.name;
                });
            if (found != tolerances->end()) {
                // La banda se guarda en las MISMAS unidades que la medida, y no
                // en píxeles: una tolerancia que hay que convertir a mano al
                // leerla es una tolerancia que alguien va a leer mal.
                ToolRunResult asMeasure = result;
                asMeasure.measured = found->toleranceMin;
                row.toleranceMin = convert(asMeasure, mmPerPixel, unit).value;
                asMeasure.measured = found->toleranceMax;
                row.toleranceMax = convert(asMeasure, mmPerPixel, unit).value;
                row.hasTolerance = true;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string measurementsToCsv(const std::vector<MeasurementRow>& rows) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    // `grupo` va la ÚLTIMA a propósito: añadir una columna al final no mueve
    // ninguna de las que ya había, así que una hoja de cálculo hecha con la
    // versión anterior sigue apuntando a la misma columna.
    out << "herramienta,valor,unidad,pixeles,estado,tolerancia_min,tolerancia_max,"
           "pieza,detalle,grupo\n";
    out << std::fixed;
    for (const auto& row : rows) {
        out << quoted(row.tool) << ',' << std::setprecision(4) << row.value << ','
            << quoted(row.unit) << ',' << std::setprecision(2) << row.pixels << ','
            << row.state << ',';
        if (row.hasTolerance) {
            out << std::setprecision(4) << row.toleranceMin << ','
                << std::setprecision(4) << row.toleranceMax;
        } else {
            out << ',';  // dos columnas vacías: no hay banda que escribir
        }
        out << ',' << (row.pieceIndex + 1) << ',' << quoted(row.detail) << ','
            << quoted(row.group) << '\n';
    }
    return out.str();
}

std::string measurementsToText(const std::vector<MeasurementRow>& rows) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2);

    // El ancho de la primera columna sale de los nombres que hay, no de un
    // número fijo: con nombres largos, una tabla alineada a 20 caracteres deja
    // de estar alineada justo cuando más falta hace.
    std::size_t widest = 11;  // «Herramienta»
    for (const auto& row : rows) {
        widest = std::max(widest, row.tool.size());
    }
    for (const auto& row : rows) {
        out << std::left << std::setw(static_cast<int>(widest)) << row.tool << "  "
            << std::right << std::setw(12) << row.value << ' ' << std::left
            << std::setw(4) << row.unit << "  " << row.state;
        if (row.hasTolerance) {
            out << "  [" << row.toleranceMin << " … " << row.toleranceMax << ']';
        }
        if (!row.detail.empty()) {
            out << "  " << row.detail;
        }
        out << '\n';
    }
    return out.str();
}

}  // namespace pci::inspection
