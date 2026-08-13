#include "ui/configure_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <utility>

#include "ui/camera_image_page.h"
#include "ui/detection_page.h"
#include "ui/performance_page.h"
#include "ui/pieces_page.h"
#include "ui/preferences_page.h"

namespace pci::ui {

namespace {

// Pestaña de las dos cosas que no son un formulario: un texto que explica qué
// se ajusta ahí y el botón que abre el asistente de siempre.
QWidget* wizardTab(const QString& explanation, const QString& buttonText,
                   QPushButton** outButton, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* label = new QLabel(explanation, page);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* button = new QPushButton(buttonText, page);
    layout->addWidget(button);
    layout->addStretch(1);
    *outButton = button;
    return page;
}

}  // namespace

ConfigureDialog::ConfigureDialog(Inputs inputs, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Configurar"));
    resize(560, 520);

    auto* root = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    root->addWidget(tabs_, 1);

    // --- Cámara e imagen ---
    if (inputs.controller != nullptr && !inputs.probedControls.empty()) {
        camera_ = new CameraImagePage(*inputs.controller, inputs.probedControls,
                                      inputs.knownResolutions, inputs.currentResolution,
                                      this);
        cameraTab_ = camera_;
        tabs_->addTab(camera_, tr("Cámara e imagen"));
    } else {
        auto* placeholder = new QWidget(this);
        auto* layout = new QVBoxLayout(placeholder);
        auto* label = new QLabel(
            tr("Inicia la cámara para ajustar su brillo, exposición, enfoque y "
               "resolución.\n\nLos controles se preguntan a la propia cámara al "
               "abrirla, así que hasta entonces no se sabe cuáles admite: mostrar "
               "deslizadores que no harían nada sería peor que no mostrarlos."),
            placeholder);
        label->setWordWrap(true);
        layout->addWidget(label);
        layout->addStretch(1);
        // El sustituto también se recuerda: sin cámara, la pestaña existe
        // igual y es justo donde hay que llegar para leer POR QUÉ está
        // vacía. Apuntar solo a la página real dejaba ese clic sin efecto
        // precisamente cuando más falta hace.
        cameraTab_ = placeholder;
        tabs_->addTab(placeholder, tr("Cámara e imagen"));
    }

    // --- Detección ---
    detection_ = new DetectionPage(inputs.segmentation, this, inputs.profiles,
                                   inputs.detectionProfileId, inputs.minAreaFraction,
                                   inputs.maxAreaFraction);
    tabs_->addTab(detection_, tr("Detección"));

    // --- Piezas ---
    pieces_ = new PiecesPage(inputs.expectedPieces, this);
    tabs_->addTab(pieces_, tr("Piezas"));

    // --- Rendimiento ---
    performance_ = new PerformancePage(inputs.zoneMode, inputs.hasFixedZone, this);
    tabs_->addTab(performance_, tr("Rendimiento"));

    // --- Escala (asistente) ---
    QPushButton* scaleButton = nullptr;
    tabs_->addTab(
        wizardTab(tr("La escala convierte los píxeles en milímetros. Se calibra "
                     "marcando dos puntos de una distancia conocida sobre una foto "
                     "de la pieza, o indicando la distancia de la cámara y su campo "
                     "de visión.\n\nComo hay que hacer clic sobre la imagen, se "
                     "ajusta en un asistente y no en un formulario."),
                  tr("Calibrar la escala…"), &scaleButton, this),
        tr("Escala"));
    connect(scaleButton, &QPushButton::clicked, this,
            &ConfigureDialog::scaleWizardRequested);

    // --- Preferencias ---
    preferences_ = new PreferencesPage(inputs.autoIntervalMs, inputs.kSigma, this);
    tabs_->addTab(preferences_, tr("Preferencias"));

    // --- Atajos (asistente) ---
    QPushButton* shortcutsButton = nullptr;
    tabs_->addTab(
        wizardTab(tr("Cada comando de la aplicación tiene una tecla y se puede "
                     "cambiar. La lista completa, con sus valores por defecto, se "
                     "edita en su propia tabla."),
                  tr("Editar los atajos…"), &shortcutsButton, this),
        tr("Atajos"));
    connect(shortcutsButton, &QPushButton::clicked, this,
            &ConfigureDialog::shortcutsRequested);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emit applied();
        close();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &ConfigureDialog::applied);
}

int ConfigureDialog::currentTab() const {
    return tabs_ != nullptr ? tabs_->currentIndex() : 0;
}

void ConfigureDialog::showPage(ConfigureTarget target) {
    QWidget* page = nullptr;
    switch (target) {
        case ConfigureTarget::Camera: page = cameraTab_; break;
        case ConfigureTarget::Performance: page = performance_; break;
        case ConfigureTarget::None: break;
    }
    if (page != nullptr && tabs_ != nullptr) {
        tabs_->setCurrentWidget(page);
    }
}

void ConfigureDialog::setCurrentTab(int index) {
    if (tabs_ != nullptr && index >= 0 && index < tabs_->count()) {
        tabs_->setCurrentIndex(index);
    }
}

}  // namespace pci::ui
