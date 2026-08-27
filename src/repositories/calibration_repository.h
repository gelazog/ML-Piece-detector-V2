#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "domain/calibration.h"

namespace pci::repositories {

// EL REGISTRO DE CALIBRACIONES.
//
// La escala px→mm vivía suelta en `Settings`, como una clave más: un número,
// sin fecha, sin decir cómo se obtuvo, y **sin que ninguna medida guardada lo
// referenciara**.
//
// El problema no es de orden, es de responder una pregunta concreta: el día que
// se descubre que la calibración estaba mal —alguien movió la cámara, se cambió
// el objetivo, se calibró contra una regla deformada— ¿QUÉ VEREDICTOS hay que
// revisar? Todos los milímetros que ha dado el programa salieron de ese número,
// y hasta ahora ninguno decía de cuál.
//
// Es lo primero que mira un auditor, y lo que pide ISO 9001 7.1.5.2: si el
// equipo aparece fuera de calibración hay que evaluar la validez de las medidas
// ANTERIORES.
//
// Esto es un REGISTRO, no un ajuste: cada calibración que se aplica añade una
// fila y la anterior se queda. Guardar solo la vigente sería el mismo agujero
// con otra forma.

struct CalibrationRecord {
    std::int64_t id = 0;
    std::string createdAt;
    domain::ScaleCalibration scale;
    std::string camera;
    // Cómo se obtuvo: «longitud conocida», «marcador ArUco», «tablero»… Sin
    // esto, dos calibraciones que dan el mismo número son indistinguibles, y no
    // lo son: una medida contra una regla de taller y una estimada por la
    // distancia de cámara valen cosas muy distintas.
    std::string method;
    // Contra QUÉ se calibró. Aquí es donde el operador escribe su patrón —«regla
    // Mitutoyo nº 4471», «retícula NIST 0,5 mm»— y es lo único que hace la
    // medida trazable. Se deja libre a propósito: exigir un formato haría que se
    // dejara en blanco.
    std::string reference;
    std::string notes;
};

class CalibrationRepository {
public:
    explicit CalibrationRepository(database::Db& db) : db_(db) {}

    // Anota una calibración recién aplicada y devuelve su id. Ese id es el que
    // guardan las inspecciones que se hagan a partir de ahora.
    [[nodiscard]] core::Result<std::int64_t> record(const CalibrationRecord& entry);

    // Las calibraciones, de la más reciente a la más antigua.
    [[nodiscard]] core::Result<std::vector<CalibrationRecord>> list();

    [[nodiscard]] core::Result<CalibrationRecord> load(std::int64_t id);

    // La que está en vigor: la última anotada. Devuelve id 0 si no hay ninguna,
    // que es lo que le pasa a una instalación que viene de antes del registro.
    [[nodiscard]] core::Result<CalibrationRecord> current();

    // CUÁNTAS INSPECCIONES DEPENDEN DE ESTA CALIBRACIÓN.
    //
    // Es la pregunta para la que existe todo esto. Cuando se descubre que una
    // calibración era mala, esto dice de golpe cuánto trabajo hay que contener.
    [[nodiscard]] core::Result<int> inspectionsUsing(std::int64_t calibrationId);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
