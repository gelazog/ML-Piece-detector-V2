#include "inspection_editor/canvas/tool_palette.h"

#include <QApplication>
#include <QPalette>
#include <QButtonGroup>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "inspection_editor/canvas/tool_icons.h"

namespace pci::inspection {

namespace {

QString label(ToolType type) { return QString::fromUtf8(toolTypeLabel(type)); }
QString description(ToolType type) { return QString::fromUtf8(toolTypeDescription(type)); }

// Un solo sitio para los márgenes del panel. Con números sueltos por el fichero
// basta con tocar uno para que la rejilla deje de cuadrar.
constexpr int kPanelMargin = 4;
constexpr int kPanelSpacing = 4;
constexpr int kToolIconSize = 28;
// Lado del botón de herramienta. No baja de 34: por debajo deja de ser cómodo
// de acertar con el ratón, y esto se usa todo el día.
constexpr int kToolButtonSide = 36;
constexpr int kFamilyIconSize = 24;
// La franja va más apretada que la rejilla a propósito: son cinco y se leen
// como un grupo, no como cinco cosas sueltas.
constexpr int kStripSpacing = 2;

// La herramienta activa tiene que verse SIN pasar el ratón por encima.
//
// Con `autoRaise` el estado marcado se dibuja como un relieve tenue que en un
// monitor de taller —con reflejos y a metro y medio— no se distingue de un
// botón cualquiera. Y saber con qué se está dibujando no es un detalle
// estético: es la diferencia entre trazar la herramienta que querías y otra.
//
// El color sale del tema (`QPalette::Highlight`), no de un valor escrito aquí:
// así sigue siendo el color de «esto está seleccionado» que el operador ya
// reconoce del resto del sistema, en claro y en oscuro.
QString checkedStyle() {
    const QColor highlight = QApplication::palette().color(QPalette::Highlight);
    return QStringLiteral("QToolButton:checked { background: %1; border: 1px solid %2; "
                          "border-radius: 3px; }")
        .arg(highlight.name(), highlight.darker(140).name());
}

// Alto MÍNIMO de la ayuda, en renglones. Ya no es un tope —el texto se enseña
// entero y se desplaza— sino el suelo por debajo del cual el bloque dejaría de
// leerse: con el panel muy bajo, la ayuda no puede reducirse a una rendija.
//
// Tres y no dos, y sale de medir: al ancho real del panel (232 px), el RESUMEN
// de 21 de las 32 herramientas ya ocupa dos renglones por sí solo.
constexpr int kHelpLines = 3;

}  // namespace

void ToolPalette::setDeletable(int selected, int total) {
    if (deleteButton_ == nullptr || deleteAllButton_ == nullptr) {
        return;
    }
    deletableTotal_ = total;
    // Deshabilitado CON MOTIVO: un botón vivo que no hace nada enseña a
    // desconfiar de los botones, y uno apagado sin explicación deja pensando qué
    // falta. El tooltip dice las dos cosas — qué hace y qué hace falta.
    deleteButton_->setEnabled(selected > 0);
    deleteButton_->setToolTip(
        selected > 0
            ? tr("Borrar la herramienta seleccionada (Supr). Se puede deshacer con Ctrl+Z.")
            : tr("Borrar la herramienta seleccionada.\n\nElige una primero con Mover/Elegir."));

    deleteAllButton_->setEnabled(total > 0);
    deleteAllButton_->setToolTip(
        total > 0 ? tr("Borrar las %n herramienta(s) de la pieza. Pregunta antes, y se puede "
                       "deshacer con Ctrl+Z.",
                       nullptr, total)
                  : tr("Borrar todas las herramientas.\n\nNo hay ninguna dibujada."));
}

ToolPalette::ToolPalette(QWidget* parent) : QWidget(parent) {
    buildPanel();
    refreshButtons();
}

void ToolPalette::buildPanel() {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, kPanelMargin);
    column->setSpacing(kPanelSpacing);

