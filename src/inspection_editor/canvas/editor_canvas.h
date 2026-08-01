#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <array>
#include <optional>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "vision/board_frame.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/types.h"

namespace pci::inspection {

// Herramienta en edición: la geometría tipada (en coords de pieza) es la
// fuente de verdad; config.geometryJson se regenera al guardar.
struct EditedTool {
    ToolConfig config;
    ToolGeometry geometry;
    bool deleted = false;
};

// Canvas del editor: pinta la imagen (aspect-fit) y las herramientas encima;
// crea por arrastre, selecciona por clic y mueve arrastrando la selección.
// Funciona en dos modos: imagen fija (diálogo del editor, setScene) o video
// en vivo (ventana principal, setFrame + setLivePiece): en vivo el fixture se
// actualiza con cada análisis y las herramientas siguen a la pieza en tiempo
// real. La herramienta seleccionada muestra manijas por extremo para editarla
// sin volver a dibujarla.
class EditorCanvas : public QWidget {
    Q_OBJECT

public:
    explicit EditorCanvas(QWidget* parent = nullptr);

    void setScene(const QImage& image, const vision::Fixture& fixture);
    void setTools(std::vector<EditedTool>* tools);
    void setCreateType(std::optional<ToolType> type);
    void setResults(const std::vector<ToolRunResult>& results);
    void clearResults();
    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const { return selected_; }
    // Selección múltiple (marco de selección en modo Mover/Elegir).
    [[nodiscard]] std::vector<int> selectedIndices() const { return multiSelected_; }
    // Escala px->mm para las etiquetas de medida (0 = mostrar px).
    void setMmPerPixel(double mmPerPixel);
    void setLengthUnit(LengthUnit unit);
    // En inspección: bloquea dibujar/mover/seleccionar (solo se lee la pieza).
    void setEditingLocked(bool locked);

    // --- zona de detección (ROI) ---
    // Con el modo activo, el siguiente arrastre define el rectángulo donde se
    // buscará el contorno automático; emite regionPicked y se desactiva solo.
    // No requiere pieza detectada (sirve justo cuando la detección falla).
    void setRegionPickMode(bool enabled);
    void setDetectionRegion(bool visible, const cv::Rect& imageRect = {});

    // --- selección de rasgo distintivo ---
    // Con el modo activo, el siguiente clic sobre la pieza emite pointPicked
    // (coords de imagen) y el modo se desactiva solo.
    void setPickMode(bool enabled);
    // Marcador del rasgo (rombo magenta) anclado a la pieza.
    void setAnchorMarker(bool visible, const cv::Point2f& piecePoint = {});

    // --- modo vivo ---
    void setFrame(const QImage& frame);  // solo la imagen; conserva el fixture
    // Actualiza el fixture con el análisis del frame y el overlay de la pieza.
    // Si found es false se conserva el último fixture (las herramientas no
    // saltan) pero se bloquea el dibujo hasta volver a detectar la pieza.
    void setLivePiece(bool found, const QPolygonF& contour, const QPointF& centroid,
                      double angleDeg, const QString& statusText);
    void setLiveContourVisible(bool visible);
    void clearLive();  // fin de la transmisión: "Sin señal"

    [[nodiscard]] QSize sizeHint() const override;

    // --- zoom (Z1/Z3) ---
    // Vuelve al encuadre "ajustar a la ventana" (zoom 1, sin desplazamiento).
    void resetView();
    // Un paso de zoom hacia el centro de la vista (botones + / − y atajos).
    void zoomIn();
    void zoomOut();
    // Topes de la vista: mínimo = imagen ajustada a la ventana, máximo = 20×.
    void zoomToMin();
    void zoomToMax();
    // 100 %: un píxel de la imagen ocupa un píxel de pantalla. Si la imagen es
    // más pequeña que el lienzo, el ajuste ya la agranda y queda en el mínimo.
    void zoomToActualPixels();
    // Zoom actual: 1.0 = imagen ajustada a la ventana.
    [[nodiscard]] double zoomFactor() const { return zoom_; }
    // Escala visible: píxeles de pantalla por píxel de imagen (1.0 = 100 %).
    // 0 si aún no hay imagen. Es lo que se muestra al operador.
    [[nodiscard]] double displayScale() const;
    [[nodiscard]] bool atMinZoom() const;
    [[nodiscard]] bool atMaxZoom() const;

