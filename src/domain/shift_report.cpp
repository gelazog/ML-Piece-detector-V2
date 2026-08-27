#include "domain/shift_report.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace pci::domain {

namespace {


std::string hourOf(const std::string& timestamp) {
    // "YYYY-MM-DD HH:MM:SS" -> "YYYY-MM-DD HH". Si viene con otra forma, se
    // devuelve tal cual: agrupar mal es mejor que perder la fila.
    return timestamp.size() >= 13 ? timestamp.substr(0, 13) : timestamp;
}

std::string percent(double fraction) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << (100.0 * fraction);
    return out.str();
}

}  // namespace

ShiftSummary summarise(const std::vector<InspectionRow>& rows) {
    ShiftSummary summary;
    summary.total = static_cast<int>(rows.size());
    if (rows.empty()) {
        return summary;
    }

    std::map<std::string, int> reasonCounts;
    std::map<std::string, HourSlice> hours;
    for (const auto& row : rows) {
        if (row.ok) {
            ++summary.okCount;
        } else {
            ++summary.ngCount;
            if (!row.reason.empty()) {
                ++reasonCounts[row.reason];
            }
        }
        if (summary.from.empty() || row.startedAt < summary.from) {
            summary.from = row.startedAt;
        }
        if (summary.to.empty() || row.startedAt > summary.to) {
            summary.to = row.startedAt;
        }
        auto& slice = hours[hourOf(row.startedAt)];
        slice.hour = hourOf(row.startedAt);
        ++slice.total;
        if (!row.ok) {
            ++slice.ngCount;
        }
    }
    summary.yield = static_cast<double>(summary.okCount) / summary.total;

    summary.reasons.reserve(reasonCounts.size());
    for (const auto& [reason, count] : reasonCounts) {
        summary.reasons.push_back({reason, count});
    }
    // Del más frecuente al menos, y con desempate por el texto para que dos
    // informes del mismo turno salgan idénticos.
    std::sort(summary.reasons.begin(), summary.reasons.end(),
              [](const ReasonCount& a, const ReasonCount& b) {
                  if (a.count != b.count) {
                      return a.count > b.count;
                  }
                  return a.reason < b.reason;
              });

    summary.hours.reserve(hours.size());
    for (const auto& [hour, slice] : hours) {
        summary.hours.push_back(slice);
    }
    // Las horas SIEMPRE en orden cronológico: es un desglose para leer de
    // arriba abajo y ver cuándo empezó a torcerse.
    std::sort(summary.hours.begin(), summary.hours.end(),
              [](const HourSlice& a, const HourSlice& b) { return a.hour < b.hour; });

    // La peor hora se elige por CUÁNTOS rechazos, no por la proporción: una hora
    // con una sola pieza y esa mala da el 100 % y no es donde hay que mirar.
    const auto worst = std::max_element(summary.hours.begin(), summary.hours.end(),
                                        [](const HourSlice& a, const HourSlice& b) {
                                            if (a.ngCount != b.ngCount) {
                                                return a.ngCount < b.ngCount;
                                            }
                                            return a.hour > b.hour;  // la más temprana gana
                                        });
    if (worst != summary.hours.end() && worst->ngCount > 0) {
        summary.worstHour = worst->hour;
    }
    return summary;
}

std::string shiftReportCsv(const std::vector<InspectionRow>& rows,
                           const ShiftSummary& summary,
                           const core::CsvDialect& dialect) {
    const char sep = dialect.separator;
    const auto field = [&dialect](const std::string& text) {
        return core::csvField(text, dialect);
    };
    std::ostringstream out;
    // La marca de orden de bytes, la primera de todo: si va después, Excel ya
    // ha decidido que el fichero es ANSI y se come todos los acentos.
    out << core::csvByteOrderMark(dialect);
    out << "RESUMEN\n";
    out << "inspecciones" << sep << summary.total << "\n";
    out << "correctas" << sep << summary.okCount << "\n";
    out << "rechazadas" << sep << summary.ngCount << "\n";
    if (summary.yield >= 0.0) {
        out << "rendimiento" << sep
            << core::csvNumber(100.0 * summary.yield, 1, dialect) << " %\n";
    }
    if (!summary.from.empty()) {
        out << "desde" << sep << field(summary.from) << "\n";
        out << "hasta" << sep << field(summary.to) << "\n";
    }
    if (!summary.worstHour.empty()) {
        out << "hora con mas rechazos" << sep << field(summary.worstHour) << "\n";
    }
    if (!summary.reasons.empty()) {
        out << "\nMOTIVOS DE RECHAZO\n";
        out << "motivo" << sep << "veces\n";
        for (const auto& reason : summary.reasons) {
            out << field(reason.reason) << sep << reason.count << "\n";
        }
    }
    if (!summary.hours.empty()) {
        out << "\nPOR HORA\n";
        out << "hora" << sep << "inspecciones" << sep << "rechazadas\n";
        for (const auto& hour : summary.hours) {
            out << field(hour.hour) << sep << hour.total << sep << hour.ngCount << "\n";
        }
    }

    out << "\nINSPECCIONES\n";
    out << "fecha" << sep << "pieza" << sep << "veredicto" << sep << "similitud" << sep
        << "version_referencia" << sep << "motivo\n";
    for (const auto& row : rows) {
        out << field(row.startedAt) << sep << field(row.piece) << sep
            << (row.ok ? "OK" : "NG") << sep
            << core::csvNumber(row.similarity, 4, dialect) << sep << row.referenceVersion
            << sep << field(row.reason) << "\n";
    }
    return out.str();
}

std::string shiftReportText(const std::vector<InspectionRow>& rows,
                            const ShiftSummary& summary) {
    std::ostringstream out;
    if (summary.total == 0) {
        return "No se inspeccionó ninguna pieza en este periodo.\n";
    }
    out << "Inspecciones: " << summary.total << " — " << summary.okCount << " correctas, "
        << summary.ngCount << " rechazadas";
    if (summary.yield >= 0.0) {
        out << " (rendimiento " << percent(summary.yield) << " %)";
    }
    out << "\n";
    if (!summary.from.empty()) {
        out << "Desde " << summary.from << " hasta " << summary.to << "\n";
    }
    if (!summary.reasons.empty()) {
        out << "\nPor qué se rechazaron:\n";
        for (const auto& reason : summary.reasons) {
            out << "  " << reason.count << " × " << reason.reason << "\n";
        }
    }
    if (!summary.worstHour.empty()) {
        out << "\nLa hora con más rechazos fue " << summary.worstHour << ".\n";
    }
    // Las filas no van en el texto: un parte para pegar en un correo con
    // cuatrocientas líneas no lo lee nadie, y para eso está el CSV.
    (void)rows;
    return out.str();
}

}  // namespace pci::domain
