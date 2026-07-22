#include "inspection_editor/canvas/editor_canvas.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "inspection_editor/execution/edge_detection.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

// Convierte la imagen visible a un cv::Mat gris (copia propia) para poder
// detectar bordes bajo el cursor sin depender del origen del frame.
cv::Mat qimageToGray(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    return cv::Mat(gray.height(), gray.width(), CV_8UC1,
                   const_cast<uchar*>(gray.bits()), gray.bytesPerLine())
        .clone();
}

QColor toolColor(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return {0, 200, 255};
        case ToolType::Circle: return {255, 170, 0};
        case ToolType::PointToLine: return {180, 120, 255};
        case ToolType::EdgeFlaw: return {0, 230, 120};
        case ToolType::Blob: return {255, 105, 180};
        case ToolType::Ruler: return {255, 255, 120};
        case ToolType::LineToLine: return {120, 220, 220};
        case ToolType::Angle: return {255, 170, 60};
        case ToolType::PolyBlob: return {200, 120, 255};
    }
    return Qt::white;
}

double distanceToSegment(const cv::Point2f& p, const cv::Point2f& a, const cv::Point2f& b) {
    const cv::Point2f ab = b - a;
    const double len2 = static_cast<double>(ab.x) * ab.x + static_cast<double>(ab.y) * ab.y;
    if (len2 < 1e-9) {
        return cv::norm(p - a);
    }
    const double t = std::clamp(
        (static_cast<double>(p.x - a.x) * ab.x + static_cast<double>(p.y - a.y) * ab.y) / len2,
        0.0, 1.0);
    const cv::Point2f proj = a + ab * static_cast<float>(t);
    return cv::norm(p - proj);
}

// Puntos representativos de una geometría (coords de pieza) para el marco de
// selección múltiple.
std::vector<cv::Point2f> referencePoints(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::vector<cv::Point2f> {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                return {g.p0, g.p1};
            } else if constexpr (std::is_same_v<T, CircleGeometry> ||
                                 std::is_same_v<T, BlobGeometry>) {
                return {g.center};
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return {g.a0, g.a1, g.b0, g.b1};
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return {g.vertex, g.end0, g.end1};
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                return g.vertices;
            } else {
                return {g.lineA, g.lineB};
            }
        },
        geometry);
}

// Traslada todos los puntos de la geometría (coords de pieza).
void translateGeometry(ToolGeometry& geometry, const cv::Point2f& delta) {
    std::visit(
        [&delta](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                g.p0 += delta;
                g.p1 += delta;
            } else if constexpr (std::is_same_v<T, CircleGeometry> ||
                                 std::is_same_v<T, BlobGeometry>) {
                g.center += delta;
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                g.lineA += delta;
                g.lineB += delta;
                g.scanA += delta;
                g.scanB += delta;
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                g.a0 += delta;
                g.a1 += delta;
                g.b0 += delta;
                g.b1 += delta;
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                g.vertex += delta;
                g.end0 += delta;
                g.end1 += delta;
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                for (auto& v : g.vertices) {
                    v += delta;
                }
            }
        },
        geometry);
}

// Puntos-manija editables de una geometría (coords de pieza), en orden fijo.
// Cada manija se puede arrastrar por separado. Casos especiales: la 2ª manija
// del círculo es el radio (centro + (r,0)); la 2ª del blob es una esquina que
// redimensiona el rectángulo de forma simétrica respecto al centro.
std::vector<cv::Point2f> handlePoints(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::vector<cv::Point2f> {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                return {g.p0, g.p1};
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return {g.center, g.center + cv::Point2f(g.radius, 0.0F)};
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return {g.lineA, g.lineB, g.scanA, g.scanB};
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return {g.a0, g.a1, g.b0, g.b1};
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return {g.vertex, g.end0, g.end1};
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                return {g.center,
                        g.center + cv::Point2f(g.width / 2.0F, g.height / 2.0F)};
            } else {  // PolyBlobGeometry
                return g.vertices;
            }
        },
        geometry);
}

// Reposiciona una sola manija (coords de pieza); el índice corresponde al orden
// de handlePoints. Mantiene coherente la geometría (radio y tamaños mínimos).
void setHandlePoint(ToolGeometry& geometry, int handle, const cv::Point2f& q) {
    std::visit(
        [&](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                if (handle == 0) {
                    g.p0 = q;
                } else {
                    g.p1 = q;
                }
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                if (handle == 0) {
                    g.center = q;
                } else {
                    g.radius = std::max(4.0F, static_cast<float>(cv::norm(q - g.center)));
                }
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                switch (handle) {
                    case 0: g.lineA = q; break;
                    case 1: g.lineB = q; break;
                    case 2: g.scanA = q; break;
                    default: g.scanB = q; break;
                }
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                switch (handle) {
                    case 0: g.a0 = q; break;
                    case 1: g.a1 = q; break;
                    case 2: g.b0 = q; break;
                    default: g.b1 = q; break;
                }
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                switch (handle) {
                    case 0: g.vertex = q; break;
                    case 1: g.end0 = q; break;
                    default: g.end1 = q; break;
                }
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                if (handle == 0) {
                    g.center = q;
                } else {
                    g.width = std::max(8.0F, 2.0F * std::abs(q.x - g.center.x));
                    g.height = std::max(8.0F, 2.0F * std::abs(q.y - g.center.y));
                }
            } else {  // PolyBlobGeometry
                if (handle >= 0 && handle < static_cast<int>(g.vertices.size())) {
                    g.vertices[static_cast<std::size_t>(handle)] = q;
                }
            }
        },
        geometry);
}

}  // namespace

