#include "core/csv_dialect.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pci::core {

namespace {

#ifdef _WIN32
// Un dato suelto de la configuración regional del usuario, o vacío.
std::string localeSetting(LCTYPE what) {
    wchar_t buffer[16] = {};
    const int written =
        ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, what, buffer, static_cast<int>(
                                                                     std::size(buffer)));
    if (written <= 1) {
        return {};
    }
    // Solo interesan separadores de un carácter ASCII. Un separador exótico se
    // trata como «no se sabe» y se cae al formato clásico, que al menos es un
    // CSV válido.
    if (written != 2 || buffer[0] > 127) {
        return {};
    }
    return std::string(1, static_cast<char>(buffer[0]));
}
#endif

}  // namespace

CsvDialect systemCsvDialect() {
    CsvDialect dialect;
#ifdef _WIN32
    const std::string decimal = localeSetting(LOCALE_SDECIMAL);
    const std::string list = localeSetting(LOCALE_SLIST);
    if (!decimal.empty()) {
        dialect.decimal = decimal[0];
    }
    if (!list.empty()) {
        dialect.separator = list[0];
    }
    // Y LA COMBINACIÓN IMPOSIBLE SE ARREGLA AQUÍ.
    //
    // Si el equipo dice coma decimal y coma de lista —configuración a medio
    // hacer, y las hay— el fichero saldría con comas por todas partes y no lo
    // podría leer nadie, ni Excel ni un script. Entre respetar al pie de la
    // letra una configuración rota y producir un fichero legible, lo segundo.
    if (dialect.separator == dialect.decimal) {
        dialect.separator = dialect.decimal == ',' ? ';' : ',';
    }
#endif
    return dialect;
}

std::string csvByteOrderMark(const CsvDialect& dialect) {
    return dialect.byteOrderMark ? "\xEF\xBB\xBF" : "";
}

std::string csvNumber(double value, int decimals, const CsvDialect& dialect) {
    std::ostringstream out;
    // El formato se hace SIEMPRE en el clásico y se sustituye el punto después.
    // Dejárselo al locale de C++ no vale aquí: en este toolchain
    // `std::locale("")` devuelve «C» aunque Windows esté en español, así que
    // pediría el separador correcto y saldría el punto igualmente.
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(decimals) << value;
    std::string text = out.str();
    if (dialect.decimal != '.') {
        std::replace(text.begin(), text.end(), '.', dialect.decimal);
    }
    return text;
}

std::string csvField(const std::string& text, const CsvDialect& dialect) {
    const bool needsQuotes = text.find(dialect.separator) != std::string::npos ||
                             text.find('"') != std::string::npos ||
                             text.find('\n') != std::string::npos ||
                             text.find('\r') != std::string::npos;
    if (!needsQuotes) {
        return text;
    }
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"') {
            quoted += '"';  // se dobla, que es como se escapa en CSV
        }
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

}  // namespace pci::core