    // Mover/Elegir arriba del todo y CON TEXTO, fuera de la franja: no es una
    // familia, y ponerlo entre ellas invitaría a leerlo como una más.
    selectButton_ = new QToolButton(this);
    selectButton_->setIcon(moveModeIcon());
    selectButton_->setIconSize(QSize(kFamilyIconSize, kFamilyIconSize));
    selectButton_->setText(tr("Mover/Elegir"));
    // Alcanzable con el tabulador. Es la forma de SALIR del modo de dibujo, así
    // que dejarla fuera del recorrido del teclado deja a quien navega así
    // atrapado dibujando — y un recorrido sin salida es lo que las guías llaman
    // una trampa de foco.
    selectButton_->setFocusPolicy(Qt::StrongFocus);
    selectButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    selectButton_->setCheckable(true);
    selectButton_->setChecked(true);
    selectButton_->setStyleSheet(checkedStyle());
    selectButton_->setToolTip(
        tr("Mover/Elegir — clic para seleccionar; arrastra para mover; arrastra en\n"
           "vacío para un marco de selección múltiple."));
    connect(selectButton_, &QToolButton::clicked, this, [this] { activate(std::nullopt); });

    // Borrar va JUNTO A MOVER/ELEGIR, y no suelto debajo del panel, porque es la
    // continuación natural del mismo gesto: se elige una herramienta con
    // Mover/Elegir y lo siguiente que se hace con ella es moverla o quitarla.
    // Tenerlo a un palmo del sitio donde se selecciona ahorra el viaje de ida y
    // vuelta que había hasta el borde del panel.
    auto* selectRow = new QHBoxLayout();
    selectRow->setContentsMargins(0, 0, 0, 0);
    selectRow->setSpacing(kStripSpacing);
    selectRow->addWidget(selectButton_, 1);

    deleteButton_ = new QToolButton(this);
    // Nombre estable: lo usan los tests para señalarlos sin confundirlos con los
    // iconos de familia, que también son QToolButton sin texto.
    deleteButton_->setObjectName(QStringLiteral("deleteTool"));
    deleteButton_->setIcon(deleteIcon());
    deleteButton_->setIconSize(QSize(kFamilyIconSize, kFamilyIconSize));
    deleteButton_->setAutoRaise(true);
    deleteButton_->setFocusPolicy(Qt::NoFocus);
    deleteButton_->setEnabled(false);
    connect(deleteButton_, &QToolButton::clicked, this, &ToolPalette::deleteRequested);
    selectRow->addWidget(deleteButton_);

    deleteAllButton_ = new QToolButton(this);
    deleteAllButton_->setObjectName(QStringLiteral("deleteAllTools"));
    deleteAllButton_->setIcon(deleteAllIcon());
    deleteAllButton_->setIconSize(QSize(kFamilyIconSize, kFamilyIconSize));
    deleteAllButton_->setAutoRaise(true);
    deleteAllButton_->setFocusPolicy(Qt::NoFocus);
    deleteAllButton_->setEnabled(false);
    connect(deleteAllButton_, &QToolButton::clicked, this, &ToolPalette::deleteAllRequested);
    selectRow->addWidget(deleteAllButton_);

    column->addLayout(selectRow);
    setDeletable(0, 0);

    // Franja de familias: exclusiva, solo iconos.
    auto* strip = new QHBoxLayout();
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(kStripSpacing);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (const ToolCategory category : allToolCategories()) {
        if (toolsInCategory(category).empty()) {
            continue;  // familia declarada y todavía sin herramientas
        }
        auto* button = new QToolButton(this);
        button->setIcon(categoryIcon(category));
        button->setIconSize(QSize(kFamilyIconSize, kFamilyIconSize));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(QString::fromUtf8(categoryDescription(category)));
        button->setStyleSheet(checkedStyle());
        group->addButton(button);
        strip->addWidget(button);
        // Pulsar una familia LA ABRE; no elige herramienta. Es un gesto distinto
        // del atajo —que sí elige la primera, porque quien pulsa un atajo quiere
        // dibujar ya— y confundirlos haría que abrir un cajón para mirar
        // cambiara con qué estás dibujando.
        connect(button, &QToolButton::clicked, this, [this, category] {
            currentCategory_ = category;
            rebuildGrid();
            refreshButtons();
        });
        families_.push_back({category, button});
    }
    strip->addStretch(1);
    column->addLayout(strip);

    familyTitle_ = new QLabel(this);
    QFont titleFont = familyTitle_->font();
    titleFont.setBold(true);
    familyTitle_->setFont(titleFont);
    column->addWidget(familyTitle_);