EditorCanvas::EditorCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(false);
}

QSize EditorCanvas::sizeHint() const {
    return {800, 600};
}

void EditorCanvas::setScene(const QImage& image, const vision::Fixture& fixture) {
    image_ = image;
    fixture_ = fixture;
    liveMode_ = false;
    hasFixture_ = true;
    pieceVisible_ = true;
    update();
}

void EditorCanvas::setFrame(const QImage& frame) {
    image_ = frame;
    liveMode_ = true;
    update();
}

void EditorCanvas::setLivePiece(bool found, const QPolygonF& contour, const QPointF& centroid,
                                double angleDeg, const QString& statusText) {
    liveMode_ = true;
    pieceVisible_ = found;
    liveStatus_ = statusText;
    if (found) {
        fixture_.origin = {static_cast<float>(centroid.x()), static_cast<float>(centroid.y())};
        fixture_.angleDeg = angleDeg;
        hasFixture_ = true;
        liveContour_ = contour;
        liveCentroid_ = centroid;
    }
    update();
}

void EditorCanvas::setLiveContourVisible(bool visible) {
    showLiveContour_ = visible;
    update();
}

void EditorCanvas::clearLive() {
    image_ = QImage();
    pieceVisible_ = false;
    liveStatus_.clear();
    results_.clear();
    update();
}

void EditorCanvas::setTools(std::vector<EditedTool>* tools) {
    tools_ = tools;
    selected_ = -1;
    update();
}

void EditorCanvas::setCreateType(std::optional<ToolType> type) {
    createType_ = type;
    pendingLineA_.reset();    // cancela una Línea-Línea a medio crear
    pendingAngle_.reset();    // cancela un Ángulo a medio crear
    pendingPolygon_.clear();  // cancela un Blob poligonal a medio crear
    snapImg_.reset();         // limpia el resaltado de snap al borde
    setCursor(type.has_value() ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void EditorCanvas::setRegionPickMode(bool enabled) {
    regionPick_ = enabled;
    regionDrag_ = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void EditorCanvas::setDetectionRegion(bool visible, const cv::Rect& imageRect) {
    regionVisible_ = visible;
    regionRect_ = imageRect;
    update();
}

void EditorCanvas::setPickMode(bool enabled) {
    pickMode_ = enabled;
    setCursor(enabled ? Qt::PointingHandCursor
                      : (createType_.has_value() ? Qt::CrossCursor : Qt::ArrowCursor));
}

void EditorCanvas::setAnchorMarker(bool visible, const cv::Point2f& piecePoint) {
    anchorVisible_ = visible;
    anchorPiecePoint_ = piecePoint;
    update();
}

void EditorCanvas::setResults(const std::vector<ToolRunResult>& results) {
    results_ = results;
    update();
}

void EditorCanvas::clearResults() {
    results_.clear();
    update();
}

void EditorCanvas::setSelectedIndex(int index) {
    selected_ = index;
    multiSelected_.clear();
    if (index >= 0) {
        multiSelected_.push_back(index);
    }
    update();
}

void EditorCanvas::setMmPerPixel(double mmPerPixel) {
    mmPerPixel_ = mmPerPixel;
    update();
}

bool EditorCanvas::isSelected(int index) const {
    return std::find(multiSelected_.begin(), multiSelected_.end(), index) !=
           multiSelected_.end();
}

void EditorCanvas::setLengthUnit(LengthUnit unit) {
    unit_ = unit;
    update();
}

void EditorCanvas::setEditingLocked(bool locked) {
    editingLocked_ = locked;
    if (locked) {
        createType_.reset();
        setCursor(Qt::ArrowCursor);
    }
    update();
}

QRectF EditorCanvas::targetRect() const {
    if (image_.isNull()) {
        return {};
    }
    QSizeF target = image_.size();
    target.scale(size(), Qt::KeepAspectRatio);
    QRectF rect(QPointF(0, 0), target);
    rect.moveCenter(QRectF(this->rect()).center());
    return rect;
}

QPointF EditorCanvas::imageToWidget(const cv::Point2f& p) const {
    const QRectF target = targetRect();
    const double sx = target.width() / image_.width();
    const double sy = target.height() / image_.height();
    return {target.left() + p.x * sx, target.top() + p.y * sy};
}

cv::Point2f EditorCanvas::widgetToImage(const QPointF& p) const {
    const QRectF target = targetRect();
    const double sx = image_.width() / target.width();
    const double sy = image_.height() / target.height();
    return {static_cast<float>((p.x() - target.left()) * sx),
            static_cast<float>((p.y() - target.top()) * sy)};
}

cv::Point2f EditorCanvas::toImg(const cv::Point2f& piecePoint) const {
    return vision::toImageCoords(fixture_, piecePoint);
}

int EditorCanvas::hitTest(const cv::Point2f& p) const {
    if (tools_ == nullptr) {
        return -1;
    }
    constexpr double kThreshold = 14.0;  // zona de clic generosa
    int best = -1;
    double bestDistance = kThreshold;

    for (int i = 0; i < static_cast<int>(tools_->size()); ++i) {
        const auto& tool = (*tools_)[static_cast<std::size_t>(i)];
        if (tool.deleted) {
            continue;
        }
        double d = 1e9;
        std::visit(
            [&](const auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, CaliperGeometry> ||
                              std::is_same_v<T, EdgeFlawGeometry> ||
                              std::is_same_v<T, RulerGeometry>) {
                    d = distanceToSegment(p, toImg(g.p0), toImg(g.p1));
                } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                    d = std::abs(cv::norm(p - toImg(g.center)) - g.radius);
                } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                    d = std::min(distanceToSegment(p, toImg(g.lineA), toImg(g.lineB)),
                                 distanceToSegment(p, toImg(g.scanA), toImg(g.scanB)));
                } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                    d = std::min(distanceToSegment(p, toImg(g.a0), toImg(g.a1)),
                                 distanceToSegment(p, toImg(g.b0), toImg(g.b1)));
                } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                    d = std::min(distanceToSegment(p, toImg(g.vertex), toImg(g.end0)),
                                 distanceToSegment(p, toImg(g.vertex), toImg(g.end1)));
                } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                    const float hw = g.width / 2.0F;
                    const float hh = g.height / 2.0F;
                    const cv::Point2f c[4] = {
                        toImg(g.center + cv::Point2f(-hw, -hh)),
                        toImg(g.center + cv::Point2f(hw, -hh)),
                        toImg(g.center + cv::Point2f(hw, hh)),
                        toImg(g.center + cv::Point2f(-hw, hh))};
                    for (int k = 0; k < 4; ++k) {
                        d = std::min(d, distanceToSegment(p, c[k], c[(k + 1) % 4]));
                    }
                } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                    const std::size_t n = g.vertices.size();
                    for (std::size_t k = 0; k < n; ++k) {
                        d = std::min(d, distanceToSegment(p, toImg(g.vertices[k]),
                                                          toImg(g.vertices[(k + 1) % n])));
                    }
                }
            },
            tool.geometry);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