    // --- tablero de referencia centrado (T2) ---
    // Ejes y grilla con el 0 en el origen elegido. Informa, no estorba: líneas
    // finas semitransparentes por debajo de las herramientas.
    void setBoardVisible(bool visible);
    [[nodiscard]] bool boardVisible() const { return boardVisible_; }
    void setBoardConfig(const vision::BoardConfig& config);
    // Centro geométrico del contorno de la pieza (coords de imagen). Es el que
    // usa el centrado automático del tablero; sin él se cae al centro de masa.
    void setPieceBoundsCenter(bool known, const cv::Point2f& center = {});
    [[nodiscard]] const vision::BoardConfig& boardConfig() const { return boardConfig_; }
    // Tablero resuelto para el frame actual (lo reutilizan las lecturas de T3/T4).
    [[nodiscard]] vision::BoardFrame boardFrame() const;

    // --- regla graduada ---
    // Reglas en los bordes superior e izquierdo, con marcas y números en la
    // unidad activa, más una barra de escala. Sirven para leer una medida de un
    // vistazo sin tener que dibujar una herramienta.
    void setRulerVisible(bool visible);
    [[nodiscard]] bool rulerVisible() const { return rulerVisible_; }

signals:
    // El zoom, el desplazamiento o el tamaño del lienzo cambiaron: quien
    // muestre el porcentaje debe releer displayScale().
    void viewChanged();
    void toolCreated(const pci::inspection::ToolGeometry& geometry);
    void selectionChanged(int index);
    void toolModified();
    void pointPicked(const cv::Point2f& imagePoint);
    void regionPicked(const cv::Rect& imageRect);
    void toolRightClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Encuadre base (imagen ajustada a la ventana, sin zoom ni desplazamiento).
    [[nodiscard]] QRectF fitRect() const;
    // Encuadre efectivo = fitRect con el zoom y el desplazamiento aplicados.
    // ÚNICO punto del que dependen imageToWidget/widgetToImage y todo el
    // pintado, así que el zoom se propaga solo al resto del canvas.
    [[nodiscard]] QRectF targetRect() const;
    // Aplica zoom manteniendo quieto el punto de la imagen bajo `widgetPos`.
    void zoomAt(const QPointF& widgetPos, double factor);
    // Limita el desplazamiento para que la imagen nunca deje huecos ni se
    // pierda de vista; si con el zoom actual cabe entera en un eje, la centra.
    [[nodiscard]] QPointF clampedPan(const QPointF& pan) const;
    // Cursor que corresponde al modo actual (cruz al dibujar, flecha si no).
    void restoreCursor();
    [[nodiscard]] QPointF imageToWidget(const cv::Point2f& p) const;
    [[nodiscard]] cv::Point2f widgetToImage(const QPointF& p) const;
    [[nodiscard]] cv::Point2f toImg(const cv::Point2f& piecePoint) const;
    [[nodiscard]] int hitTest(const cv::Point2f& imagePoint) const;
    // Manija (extremo editable) de la herramienta seleccionada bajo el cursor,
    // o -1 si ninguna. Permite arrastrar un punto suelto en vez del conjunto.
    [[nodiscard]] int hitHandle(const cv::Point2f& imagePoint) const;
    // Borde detectado (coords de imagen) cerca del cursor a lo largo del trazo
    // en curso, para "pegar" el extremo de un Caliper/Regla/Borde liso a él.
    [[nodiscard]] std::optional<cv::Point2f> snapEdge(const cv::Point2f& cursor,
                                                      const cv::Point2f& dir) const;

