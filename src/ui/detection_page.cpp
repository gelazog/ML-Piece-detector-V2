#include "ui/detection_page.h"
#include "ui/theme.h"

#include "vision/pipeline.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include "vision/edge_segmentation.h"
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
    autoThreshold_->setToolTip(
        tr("El programa elige solo el nivel de gris que separa la pieza del\n"
           "fondo, mirando el histograma de cada imagen (método de Otsu).\n\n"
           "Es lo que quieres casi siempre: se adapta si cambia la luz.\n\n"
           "Desactívalo solo si la pieza y el fondo se parecen tanto que la\n"
           "elección automática baila entre fotogramas — entonces fija tú el\n"
           "umbral con la barra de abajo."));
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

    // CÓMO se separa la pieza del fondo. Va ANTES del umbral y la polaridad
    // porque decide si esos dos significan algo: segmentando por el canto no hay
    // corte de gris que ajustar.
    // El aviso va JUSTO ENCIMA del selector de metodo, no al final de la
    // pestaña: un consejo lejos del control que hay que tocar obliga a buscarlo.
    sceneHint_ = new QLabel(this);
    sceneHint_->setWordWrap(true);
    sceneHint_->setVisible(false);
    form->addRow(sceneHint_);
    useEdgesButton_ = new QPushButton(tr("Cambiar a «por el canto»"), this);
    useEdgesButton_->setToolTip(
        tr("Pasa a separar la pieza por su CANTO en vez de por el nivel de gris.\n\n"
           "Aparece solo cuando el programa ha mirado la imagen y ha visto que\n"
           "no hay un gris que sirva de corte: pasa cuando la pieza tiene\n"
           "brillos más claros que el fondo y sombras más oscuras a la vez,\n"
           "como una tuerca metálica sobre una mesa clara.\n\n"
           "En escenas fáciles NO es mejor: por eso es un botón que aparece\n"
           "cuando hace falta y no el método por defecto."));
    useEdgesButton_->setVisible(false);
    form->addRow(useEdgesButton_);

    // ¿ESTÁ EL UMBRAL CORTANDO LA PIEZA?
    //
    // Es el fallo que no falla: el programa mide corto, el contorno sale
    // limpio, y no hay nada que avisar de que falta pieza. Medido sobre las
    // fotos reales, el umbral automático se come la cabeza cromada de un
    // tornillo y le quita el 36 % del área sin una sola señal.
    //
    // Va a petición y no continuo porque cuesta dos análisis completos: 60 ms
    // con cien piezas. Hacerlo por fotograma para responder casi siempre «no
    // pasa nada» sería pagar mucho por poco.
    clipCheckButton_ = new QPushButton(tr("¿Está el umbral cortando la pieza?"), this);
    clipCheckButton_->setToolTip(
        tr("Afloja el umbral unos niveles y mira cuánta pieza aparece.\n\n"
           "Si aparece mucha, es que el corte cae DENTRO de la pieza y no en su\n"
           "borde: hay partes suyas casi tan claras como la mesa —una cabeza\n"
           "cromada, un canto pulido— y se están quedando fuera.\n\n"
           "Eso hace que las medidas salgan CORTAS sin que nada avise, porque un\n"
           "contorno recortado es perfectamente limpio: no hay nada sucio, hay\n"
           "pieza que falta.\n\n"
           "No hace falta saber cuánto mide la pieza de verdad: se compara la\n"
           "imagen consigo misma."));
    connect(clipCheckButton_, &QPushButton::clicked, this,
            &DetectionPage::clippingCheckRequested);
    form->addRow(clipCheckButton_);

    clipResult_ = new QLabel(this);
    clipResult_->setWordWrap(true);
    clipResult_->setVisible(false);
    form->addRow(clipResult_);

    // SEPARAR LAS PIEZAS QUE SE TOCAN.
    //
    // Sale de una queja directa: «si las piezas están muy pegadas, las detecta
    // como una sola». Es literal — dos engranajes engranados salían como UNA
    // pieza, y entonces no hay nada que recorrer con las flechas ni que enseñar
    // en el mosaico.
    //
    // Nace apagado y está medido por qué: no gana siempre.
    splitTouching_ = new QCheckBox(tr("Separar las piezas que se tocan"), this);
    splitTouching_->setToolTip(
        tr("Cuando dos piezas se rozan, el contorno exterior las devuelve como\n"
           "UNA. Con esto, cada mancha se mira por dentro: se busca el «corazón»\n"
           "de cada pieza —la zona más alejada del fondo— y se corta por el\n"
           "cuello que las une.\n\n"
           "Medido sobre imágenes reales, con esto encendido:\n"
           "  · dos engranajes engranados:   1 → 2 piezas   LO ARREGLA\n"
           "  · tres tornillos en fila:      3 → 3          igual\n"
           "  · bandeja de cien tuercas:   100 → 100        igual\n"
           "  · un tornillo largo solo:      1 → 2          LO ROMPE\n\n"
           "Un tornillo largo tiene la cabeza y el vástago lo bastante distintos\n"
           "como para parecer dos piezas. Por eso es una opción y no viene de\n"
           "fábrica: enciéndela si tus piezas se tocan, déjala apagada si son\n"
           "alargadas con cabeza.\n\n"
           "Cuesta entre 3 y 16 ms por análisis."));
    form->addRow(splitTouching_);

    // RECUPERAR LO QUE EL BRILLO SE LLEVA.
    //
    // Queja de uso: «tengo una tuerca, con reflejos, brillo, sombras, y eso
    // afecta a la medición y la forma en que toma los bordes». Va justo debajo
    // de la otra corrección de contorno porque las dos responden a lo mismo —la
    // silueta no es la que se ve— y separarlas obligaría a buscar en dos sitios.
    recoverGlare_ = new QCheckBox(tr("Recuperar lo que el brillo se lleva"), this);
    recoverGlare_->setToolTip(
        tr("El reflejo de una pieza metálica sube hasta el nivel del fondo, el\n"
           "corte de gris lo deja fuera, y la pieza sale MORDIDA o partida en\n"
           "trozos. No es un fallo del brillo: es que un corte único supone que\n"
           "la pieza cae entera de un lado, y sobre metal eso es falso.\n\n"
           "Con esto se corta dos veces. El corte de siempre da las SEMILLAS —lo\n"
           "que es pieza con seguridad—; un corte aflojado doce niveles dice\n"
           "hasta dónde PODRÍA llegar; y se conserva solo lo aflojado que TOQUE\n"
           "una semilla.\n\n"
           "Por eso no deja entrar el fondo: la mesa aflojada tampoco toca\n"
           "ninguna semilla. Sube el brillo de la cara de la pieza, que está\n"
           "pegado a ella, y no la sombra pegada a la mesa.\n\n"
           "Medido sobre las fotos reales, con esto encendido:\n"
           "  · tres tornillos cincados:   5 → 3 piezas   LO ARREGLA\n"
           "  · un tornillo galvanizado:   2 → 1          LO ARREGLA\n"
           "  · bandeja de cien tuercas: 100 → 100        igual\n"
           "  · un engranaje:              1 → 1          igual\n\n"
           "Nace apagado porque cambia lo que se mide."));
    form->addRow(recoverGlare_);

    // SEPARAR POR EL COLOR DEL FONDO.
    //
    // Queja de uso: «en arandelas-1 el fondo es rojo, y solo detecta las piezas
    // de color gris o cromado, las demás no las toma en cuenta». Era literal, y
    // el motivo es que lo primero que hacía el programa con una foto en color
    // era tirar el color.
    //
    // Va aquí, junto a las otras dos correcciones de contorno, porque las tres
    // responden a lo mismo: la silueta que sale no es la que se ve.
    backgroundKey_ = new QComboBox(this);
    backgroundKey_->addItem(tr("No: separar por claridad (lo habitual)"));
    backgroundKey_->addItem(tr("Sí, y el color del fondo lo busca solo"));
    backgroundKey_->addItem(tr("Sí, y el color del fondo lo digo yo"));
    backgroundKey_->setToolTip(
        tr("Separa la pieza por lo distinto que es su COLOR del color del fondo,\n"
           "en vez de por lo claro u oscuro que sea.\n"
           "\n"
           "Sirve cuando el fondo tiene color. Sobre un cartón rojo, una arandela\n"
           "de latón tiene casi la misma CLARIDAD que el fondo —el rojo cae en\n"
           "gris 116, un gris medio— y lo único que las separa es el tono.\n"
           "\n"
           "Medido sobre esa foto, con una veintena de arandelas de acero, latón,\n"
           "cobre, caucho, fibra y plástico:\n"
           "  · por claridad:   7 piezas, 11 %% del cuadro\n"
           "  · por color:     20 piezas, 23 %%\n"
           "\n"
           "Las trece que aparecen son las que no son cromadas.\n"
           "\n"
           "Sobre fondo blanco no cambia nada: el engranaje, el cáncamo y la\n"
           "bandeja de cien tuercas dan las mismas piezas por los dos caminos.\n"
           "\n"
           "«Lo busca solo» toma la mediana del marco de la imagen, que es fondo\n"
           "casi siempre. Dilo tú si el puesto tiene piezas pegadas al borde o si\n"
           "quieres que no dependa de lo que haya en la escena.\n"
           "\n"
           "Nace apagado porque cambia lo que se mide."));
    form->addRow(tr("Clave de color de fondo"), backgroundKey_);

    backgroundColour_ = new QPushButton(this);
    backgroundColour_->setToolTip(
        tr("El color exacto del fondo del puesto. Solo se usa con «lo digo yo»."));
    form->addRow(QString(), backgroundColour_);
    connect(backgroundKey_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                backgroundColour_->setEnabled(index == 2);
            });
    connect(backgroundColour_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(background_, this,
                                                     tr("Color del fondo del puesto"));
        if (chosen.isValid()) {
            background_ = chosen;
            paintBackgroundSwatch();
        }
    });
    // Y SE PARTE DE LO QUE ENTRA, no de un blanco fijo.
    //
    // La primera versión ponía aquí blanco y «no» a pelo, sin mirar los ajustes
    // que recibe la página. El efecto: entrabas a cambiar el umbral, aceptabas,
    // y de paso te apagaba la clave de color y te borraba el color del puesto —
    // sin decirlo, y la detección empeoraba «desde hace un tiempo». Lo cazó la
    // prueba de ida y vuelta que ya existía justo para esta familia de fallos.
    backgroundKey_->setCurrentIndex(static_cast<int>(current.backgroundKey));
    background_ = QColor(current.background[2], current.background[1], current.background[0]);
    paintBackgroundSwatch();
    backgroundColour_->setEnabled(current.backgroundKey ==
                                  vision::SegmentationOptions::BackgroundKey::Fixed);

    method_ = new QComboBox(this);
    method_->addItem(tr("Por nivel de gris (lo habitual)"));
    method_->addItem(tr("Por el canto de la pieza"));
    method_->setCurrentIndex(static_cast<int>(current.method));
    splitTouching_->setChecked(current.splitTouchingPieces);
    recoverGlare_->setChecked(current.recoverHighlightsBy > 0);
    connect(useEdgesButton_, &QPushButton::clicked, this, [this] {
        method_->setCurrentIndex(static_cast<int>(vision::SegmentationMethod::Edges));
    });
    method_->setToolTip(
        tr("«Por nivel» busca un corte de gris que deje la pieza a un lado y el\n"
           "fondo al otro. Es lo que funciona casi siempre.\n"
           "\n"
           "«Por el canto» no mira el nivel sino el borde, y hace falta cuando la\n"
           "pieza tiene a la vez reflejos más claros y sombras más oscuras que la\n"
           "mesa: entonces NINGÚN corte único la separa — el que recoge unas\n"
           "partes deja fuera a otras.\n"
           "\n"
           "Medido sobre una foto de siete tuercas metálicas: por nivel salen seis\n"
           "piezas, con tres fundidas por puentes de sombra; por el canto salen\n"
           "las siete enteras. En una pieza oscura sobre fondo claro es al revés,\n"
           "así que no es «mejor»: es para otra escena."));
    form->addRow(tr("Cómo separar la pieza:"), method_);

    polarity_ = new QComboBox(this);
    polarity_->addItem(tr("Automática (el fondo domina el borde)"));
    polarity_->addItem(tr("Pieza oscura sobre fondo claro"));
    polarity_->addItem(tr("Pieza clara sobre fondo oscuro"));
    polarity_->setCurrentIndex(static_cast<int>(current.polarity));
    polarity_->setToolTip(
        tr("Cuál de los dos lados del corte de gris es la PIEZA.\n\n"
           "«Automática» decide mirando el borde del encuadre: lo que toca el\n"
           "marco se toma por fondo. Acierta casi siempre y es lo que quieres\n"
           "salvo que la pieza llegue cortada por el borde.\n\n"
           "Fíjala a mano cuando el resultado sale invertido —se mide el hueco\n"
           "en vez de la pieza— o cuando la pieza toca el canto del encuadre y\n"
           "la decisión automática la confunde con el fondo.\n\n"
           "Solo se aplica separando «por nivel de gris»: por el canto no hay\n"
           "corte que orientar."));
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
    method_->setCurrentIndex(static_cast<int>(options.method));
    polarity_->setCurrentIndex(static_cast<int>(options.polarity));
    blur_->setValue(options.blurKernel);
    morph_->setValue(options.morphKernel);
    if (backgroundKey_ != nullptr) {
        backgroundKey_->setCurrentIndex(static_cast<int>(options.backgroundKey));
        background_ = QColor(options.background[2], options.background[1], options.background[0]);
        paintBackgroundSwatch();
        backgroundColour_->setEnabled(options.backgroundKey ==
                                      vision::SegmentationOptions::BackgroundKey::Fixed);
    }
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

