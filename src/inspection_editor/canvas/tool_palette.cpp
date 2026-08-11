#include "inspection_editor/canvas/tool_palette.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "inspection_editor/canvas/tool_icons.h"

namespace pci::inspection {

namespace {

QString label(ToolType type) { return QString::fromUtf8(toolTypeLabel(type)); }
QString description(ToolType type) { return QString::fromUtf8(toolTypeDescription(type)); }

}  // namespace

ToolPalette::ToolPalette(Shape shape, QWidget* parent) : QWidget(parent), shape_(shape) {
    if (shape_ == Shape::Compact) {
        buildCompact();
    } else {
        buildAccordion();
    }
    refreshButtons();
}

void ToolPalette::buildCompact() {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    selectButton_ = new QToolButton(this);
    selectButton_->setIcon(moveModeIcon());
    selectButton_->setIconSize(QSize(24, 24));
    selectButton_->setCheckable(true);
    selectButton_->setChecked(true);
    selectButton_->setToolTip(
        tr("Mover/Elegir — clic para seleccionar; arrastra para mover; arrastra en\n"
           "vacío para un marco de selección múltiple."));
    connect(selectButton_, &QToolButton::clicked, this, [this] { activate(std::nullopt); });
    row->addWidget(selectButton_);

    // Un botón por familia con su menú. Con catorce herramientas —y treinta a
    // la vista— una fila de iconos sueltos no cabe, y aunque cupiera no se
    // encuentra nada en ella.
    for (const ToolCategory category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.empty()) {
            continue;  // familia declarada y todavía sin herramientas
        }
        auto* button = new QToolButton(this);
        button->setText(QString::fromUtf8(categoryLabel(category)));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolTip(QString::fromUtf8(categoryDescription(category)));

        auto* menu = new QMenu(button);
        for (const ToolType type : tools) {
            auto* action = menu->addAction(toolIcon(type), label(type));
            action->setToolTip(description(type));
            connect(action, &QAction::triggered, this, [this, type] { activate(type); });
        }
        button->setMenu(menu);
        row->addWidget(button);
        families_.push_back({category, button, -1});
    }
    row->addStretch(0);
}

void ToolPalette::buildAccordion() {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);

    selectButton_ = new QToolButton(this);
    selectButton_->setText(tr("Seleccionar"));
    selectButton_->setIcon(moveModeIcon());
    selectButton_->setIconSize(QSize(22, 22));
    selectButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    selectButton_->setCheckable(true);
    selectButton_->setChecked(true);
    selectButton_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    selectButton_->setToolTip(tr("Clic para seleccionar; arrastra para mover."));
    connect(selectButton_, &QToolButton::clicked, this, [this] { activate(std::nullopt); });
    column->addWidget(selectButton_);

    accordion_ = new QToolBox(this);
    for (const ToolCategory category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.empty()) {
            continue;
        }
        auto* page = new QWidget(accordion_);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(1);
        for (const ToolType type : tools) {
            auto* button = new QToolButton(page);
            button->setText(label(type));
            button->setIcon(toolIcon(type));
            button->setIconSize(QSize(22, 22));
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setCheckable(true);
            button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            button->setToolTip(description(type));
            connect(button, &QToolButton::clicked, this, [this, type] { activate(type); });
            pageLayout->addWidget(button);
            toolButtons_.emplace_back(type, button);
        }
        pageLayout->addStretch(1);
        const int index = accordion_->addItem(page, QString::fromUtf8(categoryLabel(category)));
        accordion_->setItemToolTip(index, QString::fromUtf8(categoryDescription(category)));
        families_.push_back({category, nullptr, index});
    }
    // La sección abierta ES la familia activa: abrirla con el ratón equivale a
    // elegir esa familia con el atajo, y así los dos caminos no se contradicen.
    connect(accordion_, &QToolBox::currentChanged, this, [this](int index) {
        for (const auto& family : families_) {
            if (family.accordionPage == index) {
                currentCategory_ = family.category;
            }
        }
    });
    column->addWidget(accordion_, 1);
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
    if (accordion_ != nullptr) {
        for (const auto& family : families_) {
            if (family.category == category && family.accordionPage >= 0) {
                accordion_->setCurrentIndex(family.accordionPage);
            }
        }
    }
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
    // En la fila compacta, el botón de la familia activa lleva el icono de la
    // herramienta elegida: sin eso, cerrado el menú no hay forma de saber con
    // qué se está dibujando.
    for (const auto& family : families_) {
        if (family.button == nullptr) {
            continue;
        }
        const bool active = current_.has_value() && categoryOf(*current_) == family.category;
        if (active) {
            family.button->setIcon(toolIcon(*current_));
            family.button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            family.button->setText(label(*current_));
        } else {
            family.button->setIcon(QIcon());
            family.button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            family.button->setText(QString::fromUtf8(categoryLabel(family.category)));
        }
    }
}

}  // namespace pci::inspection
