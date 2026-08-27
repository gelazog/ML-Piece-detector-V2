#pragma once

#include <string>

namespace pci::core {

// EL CSV QUE LA HOJA DE CÁLCULO DE ESTE EQUIPO SABE ABRIR.
//
// Queja del taller: «cuando lo conviertes a Excel también está mal el formato
// en el que lo da».
//
// Los tres exportadores escribían el mismo CSV: coma de separador, punto
// decimal y sin marca de orden de bytes. Sobre un Windows en español —el de la
// estación donde se usa esto— eso falla por TRES sitios a la vez, y ninguno
// tiene que ver con los otros:
//
//     sDecimal  = ,     «26.9864» se lee 269864, o como fecha
//     sList     = ;     con comas, la fila entera cae en la columna A
//     sin BOM           «Perímetro» sale «PerÃ­metro», «mm²» sale «mmÂ²»
//
// El resultado es una única columna de texto ilegible. No es un matiz de
// presentación: el fichero no se puede usar.
//
// Excel resuelve esto mirando la configuración regional del usuario, y aquí se
// hace lo mismo. No se fuerza el punto y coma a todo el mundo: en un equipo con
// punto decimal, el punto y coma sería el error simétrico.
//
// Por qué no vale `std::locale("")`: en este toolchain (MSYS2/UCRT64) devuelve
// «C» —decimal '.'— aunque Windows esté en español. Se comprobó ejecutándolo.
// La configuración de verdad está en la API de localización de Windows, y ahí
// se pregunta.
struct CsvDialect {
    char separator = ',';
    char decimal = '.';
    // Sin esto Excel decodifica el fichero con la página de códigos ANSI y se
    // come todos los acentos. Es el mismo fallo que tenía el registro de
    // `reanudar.ps1`, por el mismo motivo, y en otro fichero.
    bool byteOrderMark = true;
};

// Lo que dice la configuración regional del equipo. En Windows sale de
// `GetLocaleInfoEx`; en cualquier otro sitio, del formato clásico.
[[nodiscard]] CsvDialect systemCsvDialect();

// La marca de orden de bytes de UTF-8, para encabezar el fichero.
[[nodiscard]] std::string csvByteOrderMark(const CsvDialect& dialect);

// Un número con el separador decimal que toca. `decimals` es cuántas cifras
// detrás, en punto fijo: un CSV de medidas donde una fila trae 4 decimales y la
// siguiente 4,0001e+00 no lo lee ninguna hoja de cálculo.
[[nodiscard]] std::string csvNumber(double value, int decimals, const CsvDialect& dialect);

// Un campo de texto, entrecomillado si lo necesita.
//
// Necesita comillas si lleva el separador dentro, y con separador PUNTO Y COMA
// eso cambia respecto a la coma: «corona circular: Ø 167 px, agujero de 41» ya
// no hace falta entrecomillarlo, y en cambio un texto con «;» sí. Por eso esto
// vive junto al dialecto y no suelto en cada exportador — tres copias de la
// regla serían tres ocasiones de que una se quede con el separador viejo.
[[nodiscard]] std::string csvField(const std::string& text, const CsvDialect& dialect);

}  // namespace pci::core
