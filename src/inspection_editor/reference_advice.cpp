#include "inspection_editor/reference_advice.h"

#include <algorithm>

namespace pci::inspection {
namespace {

// Si un elemento derivado sirve para lo que se pide.
bool fits(const DerivedElement& element, OperandKind kind) {
    switch (kind) {
        case OperandKind::Point: return element.hasPoint();
        case OperandKind::Line: return element.hasLine();
        case OperandKind::Circle: return element.kind == DerivedKind::Circle;
        case OperandKind::PointOrLine: return element.hasPoint() || element.hasLine();
        case OperandKind::Unused: return false;
    }
    return false;
}

std::vector<std::string> candidatesFor(OperandKind kind,
                                       const std::vector<ToolRunResult>& measured,
                                       const std::string& itself) {
    std::vector<std::string> names;
    for (const auto& result : measured) {
        // Una herramienta no puede referenciarse a sí misma, y las que no
        // midieron no ofrecen nada: proponer una referencia que este frame no
        // ha producido dejaría la cadena rota igual, pero con la culpa repartida.
        if (result.name == itself || !result.derived.valid()) {
            continue;
        }
        if (fits(result.derived, kind) &&
            std::find(names.begin(), names.end(), result.name) == names.end()) {
            names.push_back(result.name);
        }
    }
    return names;
}

std::string listOf(const std::vector<std::string>& names) {
    std::string text;
    for (const auto& name : names) {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text;
}

}  // namespace

ReferenceAdvice adviseReference(const ToolConfig& config, const ToolGeometry& geometry,
                                const std::vector<ToolRunResult>& measured) {
    ReferenceAdvice advice;
    const auto needs = referenceOperandsOf(geometry);
    if (needs[0] == OperandKind::Unused && needs[1] == OperandKind::Unused) {
        return advice;  // las 27 que no llevan referencia
    }
    advice.needed = true;

    const auto firstNames = candidatesFor(needs[0], measured, config.name);
    const auto secondNames = needs[1] == OperandKind::Unused
                                 ? std::vector<std::string>{}
                                 : candidatesFor(needs[1], measured, config.name);
    advice.candidatesFirst = static_cast<int>(firstNames.size());
    advice.candidatesSecond = static_cast<int>(secondNames.size());

    // UNA candidata se pone sola; varias se preguntan; ninguna se dice.
    if (firstNames.size() == 1) {
        advice.first = firstNames.front();
    }
    if (secondNames.size() == 1) {
        // Con dos operandos del mismo tipo y una sola candidata, la segunda
        // referencia sería la misma herramienta que la primera: un punto medio
        // «entre A y A» es A, que no es lo que nadie quiso dibujar.
        if (secondNames.front() != advice.first) {
            advice.second = secondNames.front();
        }
    }

    // El porqué, escrito para el momento de dibujar. Se nombra QUÉ hace falta
    // —«una recta», «un punto o un círculo»— y no el tipo interno, porque el
    // operador no elige entre `OperandKind`: elige entre las herramientas que
    // tiene en la lista.
    const std::string wanted = operandKindLabel(needs[0]);
    if (firstNames.empty()) {
        advice.why = "«" + config.name + "» mide contra una referencia y no hay ninguna: "
                     "hace falta " + wanted +
                     ". Dibuja primero la herramienta que la dé y vuelve a asignarla en el "
                     "panel.";
        return advice;
    }
    if (firstNames.size() > 1 && advice.first.empty()) {
        advice.why = "«" + config.name + "» mide contra " + wanted + " y hay " +
                     std::to_string(firstNames.size()) + " que valen (" + listOf(firstNames) +
                     "): elígela en el panel. No se elige sola porque medir contra el datum "
                     "equivocado da un número que parece correcto.";
        return advice;
    }
    advice.why = "«" + config.name + "» mide contra " + wanted +
                 " y solo había una: se ha puesto «" + advice.first +
                 "» como referencia. Cámbiala en el panel si no era ésa.";
    if (!advice.second.empty()) {
        advice.why += " Segunda referencia: «" + advice.second + "».";
    } else if (needs[1] != OperandKind::Unused && secondNames.size() > 1) {
        advice.why += " Le falta la segunda (" + std::string(operandKindLabel(needs[1])) +
                      "): hay varias y hay que elegir.";
    } else if (needs[1] != OperandKind::Unused && secondNames.empty()) {
        advice.why += " Le falta la segunda: hace falta " +
                      std::string(operandKindLabel(needs[1])) + ".";
    } else if (needs[1] != OperandKind::Unused) {
        // La única candidata para la segunda es la que ya ocupa la primera. No
        // se pone: un punto medio «entre A y A» es A, y saldría medido sin
        // fallar y sin significar nada.
        advice.why += " Le falta la segunda: la única que valdría es la misma que la "
                      "primera, y una construcción entre una cosa y ella misma no mide "
                      "nada. Dibuja otra.";
    }
    return advice;
}

}  // namespace pci::inspection