    // --- Línea de ayuda (P3) ---
    //
    // Es lo que sustituye al texto que la rejilla le quita a los botones. Sin
    // esto, el panel sería más bonito y peor: iconos sin nombre obligan a
    // adivinar o a esperar el tooltip.
    auto* helpRow = new QHBoxLayout();
    helpRow->setContentsMargins(0, 0, 0, 0);
    helpName_ = new QLabel(this);
    QFont nameFont = helpName_->font();
    nameFont.setBold(true);
    helpName_->setFont(nameFont);
    helpRow->addWidget(helpName_, 1);
    helpShortcut_ = new QLabel(this);
    helpShortcut_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    helpRow->addWidget(helpShortcut_);

    helpText_ = new QLabel(this);
    helpText_->setWordWrap(true);
    helpText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Se puede seleccionar con el ratón: una cota o un aviso que hay que copiar
    // a un parte no se copia de un tooltip.
    helpText_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // El texto entero, dentro de un área que se desplaza.
    //
    // La regla que había —alto FIJO— nació de un problema real: si el bloque
    // creciera y menguara al pasar el ratón por la rejilla, la rejilla botaría
    // bajo el cursor y elegir sería un juego de puntería. Esa regla se conserva,
    // y lo que cambia es cómo se cumple: el ALTO lo fija el sitio que sobra en
    // el panel, no el largo del texto, así que la rejilla sigue sin moverse y
    // además cabe la descripción entera.
    //
    // De paso se recupera el hueco que antes se iba en un `addStretch`: era
    // espacio vacío debajo de una ayuda truncada.
    helpScroll_ = new QScrollArea(this);
    helpScroll_->setWidget(helpText_);
    helpScroll_->setWidgetResizable(true);
    helpScroll_->setFrameShape(QFrame::NoFrame);
    helpScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QFontMetrics metrics(helpText_->font());
    helpScroll_->setMinimumHeight(metrics.lineSpacing() * kHelpLines + 2);

    gridHost_ = new QWidget(this);
    // La rejilla NO impone su ancho, y esto es lo que hace que el reflujo
    // funcione de verdad.
    //
    // Sin esto hay una pescadilla que se muerde la cola: el mínimo de un
    // `QGridLayout` es el de sus columnas, así que con las ocho herramientas de
    // una familia en una fila el panel pedía 324 px y Qt no le dejaba
    // estrecharse por debajo; y como no se estrechaba, el reflujo a cuatro
    // columnas no llegaba a ocurrir nunca. Medido: `resize(180)` devolvía un
    // ancho real de 324.
    //
    // Con `Ignored` el contenedor acepta el ancho que le den y la rejilla se
    // recoloca dentro. El mínimo del panel pasa a ser el de la franja de
    // familias, que es lo que de verdad no se puede encoger.
    gridHost_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    gridHost_->setMinimumWidth(kToolButtonSide);
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(kPanelSpacing);
    column->addWidget(gridHost_);
    column->addLayout(helpRow);
    column->addWidget(helpScroll_, 1);

    rebuildGrid();
    updateHelpLine();
}

QString ToolPalette::shortcutHint(ToolType type) {
    const ToolCategory category = categoryOf(type);
    int familyNumber = 0;
    for (const auto candidate : allToolCategories()) {
        if (toolsInCategory(candidate).empty()) {
            continue;
        }
        ++familyNumber;
        if (candidate == category) {
            break;
        }
    }
    const auto tools = toolsInCategory(category);
    int position = 0;
    for (std::size_t i = 0; i < tools.size(); ++i) {
        if (tools[i] == type) {
            position = static_cast<int>(i) + 1;
        }
    }
    if (familyNumber <= 0 || position <= 0 || position > 9) {
        return {};  // más allá del noveno no hay dígito que ofrecer
    }
    return tr("Ctrl+%1, luego %2").arg(familyNumber).arg(position);
}

