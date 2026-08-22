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
        // El motivo depende de POR QUÉ no hay controles, y son dos cosas
        // distintas. Con la cámara parada, la respuesta es «arráncala». Con una
        // imagen o un vídeo, decirle eso al operador sería mandarle a hacer
        // algo que ya hizo —la fuente está funcionando— y dejarle pensando que
        // la aplicación no se entera.
        const QString why =
            inputs.sourceKind == camera::SourceKind::Camera
                ? tr("Inicia la cámara para ajustar su brillo, exposición, enfoque y "
                     "resolución.\n\nLos controles se preguntan a la propia cámara al "
                     "abrirla, así que hasta entonces no se sabe cuáles admite: mostrar "
                     "deslizadores que no harían nada sería peor que no mostrarlos.")
                : camera::whyNotAdjustable(inputs.sourceKind) +
                      tr("\n\nTodo lo demás —detección, zona de trabajo, herramientas, "
                         "medición automática e inspección— funciona igual que con la "
                         "cámara.");
        auto* label = new QLabel(why, placeholder);
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
                                   inputs.maxAreaFraction, inputs.subpixelEdges);
    tabs_->addTab(detection_, tr("Detección"));

    // --- Piezas ---
    pieces_ = new PiecesPage(inputs.expectedPieces, this);
    tabs_->addTab(pieces_, tr("Piezas"));

    // --- Rendimiento ---
    performance_ = new PerformancePage(inputs.zoneMode, inputs.hasFixedZone,
                                       inputs.hasFreeZone, this);
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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply |
                                             QDialogButtonBox::RestoreDefaults |
                                             QDialogButtonBox::Close,
                                         this);
    // `RestoreDefaults` y no un botón propio: es el papel que Qt ya tiene para
    // esto, así que sale colocado donde el operador lo espera en su sistema y
    // con el texto de su idioma.
    //
    // Restablece LA PESTAÑA QUE SE ESTÁ VIENDO, no todo. Restablecer entero es
    // otra cosa y vive en Archivo, con su propia confirmación: quien viene aquí
    // a desenredar el umbral no quiere perder la calibración de la máquina.
    auto* restore = buttons->button(QDialogButtonBox::RestoreDefaults);
    connect(restore, &QPushButton::clicked, this, [this, restore] {
        if (detection_ == nullptr || tabs_ == nullptr ||
            tabs_->currentWidget() != detection_) {
            return;
        }
        detection_->restoreDefaults();
        emit applied();
        // Se dice qué se ha hecho: un formulario que cambia solo bajo el cursor,
        // sin decir por qué, parece que se ha estropeado.
        restore->setToolTip(tr("Detección devuelta a los valores de fábrica."));
    });
    // Encendido SOLO donde hay algo que restablecer, y apagado con su motivo en
    // el resto: un botón vivo que no hace nada enseña a desconfiar de los
    // botones, y uno apagado sin explicación deja pensando qué falta.
    const auto updateRestore = [this, restore] {
        const bool canRestore = detection_ != nullptr && tabs_ != nullptr &&
                                tabs_->currentWidget() == detection_;
        restore->setEnabled(canRestore);
        restore->setToolTip(canRestore
                                ? tr("Devuelve esta pestaña a los valores de fábrica.\n"
                                     "Para restablecerlo todo: Archivo ▸ Restablecer "
                                     "configuración de fábrica…")
                                : tr("Esta pestaña no tiene valores de fábrica que "
                                     "restablecer.\nPara restablecerlo todo: Archivo ▸ "
                                     "Restablecer configuración de fábrica…"));
    };
    connect(tabs_, &QTabWidget::currentChanged, this, [updateRestore](int) { updateRestore(); });
    updateRestore();
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

bool ConfigureDialog::showingPieceCount() const {
    // Por el widget visible y no por un índice: este proyecto ya pagó una vez
    // el precio de señalar pestañas por su número.
    return tabs_ != nullptr && pieces_ != nullptr && tabs_->currentWidget() == pieces_;
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
