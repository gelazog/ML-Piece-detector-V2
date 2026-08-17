#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>

#include <vector>

#include "core/result.h"

namespace pci::ui {

// La bandeja de capturas: las fotos que se van tomando del vídeo, juntas.
//
// Hasta ahora «Capturar foto» congelaba el frame y ahí se quedaba: tomar la
// siguiente tiraba la anterior. Eso sirve para medir una pieza y no sirve para
// las tres cosas que se piden de un montón de fotos —tener historial, comparar
// unas con otras, y alimentar el aprendizaje de la referencia— porque las tres
// necesitan que las fotos coexistan.
//
// La bandeja vive en memoria durante la sesión y se vuelca a disco cuando el
// operador lo pide. No se guarda sola a cada disparo, y es deliberado: en una
// puesta a punto se disparan veinte fotos de las que interesan tres, y una
// carpeta con diecisiete descartes es peor que no tener carpeta.

struct Capture {
    QImage image;
    QDateTime taken;
    // De dónde salió: el nombre de la cámara o del fichero. Sin esto, dos fotos
    // de dos montajes distintos son indistinguibles una semana después, que es
    // justo cuando se miran.
    QString source;
};

class CaptureTray {
public:
    void add(QImage image, QString source, QDateTime taken = QDateTime::currentDateTime());
    [[nodiscard]] int count() const { return static_cast<int>(captures_.size()); }
    [[nodiscard]] bool empty() const { return captures_.empty(); }
    [[nodiscard]] const Capture& at(int index) const { return captures_[static_cast<std::size_t>(index)]; }
    // Fuera de rango no hace nada: borrar es lo que más se pulsa por error.
    void removeAt(int index);
    void clear();

    // Nombre de fichero de una captura.
    //
    // El formato es `<pieza>_<AAAAMMDD-HHMMSS>_<nn>.png`, y cada parte está por
    // una razón:
    //
    // - La **pieza** primero, porque es lo que se busca al abrir la carpeta.
    // - La **fecha en ese orden**, porque así el orden alfabético ES el
    //   cronológico. Con `HH-MM-SS_DD-MM-AAAA` la carpeta se ordena por hora del
    //   día y mezcla semanas.
    // - El **número**, porque en una ráfaga entran varias fotos en el mismo
    //   segundo y si no se pisarían entre ellas.
    //
    // PNG y no JPEG: estas fotos son para volver a medir sobre ellas, y el JPEG
    // inventa bordes donde no los hay. Este proyecto ya midió lo que le cuesta a
    // la detección.
    [[nodiscard]] static QString fileNameFor(const Capture& capture, const QString& piece,
                                             int index);

    // Escribe todas en la carpeta. Devuelve cuántas se guardaron.
    //
    // No sobrescribe: si el nombre ya existe se le añade un sufijo. Perder una
    // captura anterior por repetir un nombre sería el peor fallo posible aquí,
    // porque el operador no se entera hasta que va a buscarla.
    [[nodiscard]] core::Result<int> saveAll(const QString& folder, const QString& piece) const;

private:
    std::vector<Capture> captures_;
};

}  // namespace pci::ui
