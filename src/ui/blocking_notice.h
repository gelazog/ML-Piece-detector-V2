#pragma once

#include <QFrame>
#include <QString>

#include <map>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace pci::ui {

// LO QUE IMPIDE MEDIR, EN UNA BANDA QUE NO SE VA SOLA.
//
// Una cámara que se cae decía «Error: Cámara desconectada o sin señal» en la
// barra de estado, y el siguiente mensaje cualquiera —«Hoy: 12 inspecciones»,
// el disparo por paso de pieza explicando por qué no dispara— lo borraba a los
// pocos segundos. El operador que volvía de colocar una pieza encontraba la
// imagen quieta y ninguna explicación. La base de datos caída solo se veía como
// «BD ✕» en la esquina, en letra de barra de estado.
//
// Esta banda es para eso y SOLO para eso: problemas que bloquean medir. Se
// queda mientras dure el problema, dice qué pasó y qué hacer en una frase, y
// lleva botón cuando hay algo que pulsar. Se quita cuando el problema se
// resuelve —llega un fotograma, se vuelve a ver el marcador—, nunca por tiempo.
// Un aviso informativo aquí enseñaría a no mirarla.
enum class Blocker {
    Source,    // la cámara o el fichero no dan imagen
    Marker,    // escala por ArUco encendida y el marcador no se ve
    Database,  // no hay base de datos: no se puede inspeccionar ni guardar
};

class BlockingNotice : public QFrame {
    Q_OBJECT

public:
    explicit BlockingNotice(QWidget* parent = nullptr);

    // Pone (o cambia) un problema. `action` vacío = sin botón. Con el mismo
    // texto y la misma acción no toca nada: hay problemas que se confirman en
    // cada fotograma.
    void report(Blocker blocker, const QString& text, const QString& action = {});
    // El problema se ha resuelto. Si no quedan otros, la banda desaparece.
    void resolve(Blocker blocker);

    [[nodiscard]] bool isReported(Blocker blocker) const;
    [[nodiscard]] QString text(Blocker blocker) const;

signals:
    // Se pulsó el botón de ese problema. La banda no sabe arreglar nada: avisa
    // y deja que la ventana, que tiene la cámara, lo intente.
    void actionRequested(pci::ui::Blocker blocker);

private:
    struct Row {
        QWidget* row = nullptr;
        QLabel* label = nullptr;
        QPushButton* button = nullptr;
        QString text;
        QString action;
    };
    QVBoxLayout* rows_ = nullptr;
    // Ordenado por el enum: la fuente primero, porque sin imagen nada de lo
    // demás importa.
    std::map<Blocker, Row> reported_;
};

// CUÁNDO UN MARCADOR «NO SE VE».
//
// El marcador ArUco se pierde fotogramas sueltos —un reflejo, la mano al dejar
// la pieza, desenfoque de movimiento— y vuelve al siguiente. Sacar la banda
// roja en el primer fallo la haría parpadear, y una alarma que parpadea por
// nada se aprende a ignorar. Por eso hace falta una racha de fallos seguidos
// para dar el problema por cierto, y basta UN fotograma bueno para quitarlo:
// equivocarse quitándolo cuesta poco, equivocarse poniéndolo cuesta la
// confianza en la banda.
class MissStreak {
public:
    explicit MissStreak(int missesToReport) : missesToReport_(missesToReport) {}
    // Un fotograma más. Devuelve si el problema está en pie después de él.
    bool observe(bool seen) {
        misses_ = seen ? 0 : misses_ + 1;
        return misses_ >= missesToReport_;
    }
    void reset() { misses_ = 0; }

private:
    int missesToReport_;
    int misses_ = 0;
};

}  // namespace pci::ui
