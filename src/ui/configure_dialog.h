#pragma once

#include <QDialog>

#include "ui/station_status.h"

#include <cstdint>
#include <vector>

#include "camera/camera_controls.h"
#include "camera/frame_source.h"
#include "vision/auto_roi.h"
#include "vision/segmentation.h"

class QTabWidget;

namespace pci::camera {
class CameraController;
}

namespace pci::repositories {
class DetectionProfileRepository;
class SettingsRepository;
}

namespace pci::ui {

class CameraImagePage;
class DetectionPage;
class PerformancePage;
class PiecesPage;
class PreferencesPage;

// Panel «Configurar»: el único sitio donde se ajusta cómo se ve y cómo se
// detecta la pieza.
//
// Existe porque los ajustes estaban repartidos en siete diálogos colgados de
// cuatro menús distintos: para cambiar el enfoque y el umbral había que saber
// que uno vivía en *Cámara* y el otro en *Inspección*. Aquí no hay nada nuevo
// que aprender, solo un sitio donde buscar.
//
// **No es modal, y eso es deliberado.** Ajustar un umbral o un enfoque consiste
// en mover y mirar: con un diálogo modal encima del vídeo no se ve el efecto de
// lo que se toca. Por eso se abre sin bloquear la ventana y trae *Aplicar*
// además de *Aceptar*.
//
// Dos páginas no son formularios sino asistentes —la escala se calibra haciendo
// clic en dos puntos de una foto, y los atajos son una tabla que se edita— así
// que su pestaña explica y abre el asistente de siempre en vez de fingir ser un
// panel de ajustes. Meterlos a la fuerza aquí los haría peores, no mejores.
class ConfigureDialog : public QDialog {
    Q_OBJECT

public:
    // Todo lo que las páginas necesitan para nacer con los valores de hoy.
    // `controller` puede ser nulo (cámara parada): la página de cámara lo dice
    // en vez de mostrar deslizadores que no harían nada.
    struct Inputs {
        vision::SegmentationOptions segmentation;
        std::int64_t detectionProfileId = 0;
        double minAreaFraction = 0.005;
        double maxAreaFraction = 0.9;
        repositories::DetectionProfileRepository* profiles = nullptr;
        camera::CameraController* controller = nullptr;
        // De dónde vienen los frames. Decide QUÉ MOTIVO se le da al operador
        // cuando la pestaña de cámara no tiene nada que ofrecer: no es lo mismo
        // «todavía no has arrancado» que «esto es un fichero y ya no se puede
        // tocar», y darle el primero cuando el caso es el segundo le manda a
        // hacer algo que ya hizo.
        camera::SourceKind sourceKind = camera::SourceKind::Camera;
        std::vector<camera::CameraControlState> probedControls;
        std::vector<camera::CameraResolution> knownResolutions;
        camera::CameraResolution currentResolution;
        int autoIntervalMs = 700;
        double kSigma = 3.0;
        vision::WorkingZoneMode zoneMode = vision::WorkingZoneMode::Off;
        bool hasFixedZone = false;
        bool hasFreeZone = false;
        int expectedPieces = 1;
    };

    ConfigureDialog(Inputs inputs, QWidget* parent = nullptr);

    // Las páginas de formulario, para que la ventana lea sus valores al
    // aplicar. La de cámara se expone para conectar sus señales: aplica sola.
    [[nodiscard]] DetectionPage* detectionPage() const { return detection_; }
    [[nodiscard]] PreferencesPage* preferencesPage() const { return preferences_; }
    [[nodiscard]] CameraImagePage* cameraPage() const { return camera_; }
    [[nodiscard]] PerformancePage* performancePage() const { return performance_; }
    [[nodiscard]] PiecesPage* piecesPage() const { return pieces_; }

    // Índice de la pestaña visible, para recordarla entre sesiones.
    [[nodiscard]] int currentTab() const;
    void setCurrentTab(int index);

    // ¿Está el operador MIRANDO el recuento de piezas? Lo pregunta la ventana
    // porque contar no es gratis: cuesta una segmentación multi-pieza y obliga
    // a soltar el recorte automático, así que se hace para quien lo lee y no
    // por tener el panel abierto.
    [[nodiscard]] bool showingPieceCount() const;

    // Abrir la página que arregla algo, dicho por su NOMBRE. El diálogo es
    // quien sabe en qué posición tiene cada pestaña, y así reordenarlas no
    // rompe en silencio a quien las señala desde fuera.
    void showPage(ConfigureTarget target);

signals:
    // El operador pulsó Aplicar o Aceptar: la ventana debe leer las páginas.
    void applied();
    // Pestañas que son asistentes y no formularios.
    void scaleWizardRequested();
    void shortcutsRequested();

private:
    QTabWidget* tabs_ = nullptr;
    DetectionPage* detection_ = nullptr;
    PerformancePage* performance_ = nullptr;
    PiecesPage* pieces_ = nullptr;
    PreferencesPage* preferences_ = nullptr;
    CameraImagePage* camera_ = nullptr;
    // La pestaña de cámara, sea la página real o el sustituto de «todavía
    // no hay cámara». Se guarda aparte porque hay que poder llevar al
    // operador ahí en los dos casos.
    QWidget* cameraTab_ = nullptr;
};

}  // namespace pci::ui
