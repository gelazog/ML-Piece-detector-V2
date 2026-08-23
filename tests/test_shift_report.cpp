// EL INFORME DE UN TURNO: qué preguntas contesta.
//
// Sacar el historial a CSV es fácil y no es lo que hace falta. Un turno son
// cientos de inspecciones, y una hoja con cuatrocientas filas contesta «qué pasó
// exactamente a las 14:32» —que casi nunca se pregunta— y esconde las tres que
// sí: cuántas van, QUÉ está fallando y DESDE CUÁNDO.
//
// Así que lo que se comprueba aquí no es que el CSV se genere, sino que el
// resumen conteste esas tres.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "domain/shift_report.h"

using pci::domain::InspectionRow;
using pci::domain::shiftReportCsv;
using pci::domain::shiftReportText;
using pci::domain::summarise;

namespace {

InspectionRow ok(const std::string& when) {
    InspectionRow row;
    row.startedAt = when;
    row.piece = "brida";
    row.ok = true;
    row.similarity = 0.981;
    return row;
}

InspectionRow ng(const std::string& when, const std::string& reason) {
    InspectionRow row;
    row.startedAt = when;
    row.piece = "brida";
    row.ok = false;
    row.similarity = 0.902;
    row.reason = reason;
    return row;
}

// Un turno con una avería que empieza a media mañana: hasta las 10 va bien, y a
// partir de las 11 casi todo se cae por el mismo motivo.
std::vector<InspectionRow> shiftWithADriftAtEleven() {
    std::vector<InspectionRow> rows;
    for (int i = 0; i < 12; ++i) {
        rows.push_back(ok("2026-08-22 09:" + std::string(i < 10 ? "0" : "") +
                          std::to_string(i) + ":00"));
    }
    for (int i = 0; i < 10; ++i) {
        rows.push_back(ok("2026-08-22 10:" + std::string(i < 10 ? "0" : "") +
                          std::to_string(i) + ":00"));
    }
    rows.push_back(ng("2026-08-22 10:30:00", "diámetro exterior fuera de tolerancia"));
    for (int i = 0; i < 9; ++i) {
        rows.push_back(ng("2026-08-22 11:" + std::string(i < 10 ? "0" : "") +
                              std::to_string(i) + ":00",
                          "diámetro exterior fuera de tolerancia"));
    }
    rows.push_back(ng("2026-08-22 11:40:00", "apariencia"));
    rows.push_back(ok("2026-08-22 11:50:00"));
    return rows;
}

}  // namespace

// PRIMERA PREGUNTA: cuántas van y cuántas han pasado.
TEST(ShiftReport, ItCountsTheShift) {
    const auto rows = shiftWithADriftAtEleven();
    const auto summary = summarise(rows);

    std::printf("  [turno] %d inspecciones: %d correctas, %d rechazadas, rendimiento "
                "%.1f %%\n",
                summary.total, summary.okCount, summary.ngCount, 100.0 * summary.yield);
    // 12 buenas a las 09, 10 buenas + 1 mala a las 10, y 10 malas + 1 buena a
    // las 11. Total 34, y las cuentas se hacen aquí a mano a propósito: si el
    // resumen y la prueba sacaran el número de la misma suma, no se estaría
    // comprobando nada.
    EXPECT_EQ(summary.total, 34);
    EXPECT_EQ(summary.okCount, 23);
    EXPECT_EQ(summary.ngCount, 11);
    EXPECT_NEAR(summary.yield, 23.0 / 34.0, 1e-9);
    EXPECT_EQ(summary.from, "2026-08-22 09:00:00");
    EXPECT_EQ(summary.to, "2026-08-22 11:50:00");
}

// SEGUNDA: qué está fallando. Es la que convierte el historial en información.
TEST(ShiftReport, ItSaysWhatIsFailingAndHowOften) {
    const auto rows = shiftWithADriftAtEleven();
    const auto summary = summarise(rows);

    ASSERT_FALSE(summary.reasons.empty());
    std::printf("  [turno] motivo principal: %d × %s\n", summary.reasons.front().count,
                summary.reasons.front().reason.c_str());
    EXPECT_EQ(summary.reasons.front().reason, "diámetro exterior fuera de tolerancia");
    EXPECT_EQ(summary.reasons.front().count, 10);
    ASSERT_EQ(summary.reasons.size(), 2U);
    EXPECT_EQ(summary.reasons[1].reason, "apariencia");
    EXPECT_EQ(summary.reasons[1].count, 1);
}

// TERCERA: desde cuándo. Una lista ordenada por fecha no la contesta — hay que
// leerla entera llevando la cuenta a mano.
TEST(ShiftReport, ItSaysWhenItStartedGoingWrong) {
    const auto rows = shiftWithADriftAtEleven();
    const auto summary = summarise(rows);

    ASSERT_EQ(summary.hours.size(), 3U);
    for (const auto& hour : summary.hours) {
        std::printf("  [turno] %s: %d inspecciones, %d rechazadas\n", hour.hour.c_str(),
                    hour.total, hour.ngCount);
    }
    EXPECT_EQ(summary.hours[0].hour, "2026-08-22 09");
    EXPECT_EQ(summary.hours[0].ngCount, 0);
    EXPECT_EQ(summary.hours[1].ngCount, 1);
    EXPECT_EQ(summary.hours[2].ngCount, 10);
    EXPECT_EQ(summary.worstHour, "2026-08-22 11");
}

