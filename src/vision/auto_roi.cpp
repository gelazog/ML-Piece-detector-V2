#include "vision/auto_roi.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace pci::vision {

namespace {

// Envolvente de la pieza más el margen, acotada al frame.
cv::Rect withMargin(const cv::Rect& bounds, double marginFraction,
                    const cv::Size& frameSize) {
    const int padX = static_cast<int>(std::lround(bounds.width * marginFraction));
    const int padY = static_cast<int>(std::lround(bounds.height * marginFraction));
    const cv::Rect grown(bounds.x - padX, bounds.y - padY, bounds.width + 2 * padX,
                         bounds.height + 2 * padY);
    return grown & cv::Rect(0, 0, frameSize.width, frameSize.height);
}

// Interpolación de un rectángulo hacia otro. Solo se usa para ENCOGER.
cv::Rect lerpRect(const cv::Rect& from, const cv::Rect& to, double t) {
    const auto mix = [t](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * t));
    };
    return {mix(from.x, to.x), mix(from.y, to.y), mix(from.width, to.width),
            mix(from.height, to.height)};
}

}  // namespace

const char* workingZoneModeKey(WorkingZoneMode mode) {
    switch (mode) {
        case WorkingZoneMode::Off: return "off";
        case WorkingZoneMode::Automatic: return "auto";
        case WorkingZoneMode::Fixed: return "fixed";
    }
    return "off";
}

WorkingZoneMode workingZoneModeFromKey(const char* key) {
    if (key == nullptr) {
        return WorkingZoneMode::Off;
    }
    const std::string_view name(key);
    if (name == "auto") {
        return WorkingZoneMode::Automatic;
    }
    if (name == "fixed") {
        return WorkingZoneMode::Fixed;
    }
    // Cualquier cosa que no se reconozca cae al modo más conservador: procesar
    // la imagen entera nunca da un resultado equivocado, solo uno más lento.
    return WorkingZoneMode::Off;
}

const char* giveUpReason(AutoRoiGiveUp reason) {
    switch (reason) {
        case AutoRoiGiveUp::None: return "";
        case AutoRoiGiveUp::PieceLost:
            return "se dejó de ver la pieza: se vuelve a mirar la imagen entera";
        case AutoRoiGiveUp::PieceEscaping:
            return "la pieza tocó el borde de la zona: se vuelve a la imagen entera";
        case AutoRoiGiveUp::AreaJumped:
            return "la pieza cambió de tamaño de golpe: se vuelve a la imagen entera";
    }
    return "";
}

cv::Rect effectiveWorkingZone(WorkingZoneMode mode, const cv::Rect& fixedZone,
                              const cv::Rect& automaticZone) {
    switch (mode) {
        case WorkingZoneMode::Off: return {};
        case WorkingZoneMode::Automatic: return automaticZone;
        case WorkingZoneMode::Fixed: return fixedZone;
    }
    return {};
}

WorkingZoneMode modeAfterFixedZoneChanged(WorkingZoneMode current, bool hasFixedZone) {
    if (hasFixedZone) {
        // El gesto explícito manda, incluso sobre el modo automático: quien
        // acaba de dibujar dónde mirar quiere que se mire ahí.
        return WorkingZoneMode::Fixed;
    }
    // Sin zona, «fija» no puede seguir siendo el modo. Los otros dos no
    // dependen de ella y se quedan como estaban.
    return current == WorkingZoneMode::Fixed ? WorkingZoneMode::Off : current;
}

void AutoRoiTracker::reset() {
    roi_ = cv::Rect();
    lastArea_ = 0.0;
    lostFrames_ = 0;
    giveUp_ = AutoRoiGiveUp::None;
}

void AutoRoiTracker::update(bool pieceFound, const cv::Rect& pieceBounds,
                            const cv::Size& frameSize) {
    if (frameSize.width <= 0 || frameSize.height <= 0) {
        reset();
        return;
    }

    if (!pieceFound || pieceBounds.area() <= 0) {
        // Se toleran algunos frames sin pieza (un parpadeo de la luz, una mano
        // que pasa) antes de tirar el seguimiento; pasados esos, imagen entera.
        if (++lostFrames_ > options_.lostFramesAllowed) {
            const bool wasTracking = tracking();
            reset();
            giveUp_ = wasTracking ? AutoRoiGiveUp::PieceLost : AutoRoiGiveUp::None;
        }
        return;
    }
    lostFrames_ = 0;

    // La pieza tocando el borde del recorte significa que se está saliendo, y
    // que lo que se ve de ella puede estar ya cortado: el análisis del próximo
    // frame no sería de fiar. Se abandona el recorte en vez de intentar
    // estirarlo, porque un recorte que persigue a una pieza cortada arrastra el
    // error en lugar de corregirlo.
    //
    // Esta comprobación va ANTES que la del salto de área, y el orden importa:
    // una pieza que se sale acaba recortada, y al recortarse su área cae de
    // golpe. Es decir, salirse es la CAUSA y el salto de área el síntoma. Al
    // revés, el motivo que se le enseñaría al operador sería el equivocado.
    if (tracking()) {
        constexpr int kEdge = 2;
        const bool touching = pieceBounds.x <= roi_.x + kEdge ||
                              pieceBounds.y <= roi_.y + kEdge ||
                              pieceBounds.br().x >= roi_.br().x - kEdge ||
                              pieceBounds.br().y >= roi_.br().y - kEdge;
        if (touching) {
            reset();
            giveUp_ = AutoRoiGiveUp::PieceEscaping;
            return;
        }
    }

    // Un salto brusco de área es "han puesto otra pieza": el recorte de la
    // anterior no vale, y seguir usándolo mediría dentro de una ventana que
    // ya no corresponde.
    const double area = static_cast<double>(pieceBounds.area());
    if (lastArea_ > 0.0) {
        const double ratio = std::max(area / lastArea_, lastArea_ / area);
        if (ratio > options_.areaJumpRatio) {
            reset();
            lastArea_ = area;
            giveUp_ = AutoRoiGiveUp::AreaJumped;
            return;
        }
    }
    lastArea_ = area;

    const cv::Rect target = withMargin(pieceBounds, options_.marginFraction, frameSize);
    if (target.width < options_.minRoiSizePx || target.height < options_.minRoiSizePx) {
        // Recortar tan poco no ahorra nada y sí puede fallar.
        roi_ = cv::Rect();
        return;
    }
    // Recortar casi todo el frame tampoco aporta: el coste del recorte se come
    // el ahorro y solo se añade una forma nueva de equivocarse.
    if (target.area() > 0.85 * frameSize.width * frameSize.height) {
        roi_ = cv::Rect();
        return;
    }

    if (!tracking()) {
        roi_ = target;
        giveUp_ = AutoRoiGiveUp::None;
        return;
    }
    // Crecer es inmediato y encoger es lento: la unión con el objetivo
    // garantiza que el recorte NUNCA corta a la pieza, y la interpolación evita
    // que lata con el ruido de la segmentación cuando la pieza está quieta.
    const cv::Rect eased = lerpRect(roi_, target, 1.0 - options_.shrinkInertia);
    roi_ = (eased | target) & cv::Rect(0, 0, frameSize.width, frameSize.height);
    giveUp_ = AutoRoiGiveUp::None;
}

}  // namespace pci::vision
