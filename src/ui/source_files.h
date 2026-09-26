#pragma once

#include <QString>
#include <QStringList>

#include <optional>

#include "camera/frame_source.h"

namespace pci::ui {

// Qué ficheros se pueden abrir como fuente, y la lista de recientes.
//
// Vive aparte de la ventana porque hay TRES puertas al mismo sitio —el
// desplegable de fuente, «Abrir imagen o vídeo…» y arrastrar el fichero— y las
// tres tienen que estar de acuerdo en qué es una imagen y qué es un vídeo. Con
// las extensiones escritas en cada una, la primera que se añadiera a una sola
// haría que un fichero se abriera desde el diálogo y se rechazara al soltarlo.

// Extensiones en minúscula y sin punto.
const QStringList& imageExtensions();
const QStringList& videoExtensions();

// Imagen o vídeo según la extensión, sin mirar el contenido. nullopt si no es
// ninguna de las dos: quien llama lo dice en la barra de estado.
std::optional<camera::SourceKind> sourceKindForFile(const QString& path);

// Filtros del diálogo de abrir: los dos juntos primero, que es lo que se busca.
QString imageFileFilter();
QString videoFileFilter();
QString imageOrVideoFileFilter();

// «PNG, JPG, …» para los mensajes.
QString describeExtensions(const QStringList& extensions);

constexpr int kMaxRecentFiles = 5;

// La lista con `path` delante, sin repetirlo y cortada a `max`. Dos rutas que
// solo difieren en mayúsculas son el mismo fichero en Windows.
QStringList withRecentFile(const QStringList& recent, const QString& path,
                           int max = kMaxRecentFiles);
// Sin `path`, para quitar uno que ya no está en disco.
QStringList withoutRecentFile(const QStringList& recent, const QString& path);

// Una ruta por línea: así cabe en un ajuste de texto, como la carpeta de la
// última fuente. Una ruta de Windows no puede llevar un salto de línea.
QString encodeRecentFiles(const QStringList& recent);
QStringList decodeRecentFiles(const QString& text);

}  // namespace pci::ui
