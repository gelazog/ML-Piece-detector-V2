#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace pci::inspection {

// Pila de deshacer/rehacer por instantáneas: push() guarda el estado ANTERIOR
// a cada mutación y limpia el camino de rehacer; undo()/redo() intercambian el
// estado actual por el guardado. Con listas pequeñas de herramientas, copiar
// la instantánea completa es barato y elimina toda la contabilidad de deltas.
template <typename State>
class UndoStack {
public:
    explicit UndoStack(std::size_t limit = 50) : limit_(limit) {}

    void push(const State& snapshot) {
        undo_.push_back(snapshot);
        if (undo_.size() > limit_) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
    }

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

    std::optional<State> undo(const State& current) {
        if (undo_.empty()) {
            return std::nullopt;
        }
        redo_.push_back(current);
        State state = std::move(undo_.back());
        undo_.pop_back();
        return state;
    }

    std::optional<State> redo(const State& current) {
        if (redo_.empty()) {
            return std::nullopt;
        }
        undo_.push_back(current);
        State state = std::move(redo_.back());
        redo_.pop_back();
        return state;
    }

    void clear() {
        undo_.clear();
        redo_.clear();
    }

private:
    std::vector<State> undo_;
    std::vector<State> redo_;
    std::size_t limit_;
};

}  // namespace pci::inspection
