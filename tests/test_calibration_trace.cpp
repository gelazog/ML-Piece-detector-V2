// ¿QUÉ VEREDICTOS DEPENDEN DE ESTA CALIBRACIÓN?
//
// Es la pregunta para la que existe el registro de calibraciones, y hasta ahora
// no tenía respuesta. La escala px→mm vivía suelta en `Settings`: un número, sin
// fecha, sin decir cómo se obtuvo ni contra qué patrón, y sin que ninguna medida
// guardada lo referenciara.
//
// El día que se descubre que la calibración estaba mal —alguien movió la cámara,
// se cambió el objetivo, se calibró contra una regla de plástico deformada—
// TODOS los milímetros que ha dado el programa salieron de ese número, y ninguna
// medida decía de cuál. La respuesta honesta era «no lo sé», y en una planta eso
// significa contener todo el trabajo o ninguno.
//
// ISO 9001 7.1.5.2 lo pide con estas palabras: si el equipo aparece fuera de
// calibración, hay que evaluar la validez de las medidas ANTERIORES.

#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/calibration_repository.h"
#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"

using namespace pci;

namespace {

struct Bench {
    QTemporaryDir dir;
    std::unique_ptr<database::Db> db;
};

bool open(Bench& bench) {
    if (!bench.dir.isValid()) {
        return false;
    }
    auto opened = database::Db::open(
        QDir(bench.dir.path()).filePath(QStringLiteral("c.db")).toStdString());
    if (!opened.isOk()) {
        return false;
    }
    bench.db = std::move(opened.value());
    return database::migrate(*bench.db).isOk();
}

repositories::CalibrationRecord aCalibration(double mmPerPixel, const char* reference) {
    repositories::CalibrationRecord entry;
    entry.scale.mmPerPixel = mmPerPixel;
    entry.scale.calibratedWidth = 1280;
    entry.scale.calibratedHeight = 720;
    entry.camera = "Integrated Camera";
    entry.method = "longitud conocida";
    entry.reference = reference;
    return entry;
}

domain::InspectionVerdict aVerdict(bool ok) {
    domain::InspectionVerdict verdict;
    verdict.ok = ok;
    return verdict;
}

}  // namespace

TEST(CalibrationTrace, EachInspectionRemembersWhichCalibrationMeasuredIt) {
    Bench bench;
    ASSERT_TRUE(open(bench));
    repositories::CalibrationRepository calibrations(*bench.db);
    repositories::InspectionRepository inspections(*bench.db);
    repositories::PieceRepository pieces(*bench.db);

    auto piece = pieces.createPiece("tuerca M8");
    ASSERT_TRUE(piece.isOk()) << piece.error().message;

    // Dos calibraciones seguidas, como un puesto real: se calibra, se trabaja,
    // se vuelve a calibrar tras mover la cámara.
    auto first = calibrations.record(aCalibration(0.0521, "regla de taller nº 4"));
    ASSERT_TRUE(first.isOk()) << first.error().message;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(inspections
                        .saveInspection(piece.value(), 1, aVerdict(true), {}, {},
                                        first.value())
                        .isOk());
    }

    auto second = calibrations.record(aCalibration(0.0498, "retícula 0,5 mm"));
    ASSERT_TRUE(second.isOk()) << second.error().message;
    ASSERT_TRUE(
        inspections.saveInspection(piece.value(), 1, aVerdict(false), {}, {}, second.value())
            .isOk());

    // LA PREGUNTA. «Esa regla estaba doblada»: ¿cuánto trabajo hay que contener?
    auto affected = calibrations.inspectionsUsing(first.value());
    ASSERT_TRUE(affected.isOk()) << affected.error().message;
    std::printf("  [trazabilidad] la calibración nº%lld midió %d inspecciones\n",
                static_cast<long long>(first.value()), affected.value());
    EXPECT_EQ(affected.value(), 3)
        << "no se sabe cuántas inspecciones dependen de una calibración concreta, así que "
           "el día que resulte mala no hay forma de saber qué veredictos revisar";

    auto later = calibrations.inspectionsUsing(second.value());
    ASSERT_TRUE(later.isOk());
    EXPECT_EQ(later.value(), 1)
        << "las inspecciones se están atribuyendo a la calibración equivocada";
}