int EditorCanvas::hitHandle(const cv::Point2f& imagePoint) const {
    if (tools_ == nullptr || selected_ < 0 ||
        selected_ >= static_cast<int>(tools_->size())) {
        return -1;
    }
    const auto& tool = (*tools_)[static_cast<std::size_t>(selected_)];
    if (tool.deleted) {
        return -1;
    }
    constexpr double kHandleRadius = 9.0;  // zona de agarre de la manija (px imagen)
    int best = -1;
    double bestDistance = kHandleRadius;
    const auto handles = handlePoints(tool.geometry);
    for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
        const double d = cv::norm(imagePoint - toImg(handles[static_cast<std::size_t>(i)]));
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

std::optional<cv::Point2f> EditorCanvas::snapEdge(const cv::Point2f& cursor,
                                                  const cv::Point2f& dir) const {
    if (dragGray_.empty()) {
        return std::nullopt;
    }
    const double len = cv::norm(dir);
    if (len < 1.0) {
        return std::nullopt;  // trazo aún demasiado corto para orientar el escaneo
    }
    // Escaneo corto centrado en el cursor y alineado con el trazo: el borde más
    // fuerte en esa ventana es el candidato al que "pegar" el extremo.
    const cv::Point2f u = dir / static_cast<float>(len);
    constexpr float kReach = 14.0F;
    const auto edges =
        detectEdges(dragGray_, cursor - u * kReach, cursor + u * kReach, 3.0F, 1);
    if (edges.empty()) {
        return std::nullopt;
    }
    return edges.front().point;
}

// En vivo solo se puede dibujar/editar con la pieza detectada en el frame
// actual: la geometría se guarda relativa a su fixture.
bool EditorCanvas::interactive() const {
    return !image_.isNull() && hasFixture_ && (!liveMode_ || pieceVisible_);
}

void EditorCanvas::mousePressEvent(QMouseEvent* event) {
    if (image_.isNull()) {
        return;
    }
    const cv::Point2f pressPoint = widgetToImage(event->position());

    // Clic derecho sobre una herramienta: borrado rápido (aunque la edición
    // esté bloqueada por inspección, borrar sigue permitido salvo bloqueo).
    if (event->button() == Qt::RightButton) {
        if (!editingLocked_) {
            const int hit = hitTest(pressPoint);
            if (hit >= 0) {
                emit toolRightClicked(hit);
            }
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    // En inspección no se dibuja ni se mueve: solo se lee la pieza.
    if (editingLocked_ && !regionPick_ && !pickMode_) {
        return;
    }

    // La zona de detección se define sin pieza detectada: es justo la
    // herramienta para cuando la detección automática está fallando.
    if (regionPick_) {
        dragStart_ = pressPoint;
        dragCurrent_ = pressPoint;
        regionDrag_ = true;
        return;
    }

    if (!interactive()) {
        return;
    }
    const cv::Point2f p = pressPoint;
    dragStart_ = p;
    dragCurrent_ = p;

    if (pickMode_) {
        setPickMode(false);
        emit pointPicked(p);
        return;
    }

    if (createType_.has_value()) {
        creating_ = true;
        // Cachea el gris del frame actual para el snap al borde durante el trazo.
        dragGray_ = qimageToGray(image_);
        snapImg_.reset();
    } else if (const int handle = hitHandle(p); handle >= 0) {
        // Prioridad a las manijas de la herramienta ya seleccionada: se arrastra
        // ese extremo suelto en vez de mover el conjunto completo.
        draggingHandle_ = true;
        handleIndex_ = handle;
        update();
    } else {
        const int hit = hitTest(p);
        if (hit >= 0) {
            // Clic sobre una herramienta ya en el grupo: se mueve el grupo
            // completo; si no, la selección pasa a esa herramienta.
            if (!isSelected(hit)) {
                multiSelected_ = {hit};
            }
            if (hit != selected_) {
                selected_ = hit;
                emit selectionChanged(selected_);
            }
            moving_ = true;
        } else {
            // Clic en vacío: arrastrar dibuja un marco de selección.
            marquee_ = true;
        }
        update();
    }
}

void EditorCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!creating_ && !moving_ && !marquee_ && !regionDrag_ && !draggingHandle_) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());

    if (draggingHandle_ && tools_ != nullptr && selected_ >= 0 &&
        selected_ < static_cast<int>(tools_->size())) {
        // La manija se coloca en el punto del cursor, en coords de pieza (la
        // rotación del fixture se cancela sola).
        setHandlePoint((*tools_)[static_cast<std::size_t>(selected_)].geometry,
                       handleIndex_, vision::toPieceCoords(fixture_, p));
    }

    // Snap al borde bajo el cursor mientras se traza una herramienta de línea.
    if (creating_ && createType_.has_value() &&
        (*createType_ == ToolType::Caliper || *createType_ == ToolType::Ruler ||
         *createType_ == ToolType::EdgeFlaw)) {
        snapImg_ = snapEdge(p, p - dragStart_);
    }

    if (moving_ && tools_ != nullptr && !multiSelected_.empty()) {
        // Delta en coords de pieza (la rotación del fixture se cancela sola);
        // se mueve todo el grupo seleccionado.
        const cv::Point2f delta = vision::toPieceCoords(fixture_, p) -
                                  vision::toPieceCoords(fixture_, dragCurrent_);
        for (const int index : multiSelected_) {
            if (index >= 0 && index < static_cast<int>(tools_->size())) {
                translateGeometry((*tools_)[static_cast<std::size_t>(index)].geometry,
                                  delta);
            }
        }
    }
    dragCurrent_ = p;
    update();
}

