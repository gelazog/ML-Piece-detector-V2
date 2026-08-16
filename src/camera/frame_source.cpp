#include "camera/frame_source.h"

#include <QCoreApplication>

namespace pci::camera {

SourceCapabilities capabilitiesOf(SourceKind kind) {
    SourceCapabilities caps;
    switch (kind) {
        case SourceKind::Camera:
            caps.adjustableControls = true;
            caps.selectableResolution = true;
            caps.meaningfulCaptureFps = true;
            caps.focusable = true;
            return caps;
        case SourceKind::Photo:
        case SourceKind::Image:
            // Nada de lo anterior. Ni siquiera los fps: una imagen fija se
            // reemite a un ritmo que se inventa la aplicación, así que
            // enseñarlo sería responder a una pregunta que nadie hizo.
            //
            // La foto tampoco es enfocable, y eso puede chocar: la cámara sigue
            // ahí detrás. Pero enfocar mientras se mira una foto no cambiaría la
            // foto, así que ofrecerlo sería ofrecer un control que no hace nada.
            return caps;
        case SourceKind::Video:
            // Los fps SÍ significan algo —son los del fichero, y dicen a qué
            // ritmo se está viendo— pero no hay nada que ajustar: el vídeo se
            // grabó con la exposición y el enfoque que tuviera.
            caps.meaningfulCaptureFps = true;
            return caps;
    }
    return caps;
}

QString whyNotAdjustable(SourceKind kind) {
    switch (kind) {
        case SourceKind::Camera: return {};
        case SourceKind::Photo:
            return QCoreApplication::translate(
                "pci::camera",
                "Estás mirando una foto congelada. La cámara sigue conectada: vuelve al vídeo "
                "en vivo para ajustar su brillo, exposición o enfoque, y congela otra vez "
                "cuando la imagen te guste.");
        case SourceKind::Image:
            return QCoreApplication::translate(
                "pci::camera",
                "La fuente es una imagen de archivo: su brillo, exposición y enfoque son los "
                "que tenía cuando se tomó y ya no se pueden cambiar.");
        case SourceKind::Video:
            return QCoreApplication::translate(
                "pci::camera",
                "La fuente es un vídeo de archivo: se grabó con la exposición y el enfoque que "
                "tuviera, y desde aquí ya no se pueden tocar.");
    }
    return {};
}

}  // namespace pci::camera
