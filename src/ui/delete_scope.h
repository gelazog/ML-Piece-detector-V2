#pragma once

namespace pci::ui {

// QUÉ PUEDE OFRECER EL BOTÓN DE «BORRAR TODAS LAS HERRAMIENTAS».
//
// Sale de una queja de uso: «la herramienta de borrar todo no detecta nada o no
// me deja usarla, hasta que selecciono una pieza». Y era exacto, con una ironía
// dentro: el botón se iba EN SILENCIO cuando la pieza abierta no tenía
// herramientas, y la salida «borrar las de todas las piezas» —añadida justo para
// no tener que ir pieza por pieza— vivía dentro de ese diálogo que no se abría.
//
// La decisión se saca aquí, fuera de la ventana, porque es la parte que se puede
// equivocar y la única que se puede comprobar sin abrir un diálogo modal.
struct DeleteScope {
    // No hay nada que borrar en ninguna parte. El botón tiene que DECIRLO: uno
    // que no hace nada y no explica por qué se lee como un botón roto.
    bool nothingAnywhere = false;
    // Ofrecer «borrar las de esta pieza». Solo si la pieza abierta tiene algo.
    bool offerThisPiece = false;
    // Ofrecer «borrar las de todas las piezas». Basta con que el programa
    // guarde más de las que hay aquí — con la pieza sin abrir, eso es cierto en
    // cuanto exista una sola, que es justo el caso que antes se perdía.
    bool offerEverywhere = false;
    // Prometer Ctrl+Z. La pila de deshacer guarda las herramientas de la pieza
    // ABIERTA: sin pieza abierta no hay vuelta atrás que prometer, y prometerla
    // sería peor que avisar de que no la hay.
    bool undoCanBringThemBack = false;
};

[[nodiscard]] constexpr DeleteScope decideDeleteScope(int toolsHere, int toolsEverywhere) {
    DeleteScope scope;
    scope.nothingAnywhere = toolsHere == 0 && toolsEverywhere == 0;
    scope.offerThisPiece = toolsHere > 0;
    scope.offerEverywhere = toolsEverywhere > toolsHere;
    scope.undoCanBringThemBack = toolsHere > 0;
    return scope;
}

}  // namespace pci::ui