// Cierre del marco de selección: quedan seleccionadas las herramientas con
// algún punto de referencia dentro del rectángulo.
void EditorCanvas::finishMarquee(const cv::Point2f& releasePoint) {
    marquee_ = false;
    if (tools_ == nullptr) {
        return;
    }

    if (cv::norm(releasePoint - dragStart_) < 6.0) {
        // Clic simple en vacío: deseleccionar.
        multiSelected_.clear();
        if (selected_ != -1) {
            selected_ = -1;
            emit selectionChanged(-1);
        }
        update();
        return;
    }

    const float left = std::min(dragStart_.x, releasePoint.x);
    const float right = std::max(dragStart_.x, releasePoint.x);
    const float top = std::min(dragStart_.y, releasePoint.y);
    const float bottom = std::max(dragStart_.y, releasePoint.y);

    multiSelected_.clear();
    for (int i = 0; i < static_cast<int>(tools_->size()); ++i) {
        const auto& tool = (*tools_)[static_cast<std::size_t>(i)];
        if (tool.deleted) {
            continue;
        }
        for (const auto& piecePoint : referencePoints(tool.geometry)) {
            const cv::Point2f q = toImg(piecePoint);
            if (q.x >= left && q.x <= right && q.y >= top && q.y <= bottom) {
                multiSelected_.push_back(i);
                break;
            }
        }
    }
    const int primary = multiSelected_.empty() ? -1 : multiSelected_.front();
    if (primary != selected_) {
        selected_ = primary;
        emit selectionChanged(selected_);
    }
    update();
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());

    if (regionDrag_) {
        regionDrag_ = false;
        setRegionPickMode(false);
        const int left = cvRound(std::min(dragStart_.x, p.x));
        const int top = cvRound(std::min(dragStart_.y, p.y));
        const int width = cvRound(std::abs(p.x - dragStart_.x));
        const int height = cvRound(std::abs(p.y - dragStart_.y));
        if (width >= 20 && height >= 20) {
            emit regionPicked(cv::Rect(left, top, width, height));
        }
        update();
        return;
    }

    if (draggingHandle_) {
        draggingHandle_ = false;
        handleIndex_ = -1;
        emit toolModified();
        return;
    }
    if (marquee_) {
        finishMarquee(p);
        return;
    }
    if (moving_) {
        moving_ = false;
        emit toolModified();
        return;
    }
    if (!creating_) {
        return;
    }
    creating_ = false;
    update();

    // Blob poligonal: se construye por clics sucesivos (no por arrastre). Cada
    // clic añade un vértice; hacer clic cerca del primero (con >= 3 vértices)
    // cierra el polígono y crea la herramienta.
    if (createType_.has_value() && *createType_ == ToolType::PolyBlob) {
        if (pendingPolygon_.size() >= 3 &&
            cv::norm(toImg(pendingPolygon_.front()) - p) < 12.0) {
            PolyBlobGeometry g;
            g.vertices = pendingPolygon_;
            pendingPolygon_.clear();
            emit toolCreated(ToolGeometry(g));
            update();
            return;
        }
        pendingPolygon_.push_back(vision::toPieceCoords(fixture_, p));
        update();
        return;
    }

    if (!createType_.has_value() || cv::norm(p - dragStart_) < 8.0) {
        snapImg_.reset();
        return;  // arrastre demasiado corto: ignorado
    }

    // Si hay un borde resaltado bajo el cursor, el extremo se pega a él.
    const cv::Point2f releaseImg = snapImg_.value_or(p);
    snapImg_.reset();
    const cv::Point2f a = vision::toPieceCoords(fixture_, dragStart_);
    const cv::Point2f b = vision::toPieceCoords(fixture_, releaseImg);

    // Línea-Línea se crea en dos arrastres: el primero fija la línea A (que se
    // dibuja mientras se traza la B), el segundo cierra la herramienta.
    if (*createType_ == ToolType::LineToLine) {
        if (!pendingLineA_.has_value()) {
            pendingLineA_ = std::array<cv::Point2f, 2>{a, b};
            update();
            return;
        }
        LineToLineGeometry g;
        g.a0 = (*pendingLineA_)[0];
        g.a1 = (*pendingLineA_)[1];
        g.b0 = a;
        g.b1 = b;
        pendingLineA_.reset();
        emit toolCreated(ToolGeometry(g));
        return;
    }

    // Ángulo: el primer arrastre fija vértice (inicio) y primer lado (fin); el
    // segundo arrastre define el segundo lado (su punto final), compartiendo el
    // vértice ya fijado.
    if (*createType_ == ToolType::Angle) {
        if (!pendingAngle_.has_value()) {
            pendingAngle_ = std::array<cv::Point2f, 2>{a, b};
            update();
            return;
        }
        AngleGeometry g;
        g.vertex = (*pendingAngle_)[0];
        g.end0 = (*pendingAngle_)[1];
        g.end1 = b;
        pendingAngle_.reset();
        emit toolCreated(ToolGeometry(g));
        return;
    }

    ToolGeometry geometry = CaliperGeometry{};
    switch (*createType_) {
        case ToolType::Caliper:
            geometry = CaliperGeometry{a, b, 10.0F};
            break;
        case ToolType::Circle: {
            CircleGeometry g;
            g.center = a;
            g.radius = static_cast<float>(cv::norm(b - a));
            g.searchBand = std::min(12.0F, g.radius / 2.0F);
            geometry = g;
            break;
        }
        case ToolType::PointToLine: {
            PointToLineGeometry g;
            g.lineA = a;
            g.lineB = b;
            const cv::Point2f mid = (a + b) / 2.0F;
            cv::Point2f dir = b - a;
            const float len = static_cast<float>(cv::norm(dir));
            dir /= len;
            const cv::Point2f n(-dir.y, dir.x);
            g.scanA = mid - n * 40.0F;
            g.scanB = mid + n * 40.0F;
            geometry = g;
            break;
        }
        case ToolType::EdgeFlaw:
            geometry = EdgeFlawGeometry{a, b, 16.0F, 20};
            break;
        case ToolType::Ruler:
            geometry = RulerGeometry{a, b};
            break;
        case ToolType::Blob: {
            BlobGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(10.0F, std::abs(b.x - a.x));
            g.height = std::max(10.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::LineToLine:
        case ToolType::Angle:
        case ToolType::PolyBlob:
            return;  // gestionado arriba (creación en varios pasos)
    }
    emit toolCreated(geometry);
}

void EditorCanvas::paintTool(QPainter& painter, const EditedTool& tool, bool selected) const {
    QColor color = toolColor(tool.config.type);
    QPen pen(color);
    pen.setWidthF(selected ? 3.0 : 1.8);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPointF labelPos;
    std::visit(
        [&](const auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry>) {
                const cv::Point2f p0 = toImg(g.p0);
                const cv::Point2f p1 = toImg(g.p1);
                const QPointF a = imageToWidget(p0);
                const QPointF b = imageToWidget(p1);
                painter.drawLine(a, b);
                painter.drawEllipse(a, 3.0, 3.0);
                painter.drawEllipse(b, 3.0, 3.0);

                // Banda de muestreo visible: al cambiar "Puntos" se ve.
                const cv::Point2f delta = p1 - p0;
                const float length = static_cast<float>(cv::norm(delta));
                if (length > 1.0F) {
                    float half = 0.0F;
                    if constexpr (std::is_same_v<T, CaliperGeometry>) {
                        half = g.bandWidth / 2.0F;
                    } else {
                        half = g.scanLength / 2.0F;
                    }
                    const cv::Point2f u = delta / length;
                    const cv::Point2f n(-u.y * half, u.x * half);
                    QPen dashed = painter.pen();
                    dashed.setStyle(Qt::DashLine);
                    dashed.setWidthF(1.0);
                    painter.save();
                    painter.setPen(dashed);
                    painter.drawLine(imageToWidget(p0 + n), imageToWidget(p1 + n));
                    painter.drawLine(imageToWidget(p0 - n), imageToWidget(p1 - n));
                    painter.restore();
                }
                labelPos = (a + b) / 2.0;
            } else if constexpr (std::is_same_v<T, RulerGeometry>) {
                const QPointF a = imageToWidget(toImg(g.p0));
                const QPointF b = imageToWidget(toImg(g.p1));
                painter.drawLine(a, b);
                // Topes de regla en los extremos.
                QLineF line(a, b);
                const QLineF normal = line.normalVector().unitVector();
                const QPointF tick(normal.dx() * 6.0, normal.dy() * 6.0);
                painter.drawLine(a - tick, a + tick);
                painter.drawLine(b - tick, b + tick);
                labelPos = (a + b) / 2.0;
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                const QPointF c = imageToWidget(toImg(g.center));
                const double scale = targetRect().width() / image_.width();
                painter.drawEllipse(c, g.radius * scale, g.radius * scale);
                QPen dashed(pen);
                dashed.setStyle(Qt::DashLine);
                dashed.setWidthF(1.0);
                painter.setPen(dashed);
                painter.drawEllipse(c, (g.radius - g.searchBand) * scale,
                                    (g.radius - g.searchBand) * scale);
                painter.drawEllipse(c, (g.radius + g.searchBand) * scale,
                                    (g.radius + g.searchBand) * scale);
                labelPos = c + QPointF(0, -g.radius * scale - 6);
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                const QPointF la = imageToWidget(toImg(g.lineA));
                const QPointF lb = imageToWidget(toImg(g.lineB));
                painter.drawLine(la, lb);
                QPen dashed(pen);
                dashed.setStyle(Qt::DashLine);
                painter.setPen(dashed);
                painter.drawLine(imageToWidget(toImg(g.scanA)), imageToWidget(toImg(g.scanB)));
                labelPos = (la + lb) / 2.0;
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                const float hw = g.width / 2.0F;
                const float hh = g.height / 2.0F;
                QPolygonF quad;
                quad << imageToWidget(toImg(g.center + cv::Point2f(-hw, -hh)))
                     << imageToWidget(toImg(g.center + cv::Point2f(hw, -hh)))
                     << imageToWidget(toImg(g.center + cv::Point2f(hw, hh)))
                     << imageToWidget(toImg(g.center + cv::Point2f(-hw, hh)));
                painter.drawPolygon(quad);
                labelPos = quad.boundingRect().topLeft() + QPointF(2, -4);
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                const QPointF a0 = imageToWidget(toImg(g.a0));
                const QPointF a1 = imageToWidget(toImg(g.a1));
                const QPointF b0 = imageToWidget(toImg(g.b0));
                const QPointF b1 = imageToWidget(toImg(g.b1));
                painter.drawLine(a0, a1);
                painter.drawLine(b0, b1);
                painter.drawEllipse(a0, 3.0, 3.0);
                painter.drawEllipse(b0, 3.0, 3.0);
                labelPos = (a0 + b1) / 2.0;
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                const QPointF v = imageToWidget(toImg(g.vertex));
                const QPointF e0 = imageToWidget(toImg(g.end0));
                const QPointF e1 = imageToWidget(toImg(g.end1));
                painter.drawLine(v, e0);
                painter.drawLine(v, e1);
                painter.drawEllipse(v, 3.0, 3.0);
                labelPos = v;
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                QPolygonF poly;
                for (const auto& vtx : g.vertices) {
                    poly << imageToWidget(toImg(vtx));
                }
                painter.drawPolygon(poly);
                labelPos = poly.boundingRect().topLeft() + QPointF(2, -4);
            }
        },
        tool.geometry);

    painter.setPen(selected ? Qt::white : color);
    painter.drawText(labelPos + QPointF(6, -4),
                     QString::fromStdString(tool.config.name));

    // Manijas de edición: cuadraditos blancos en cada extremo editable de la
    // herramienta seleccionada (arrástralos para afinar sin volver a dibujar).
    if (selected && !editingLocked_) {
        QPen handlePen(Qt::white);
        handlePen.setWidthF(1.5);
        handlePen.setCosmetic(true);
        painter.setPen(handlePen);
        painter.setBrush(QColor(40, 40, 40));
        for (const auto& hp : handlePoints(tool.geometry)) {
            const QPointF w = imageToWidget(toImg(hp));
            painter.drawRect(QRectF(w.x() - 3.5, w.y() - 3.5, 7.0, 7.0));
        }
        painter.setBrush(Qt::NoBrush);
    }
}

