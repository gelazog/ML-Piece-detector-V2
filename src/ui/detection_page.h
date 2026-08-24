#pragma once

#include <QWidget>

#include <cstdint>

#include "repositories/detection_profile_repository.h"
#include "vision/pipeline.h"
#include "vision/edge_segmentation.h"
#include "vision/segmentation.h"

class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QSlider;
class QDoubleSpinBox;
class QSpinBox;

namespace pci::ui {

// Pagina «Deteccion» del panel Configurar: umbral (Otsu o manual), polaridad
// de la pieza, suavizado y limpieza morfologica — para pelear contra luces y
// sombras dificiles. Es un formulario: no aplica nada por su cuenta, la ventana
// le pide los valores cuando el operador pulsa Aplicar.
class DetectionPage : public QWidget {
    Q_OBJECT

public:
    // profiles puede ser nulo (sin base de datos): el diálogo funciona igual,
    // solo sin la parte de perfiles con nombre (O3).
    DetectionPage(vision::SegmentationOptions current, QWidget* parent = nullptr,
                    repositories::DetectionProfileRepository* profiles = nullptr,
                    std::int64_t selectedProfileId = 0,
                    double minAreaFraction = 0.005, double maxAreaFraction = 0.9,
                    bool subpixelEdges = false);

    [[nodiscard]] vision::SegmentationOptions options() const;
    // Qué se acepta como pieza, en fracción del área de la imagen. Estaban
    // fijas en el código y decidían la frontera entre "no hay pieza" y "hay
    // pieza": con piezas pequeñas, el 0,5 % por defecto es justo esa frontera y
    // no se podía mover sin recompilar.
    [[nodiscard]] double minAreaFraction() const;
    [[nodiscard]] double maxAreaFraction() const;
    // Afinado subpíxel del borde. Se ofrece aquí, junto a lo que decide DÓNDE
    // está el borde, porque es exactamente eso: no es un filtro de calidad, es
    // otra definición del borde.
    [[nodiscard]] bool subpixelEdges() const;
    // Perfil elegido al aceptar: 0 = ninguno (ajustes sueltos, como antes).
    [[nodiscard]] std::int64_t selectedProfileId() const;

    // Devolver la página a los valores de FÁBRICA.
    //
    // Los valores no se escriben aquí: salen de construir por defecto las
    // propias estructuras del modelo —`SegmentationOptions{}` y
    // `PipelineConfig{}`—, que es donde ya vivían. Escribir aquí una copia
    // crearía dos listas que mantener, y a la primera que alguien cambiara una
    // sola, «restablecer» dejaría la página en un estado que no es ni el suyo
    // ni el de fábrica. Es la misma regla que gobierna `SettingsRepository::forget`.
    void restoreDefaults();

    // VOLVER A CARGAR LO DE OTRA PIEZA, con la ventana ya abierta.
    //
    // La ventana de Configurar es única y el selector de piezas vive fuera de
    // ella, así que se puede cambiar de trabajo con la ventana delante. Lo que
    // hubiera dentro sería entonces de la pieza ANTERIOR — y aceptar le
    // asignaría a la nueva el perfil y los ajustes de la otra.
    void reloadFor(vision::SegmentationOptions options, std::int64_t profileId,
                   double minAreaFraction, double maxAreaFraction, bool subpixelEdges);

private slots:
    void onAutoThresholdToggled(bool automatic);
    void onThresholdMoved(int value);
    void onProfileChosen(int index);  // vuelca el perfil en los controles
    void onSaveProfile();             // guarda los controles como perfil
    void onDeleteProfile();

private:
    void reloadProfiles(std::int64_t selectId);
    void applyOptions(const vision::SegmentationOptions& options);

    repositories::DetectionProfileRepository* profiles_ = nullptr;
    QComboBox* profileCombo_ = nullptr;
    QCheckBox* autoThreshold_ = nullptr;
    QCheckBox* subpixel_ = nullptr;
    QSlider* threshold_ = nullptr;
    QLabel* thresholdValue_ = nullptr;
    QLabel* sceneHint_ = nullptr;
    QPushButton* useEdgesButton_ = nullptr;
    QPushButton* clipCheckButton_ = nullptr;
    QCheckBox* splitTouching_ = nullptr;
    QLabel* clipResult_ = nullptr;
    QComboBox* method_ = nullptr;
    QComboBox* polarity_ = nullptr;

public:
    // LO QUE LA IMAGEN DE AHORA DICE DE SI MISMA.
    //
    // Sin esto, «Por el canto de la pieza» solo lo encuentra quien ya sabe que
    // esta ahi — y quien esta en esta pestaña justo esta aqui porque la
    // deteccion no le funciona. La lectura de la escena sabe distinguir cuando
    // ningun umbral por nivel puede servir (`vision/edge_segmentation.h`), y ese
    // es el momento de decirlo, no despues.
    void setSceneReading(const vision::SceneReading& reading);

    // EL RESULTADO DE «¿ESTÁ CORTANDO LA PIEZA?».
    //
    // Se enseña aquí y no en un aviso aparte porque la salida está justo al
    // lado: el selector de método y el umbral manual. Un diagnóstico lejos del
    // control que hay que tocar obliga a buscarlo.
    void setClippingCheck(const vision::ClippingCheck& check);

signals:
    // El operador pide la comprobación. La ventana la hace, porque cuesta dos
    // análisis completos —60 ms con cien piezas— y esta página no tiene la
    // imagen.
    void clippingCheckRequested();

public:
    QSpinBox* blur_ = nullptr;
    QSpinBox* morph_ = nullptr;
    QDoubleSpinBox* minArea_ = nullptr;
    QDoubleSpinBox* maxArea_ = nullptr;
};

}  // namespace pci::ui
