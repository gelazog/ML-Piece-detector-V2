#include "ui/detection_page.h"

#include "vision/pipeline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pci::ui {

DetectionPage::DetectionPage(vision::SegmentationOptions current, QWidget* parent,
                                 repositories::DetectionProfileRepository* profiles,
                                 std::int64_t selectedProfileId, double minAreaFraction,
                                 double maxAreaFraction,
                             bool subpixelEdges)
    : QWidget(parent), profiles_(profiles) {
    auto* rootLayout = new QVBoxLayout(this);
    auto* help = new QLabel(
        tr("Si las luces o sombras arruinan el contorno automático: fija el umbral a "
           "mano, dile si la pieza es oscura o clara, y sube el suavizado. Combínalo "
           "con la \"Zona de detección\" para ignorar todo lo que quede fuera."),
        this);
    help->setWordWrap(true);
    rootLayout->addWidget(help);

    // Perfiles con nombre (O3): la misma pelea contra la luz se repite en cada
    // línea, así que se guarda con etiqueta y se reutiliza.
    if (profiles_ != nullptr) {
        auto* profileRow = new QHBoxLayout();
        profileRow->addWidget(new QLabel(tr("Perfil:"), this));
        profileCombo_ = new QComboBox(this);
        profileCombo_->setMinimumWidth(180);
        profileCombo_->setToolTip(
            tr("Juegos de ajustes guardados con nombre (p. ej. 'luz brillante').\n"
               "El perfil elegido se guarda con la pieza seleccionada."));
        profileRow->addWidget(profileCombo_, 1);
        auto* saveButton = new QPushButton(tr("Guardar como…"), this);
        auto* deleteButton = new QPushButton(tr("Eliminar"), this);
        profileRow->addWidget(saveButton);
        profileRow->addWidget(deleteButton);
        rootLayout->addLayout(profileRow);
        connect(saveButton, &QPushButton::clicked, this, &DetectionPage::onSaveProfile);
        connect(deleteButton, &QPushButton::clicked, this, &DetectionPage::onDeleteProfile);
        connect(profileCombo_, &QComboBox::currentIndexChanged, this,
                &DetectionPage::onProfileChosen);
    }

    auto* form = new QFormLayout();

    autoThreshold_ = new QCheckBox(tr("Automático (Otsu)"), this);
    autoThreshold_->setChecked(current.manualThreshold < 0);
    form->addRow(tr("Umbral:"), autoThreshold_);

    threshold_ = new QSlider(Qt::Horizontal, this);
    threshold_->setRange(0, 255);
    threshold_->setValue(current.manualThreshold >= 0 ? current.manualThreshold : 128);
    threshold_->setEnabled(current.manualThreshold >= 0);
    thresholdValue_ = new QLabel(QString::number(threshold_->value()), this);
    auto* sliderRow = new QHBoxLayout();
    sliderRow->addWidget(threshold_, 1);
    sliderRow->addWidget(thresholdValue_);
    form->addRow(tr("Umbral manual:"), sliderRow);

    polarity_ = new QComboBox(this);
    polarity_->addItem(tr("Automática (el fondo domina el borde)"));
    polarity_->addItem(tr("Pieza oscura sobre fondo claro"));
    polarity_->addItem(tr("Pieza clara sobre fondo oscuro"));
    polarity_->setCurrentIndex(static_cast<int>(current.polarity));
    form->addRow(tr("Polaridad:"), polarity_);

    blur_ = new QSpinBox(this);
    blur_->setRange(1, 31);
    blur_->setSingleStep(2);
    blur_->setValue(current.blurKernel);
    blur_->setToolTip(tr("Suavizado previo: más alto = menos ruido, bordes menos finos"));
    form->addRow(tr("Suavizado (px):"), blur_);

    morph_ = new QSpinBox(this);
    morph_->setRange(1, 31);
    morph_->setSingleStep(2);
    morph_->setValue(current.morphKernel);
    morph_->setToolTip(
        tr("Limpieza morfológica: elimina motas y rellena huecos de ese tamaño"));
    form->addRow(tr("Limpieza (px):"), morph_);

    // Qué cuenta como pieza. Estaba fijo en el código; con piezas pequeñas el
    // 0,5 % por defecto es justo la frontera entre "no hay pieza" y "hay
    // pieza", y no se podía mover sin recompilar.
    minArea_ = new QDoubleSpinBox(this);
    minArea_->setRange(0.01, 50.0);
    minArea_->setDecimals(2);
    minArea_->setSingleStep(0.1);
    minArea_->setSuffix(tr(" %"));
    minArea_->setValue(minAreaFraction * 100.0);
    minArea_->setToolTip(
        tr("Por debajo de esta fracción de la imagen, una mancha no es una pieza.\n"
           "Por defecto 0,50 %. Bájalo si tus piezas son pequeñas y no se detectan;\n"
           "súbelo si se cuela ruido."));
    form->addRow(tr("Área mínima de pieza:"), minArea_);

    maxArea_ = new QDoubleSpinBox(this);
    maxArea_->setRange(10.0, 100.0);
    maxArea_->setDecimals(1);
    maxArea_->setSingleStep(1.0);
    maxArea_->setSuffix(tr(" %"));
    maxArea_->setValue(maxAreaFraction * 100.0);
    maxArea_->setToolTip(
        tr("Por encima de esta fracción se considera que la segmentación falló\n"
           "(la luz marcó toda la imagen) en vez de que la pieza sea enorme.\n"
           "Por defecto 90 %."));
    form->addRow(tr("Área máxima de pieza:"), maxArea_);

    // Afinado subpíxel del borde.
    //
    // Va aquí, entre lo que decide DÓNDE está el borde, porque es justo eso:
    // otra definición del borde, no un filtro de calidad.
    //
    // Y nace apagado con su advertencia escrita, no escondida en la ayuda: al
    // cambiar dónde cae el borde, cambian el área, el perímetro y todas las
    // cotas de la pieza a la vez. Quien tenga tolerancias ajustadas contra el
    // borde de antes tiene que volver a mirarlas — si no, una pieza buena
    // empieza a salir NG por un cambio de definición y no por un defecto.
    subpixel_ = new QCheckBox(tr("Afinar el borde a subpíxel"), this);
    subpixel_->setChecked(subpixelEdges);
    subpixel_->setToolTip(
        tr("El borde de una pieza no es un escalón: la intensidad cambia a lo largo de\n"
           "varios píxeles. Medido sobre una foto real, esa rampa ocupaba 15 px, y un\n"
           "umbral coloca el borde en cualquier punto de ella según la iluminación.\n\n"
           "Con esto, cada punto del contorno se coloca donde el brillo cruza la mitad\n"
           "entre el nivel de dentro y el de fuera EN ESE PUNTO, interpolando entre\n"
           "píxeles. Medido sobre un borde de posición conocida, el error pasa de\n"
           "0,417 px a 0,025 px.\n\n"
           "OJO: cambia dónde está el borde, así que cambian el área, el perímetro y\n"
           "todas las cotas de la pieza a la vez. Si ya tienes tolerancias ajustadas,\n"
           "revísalas después de encenderlo."));
    form->addRow(tr("Precisión:"), subpixel_);

    rootLayout->addLayout(form);
    rootLayout->addStretch(1);

    connect(autoThreshold_, &QCheckBox::toggled, this,
            &DetectionPage::onAutoThresholdToggled);
    connect(threshold_, &QSlider::valueChanged, this, &DetectionPage::onThresholdMoved);

    if (profiles_ != nullptr) {
        reloadProfiles(selectedProfileId);
    }
}

