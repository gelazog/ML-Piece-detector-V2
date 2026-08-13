#include "inspection_editor/canvas/tool_palette.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
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

// Primer renglón de la descripción: es el que resume qué mide la herramienta.
// El resto —cómo trazarla, sus avisos— es demasiado para una línea que cambia
// al pasar el ratón, y va al tooltip de la propia línea.
QString firstLine(const QString& text) {
    const int cut = text.indexOf(QLatin1Char('\n'));
    return cut < 0 ? text : text.left(cut);
}

}  // namespace

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
    selectButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    selectButton_->setCheckable(true);
    selectButton_->setChecked(true);
    selectButton_->setFocusPolicy(Qt::NoFocus);
    selectButton_->setToolTip(
        tr("Mover/Elegir — clic para seleccionar; arrastra para mover; arrastra en\n"
           "vacío para un marco de selección múltiple."));
    connect(selectButton_, &QToolButton::clicked, this, [this] { activate(std::nullopt); });
    column->addWidget(selectButton_);

    // Franja de familias: exclusiva, solo iconos.
    auto* strip = new QHBoxLayout();
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(2);
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
    // Alto FIJO de dos renglones. Si creciera y menguara al pasar el ratón por
    // la rejilla, la rejilla botaría bajo el cursor y elegir se volvería un
    // juego de puntería.
    const QFontMetrics metrics(helpText_->font());
    helpText_->setFixedHeight(metrics.lineSpacing() * 2 + 2);

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
    column->addWidget(helpText_);
    column->addStretch(1);

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
    const QString full = description(*shown);
    helpText_->setText(firstLine(full));
    helpText_->setToolTip(full);
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
        // El nombre va en el tooltip Y en la línea de ayuda (P3). Aquí solo el
        // nombre: la descripción entera en un tooltip que salta al pasar es
        // ilegible.
        button->setToolTip(label(type));
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