void EditorCanvas::paintResults(QPainter& painter) const {
    painter.save();
    QFont measureFont = painter.font();
    measureFont.setBold(true);
    painter.setFont(measureFont);
    for (const auto& result : results_) {
        const QColor color = result.ok ? QColor(0, 220, 0) : QColor(255, 70, 70);
        QPen pen(color);
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        painter.setPen(pen);
        for (const auto& segment : result.overlaySegments) {
            painter.drawLine(imageToWidget(segment[0]), imageToWidget(segment[1]));
        }
        for (const auto& point : result.overlayPoints) {
            const QPointF p = imageToWidget(point);
            painter.drawLine(p + QPointF(-5, 0), p + QPointF(5, 0));
            painter.drawLine(p + QPointF(0, -5), p + QPointF(0, 5));
        }

        // Etiqueta con la medida junto a la herramienta (px o mm calibrados).
        QPointF labelPos;
        if (!result.overlaySegments.empty()) {
            labelPos = imageToWidget(result.overlaySegments.front()[0]);
        } else if (!result.overlayPoints.empty()) {
            labelPos = imageToWidget(result.overlayPoints.front());
        } else {
            continue;
        }
        QString measure;
        if (result.measuredIsAngle) {
            measure = QStringLiteral("%1°").arg(result.measured, 0, 'f', 1);
        } else if (result.type == ToolType::Blob) {
            measure = QStringLiteral("n=%1").arg(result.measured, 0, 'f', 0);
        } else if (mmPerPixel_ > 0.0 && unit_ != LengthUnit::Pixels) {
            const double mm = result.measured * mmPerPixel_;
            const bool useCm = unit_ == LengthUnit::Centimeters ||
                               (unit_ == LengthUnit::Auto && mm >= 100.0);
            measure = useCm ? QStringLiteral("%1 cm").arg(mm / 10.0, 0, 'f', 2)
                            : QStringLiteral("%1 mm").arg(mm, 0, 'f', 2);
        } else {
            measure = QStringLiteral("%1 px").arg(result.measured, 0, 'f', 1);
        }
        const QString text =
            QString::fromStdString(result.name) + QStringLiteral(": ") + measure;
        const QFontMetricsF metrics(painter.font());
        const QRectF box = metrics.boundingRect(text).adjusted(-4, -2, 4, 2)
                               .translated(labelPos + QPointF(8, -10));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRect(box);
        painter.setPen(color);
        painter.drawText(box, Qt::AlignCenter, text);
    }
    painter.restore();
}