void DetectionPage::reloadProfiles(std::int64_t selectId) {
    QSignalBlocker blocker(profileCombo_);
    profileCombo_->clear();
    profileCombo_->addItem(tr("(ajustes sueltos, sin perfil)"), QVariant::fromValue<qint64>(0));
    auto listed = profiles_->list();
    if (!listed.isOk()) {
        return;  // sin perfiles utilizables: el diálogo sigue sirviendo igual
    }
    for (const auto& profile : listed.value()) {
        profileCombo_->addItem(QString::fromStdString(profile.name),
                               QVariant::fromValue<qint64>(profile.id));
    }
    const int index = profileCombo_->findData(QVariant::fromValue<qint64>(selectId));
    profileCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void DetectionPage::applyOptions(const vision::SegmentationOptions& options) {
    autoThreshold_->setChecked(options.manualThreshold < 0);
    threshold_->setValue(options.manualThreshold >= 0 ? options.manualThreshold : 128);
    threshold_->setEnabled(options.manualThreshold >= 0);
    polarity_->setCurrentIndex(static_cast<int>(options.polarity));
    blur_->setValue(options.blurKernel);
    morph_->setValue(options.morphKernel);
}

void DetectionPage::restoreDefaults() {
    // Construidas por defecto: los valores de fábrica son los inicializadores
    // de miembro de las propias estructuras, no una copia escrita aquí.
    applyOptions(vision::SegmentationOptions{});
    const vision::PipelineConfig factory;
    minArea_->setValue(factory.minAreaFraction * 100.0);
    maxArea_->setValue(factory.maxAreaFraction * 100.0);
    // Y sin perfil: un perfil es una elección del operador, así que restablecer
    // vuelve a «ajustes sueltos» en vez de dejar puesto uno que ya no
    // corresponde a lo que enseñan los controles.
    if (profileCombo_ != nullptr) {
        profileCombo_->setCurrentIndex(0);
    }
}

void DetectionPage::onProfileChosen(int index) {
    if (profiles_ == nullptr || index < 0) {
        return;
    }
    const auto id = profileCombo_->itemData(index).toLongLong();
    if (id <= 0) {
        return;  // "sin perfil": se conservan los valores que haya en pantalla
    }
    if (auto profile = profiles_->load(id); profile.isOk()) {
        applyOptions(profile.value().options);
    }
}

void DetectionPage::onSaveProfile() {
    if (profiles_ == nullptr) {
        return;
    }
    const QString suggestion = profileCombo_->currentData().toLongLong() > 0
                                   ? profileCombo_->currentText()
                                   : QString();
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Guardar perfil de detección"),
                              tr("Nombre del perfil (p. ej. 'luz brillante'):"),
                              QLineEdit::Normal, suggestion, &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    auto saved = profiles_->save(name.toStdString(), options());
    if (!saved.isOk()) {
        QMessageBox::warning(this, tr("No se pudo guardar"),
                             QString::fromStdString(saved.error().message));
        return;
    }
    reloadProfiles(saved.value());
}

void DetectionPage::onDeleteProfile() {
    if (profiles_ == nullptr) {
        return;
    }
    const auto id = profileCombo_->currentData().toLongLong();
    if (id <= 0) {
        return;
    }
    const QString name = profileCombo_->currentText();
    if (QMessageBox::question(
            this, tr("Eliminar perfil"),
            tr("¿Eliminar el perfil '%1'? Las piezas que lo usaban volverán a los "
               "ajustes globales.")
                .arg(name)) != QMessageBox::Yes) {
        return;
    }
    if (auto removed = profiles_->remove(id); !removed.isOk()) {
        QMessageBox::warning(this, tr("No se pudo eliminar"),
                             QString::fromStdString(removed.error().message));
        return;
    }
    reloadProfiles(0);
}

std::int64_t DetectionPage::selectedProfileId() const {
    return profileCombo_ != nullptr ? profileCombo_->currentData().toLongLong() : 0;
}

void DetectionPage::onAutoThresholdToggled(bool automatic) {
    threshold_->setEnabled(!automatic);
}

void DetectionPage::onThresholdMoved(int value) {
    thresholdValue_->setText(QString::number(value));
}

double DetectionPage::minAreaFraction() const {
    return minArea_ != nullptr ? minArea_->value() / 100.0 : 0.005;
}

double DetectionPage::maxAreaFraction() const {
    return maxArea_ != nullptr ? maxArea_->value() / 100.0 : 0.9;
}

vision::SegmentationOptions DetectionPage::options() const {
    vision::SegmentationOptions result;
    result.manualThreshold = autoThreshold_->isChecked() ? -1 : threshold_->value();
    result.polarity = static_cast<vision::SegmentationPolarity>(polarity_->currentIndex());
    result.blurKernel = blur_->value();
    result.morphKernel = morph_->value();
    return result;
}

bool DetectionPage::subpixelEdges() const {
    return subpixel_ != nullptr && subpixel_->isChecked();
}

}  // namespace pci::ui