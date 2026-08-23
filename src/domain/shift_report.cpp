#include "domain/shift_report.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace pci::domain {

namespace {

// Un campo de CSV a prueba de comas y comillas. Los motivos de rechazo llevan
// comas —«ancho fuera de tolerancia, 12,4 mm»— así que esto no es teórico.
std::string csvField(const std::string& text) {
    if (text.find_first_of(",\"\n\r") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"') {
            quoted += '"';
        }
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

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
                           const ShiftSummary& summary) {
    std::ostringstream out;
    out << "RESUMEN\n";
    out << "inspecciones," << summary.total << "\n";
    out << "correctas," << summary.okCount << "\n";
    out << "rechazadas," << summary.ngCount << "\n";
    if (summary.yield >= 0.0) {
        out << "rendimiento," << percent(summary.yield) << " %\n";
    }
    if (!summary.from.empty()) {
        out << "desde," << csvField(summary.from) << "\n";
        out << "hasta," << csvField(summary.to) << "\n";
    }
    if (!summary.worstHour.empty()) {
        out << "hora con mas rechazos," << csvField(summary.worstHour) << "\n";
    }
    if (!summary.reasons.empty()) {
        out << "\nMOTIVOS DE RECHAZO\n";
        out << "motivo,veces\n";
        for (const auto& reason : summary.reasons) {
            out << csvField(reason.reason) << "," << reason.count << "\n";
        }
    }
    if (!summary.hours.empty()) {
        out << "\nPOR HORA\n";
        out << "hora,inspecciones,rechazadas\n";
        for (const auto& hour : summary.hours) {
            out << csvField(hour.hour) << "," << hour.total << "," << hour.ngCount << "\n";
        }
    }

    out << "\nINSPECCIONES\n";
    out << "fecha,pieza,veredicto,similitud,version_referencia,motivo\n";
    for (const auto& row : rows) {
        out << csvField(row.startedAt) << "," << csvField(row.piece) << ","
            << (row.ok ? "OK" : "NG") << "," << std::fixed << std::setprecision(4)
            << row.similarity << "," << row.referenceVersion << ","
            << csvField(row.reason) << "\n";
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