void EditorCanvas::paintCreationPreview(QPainter& painter) const {
    QPen pen(Qt::white);
    pen.setStyle(Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Línea A ya trazada de una Línea-Línea en curso: se mantiene visible
    // mientras se dibuja la línea B.
    if (pendingLineA_.has_value()) {
        painter.drawLine(imageToWidget(toImg((*pendingLineA_)[0])),
                         imageToWidget(toImg((*pendingLineA_)[1])));
    }
    // Primer lado de un Ángulo en curso: vértice + primer lado ya fijados.
    if (pendingAngle_.has_value()) {
        painter.drawLine(imageToWidget(toImg((*pendingAngle_)[0])),
                         imageToWidget(toImg((*pendingAngle_)[1])));
    }
    // Blob poligonal en curso: vértices marcados, con el primero resaltado para
    // indicar dónde cerrar.
    if (!pendingPolygon_.empty()) {
        for (std::size_t i = 0; i + 1 < pendingPolygon_.size(); ++i) {
            painter.drawLine(imageToWidget(toImg(pendingPolygon_[i])),
                             imageToWidget(toImg(pendingPolygon_[i + 1])));
        }
        for (const auto& v : pendingPolygon_) {
            painter.drawEllipse(imageToWidget(toImg(v)), 2.5, 2.5);
        }
        painter.setBrush(QColor(0, 220, 0));
        painter.drawEllipse(imageToWidget(toImg(pendingPolygon_.front())), 4.0, 4.0);
        painter.setBrush(Qt::NoBrush);
    }
    if (!creating_) {
        return;
    }

    const QPointF a = imageToWidget(dragStart_);
    const QPointF b = imageToWidget(dragCurrent_);
    if (createType_ == ToolType::Circle) {
        const double r = std::hypot(b.x() - a.x(), b.y() - a.y());
        painter.drawEllipse(a, r, r);
    } else if (createType_ == ToolType::Blob) {
        painter.drawRect(QRectF(a, b).normalized());
    } else {
        painter.drawLine(a, b);
    }

    // Resaltado del borde bajo el cursor (snap): el extremo se pegará aquí.
    if (snapImg_.has_value()) {
        const QPointF s = imageToWidget(*snapImg_);
        QPen snapPen(QColor(255, 230, 0));
        snapPen.setWidthF(2.0);
        snapPen.setCosmetic(true);
        painter.setPen(snapPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(s, 5.0, 5.0);
        painter.drawLine(s + QPointF(-8, 0), s + QPointF(8, 0));
        painter.drawLine(s + QPointF(0, -8), s + QPointF(0, 8));
    }
}

void EditorCanvas::paintLiveOverlay(QPainter& painter) const {
    if (!liveMode_) {
        return;
    }

    if (pieceVisible_ && showLiveContour_ && !liveContour_.isEmpty()) {
        painter.save();
        const QRectF target = targetRect();
        painter.translate(target.topLeft());
        painter.scale(target.width() / image_.width(), target.height() / image_.height());

        QPen contourPen(QColor(0, 220, 0));
        contourPen.setWidthF(2.0);
        contourPen.setCosmetic(true);
        painter.setPen(contourPen);
        painter.drawPolygon(liveContour_);

        QPen axisPen(QColor(0, 200, 255));
        axisPen.setWidthF(2.0);
        axisPen.setCosmetic(true);
        painter.setPen(axisPen);
        const double rad = fixture_.angleDeg * 3.14159265358979323846 / 180.0;
        const double len = image_.width() * 0.12;
        painter.drawLine(liveCentroid_,
                         liveCentroid_ + QPointF(std::cos(rad) * len, std::sin(rad) * len));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 60, 60));
        painter.drawEllipse(liveCentroid_, 4.0, 4.0);
        painter.restore();
    }

    if (!liveStatus_.isEmpty()) {
        const QRectF target = targetRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        const QRectF textRect(target.left() + 8, target.top() + 8, 280, 24);
        painter.drawRect(textRect);
        painter.setPen(pieceVisible_ ? QColor(0, 220, 0) : QColor(255, 150, 100));
        painter.drawText(textRect.adjusted(6, 0, 0, 0), Qt::AlignVCenter, liveStatus_);
    }
}

