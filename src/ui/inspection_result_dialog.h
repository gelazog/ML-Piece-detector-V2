#pragma once

#include <QDialog>
#include <QImage>

#include <cstdint>

#include "engine/inspection_engine.h"

class QLabel;
class QPushButton;

namespace pci::ui {

class VideoWidget;

// Resultado de una inspección: banner OK/NG, imagen anotada (contorno +
// herramientas), tabla de mediciones y aprendizaje incremental opcional.
class InspectionResultDialog : public QDialog {
    Q_OBJECT

public:
    // referenceThumb: miniatura de la pieza registrada (puede ser nula) para
    // la comparación visual contra el recorte de la pieza inspeccionada.
    InspectionResultDialog(const QImage& frame, engine::InspectionEngine::Outcome outcome,
                           engine::InspectionEngine* engine, std::int64_t pieceId,
                           const QImage& referenceThumb = {}, QWidget* parent = nullptr);

private slots:
    void onLearnClicked();

private:
    [[nodiscard]] QImage annotatedFrame(const QImage& frame) const;

    engine::InspectionEngine::Outcome outcome_;
    engine::InspectionEngine* engine_ = nullptr;
    std::int64_t pieceId_ = -1;

    QLabel* learnStatus_ = nullptr;
    QPushButton* learnButton_ = nullptr;
};

}  // namespace pci::ui
