#pragma once

class QDialogButtonBox;

namespace pci::ui {

// LOS BOTONES DE LOS DIÁLOGOS, EN ESPAÑOL.
//
// `QDialogButtonBox` rotula sus botones con el texto que traiga la traducción
// de Qt para el idioma del sistema. Aquí no hay ninguna instalada —el paquete
// de Qt de MSYS2 viene sin `share/qt6/translations`— así que salían «OK»,
// «Close» y «Apply» en una aplicación que está entera en español.
//
// Se ve en seis diálogos, y es de esas cosas que no rompen nada pero hacen que
// el programa parezca a medio terminar: el operador lee «Close» debajo de
// «Umbral de detección». Es literalmente lo que el usuario describió como
// «tosco».
//
// Se ponen los textos a mano en vez de empaquetar los .qm de Qt porque esto es
// una aplicación offline que se despliega copiando: una dependencia más de
// ficheros de idioma es una cosa más que puede faltar en la PC de la línea, y
// fallaría en silencio volviendo al inglés.
//
// Además se les pone a cada uno qué hace. «Aplicar» y «Aceptar» juntos son la
// duda clásica de cualquier ventana de ajustes: cuál de los dos guarda.
void nameButtonsInSpanish(QDialogButtonBox* buttons);

}  // namespace pci::ui