void EditorCanvas::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(25, 25, 25));

    if (image_.isNull()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter,
                         liveMode_ ? tr("Sin señal") : tr("Sin imagen de referencia"));
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(targetRect(), image_);

    paintLiveOverlay(painter);

    // Marcador del rasgo distintivo (rombo magenta anclado a la pieza).
    if (anchorVisible_ && hasFixture_) {
        const QPointF p = imageToWidget(toImg(anchorPiecePoint_));
        QPen pen(QColor(255, 0, 255));
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        QPolygonF diamond;
        diamond << p + QPointF(0, -8) << p + QPointF(8, 0) << p + QPointF(0, 8)
                << p + QPointF(-8, 0);
        painter.drawPolygon(diamond);
        painter.drawEllipse(p, 1.5, 1.5);
    }

    if (tools_ != nullptr) {
        for (int i = 0; i < static_cast<int>(tools_->size()); ++i) {
            const auto& tool = (*tools_)[static_cast<std::size_t>(i)];
            if (!tool.deleted) {
                paintTool(painter, tool, isSelected(i));
            }
        }
    }
    paintResults(painter);
    paintCreationPreview(painter);

    // Marco de selección múltiple en curso.
    if (marquee_) {
        QPen pen(Qt::white);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 255, 255, 30));
        painter.drawRect(QRectF(imageToWidget(dragStart_), imageToWidget(dragCurrent_))
                             .normalized());
    }

    // Zona de detección: la guardada (amarillo punteado) y la que se está
    // arrastrando ahora mismo.
    QPen regionPen(QColor(255, 210, 0));
    regionPen.setStyle(Qt::DashLine);
    regionPen.setWidthF(2.0);
    regionPen.setCosmetic(true);
    if (regionVisible_ && regionRect_.area() > 0) {
        painter.setPen(regionPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(imageToWidget({static_cast<float>(regionRect_.x),
                                               static_cast<float>(regionRect_.y)}),
                                imageToWidget({static_cast<float>(regionRect_.br().x),
                                               static_cast<float>(regionRect_.br().y)})));
    }
    if (regionDrag_) {
        painter.setPen(regionPen);
        painter.setBrush(QColor(255, 210, 0, 25));
        painter.drawRect(QRectF(imageToWidget(dragStart_), imageToWidget(dragCurrent_))
                             .normalized());
    }
}

}  // namespace pci::inspection
