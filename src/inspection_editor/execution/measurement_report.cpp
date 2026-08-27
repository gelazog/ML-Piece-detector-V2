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
            const UnitPick pick = pickArea(mm2, unit);
            return Converted{pick.value, pick.suffix};
        }
        case MeasuredKind::Length: {
            if (mmPerPixel <= 0.0 || unit == LengthUnit::Pixels) {
                return {result.measured, "px"};
            }
            const double mm = result.measured * mmPerPixel;
            const UnitPick pick = pickLength(mm, unit);
            return Converted{pick.value, pick.suffix};
        }
    }
    return {result.measured, "px"};
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

std::string measurementsToCsv(const std::vector<MeasurementRow>& rows,
                              const std::vector<std::string>& warnings,
                              const core::CsvDialect& dialect) {
    // Los avisos van primero: el CSV es lo que sobrevive a la ventana, y un
    // aviso que no viaja con el fichero no llega a quien lo abre despues.
    const char sep = dialect.separator;
    std::string preamble;
    for (const auto& warning : warnings) {
        preamble += "AVISO" + std::string(1, sep) + core::csvField(warning, dialect) + "\n";
    }
    if (!preamble.empty()) {
        preamble += "\n";
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    // La marca de orden de bytes va la PRIMERA de todo, antes incluso de los
    // avisos: si va después, Excel ya ha decidido que el fichero es ANSI.
    out << core::csvByteOrderMark(dialect) << preamble;
    // `grupo` va la ÚLTIMA a propósito: añadir una columna al final no mueve
    // ninguna de las que ya había, así que una hoja de cálculo hecha con la
    // versión anterior sigue apuntando a la misma columna.
    for (const char* column :
         {"herramienta", "valor", "unidad", "pixeles", "estado", "tolerancia_min",
          "tolerancia_max", "pieza", "detalle", "grupo"}) {
        if (column != std::string("herramienta")) {
            out << sep;
        }
        out << column;
    }
    out << '\n';
    for (const auto& row : rows) {
        out << core::csvField(row.tool, dialect) << sep
            << core::csvNumber(row.value, 4, dialect) << sep
            << core::csvField(row.unit, dialect) << sep
            << core::csvNumber(row.pixels, 2, dialect) << sep << row.state << sep;
        if (row.hasTolerance) {
            out << core::csvNumber(row.toleranceMin, 4, dialect) << sep
                << core::csvNumber(row.toleranceMax, 4, dialect);
        } else {
            out << sep;  // dos columnas vacías: no hay banda que escribir
        }
        out << sep << (row.pieceIndex + 1) << sep << core::csvField(row.detail, dialect)
            << sep << core::csvField(row.group, dialect) << '\n';
    }
    return out.str();
}

std::string measurementsToText(const std::vector<MeasurementRow>& rows,
                               const std::vector<std::string>& warnings) {
    std::string preamble;
    for (const auto& warning : warnings) {
        preamble += "AVISO: " + warning + "\n";
    }
    if (!preamble.empty()) {
        preamble += "\n";
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << preamble;
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