    void paintTool(QPainter& painter, const EditedTool& tool, bool selected) const;
    void paintResults(QPainter& painter) const;
    // ¿Hay un resultado medido para esta herramienta? Si lo hay, su etiqueta ya
    // incluye el nombre y no hay que pintarlo por duplicado.
    [[nodiscard]] bool hasResultFor(const ToolConfig& config) const;
    // Medida formateada según su tipo (conteo, grados o longitud en la unidad
    // activa).
    [[nodiscard]] QString measureText(const ToolRunResult& result) const;
    void paintCreationPreview(QPainter& painter) const;
    void paintLiveOverlay(QPainter& painter) const;
    void paintBoard(QPainter& painter) const;
    void paintRuler(QPainter& painter) const;
    // Valor del tablero (px de imagen) en la unidad activa, compacto.
    [[nodiscard]] QString boardValueText(double px, bool signPrefix) const;
    [[nodiscard]] bool interactive() const;
    [[nodiscard]] bool isSelected(int index) const;
    void finishMarquee(const cv::Point2f& releasePoint);

    QImage image_;
    vision::Fixture fixture_;
    std::vector<EditedTool>* tools_ = nullptr;
    std::vector<ToolRunResult> results_;
    std::optional<ToolType> createType_;
    int selected_ = -1;
    std::vector<int> multiSelected_;
    double mmPerPixel_ = 0.0;
    LengthUnit unit_ = LengthUnit::Auto;
    bool editingLocked_ = false;

    // Vista: zoom sobre el encuadre ajustado y desplazamiento en píxeles de
    // widget (Z1). El desplazamiento lo genera el zoom hacia el cursor; el
    // arrastre manual llega en Z2.
    double zoom_ = 1.0;
    QPointF pan_;
    // Arrastre de la vista (botón central o Ctrl + botón izquierdo).
    bool panning_ = false;
    QPointF panStartWidget_;
    QPointF panStartOffset_;

    // Tablero de referencia (T2): visibilidad y elección de origen/ejes.
    bool boardVisible_ = false;
    bool rulerVisible_ = false;
    vision::BoardConfig boardConfig_;
    bool hasBoundsCenter_ = false;
    cv::Point2f boundsCenter_{0.0F, 0.0F};
    // Cursor sobre el lienzo (T4): solo se sigue con el tablero encendido.
    std::optional<QPointF> cursorWidget_;

    bool creating_ = false;
    bool moving_ = false;
    bool marquee_ = false;
    bool draggingHandle_ = false;  // arrastrando una manija de la selección
    int handleIndex_ = -1;         // índice de la manija en handlePoints()
    cv::Point2f dragStart_;
    cv::Point2f dragCurrent_;
    // Primera línea ya trazada de una Línea-Línea en curso (coords de pieza).
    std::optional<std::array<cv::Point2f, 2>> pendingLineA_;
    // Vértice + primer lado ya fijados de un Ángulo en curso (coords de pieza).
    std::optional<std::array<cv::Point2f, 2>> pendingAngle_;
    // Vértices ya marcados de un Blob poligonal en curso (coords de pieza); se
    // cierra al hacer clic cerca del primero (>= 3 vértices).
    std::vector<cv::Point2f> pendingPolygon_;
    // Snap al borde: imagen en gris del trazo actual y borde resaltado bajo el
    // cursor (coords de imagen) al que se pegará el extremo al soltar.
    cv::Mat dragGray_;
    std::optional<cv::Point2f> snapImg_;

    // Estado del modo vivo.
    bool liveMode_ = false;
    bool hasFixture_ = false;
    bool pieceVisible_ = false;
    bool showLiveContour_ = true;
    QPolygonF liveContour_;
    QPointF liveCentroid_;
    QString liveStatus_;

    // Rasgo distintivo.
    bool pickMode_ = false;
    bool anchorVisible_ = false;
    cv::Point2f anchorPiecePoint_;

    // Zona de detección.
    bool regionPick_ = false;
    bool regionDrag_ = false;
    bool regionVisible_ = false;
    cv::Rect regionRect_;
};

}  // namespace pci::inspection
