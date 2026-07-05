#include "inspection_editor/canvas/editor_canvas.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

QColor toolColor(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return {0, 200, 255};
        case ToolType::Circle: return {255, 170, 0};
        case ToolType::PointToLine: return {180, 120, 255};
        case ToolType::EdgeFlaw: return {0, 230, 120};
        case ToolType::Blob: return {255, 105, 180};
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

// Traslada todos los puntos de la geometría (coords de pieza).
void translateGeometry(ToolGeometry& geometry, const cv::Point2f& delta) {
    std::visit(
        [&delta](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry>) {
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
    setCursor(type.has_value() ? Qt::CrossCursor : Qt::ArrowCursor);
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
    constexpr double kThreshold = 10.0;
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
                              std::is_same_v<T, EdgeFlawGeometry>) {
                    d = distanceToSegment(p, toImg(g.p0), toImg(g.p1));
                } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                    d = std::abs(cv::norm(p - toImg(g.center)) - g.radius);
                } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                    d = std::min(distanceToSegment(p, toImg(g.lineA), toImg(g.lineB)),
                                 distanceToSegment(p, toImg(g.scanA), toImg(g.scanB)));
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

// En vivo solo se puede dibujar/editar con la pieza detectada en el frame
// actual: la geometría se guarda relativa a su fixture.
bool EditorCanvas::interactive() const {
    return !image_.isNull() && hasFixture_ && (!liveMode_ || pieceVisible_);
}

void EditorCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !interactive()) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());
    dragStart_ = p;
    dragCurrent_ = p;

    if (createType_.has_value()) {
        creating_ = true;
    } else {
        const int hit = hitTest(p);
        if (hit != selected_) {
            selected_ = hit;
            emit selectionChanged(selected_);
        }
        moving_ = selected_ >= 0;
        update();
    }
}

void EditorCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!creating_ && !moving_) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());

    if (moving_ && tools_ != nullptr && selected_ >= 0) {
        // Delta en coords de pieza (la rotación del fixture se cancela sola).
        const cv::Point2f delta = vision::toPieceCoords(fixture_, p) -
                                  vision::toPieceCoords(fixture_, dragCurrent_);
        translateGeometry((*tools_)[static_cast<std::size_t>(selected_)].geometry, delta);
    }
    dragCurrent_ = p;
    update();
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());

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

    if (!createType_.has_value() || cv::norm(p - dragStart_) < 8.0) {
        return;  // arrastre demasiado corto: ignorado
    }

    const cv::Point2f a = vision::toPieceCoords(fixture_, dragStart_);
    const cv::Point2f b = vision::toPieceCoords(fixture_, p);

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
        case ToolType::Blob: {
            BlobGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(10.0F, std::abs(b.x - a.x));
            g.height = std::max(10.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
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
                const QPointF a = imageToWidget(toImg(g.p0));
                const QPointF b = imageToWidget(toImg(g.p1));
                painter.drawLine(a, b);
                painter.drawEllipse(a, 3.0, 3.0);
                painter.drawEllipse(b, 3.0, 3.0);
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
            }
        },
        tool.geometry);

    painter.setPen(selected ? Qt::white : color);
    painter.drawText(labelPos + QPointF(6, -4),
                     QString::fromStdString(tool.config.name));
}

void EditorCanvas::paintResults(QPainter& painter) const {
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
    }
}

void EditorCanvas::paintCreationPreview(QPainter& painter) const {
    if (!creating_) {
        return;
    }
    QPen pen(Qt::white);
    pen.setStyle(Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

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

    if (tools_ != nullptr) {
        for (int i = 0; i < static_cast<int>(tools_->size()); ++i) {
            const auto& tool = (*tools_)[static_cast<std::size_t>(i)];
            if (!tool.deleted) {
                paintTool(painter, tool, i == selected_);
            }
        }
    }
    paintResults(painter);
    paintCreationPreview(painter);
}

}  // namespace pci::inspection