void DetectionPage::reloadFor(vision::SegmentationOptions options,
                              std::int64_t profileId, double minAreaFraction,
                              double maxAreaFraction, bool subpixelEdges) {
    applyOptions(options);
    minArea_->setValue(minAreaFraction * 100.0);
    maxArea_->setValue(maxAreaFraction * 100.0);
    if (subpixel_ != nullptr) {
        subpixel_->setChecked(subpixelEdges);
    }
    if (profileCombo_ == nullptr) {
        return;
    }
    // El perfil, SIN disparar su propia carga: `onProfileChosen` volvería a
    // aplicar los ajustes del perfil encima de los que acaban de ponerse, y con
    // una pieza que tiene perfil pero con los controles tocados a mano eso
    // perdería lo tocado.
    const QSignalBlocker quiet(profileCombo_);
    const int index = profileCombo_->findData(QVariant::fromValue(profileId));
    profileCombo_->setCurrentIndex(index >= 0 ? index : 0);
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

// Lo que la imagen de ahora dice de sí misma.
//
// Solo se enseña cuando hay algo que hacer: si la escena pide el canto y el
// método puesto es el de nivel. Un aviso que sale siempre se aprende a ignorar,
// y uno que sale cuando ya está bien puesto es ruido.
void DetectionPage::setClippingCheck(const vision::ClippingCheck& check) {
    if (clipResult_ == nullptr) {
        return;
    }
    clipResult_->setVisible(true);
    clipResult_->setText(QString::fromStdString(check.summary));
    // Rojo cuando corta, apagado cuando no. Un resultado tranquilizador con el
    // mismo aspecto que uno alarmante enseña a no leer ninguno de los dos.
    clipResult_->setStyleSheet(
        check.thresholdCutsThePiece
            ? QStringLiteral("color:#3a1010; background:#ffd9d9; border:1px solid #c04040;"
                             " border-radius:4px; padding:6px; font-weight:bold;")
            : QStringLiteral("color:#8a8a8a; padding:6px;"));
}

void DetectionPage::setSceneReading(const vision::SceneReading& reading) {
    if (sceneHint_ == nullptr || method_ == nullptr) {
        return;
    }
    const bool usingLevel =
        method_->currentIndex() == static_cast<int>(vision::SegmentationMethod::Level);
    // LA PUERTA ESTABA CERRADA CON LLAVE.
    //
    // Esto miraba `piecesStraddleTheBackground`, que con el fondo claro no puede
    // ser cierto NUNCA: «más claro que el fondo» se cuenta por encima de
    // `fondo + 12`, y con la mesa en 255 ese techo cae en 267. En las ocho
    // imágenes reales del usuario el fondo va de 244 a 255, así que este aviso
    // —y el botón que ofrece el otro método— no aparecían jamás en el montaje
    // industrial normal: pieza sobre mesa blanca.
    //
    // Ahora mira el veredicto entero, que incluye el segundo motivo por el que
    // un corte único falla: que el corte pase por dentro de la pieza.
    const bool worthSaying = reading.aSingleCutCannotDoIt && usingLevel;
    sceneHint_->setVisible(worthSaying);
    useEdgesButton_->setVisible(worthSaying);
    if (!worthSaying) {
        return;
    }
    // Con las CIFRAS dentro. «Prueba el otro método» es una corazonada; «el 27 %
    // de la imagen es más oscuro que la mesa y el 2 % más claro» es un motivo, y
    // el operador puede comprobarlo mirando su propia pieza.
    //
    // Y las cifras que se enseñan son las del motivo que disparó el aviso. Sacar
    // los porcentajes de claro y oscuro cuando lo que falla es el recorte daría
    // un motivo que no es el suyo — y encima uno de los dos números sería el
    // 0,0 % que no se pudo medir.
    if (reading.piecesStraddleTheBackground) {
        sceneHint_->setText(
            tr("En esta imagen, el %1 % es más claro que la mesa y el %2 % más oscuro. "
               "Ningún umbral por nivel puede separar las dos cosas a la vez: el corte "
               "que recoge unas partes deja fuera a otras.")
                .arg(100.0 * reading.brighterThanBackground, 0, 'f', 1)
                .arg(100.0 * reading.darkerThanBackground, 0, 'f', 1));
    } else {
        sceneHint_->setText(
            tr("El corte de gris está pasando por dentro de la pieza: aflojarlo un poco "
               "cambia la silueta un %1 %. Eso es material que se queda fuera, y por eso "
               "una pieza brillante sale partida en trozos o medida corta. Segmentar por "
               "el borde no depende del nivel de gris.")
                .arg(100.0 * reading.thresholdSwing, 0, 'f', 1));
    }
    sceneHint_->setStyleSheet(theme::textStyle(theme::kWarn));
}

// EL BOTÓN ENSEÑA EL COLOR, no solo lo nombra.
//
// Un botón que pusiera «#EE3F4D» obligaría a traducir un hexadecimal a un color
// mentalmente. Se pinta el color de fondo del propio botón y se escribe encima
// su nombre en claro, con la letra en blanco o negro según cuál de las dos se
// lea mejor sobre él — que es la misma cuenta de contraste de `ui/theme.h`.
void DetectionPage::paintBackgroundSwatch() {
    if (backgroundColour_ == nullptr) {
        return;
    }
    const double luminance = 0.2126 * background_.redF() + 0.7152 * background_.greenF() +
                             0.0722 * background_.blueF();
    const char* ink = luminance > 0.45 ? theme::kInk : theme::kInkOnDark;
    backgroundColour_->setText(tr("Color del fondo: %1").arg(background_.name().toUpper()));
    backgroundColour_->setStyleSheet(QStringLiteral("background:%1; color:%2; padding:6px;")
                                         .arg(background_.name(), QString::fromUtf8(ink)));
}

vision::SegmentationOptions DetectionPage::options() const {
    vision::SegmentationOptions result;
    result.method = static_cast<vision::SegmentationMethod>(method_->currentIndex());
    result.manualThreshold = autoThreshold_->isChecked() ? -1 : threshold_->value();
    result.polarity = static_cast<vision::SegmentationPolarity>(polarity_->currentIndex());
    result.blurKernel = blur_->value();
    result.morphKernel = morph_->value();
    result.splitTouchingPieces = splitTouching_ != nullptr && splitTouching_->isChecked();
    // Doce niveles: es lo medido como seguro. Con treinta, la bandeja de cien
    // tuercas se funde en 64 y los tres tornillos en uno.
    result.recoverHighlightsBy =
        (recoverGlare_ != nullptr && recoverGlare_->isChecked()) ? 12 : 0;
    if (backgroundKey_ != nullptr) {
        result.backgroundKey = static_cast<vision::SegmentationOptions::BackgroundKey>(
            backgroundKey_->currentIndex());
        // BGR, que es como entra todo por OpenCV. Escribirlo al revés daría un
        // fondo azul donde el operador eligió rojo, y con la clave encendida eso
        // no es un detalle de color: es segmentar contra otra cosa.
        result.background = cv::Vec3b(static_cast<unsigned char>(background_.blue()),
                                      static_cast<unsigned char>(background_.green()),
                                      static_cast<unsigned char>(background_.red()));
    }
    return result;
}

bool DetectionPage::subpixelEdges() const {
    return subpixel_ != nullptr && subpixel_->isChecked();
}

}  // namespace pci::ui