#pragma once

#include <QImage>
#include <QWidget>

#include <array>
#include <optional>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
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

signals:
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

private:
    [[nodiscard]] QRectF targetRect() const;
    [[nodiscard]] QPointF imageToWidget(const cv::Point2f& p) const;
    [[nodiscard]] cv::Point2f widgetToImage(const QPointF& p) const;
    [[nodiscard]] cv::Point2f toImg(const cv::Point2f& piecePoint) const;
    [[nodiscard]] int hitTest(const cv::Point2f& imagePoint) const;
    // Manija (extremo editable) de la herramienta seleccionada bajo el cursor,
    // o -1 si ninguna. Permite arrastrar un punto suelto en vez del conjunto.
    [[nodiscard]] int hitHandle(const cv::Point2f& imagePoint) const;

    void paintTool(QPainter& painter, const EditedTool& tool, bool selected) const;
    void paintResults(QPainter& painter) const;
    void paintCreationPreview(QPainter& painter) const;
    void paintLiveOverlay(QPainter& painter) const;
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
