#pragma once

#include <QWidget>

#include <vector>

#include "camera/camera_controls.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;

namespace pci::camera {
class CameraController;
}

namespace pci::ui {

// Pagina «Camara e imagen» del panel Configurar (O2): brillo, contraste,
// ganancia, exposicion y enfoque de la camara en marcha. A diferencia del resto
// de paginas, esta **aplica al instante**: un deslizador de enfoque que solo
// tuviera efecto al pulsar Aceptar seria inservible, porque enfocar consiste en
// mover y mirar. Por eso el panel que la aloja es **no modal**.
//
// Lo que la cámara no soporta aparece deshabilitado en vez de fingir que
// funciona: OpenCV no expone la lista de propiedades y cada backend miente de
// forma distinta, así que solo se confía en lo que la cámara devolvió al abrir.
class CameraImagePage : public QWidget {
    Q_OBJECT

public:
    // `knownResolutions` evita el sondeo cuando ya se hizo para esta cámara:
    // preguntar resolución por resolución cuesta segundos y **detiene el vídeo**
    // mientras dura, así que solo se paga la primera vez (o si se pide).
    CameraImagePage(camera::CameraController& controller,
                         const std::vector<camera::CameraControlState>& probed,
                         const std::vector<camera::CameraResolution>& knownResolutions,
                         const camera::CameraResolution& currentResolution,
                         QWidget* parent = nullptr);

signals:
    // Valor cambiado por el operador, para que la ventana lo persista.
    void controlChanged(const pci::camera::CameraControlValue& control);
    // Resolución elegida, para que la ventana la persista y reajuste lo que
    // vive en píxeles (zona de detección, cero fijado del tablero).
    void resolutionChosen(const pci::camera::CameraResolution& resolution);
    // «Volver a los ajustes de medición»: olvidar lo que el operador guardó
    // para esta cámara y dejar que el perfil y el barrido decidan otra vez.
    void measurementProfileRequested();

public slots:
    // Llega desde el hilo de captura cuando termina el sondeo.
    void onResolutionsProbed(const std::vector<pci::camera::CameraResolution>& available,
                             const pci::camera::CameraResolution& current);

    // Asistente de enfoque (C2): nitidez del frame recien analizado.
    //
    // `onPiece` distingue "esto es la nitidez de la pieza" de "esto es la del
    // encuadre, porque no se detecta pieza". La diferencia importa: con un fondo
    // texturizado, la nitidez del frame entero puede subir mientras la de la
    // pieza baja, y el operador estaria enfocando la mesa.
    void setSharpness(double value, bool onPiece);
    // Olvida el maximo alcanzado. Hace falta al cambiar de pieza o de
    // iluminacion: un pico viejo e inalcanzable convierte la barra en un
    // adorno que nunca se llena.
    void resetSharpnessPeak();

private:
    struct Row {
        camera::CameraProperty property = camera::CameraProperty::Brightness;
        QSlider* slider = nullptr;
        QCheckBox* toggle = nullptr;
        bool supported = false;
    };

    void apply(camera::CameraProperty property, double value);
    // Un control manual no hace nada mientras su automático está activo: se
    // deshabilita en vez de dejar que el operador mueva algo inerte.
    void syncAutoDependencies();
    [[nodiscard]] bool autoActive(camera::CameraProperty autoProperty) const;

    camera::CameraController& controller_;
    std::vector<Row> rows_;
    QComboBox* resolutionCombo_ = nullptr;
    QPushButton* probeButton_ = nullptr;
    // Asistente de enfoque: barra relativa al maximo visto, porque la varianza
    // del Laplaciano no tiene tope y un valor absoluto no dice nada.
    QProgressBar* sharpnessBar_ = nullptr;
    QLabel* sharpnessLabel_ = nullptr;
    double sharpnessPeak_ = 0.0;
    bool comboWired_ = false;  // la señal del combo se conecta una sola vez
};

}  // namespace pci::ui
