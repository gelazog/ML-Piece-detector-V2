// EL BOTÓN DE BORRAR TODO, SIN PIEZA ABIERTA.
//
// Queja de uso: «la herramienta de borrar todo no detecta nada o no me deja
// usarla, hasta que selecciono una pieza».
//
// El botón se iba en silencio cuando la pieza abierta no tenía herramientas. Y
// como `liveTools_` solo se llena al seleccionar pieza, eso quería decir que
// recién abierto el programa no hacía NADA — ni borrar, ni explicar por qué.
//
// La ironía: la salida «borrar las de todas las piezas» se había añadido
// precisamente para no tener que ir pieza por pieza, y vivía dentro del diálogo
// que ese `return` impedía abrir.

#include <gtest/gtest.h>

#include <cstdio>

#include "ui/delete_scope.h"

using pci::ui::decideDeleteScope;

TEST(DeleteScope, WithNoPieceOpenButWorkSavedItStillOffersToDeleteEverything) {
    // El caso de la queja: acabas de abrir el programa, no has entrado en
    // ninguna pieza, y hay trabajo guardado.
    const auto scope = decideDeleteScope(0, 47);
    std::printf("  [borrar] sin pieza abierta, 47 guardadas -> esta pieza=%d, todas=%d\n",
                scope.offerThisPiece, scope.offerEverywhere);
    EXPECT_FALSE(scope.nothingAnywhere) << "dice que no hay nada y hay 47";
    EXPECT_TRUE(scope.offerEverywhere)
        << "sin pieza abierta no ofrece borrar las guardadas: es la queja exacta, "
           "obliga a entrar en una pieza para poder usar el botón";
    EXPECT_FALSE(scope.offerThisPiece) << "ofrece borrar las de una pieza que no está abierta";
    EXPECT_FALSE(scope.undoCanBringThemBack)
        << "promete Ctrl+Z, y la pila de deshacer guarda las de la pieza abierta: "
           "aquí no hay ninguna que devolver";
}

TEST(DeleteScope, WithNothingAnywhereItSaysSoInsteadOfDoingNothing) {
    const auto scope = decideDeleteScope(0, 0);
    EXPECT_TRUE(scope.nothingAnywhere)
        << "no hay nada en ninguna parte y el botón no lo dice: se lee como roto";
    EXPECT_FALSE(scope.offerThisPiece);
    EXPECT_FALSE(scope.offerEverywhere);
}

TEST(DeleteScope, WithAPieceOpenAndNothingElseOnlyThisPiece) {
    const auto scope = decideDeleteScope(6, 6);
    EXPECT_TRUE(scope.offerThisPiece);
    EXPECT_FALSE(scope.offerEverywhere)
        << "ofrece «borrar las de todas las piezas» cuando todas son las de esta: "
           "dos botones para lo mismo, y uno promete deshacer y el otro no";
    EXPECT_TRUE(scope.undoCanBringThemBack);
}

TEST(DeleteScope, WithAPieceOpenAndMoreElsewhereItOffersBoth) {
    const auto scope = decideDeleteScope(6, 47);
    EXPECT_TRUE(scope.offerThisPiece);
    EXPECT_TRUE(scope.offerEverywhere);
    EXPECT_TRUE(scope.undoCanBringThemBack)
        << "borrar las de esta pieza sí se puede deshacer";
}
