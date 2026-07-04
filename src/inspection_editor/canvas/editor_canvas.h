#pragma once

#include <QImage>
#include <QWidget>

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

// Canvas del editor: pinta la imagen de referencia (aspect-fit) y las
// herramientas encima; crea por arrastre, selecciona por clic y mueve
// arrastrando la selección. Alcance demo: sin handles de redimensionado.
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

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void toolCreated(const pci::inspection::ToolGeometry& geometry);
    void selectionChanged(int index);
    void toolModified();

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

    void paintTool(QPainter& painter, const EditedTool& tool, bool selected) const;
    void paintResults(QPainter& painter) const;
    void paintCreationPreview(QPainter& painter) const;

    QImage image_;
    vision::Fixture fixture_;
    std::vector<EditedTool>* tools_ = nullptr;
    std::vector<ToolRunResult> results_;
    std::optional<ToolType> createType_;
    int selected_ = -1;

    bool creating_ = false;
    bool moving_ = false;
    cv::Point2f dragStart_;
    cv::Point2f dragCurrent_;
};

}  // namespace pci::inspection