TEST(CalibrationTrace, ItIsALogAndNotASetting) {
    // Guardar solo la vigente sería el mismo agujero con otra forma: la anterior
    // es justamente la que hay que poder mirar cuando algo sale mal.
    Bench bench;
    ASSERT_TRUE(open(bench));
    repositories::CalibrationRepository calibrations(*bench.db);

    ASSERT_TRUE(calibrations.record(aCalibration(0.0521, "regla de taller nº 4")).isOk());
    ASSERT_TRUE(calibrations.record(aCalibration(0.0498, "retícula 0,5 mm")).isOk());

    auto all = calibrations.list();
    ASSERT_TRUE(all.isOk()) << all.error().message;
    ASSERT_EQ(all.value().size(), 2U)
        << "la calibración anterior desapareció al aplicar la nueva: entonces esto es un "
           "ajuste y no un registro";
    // De la más reciente a la más antigua.
    EXPECT_NEAR(all.value().front().scale.mmPerPixel, 0.0498, 1e-9);
    EXPECT_EQ(all.value().front().reference, "retícula 0,5 mm");
    std::printf("  [trazabilidad] %zu calibraciones, la vigente contra «%s» (%s)\n",
                all.value().size(), all.value().front().reference.c_str(),
                all.value().front().createdAt.c_str());
    EXPECT_FALSE(all.value().front().createdAt.empty())
        << "una calibración sin fecha no es trazable: no se puede decir desde cuándo vale";
}

TEST(CalibrationTrace, TheOneInForceIsTheLastOneApplied) {
    Bench bench;
    ASSERT_TRUE(open(bench));
    repositories::CalibrationRepository calibrations(*bench.db);

    // Sin ninguna anotada no es un error: es una instalación que viene de antes
    // del registro, y el id 0 lo dice.
    auto none = calibrations.current();
    ASSERT_TRUE(none.isOk()) << none.error().message;
    EXPECT_EQ(none.value().id, 0)
        << "sin calibraciones anotadas se devuelve una con id, y eso haría que las "
           "inspecciones apuntaran a una fila que no existe";

    ASSERT_TRUE(calibrations.record(aCalibration(0.0521, "regla")).isOk());
    auto second = calibrations.record(aCalibration(0.0498, "retícula"));
    ASSERT_TRUE(second.isOk());

    auto inForce = calibrations.current();
    ASSERT_TRUE(inForce.isOk());
    EXPECT_EQ(inForce.value().id, second.value());
    EXPECT_EQ(inForce.value().reference, "retícula");
}

TEST(CalibrationTrace, ACalibrationWithoutAScaleIsRefused) {
    // Anotar una calibración sin escala sería anotar que no hay calibración, y
    // dejaría una fila a la que unas inspecciones apuntan como si la tuvieran.
    Bench bench;
    ASSERT_TRUE(open(bench));
    repositories::CalibrationRepository calibrations(*bench.db);

    auto refused = calibrations.record(aCalibration(0.0, "nada"));
    EXPECT_FALSE(refused.isOk())
        << "se acepta una calibración con escala cero, que es una calibración que no "
           "calibra";
    std::printf("  [trazabilidad] escala cero -> «%s»\n", refused.error().message.c_str());
}

TEST(CalibrationTrace, OldInspectionsKeepZeroInsteadOfPretending) {
    // Una base que ya existía no puede inventarse a qué calibración pertenecían
    // sus medidas. 0 significa «no se llevaba registro entonces», y es distinto
    // de «sin calibrar»: confundirlos daría una trazabilidad falsa, que es peor
    // que no tener ninguna.
    Bench bench;
    ASSERT_TRUE(open(bench));
    repositories::InspectionRepository inspections(*bench.db);
    repositories::PieceRepository pieces(*bench.db);
    repositories::CalibrationRepository calibrations(*bench.db);

    auto piece = pieces.createPiece("pieza vieja");
    ASSERT_TRUE(piece.isOk());
    // Sin pasar id: es lo que hace el código que aún no se enteró del registro.
    ASSERT_TRUE(inspections.saveInspection(piece.value(), 1, aVerdict(true), {}, {}).isOk());

    auto orphans = calibrations.inspectionsUsing(0);
    ASSERT_TRUE(orphans.isOk());
    EXPECT_EQ(orphans.value(), 1)
        << "una inspección guardada sin calibración conocida no queda marcada como tal";
}