void ToolPalette::updateHelpLine() {
    if (helpName_ == nullptr) {
        return;
    }
    // Lo señalado manda sobre lo seleccionado: el operador está mirando eso.
    const std::optional<ToolType> shown = hovered_.has_value() ? hovered_ : current_;
    if (!shown.has_value()) {
        // Ni ratón encima ni herramienta elegida: es el primer momento, y
        // dejarlo en blanco sería desperdiciar el único sitio donde se puede
        // decir por dónde se empieza.
        helpName_->setText(tr("Elige una familia arriba"));
        helpShortcut_->clear();
        helpText_->setText(tr("Cada familia enseña sus herramientas debajo. Pasa el "
                              "ratón por encima para saber qué mide cada una."));
        helpText_->setToolTip(QString());
        return;
    }
    helpName_->setText(label(*shown));
    helpShortcut_->setText(shortcutHint(*shown));
    // El texto SALE de `toolTypeDescription`, no es una copia: escrito dos
    // veces acabaría divergiendo, que es la razón por la que la paleta se
    // compartió en su día.
    // ENTERO, con sus saltos de línea. Antes se recortaba a tres renglones con
    // unos puntos suspensivos y el resto vivía solo en el tooltip: medido, 29 de
    // las 32 descripciones no cabían, y la más larga tiene 901 caracteres. Es
    // decir, casi toda la ayuda de la aplicación solo existía al pasar el ratón
    // — y lo que solo se ve con el ratón encima no lo ve quien navega con el
    // teclado.
    //
    // Los saltos se conservan porque separan «qué mide» de «cómo se traza», que
    // es exactamente la división que hace falta mientras eliges.
    const QString full = description(*shown);
    helpText_->setText(full);
    // El tooltip deja de hacer falta y se quita: repetir en un globo lo que ya
    // está escrito debajo solo tapa el texto que se está leyendo.
    helpText_->setToolTip(QString());
    fitHelpToWidth();
    if (helpScroll_ != nullptr && helpScroll_->verticalScrollBar() != nullptr) {
        // Cada herramienta empieza por su principio. Heredar el desplazamiento
        // de la anterior deja al operador leyendo por la mitad sin saberlo.
        helpScroll_->verticalScrollBar()->setValue(0);
    }
}

bool ToolPalette::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        const bool entering = event->type() == QEvent::Enter;
        for (const auto& [type, button] : toolButtons_) {
            if (button != watched) {
                continue;
            }
            hovered_ = entering ? std::optional<ToolType>(type) : std::nullopt;
            updateHelpLine();
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ToolPalette::rebuildGrid() {
    if (grid_ == nullptr) {
        return;
    }
    // Fuera los botones de la familia anterior. `deleteLater` y no `delete` por
    // si el que se va es justo el que emitió el clic que trajo aquí.
    for (auto& [type, button] : toolButtons_) {
        button->hide();
        // Se desvincula ANTES de programar el borrado. `deleteLater` no borra
        // hasta que corra el bucle de eventos, y mientras tanto el botón sigue
        // siendo hijo del panel: `findChildren` lo devolvía y se podía acabar
        // hablando con un botón de la familia anterior que ya no está en
        // pantalla.
        button->setParent(nullptr);
        button->deleteLater();
    }
    toolButtons_.clear();

    for (const ToolType type : toolsInCategory(currentCategory_)) {
        auto* button = new QToolButton(gridHost_);
        button->setIcon(toolIcon(type));
        button->setIconSize(QSize(kToolIconSize, kToolIconSize));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(kToolButtonSide, kToolButtonSide);
        button->setStyleSheet(checkedStyle());
        // El nombre Y la descripción entera.
        //
        // Antes aquí iba solo el nombre, con el razonamiento de que un tooltip
        // largo que salta al pasar es ilegible. El razonamiento era bueno y la
        // consecuencia mala: el nombre ya está en la línea de ayuda, así que
        // este tooltip no aportaba nada, y la descripción completa quedaba solo
        // en el tooltip de la línea de ayuda — un sitio donde nadie va a
        // señalar. O sea que «cómo se traza esta herramienta» era inalcanzable.
        //
        // La línea de ayuda sigue dando el resumen al instante, que es para lo
        // que sirve mientras eliges; el tooltip es para cuando te paras a leer.
        // Son dos momentos distintos y ahora cada uno tiene su sitio.
        button->setToolTip(label(type) + QStringLiteral("\n\n") + description(type));
        button->installEventFilter(this);
        connect(button, &QToolButton::clicked, this, [this, type] { activate(type); });
        toolButtons_.emplace_back(type, button);
    }
    gridColumns_ = 0;  // fuerza recolocar
    relayoutGrid();
    if (familyTitle_ != nullptr) {
        familyTitle_->setText(QString::fromUtf8(categoryLabel(currentCategory_)));
    }
    // Los botones señalados ya no existen: quedarse con el anterior enseñaría
    // el nombre de algo que no está en pantalla.
    hovered_.reset();
    updateHelpLine();
}

int ToolPalette::gridColumnsFor(int width) {
    const int step = kToolButtonSide + kPanelSpacing;
    const int usable = std::max(kToolButtonSide, width - 2 * kPanelMargin);
    return std::max(1, (usable + kPanelSpacing) / step);
}

int ToolPalette::gridHeightFor(int toolCount, int width) {
    if (toolCount <= 0) {
        return 0;
    }
    const int columns = gridColumnsFor(width);
    const int rows = (toolCount + columns - 1) / columns;
    return rows * kToolButtonSide + (rows - 1) * kPanelSpacing;
}

void ToolPalette::relayoutGrid() {
    if (grid_ == nullptr || toolButtons_.empty()) {
        return;
    }
    const int columns = gridColumnsFor(width());
    if (columns == gridColumns_) {
        return;  // nada que mover: recolocar en cada píxel de arrastre parpadea
    }
    gridColumns_ = columns;

    // El layout se REHACE, no se recoloca. `QGridLayout` no encoge nunca su
    // número de columnas: al pasar de ocho a cuatro, las cuatro vacías siguen
    // contando y el panel sigue pidiendo el ancho de ocho. Se veía como una
    // barra de desplazamiento horizontal que no se iba al estrechar.
    delete grid_;
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(kPanelSpacing);

    for (int i = 0; i < static_cast<int>(toolButtons_.size()); ++i) {
        grid_->addWidget(toolButtons_[static_cast<std::size_t>(i)].second, i / columns,
                         i % columns, Qt::AlignLeft | Qt::AlignTop);
    }
    // La última fila incompleta se alinea a la izquierda en vez de repartirse:
    // una rejilla con el paso cambiando de fila a fila se lee peor que una con
    // un hueco al final.
    grid_->setColumnStretch(columns, 1);
}

void ToolPalette::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayoutGrid();
    fitHelpToWidth();
}