// La peor hora se elige por CUÁNTOS rechazos, no por la proporción: una hora con
// una sola pieza y esa mala da el 100 % y no es donde hay que ir a mirar.
TEST(ShiftReport, TheWorstHourIsTheOneWithMostRejectsNotTheWorstRate) {
    std::vector<InspectionRow> rows;
    // A las 08 h una sola pieza, y mala: 100 % de rechazo.
    rows.push_back(ng("2026-08-22 08:00:00", "apariencia"));
    // A las 14 h, cuarenta piezas y seis malas: 15 %, pero seis.
    for (int i = 0; i < 34; ++i) {
        rows.push_back(ok("2026-08-22 14:00:00"));
    }
    for (int i = 0; i < 6; ++i) {
        rows.push_back(ng("2026-08-22 14:30:00", "ancho"));
    }

    const auto summary = summarise(rows);
    std::printf("  [turno] peor hora: %s\n", summary.worstHour.c_str());
    EXPECT_EQ(summary.worstHour, "2026-08-22 14")
        << "señala la hora con peor porcentaje en vez de la que tiene más rechazos: "
           "mandaría al supervisor a mirar una hora en la que se hizo una sola pieza";
}

// Sin ninguna pieza, «rendimiento 0 %» sería afirmar un desastre que no ha
// ocurrido.
TEST(ShiftReport, NoPiecesIsNotZeroPercentYield) {
    const auto summary = summarise({});
    EXPECT_EQ(summary.total, 0);
    EXPECT_LT(summary.yield, 0.0) << "un turno sin piezas sale con rendimiento 0 %, que es "
                                     "una cosa muy distinta de «no se inspeccionó nada»";

    const std::string csv = shiftReportCsv({}, summary);
    EXPECT_EQ(csv.find("rendimiento"), std::string::npos)
        << "el CSV inventa un rendimiento donde no hubo piezas";
    const std::string text = shiftReportText({}, summary);
    std::printf("  [turno] sin piezas dice: %s", text.c_str());
    EXPECT_NE(text.find("ninguna"), std::string::npos);
}

// EL RESUMEN VA ARRIBA. Un resumen al final de cuatrocientas filas no lo ve
// nadie — es la misma regla que ya siguen los avisos del informe de pieza.
TEST(ShiftReport, TheSummaryComesBeforeTheRows) {
    const auto rows = shiftWithADriftAtEleven();
    const auto summary = summarise(rows);
    const std::string csv = shiftReportCsv(rows, summary);

    const auto summaryAt = csv.find("RESUMEN");
    const auto reasonsAt = csv.find("MOTIVOS DE RECHAZO");
    const auto rowsAt = csv.find("INSPECCIONES");
    ASSERT_NE(summaryAt, std::string::npos);
    ASSERT_NE(reasonsAt, std::string::npos);
    ASSERT_NE(rowsAt, std::string::npos);
    EXPECT_LT(summaryAt, reasonsAt);
    EXPECT_LT(reasonsAt, rowsAt) << "los motivos salen después de las filas: para entonces "
                                    "ya nadie los lee";

    // Y la cabecera de las filas sigue estando, entera, para quien parsee.
    EXPECT_NE(csv.find("fecha,pieza,veredicto,similitud,version_referencia,motivo"),
              std::string::npos);
    // Con todas las filas: el resumen no sustituye a los datos.
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n') > static_cast<long>(rows.size()), true);
}

// Un motivo con comas o comillas no puede romper el fichero, y los motivos
// llevan comas: «ancho fuera de tolerancia, 12,4 mm».
TEST(ShiftReport, AReasonWithCommasDoesNotBreakTheCsv) {
    std::vector<InspectionRow> rows;
    rows.push_back(ng("2026-08-22 10:00:00", "ancho fuera de tolerancia, 12,4 mm (max 12,0)"));
    rows.push_back(ng("2026-08-22 10:01:00", "el operador dijo \"revisar\""));
    const auto summary = summarise(rows);
    const std::string csv = shiftReportCsv(rows, summary);

    // Cada línea de motivo tiene que ser UNA línea, con las comillas cerradas.
    int quotes = 0;
    for (const char c : csv) {
        if (c == '"') {
            ++quotes;
        }
    }
    EXPECT_EQ(quotes % 2, 0) << "comillas sin cerrar: el fichero deja de parsearse";
    EXPECT_NE(csv.find("12,4 mm"), std::string::npos) << "el motivo se cortó por una coma";
    std::printf("  [turno] motivo con comas sobrevive entero\n");
}

// El texto para pegar en un parte no lleva las filas: cuatrocientas líneas en un
// correo no las lee nadie, y para eso está el CSV.
TEST(ShiftReport, TheTextReportIsShortEnoughToPasteInAnEmail) {
    const auto rows = shiftWithADriftAtEleven();
    const auto summary = summarise(rows);
    const std::string text = shiftReportText(rows, summary);
    const auto lines = std::count(text.begin(), text.end(), '\n');

    std::printf("  [turno] el parte ocupa %ld lineas para %d inspecciones\n",
                static_cast<long>(lines), summary.total);
    EXPECT_LT(lines, 15) << "el parte tiene una línea por inspección: eso es el CSV, no un "
                            "parte";
    EXPECT_NE(text.find("diámetro exterior"), std::string::npos)
        << "el parte no dice qué está fallando, que es para lo que se manda";
    EXPECT_NE(text.find("11"), std::string::npos) << "el parte no dice desde cuándo";
}