// El alto que necesita la ayuda con el ancho que tiene AHORA.
//
// Hace falta por una trampa de Qt que costó un test: una etiqueta con ajuste de
// línea dentro de un `QScrollArea` redimensionable **no crece sola**. El área
// le da el alto del visor y `heightForWidth` no se consulta, así que el texto
// se recortaba exactamente igual que antes — solo que ahora sin puntos
// suspensivos, que es peor: un corte mudo.
//
// El primer test que escribí no lo veía porque comprobaba `text()`, y el texto
// SÍ estaba completo; lo que no estaba era visible. Lo destapó el que mira si
// hay algo que desplazar.
void ToolPalette::fitHelpToWidth() {
    if (helpText_ == nullptr || helpScroll_ == nullptr) {
        return;
    }
    const int usable = helpScroll_->viewport()->width();
    if (usable <= 0) {
        return;
    }
    helpText_->setMinimumHeight(helpText_->heightForWidth(usable));
}

void ToolPalette::showSelection(std::optional<ToolType> type) {
    current_ = type;
    if (type.has_value()) {
        currentCategory_ = categoryOf(*type);
    }
    refreshButtons();
}

void ToolPalette::activate(std::optional<ToolType> type) {
    showSelection(type);
    emit toolChosen(type);
}

void ToolPalette::activateCategory(ToolCategory category) {
    const auto tools = toolsInCategory(category);
    currentCategory_ = category;
    // El atajo tiene que dejar la vista contando lo mismo que el atajo hizo. Si
    // eligiera una herramienta sin abrir su familia, el operador vería una
    // rejilla que no contiene lo que está dibujando y dejaría de fiarse de las
    // dos cosas.
    rebuildGrid();
    // Elegir familia elige también su primera herramienta: quien pulsa el
    // atajo quiere empezar a dibujar, no abrir un cajón.
    if (!tools.empty()) {
        activate(tools.front());
    }
}

bool ToolPalette::activateInCurrentCategory(int index) {
    const auto tools = toolsInCategory(currentCategory_);
    if (index < 0 || index >= static_cast<int>(tools.size())) {
        return false;
    }
    activate(tools[static_cast<std::size_t>(index)]);
    return true;
}

void ToolPalette::refreshButtons() {
    if (selectButton_ != nullptr) {
        selectButton_->setChecked(!current_.has_value());
    }
    for (const auto& [type, button] : toolButtons_) {
        button->setChecked(current_.has_value() && *current_ == type);
    }
    for (const auto& family : families_) {
        if (family.button != nullptr) {
            family.button->setChecked(family.category == currentCategory_);
        }
    }
    updateHelpLine();
}

}  // namespace pci::inspection
