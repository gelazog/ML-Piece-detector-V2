#include "inspection_editor/canvas/editor_canvas.h"

#include "inspection_editor/canvas/canvas_geometry.h"

#include <opencv2/imgproc.hpp>

#include <QFontMetrics>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

#include "inspection_editor/execution/edge_detection.h"
#include "vision/auto_roi.h"
#include "vision/fitting.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

// Límites de zoom (1.0 = imagen ajustada a la ventana) y factor por muesca de
// rueda: 15% da una progresión suave sin sentirse lento.
constexpr double kMinZoom = 1.0;   // por debajo del ajuste no aporta nada
constexpr double kMaxZoom = 20.0;
constexpr double kZoomStep = 1.15;

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
        case ToolType::EdgeDefects: return {120, 255, 60};
        case ToolType::Straightness: return {255, 120, 120};
        case ToolType::Roundness: return {255, 190, 90};
        case ToolType::Orientation: return {200, 140, 255};
        case ToolType::CentreOffset: return {255, 140, 200};
        case ToolType::BoltPattern: return {160, 255, 200};
        case ToolType::Profile: return {255, 235, 120};
        case ToolType::Extremes: return {120, 200, 255};
        case ToolType::Chamfer: return {255, 175, 90};
        case ToolType::Fillet: return {170, 255, 170};
        case ToolType::Blob: return {255, 105, 180};
        case ToolType::Region: return {255, 150, 200};
        case ToolType::Symmetry: return {200, 160, 255};
        case ToolType::Polygon: return {255, 200, 120};
        case ToolType::Clearance: return {120, 230, 255};
        case ToolType::Ruler: return {255, 255, 120};
        case ToolType::LineToLine: return {120, 220, 220};
        case ToolType::Angle: return {255, 170, 60};
        case ToolType::PolyBlob: return {200, 120, 255};
        case ToolType::Position: return {255, 80, 80};
        case ToolType::Arc: return {120, 255, 190};
        case ToolType::Shaft: return {255, 210, 120};
        case ToolType::Groove: return {90, 200, 220};
        case ToolType::Thread: return {200, 255, 120};
        case ToolType::Gear: return {160, 190, 255};
        // Las dos construcciones comparten color a propósito: son la misma
        // familia y lo que las distingue —punto o recta— ya se ve en el dibujo.
        case ToolType::ConstructedPoint:
        case ToolType::ConstructedLine:
        case ToolType::MedianAxis: return {150, 255, 255};
    }
    return Qt::white;
}


}  // namespace

EditorCanvas::EditorCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(false);
    // El lienzo puede recibir el foco: por tabulador y al hacer clic.
    //
    // Estaba en `NoFocus`, así que era el ÚNICO sitio de la ventana al que el
    // teclado no podía llegar — y es donde se trabaja. Se notaba en dos cosas:
    // no había forma de saber si el lienzo estaba activo, y cualquier tecla que
    // el lienzo quisiera atender no le habría llegado nunca.
    //
    // `StrongFocus` y no `ClickFocus`: al operador que navega con el teclado hay
    // que dejarle llegar hasta aquí, no solo al que usa el ratón.
    setFocusPolicy(Qt::StrongFocus);
}

QSize EditorCanvas::sizeHint() const {
    return {800, 600};
}

void EditorCanvas::setScene(const QImage& image, const vision::Fixture& fixture) {
    // Solo se reencuadra si cambia el tamaño (imagen distinta): así "Actualizar
    // desde cámara" conserva el zoom con el que el operador estaba trabajando.
    if (image_.size() != image.size()) {
        zoom_ = 1.0;
        pan_ = QPointF();
    }
    image_ = image;
    fixture_ = fixture;
    liveMode_ = false;
    hasFixture_ = true;
    pieceVisible_ = true;
    update();
    emit viewChanged();
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
    zoom_ = 1.0;  // fin de la transmisión: vista limpia para la próxima
    pan_ = QPointF();
    update();
    emit viewChanged();
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
    pendingArc_.reset();      // y un Arco a medio crear
    pendingPolygon_.clear();  // cancela un Blob poligonal a medio crear
    snapImg_.reset();         // limpia el resaltado de snap al borde
    restoreCursor();
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

void EditorCanvas::setFreeZonePickMode(bool enabled) {
    freeZonePick_ = enabled;
    freeDragging_ = false;
    freeLasso_ = false;
    freeVertices_.clear();
    freeTrace_.clear();
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void EditorCanvas::setFreeZone(bool visible, const std::vector<cv::Point>& imagePolygon) {
    freeZoneVisible_ = visible;
    freeZone_ = imagePolygon;
    update();
}

void EditorCanvas::finishFreeZone(const std::vector<cv::Point>& trace) {
    const auto polygon = vision::zonePolygonFromTrace(trace);
    freeVertices_.clear();
    freeTrace_.clear();
    freeLasso_ = false;
    if (polygon.empty()) {
        // El modo sigue encendido a propósito: lo que falló fue el trazo, no la
        // intención, y apagarlo obligaría a volver a pulsar el botón para
        // repetir el gesto que se acaba de intentar.
        update();
        emit traceRejected(
            tr("Ese trazo no encierra ninguna zona: rodea el área con el ratón, o "
               "marca al menos tres esquinas a clics y cierra sobre la primera."));
        return;
    }
    setFreeZonePickMode(false);
    emit freeZonePicked(polygon);
}


void EditorCanvas::setEdgeBrush(EdgeBrush mode, int radiusPx) {
    brush_ = mode;
    brushRadius_ = std::max(1, radiusPx);
    painting_ = false;
    // Cruz al pintar: el cursor tiene que decir que el clic va a marcar y no a
    // seleccionar.
    restoreCursor();
    update();
}

void EditorCanvas::setEdgeCorrection(const cv::Mat& forcePiece,
                                     const cv::Mat& forceBackground) {
    forcePiece_ = forcePiece.clone();
    forceBackground_ = forceBackground.clone();
    update();
}

void EditorCanvas::clearEdgeCorrection() {
    forcePiece_ = cv::Mat();
    forceBackground_ = cv::Mat();
    update();
    emit edgeCorrected(forcePiece_, forceBackground_);
}

// Una pincelada en un punto de la imagen.
//
// Las máscaras se crean del tamaño de la imagen la primera vez que hacen falta:
// reservarlas siempre costaría dos matrices del tamaño del frame por cada
// lienzo, y lo normal es no corregir nada.
void EditorCanvas::paintAt(const cv::Point2f& imagePoint) {
    if (brush_ == EdgeBrush::Off || image_.isNull()) {
        return;
    }
    const cv::Size size(image_.width(), image_.height());
    cv::Mat& target = brush_ == EdgeBrush::AddPiece ? forcePiece_ : forceBackground_;
    cv::Mat& other = brush_ == EdgeBrush::AddPiece ? forceBackground_ : forcePiece_;
    if (target.empty() || target.size() != size) {
        target = cv::Mat::zeros(size, CV_8UC1);
    }
    const cv::Point centre(cvRound(imagePoint.x), cvRound(imagePoint.y));
    // Un SEGMENTO desde el punto anterior, no un círculo suelto.
    //
    // Lo destapó el test: pintando un círculo por cada evento del ratón, un
    // trazo de 100 px con radio 20 marcaba dos manchas y dejaba el medio sin
    // tocar. El ratón no emite un evento por píxel, así que a poco que se mueva
    // rápido el pincel pinta a puntos — y el operador tendría que repasar
    // despacio para que no quedaran huecos.
    if (lastPaint_.has_value()) {
        cv::line(target, *lastPaint_, centre, cv::Scalar(255), brushRadius_ * 2,
                 cv::LINE_8);
    }
    cv::circle(target, centre, brushRadius_, cv::Scalar(255), cv::FILLED);
    // Y se borra del contrario. Sin esto, marcar fondo sobre algo marcado como
    // pieza dejaría las dos máscaras diciendo cosas opuestas del mismo píxel, y
    // el resultado dependería del orden en que se aplicaran — que es
    // exactamente la clase de estado que nadie puede razonar.
    if (!other.empty() && other.size() == size) {
        if (lastPaint_.has_value()) {
            cv::line(other, *lastPaint_, centre, cv::Scalar(0), brushRadius_ * 2, cv::LINE_8);
        }
        cv::circle(other, centre, brushRadius_, cv::Scalar(0), cv::FILLED);
    }
    lastPaint_ = centre;
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

void EditorCanvas::setFocusedPiece(int pieceIndex) {
    if (focusedPiece_ == pieceIndex) {
        return;
    }
    focusedPiece_ = pieceIndex;
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
    // Un arrastre de manija pertenece a la herramienta en la que empezó. Si la
    // selección cambia desde fuera a media faena (la lista del panel, un
    // deshacer), el arrastre se cancela: si no, el índice de manija seguiría
    // vivo sobre otra geometría, que puede tener menos manijas o de otro tipo.
    draggingHandle_ = false;
    handleIndex_ = -1;
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

void EditorCanvas::setBoardVisible(bool visible) {
    boardVisible_ = visible;
    // El seguimiento del ratón solo hace falta para la lectura del cursor (T4):
    // se enciende con el tablero y se apaga con él, para no repintar de más en
    // el modo vivo.
    setMouseTracking(visible || rulerVisible_);
    if (!visible && !rulerVisible_) {
        cursorWidget_.reset();
    }
    update();
}

void EditorCanvas::leaveEvent(QEvent* event) {
    cursorWidget_.reset();  // sin cursor sobre el lienzo no hay lectura que dar
    if (boardVisible_) {
        update();
    }
    QWidget::leaveEvent(event);
}

QString EditorCanvas::boardValueText(double px, bool signPrefix) const {
    QString text;
    if (mmPerPixel_ > 0.0 && unit_ != LengthUnit::Pixels) {
        const double mm = px * mmPerPixel_;
        text = (unit_ == LengthUnit::Centimeters)
                   ? QStringLiteral("%1 cm").arg(mm / 10.0, 0, 'f', 2)
                   : QStringLiteral("%1 mm").arg(mm, 0, 'f', 1);
    } else {
        text = QStringLiteral("%1 px").arg(px, 0, 'f', 0);
    }
    return (signPrefix && px > 0.0) ? QStringLiteral("+") + text : text;
}

void EditorCanvas::setContourReport(bool visible, const vision::ContourReport& report) {
    contourVisible_ = visible && report.valid;
    contourReport_ = report;
    update();
}

QStringList EditorCanvas::contourSummaryLines() const {
    QStringList lines;
    if (!contourReport_.valid) {
        return lines;
    }
    const auto& report = contourReport_;
    lines << tr("Perímetro: %1").arg(boardValueText(report.perimeter, false));

    // El área se convierte con el CUADRADO de la escala; hacerlo con
    // boardValueText (que es lineal) daría un número plausible y falso.
    if (mmPerPixel_ > 0.0 && unit_ != LengthUnit::Pixels) {
        const double mm2 = report.area * mmPerPixel_ * mmPerPixel_;
        lines << (unit_ == LengthUnit::Centimeters
                      ? tr("Área: %1 cm²").arg(mm2 / 100.0, 0, 'f', 2)
                      : tr("Área: %1 mm²").arg(mm2, 0, 'f', 1));
    } else {
        lines << tr("Área: %1 px²").arg(report.area, 0, 'f', 0);
    }

    lines << tr("Agujeros: %1").arg(report.holes.size());
    lines << tr("Envolvente: %1 × %2")
                 .arg(boardValueText(std::max(report.minRect.size.width,
                                              report.minRect.size.height),
                                     false),
                      boardValueText(std::min(report.minRect.size.width,
                                              report.minRect.size.height),
                                     false));

    int arcs = 0;
    for (const auto& primitive : report.primitives) {
        if (primitive.kind == vision::PrimitiveKind::Arc) {
            ++arcs;
        }
    }
    lines << tr("Tramos: %1 rectas, %2 arcos")
                 .arg(report.primitives.size() - static_cast<std::size_t>(arcs))
                 .arg(arcs);
    return lines;
}

void EditorCanvas::setRulerVisible(bool visible) {
    rulerVisible_ = visible;
    setMouseTracking(visible || boardVisible_);
    if (!visible && !boardVisible_) {
        cursorWidget_.reset();
    }
    update();
}

void EditorCanvas::setBoardConfig(const vision::BoardConfig& config) {
    boardConfig_ = config;
    update();
}

void EditorCanvas::setPieceBoundsCenter(bool known, const cv::Point2f& center) {
    hasBoundsCenter_ = known;
    boundsCenter_ = center;
    if (boardVisible_) {
        update();
    }
}

vision::BoardFrame EditorCanvas::boardFrame() const {
    // Con imagen fija (editor) el fixture siempre es válido; en vivo depende de
    // que la pieza se esté detectando ahora mismo.
    const bool pieceFound = hasFixture_ && (!liveMode_ || pieceVisible_);
    return vision::resolveBoardFrame(boardConfig_, fixture_, pieceFound,
                                     cv::Size(image_.width(), image_.height()),
                                     hasBoundsCenter_ ? &boundsCenter_ : nullptr);
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

// La aritmética de la vista vive en ViewTransform (canvas_geometry), que sí se
// puede probar sin ventana; el widget solo le pasa su estado actual. Antes
// estaba aquí dentro y era la parte del trazado sin red de pruebas.
ViewTransform EditorCanvas::view() const {
    return ViewTransform(cv::Size(image_.width(), image_.height()),
                         cv::Size(width(), height()), zoom_, cv::Point2d(pan_.x(), pan_.y()));
}

QRectF EditorCanvas::fitRect() const {
    const ViewRect fit = view().fitRect();
    return {fit.x, fit.y, fit.width, fit.height};
}

QRectF EditorCanvas::targetRect() const {
    const ViewRect target = view().targetRect();
    return {target.x, target.y, target.width, target.height};
}

QPointF EditorCanvas::clampedPan(const QPointF& pan) const {
    const cv::Point2d clamped = view().clampedPan(cv::Point2d(pan.x(), pan.y()));
    return {clamped.x, clamped.y};
}

void EditorCanvas::restoreCursor() {
    const bool aiming = createType_.has_value() || regionPick_ || freeZonePick_ ||
                        pickMode_ || brush_ != EdgeBrush::Off;
    setCursor(aiming ? Qt::CrossCursor : Qt::ArrowCursor);
}

void EditorCanvas::resetView() {
    zoom_ = 1.0;
    pan_ = QPointF();
    update();
    emit viewChanged();
}

void EditorCanvas::zoomIn() {
    zoomAt(QRectF(rect()).center(), kZoomStep);
}

void EditorCanvas::zoomOut() {
    zoomAt(QRectF(rect()).center(), 1.0 / kZoomStep);
}

void EditorCanvas::zoomToMin() {
    resetView();  // el mínimo es justo el encuadre ajustado a la ventana
}

void EditorCanvas::zoomToMax() {
    zoomAt(QRectF(rect()).center(), kMaxZoom / zoom_);
}

void EditorCanvas::zoomToActualPixels() {
    const QRectF fit = fitRect();
    if (fit.isEmpty()) {
        return;
    }
    // Zoom que hace coincidir el ancho pintado con el ancho real de la imagen.
    const double target = image_.width() / fit.width();
    zoomAt(QRectF(rect()).center(), target / zoom_);
}

double EditorCanvas::displayScale() const {
    if (image_.isNull()) {
        return 0.0;
    }
    const QRectF target = targetRect();
    if (target.isEmpty()) {
        return 0.0;
    }
    return target.width() / image_.width();
}

bool EditorCanvas::atMinZoom() const {
    return zoom_ <= kMinZoom + 1e-9;
}

bool EditorCanvas::atMaxZoom() const {
    return zoom_ >= kMaxZoom - 1e-9;
}

void EditorCanvas::zoomAt(const QPointF& widgetPos, double factor) {
    const double next = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    if (std::abs(next - zoom_) < 1e-9) {
        return;  // ya está en el tope
    }
    // Punto de imagen bajo el cursor ANTES de cambiar el zoom.
    const cv::Point2f anchor = widgetToImage(widgetPos);
    zoom_ = next;
    if (zoom_ <= 1.0) {
        // Al volver al ajuste (o menos) la imagen se recentra: es lo que el
        // operador espera y evita quedarse con la vista desplazada.
        pan_ = QPointF();
    } else {
        // Corrige el desplazamiento para que ese mismo punto siga bajo el
        // cursor: el contenido señalado no se mueve al hacer zoom.
        pan_ = clampedPan(pan_ + (widgetPos - imageToWidget(anchor)));
    }
    update();
    emit viewChanged();
}

void EditorCanvas::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;  // una muesca = 120
    if (std::abs(steps) < 1e-6) {
        return;
    }
    zoomAt(event->position(), std::pow(kZoomStep, steps));
    event->accept();
}

void EditorCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    // Doble clic = volver al encuadre completo: la salida rápida cuando uno se
    // pierde con el zoom.
    //
    // OJO (corregido tras la auditoría): NO basta con suponer que dibujar es
    // siempre por arrastre. El Blob poligonal, la Línea-Línea y el Ángulo se
    // construyen por CLICS sucesivos, y marcar el rasgo distintivo, el cero del
    // tablero o la zona de detección también son clics. En esos modos, un doble
    // clic añadía vértices y además reencuadraba la vista de golpe. Mientras hay
    // un modo de clic activo, el doble clic no toca la vista.
    //
    // Con la zona libre en marcha el doble clic tiene además un significado
    // propio: cerrar el polígono. Es el atajo que ya espera cualquiera que haya
    // dibujado un polígono en otro programa, y evita tener que acertar sobre el
    // primer vértice cuando queda lejos de donde acabó la mano.
    if (event->button() == Qt::LeftButton && freeZonePick_ && freeVertices_.size() >= 3) {
        finishFreeZone(freeVertices_);
        event->accept();
        return;
    }
    const bool clickDrivenMode =
        pickMode_ || regionPick_ || freeZonePick_ || createType_.has_value();
    if (event->button() == Qt::LeftButton && !image_.isNull() && !clickDrivenMode) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void EditorCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Al cambiar el tamaño cambia el encuadre ajustado y, con él, el porcentaje
    // visible aunque el zoom no se haya tocado.
    emit viewChanged();
}

QPointF EditorCanvas::imageToWidget(const cv::Point2f& p) const {
    const cv::Point2d widgetPoint = view().imageToWidget(p);
    return {widgetPoint.x, widgetPoint.y};
}

cv::Point2f EditorCanvas::widgetToImage(const QPointF& p) const {
    return view().widgetToImage(cv::Point2d(p.x(), p.y()));
}

cv::Point2f EditorCanvas::toImg(const cv::Point2f& piecePoint) const {
    return vision::toImageCoords(fixture_, piecePoint);
}

int EditorCanvas::hitTest(const cv::Point2f& p) const {
    if (tools_ == nullptr) {
        return -1;
    }
    // Zona de clic generosa, en píxeles de PANTALLA: es la distancia que ve el
    // operador, no la que mide la imagen (ver pickTolerance).
    constexpr double kThresholdOnScreen = 14.0;
    int best = -1;
    double bestDistance = pickTolerance(kThresholdOnScreen, displayScale());

    for (int i = 0; i < static_cast<int>(tools_->size()); ++i) {
        const auto& tool = (*tools_)[static_cast<std::size_t>(i)];
        if (tool.deleted) {
            continue;
        }
        if (const double d = distanceToGeometry(tool.geometry, fixture_, p);
            d < bestDistance) {
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
    // La manija se dibuja de 7×7 px de pantalla; se agarra desde algo más para
    // no exigir puntería, pero también en píxeles de pantalla, para que la zona
    // de agarre siga a lo que se ve con cualquier zoom.
    constexpr double kHandleRadiusOnScreen = 9.0;
    return pickHandle(tool.geometry, fixture_,
                      imagePoint, pickTolerance(kHandleRadiusOnScreen, displayScale()));
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
    // El alcance del imán es cuánto puede saltar el extremo respecto a donde se
    // soltó, así que se acota por lo que se ve: 14 px de imagen son 131 px de
    // PANTALLA al zoom máximo, y el extremo aterrizaba en un borde lejísimos.
    // Solo se recorta (nunca se amplía sobre el valor original) y con un suelo
    // para que la ventana de escaneo siga dando de sí para detectar el borde.
    constexpr float kReachOnScreen = 14.0F;
    constexpr float kMinReach = 6.0F;
    const auto reach = static_cast<float>(
        std::clamp(pickTolerance(kReachOnScreen, displayScale()),
                   static_cast<double>(kMinReach), static_cast<double>(kReachOnScreen)));
    const auto edges =
        detectEdges(dragGray_, cursor - u * reach, cursor + u * reach, 3.0F, 1);
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

    // Arrastrar la vista (Z2): botón central, o Ctrl + botón izquierdo para
    // ratones sin rueda pulsable. Funciona también con la edición bloqueada:
    // mirar de cerca no modifica nada.
    const bool panRequest =
        event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         (event->modifiers() & Qt::ControlModifier) != 0);
    if (panRequest) {
        panning_ = true;
        panStartWidget_ = event->position();
        panStartOffset_ = clampedPan(pan_);
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    const cv::Point2f pressPoint = widgetToImage(event->position());

    // Clic derecho trazando la zona libre: deshace el último vértice y, sin
    // vértices que deshacer, cancela. Es la salida del gesto — sin ella, un
    // trazo mal empezado solo se podía terminar mal.
    if (event->button() == Qt::RightButton && freeZonePick_) {
        if (!freeVertices_.empty()) {
            freeVertices_.pop_back();
            update();
        } else {
            setFreeZonePickMode(false);
            emit freeZoneCancelled();
        }
        return;
    }

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
    if (editingLocked_ && !regionPick_ && !freeZonePick_ && !pickMode_) {
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

    // Pincel de borde: pinta desde el primer clic y sigue mientras se arrastra.
    if (brush_ != EdgeBrush::Off) {
        painting_ = true;
        lastPaint_.reset();  // un trazo nuevo no se une al anterior
        paintAt(pressPoint);
        update();
        return;
    }

    // Zona libre: todavía no se sabe si esto es un trazo o un clic. Se decide al
    // mover —o al no moverse—, así que aquí solo se abre el gesto.
    if (freeZonePick_) {
        dragStart_ = pressPoint;
        dragCurrent_ = pressPoint;
        freePressWidget_ = event->position();
        freeDragging_ = true;
        freeLasso_ = false;
        freeTrace_.clear();
        freeTrace_.emplace_back(cvRound(pressPoint.x), cvRound(pressPoint.y));
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
    // Coordenadas bajo el cursor (T4). El seguimiento del ratón solo está
    // activo con el tablero encendido, así que fuera de ese modo esto no
    // añade repintados.
    // Trazando la zona libre a clics hace falta seguir el cursor aunque el
    // tablero esté apagado: sin eso no hay línea elástica, y marcar vértices a
    // ciegas es dibujar un polígono que solo se ve cuando ya está cerrado.
    if ((boardVisible_ || rulerVisible_ || freeZonePick_ || brush_ != EdgeBrush::Off) &&
        !image_.isNull()) {
        cursorWidget_ = event->position();
        update();
    }
    if (panning_) {
        pan_ = clampedPan(panStartOffset_ + (event->position() - panStartWidget_));
        update();
        return;
    }
    if (painting_) {
        paintAt(widgetToImage(event->position()));
        update();
        return;
    }
    if (!creating_ && !moving_ && !marquee_ && !regionDrag_ && !freeDragging_ &&
        !draggingHandle_) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());

    if (freeDragging_) {
        // Que esto sea un trazo o un clic se decide en píxeles de PANTALLA, no
        // de imagen: al 800 % de zoom, tres píxeles de mano son veinticuatro de
        // imagen, y el mismo gesto significaría dos cosas distintas según por
        // dónde se estuviera mirando.
        constexpr double kLassoStartsAt = 6.0;
        if (!freeLasso_ &&
            QLineF(freePressWidget_, event->position()).length() > kLassoStartsAt) {
            freeLasso_ = true;
        }
        if (freeLasso_) {
            const cv::Point q(cvRound(p.x), cvRound(p.y));
            // Un punto por píxel de imagen recorrido es todo lo que hay que
            // guardar: el ratón emite decenas de eventos sobre el mismo píxel y
            // el resto son copias.
            if (freeTrace_.empty() || q != freeTrace_.back()) {
                freeTrace_.push_back(q);
            }
        }
        dragCurrent_ = p;
        update();
        return;
    }

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

    // Distinguir un clic de un arrastre depende de cuánto movió la mano, no de
    // cuántos píxeles de imagen recorrió: en píxeles de pantalla, para que el
    // gesto signifique lo mismo con cualquier zoom.
    if (cv::norm(releasePoint - dragStart_) < pickTolerance(6.0, displayScale())) {
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
    if (panning_ &&
        (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        restoreCursor();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const cv::Point2f p = widgetToImage(event->position());

    if (painting_) {
        // Se emite al SOLTAR y no en cada punto del trazo: reanalizar la imagen
        // en cada píxel de la pincelada dejaría el pincel a tirones.
        painting_ = false;
        lastPaint_.reset();
        emit edgeCorrected(forcePiece_, forceBackground_);
        return;
    }

    if (freeDragging_) {
        freeDragging_ = false;
        if (freeLasso_) {
            finishFreeZone(freeTrace_);
            return;
        }
        // No se movió: fue un clic. O cierra el polígono, o pone un vértice.
        // La tolerancia del cierre también va en píxeles de pantalla, por lo
        // mismo que la del trazo.
        constexpr double kCloseOnScreen = 12.0;
        if (freeVertices_.size() >= 3 &&
            cv::norm(cv::Point2f(static_cast<float>(freeVertices_.front().x),
                                 static_cast<float>(freeVertices_.front().y)) -
                     p) < pickTolerance(kCloseOnScreen, displayScale())) {
            finishFreeZone(freeVertices_);
            return;
        }
        freeVertices_.emplace_back(cvRound(p.x), cvRound(p.y));
        update();
        return;
    }

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
        // El punto de cierre se dibuja de 4 px de pantalla, así que su zona de
        // clic también se mide en pantalla: con el zoom alto, 12 px de imagen
        // eran casi 50 en pantalla y cerraban el polígono al intentar poner un
        // vértice cerca del inicio.
        constexpr double kCloseOnScreen = 12.0;
        if (pendingPolygon_.size() >= 3 &&
            cv::norm(toImg(pendingPolygon_.front()) - p) <
                pickTolerance(kCloseOnScreen, displayScale())) {
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

    // Dos mínimos distintos, cada uno por su motivo:
    //
    //  - En píxeles de PANTALLA: separar un clic de un arrastre depende de
    //    cuánto movió la mano. Por debajo fue un clic y se ignora sin más.
    //  - En píxeles de IMAGEN: una herramienta más corta que esto no tiene
    //    muestras suficientes para medir nada (el perfil del calíper, los rayos
    //    del círculo). El gesto fue intencionado, así que aquí NO se calla: se
    //    avisa, que es lo que faltaba.
    constexpr double kMinDragOnScreen = 8.0;
    constexpr double kMinTraceInImage = 8.0;
    if (!createType_.has_value()) {
        snapImg_.reset();
        return;
    }
    const double traced = cv::norm(p - dragStart_);
    if (traced < pickTolerance(kMinDragOnScreen, displayScale())) {
        snapImg_.reset();
        return;  // fue un clic, no un trazo
    }
    if (traced < kMinTraceInImage) {
        snapImg_.reset();
        emit traceRejected(tr("Trazo demasiado corto para medir (%1 px de imagen; "
                              "hacen falta %2). Aléjate con el zoom o traza más largo.")
                               .arg(traced, 0, 'f', 1)
                               .arg(kMinTraceInImage, 0, 'f', 0));
        return;
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

    // Arco: primer trazo = los dos extremos; segundo = por dónde pasa.
    if (*createType_ == ToolType::Arc) {
        if (!pendingArc_.has_value()) {
            pendingArc_ = std::array<cv::Point2f, 2>{a, b};
            update();
            return;
        }
        ArcGeometry g;
        g.start = (*pendingArc_)[0];
        g.end = (*pendingArc_)[1];
        g.mid = b;
        pendingArc_.reset();
        emit toolCreated(ToolGeometry(g));
        return;
    }

    ToolGeometry geometry = CaliperGeometry{};
    switch (*createType_) {
        case ToolType::Caliper:
            geometry = CaliperGeometry{a, b, 10.0F};
            break;
        case ToolType::Roundness: {
            RoundnessGeometry g;
            g.center = a;
            g.radius = static_cast<float>(cv::norm(b - a));
            g.searchBand = std::min(12.0F, g.radius / 2.0F);
            geometry = g;
            break;
        }
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
        case ToolType::EdgeDefects:
            geometry = EdgeDefectsGeometry{a, b, 16.0F, 60, 1.5F, true};
            break;
        case ToolType::Straightness:
            geometry = StraightnessGeometry{a, b, 16.0F, 60};
            break;
        case ToolType::Orientation:
            geometry = OrientationGeometry{a, b, 16.0F, 60, 0.0F};
            break;
        case ToolType::Ruler:
            geometry = RulerGeometry{a, b};
            break;
        case ToolType::Arc:
            // Se construye en dos trazos, resueltos más arriba; aquí no llega.
            return;
        case ToolType::Gear: {
            GearGeometry g;
            g.center = a;
            // El arrastre marca la punta del diente; la raíz se estima dentro y
            // el operador la afina con la manija si hace falta.
            g.outerRadius = std::max(12.0F, static_cast<float>(cv::norm(b - a)));
            g.innerRadius = g.outerRadius * 0.75F;
            geometry = g;
            break;
        }
        case ToolType::Thread: {
            ThreadGeometry g;
            g.axisFrom = a;
            g.axisTo = b;
            g.searchBand = std::max(15.0F, static_cast<float>(cv::norm(b - a)) * 0.4F);
            geometry = g;
            break;
        }
        case ToolType::MedianAxis: {
            MedianAxisGeometry g;
            g.axisFrom = a;
            g.axisTo = b;
            // Mismo criterio que el Eje torneado: la banda sale del propio
            // trazo, porque un alcance fijo falla igual en una pieza fina que
            // en una gruesa.
            g.searchBand = std::max(15.0F, static_cast<float>(cv::norm(b - a)) * 0.4F);
            geometry = g;
            break;
        }
        case ToolType::Groove: {
            // Mismo gesto que el Eje: se traza el eje pasando por la ranura.
            GrooveGeometry g;
            g.axisFrom = a;
            g.axisTo = b;
            g.searchBand = std::max(15.0F, static_cast<float>(cv::norm(b - a)) * 0.4F);
            geometry = g;
            break;
        }
        case ToolType::Shaft: {
            ShaftGeometry g;
            g.axisFrom = a;
            g.axisTo = b;
            // La banda por defecto se saca del propio trazo: buscar el borde a
            // 60 px fijos falla tanto en un eje fino como en uno grueso.
            g.searchBand = std::max(15.0F, static_cast<float>(cv::norm(b - a)) * 0.4F);
            geometry = g;
            break;
        }
        case ToolType::Position:
            // Un solo punto: se marca donde empieza el trazo (el arrastre solo
            // sirve para confirmar; el extremo no aporta nada aquí).
            geometry = PositionGeometry{a, PositionAxis::Radial};
            break;
        case ToolType::CentreOffset:
            geometry = CentreOffsetGeometry{a};
            break;
        case ToolType::ConstructedPoint:
            // El clic solo elige DÓNDE se escribe el resultado; lo que se
            // calcula lo deciden las referencias, que se eligen después en el
            // panel. Nace con la construcción más simple de entender.
            geometry = ConstructedPointGeometry{PointConstruction::Midpoint, a};
            break;
        case ToolType::ConstructedLine:
            geometry = ConstructedLineGeometry{LineConstruction::ThroughTwoPoints, a};
            break;
        case ToolType::Fillet: {
            FilletGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(30.0F, std::abs(b.x - a.x));
            g.height = std::max(30.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Chamfer: {
            ChamferGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(30.0F, std::abs(b.x - a.x));
            g.height = std::max(30.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Extremes: {
            ExtremesGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(30.0F, std::abs(b.x - a.x));
            g.height = std::max(30.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::BoltPattern: {
            BoltPatternGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(40.0F, std::abs(b.x - a.x));
            g.height = std::max(40.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Clearance: {
            ClearanceGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(20.0F, std::abs(b.x - a.x));
            g.height = std::max(20.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Polygon: {
            PolygonGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(20.0F, std::abs(b.x - a.x));
            g.height = std::max(20.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Symmetry: {
            SymmetryGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(20.0F, std::abs(b.x - a.x));
            g.height = std::max(20.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Region: {
            RegionGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(20.0F, std::abs(b.x - a.x));
            g.height = std::max(20.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Blob: {
            BlobGeometry g;
            g.center = (a + b) / 2.0F;
            g.width = std::max(10.0F, std::abs(b.x - a.x));
            g.height = std::max(10.0F, std::abs(b.y - a.y));
            geometry = g;
            break;
        }
        case ToolType::Profile:
            // El nominal no se dibuja: lo captura la ventana del contorno de la
            // pieza que hay delante. Aquí no hay nada que trazar.
            return;
        case ToolType::LineToLine:
        case ToolType::Angle:
        case ToolType::PolyBlob:
            return;  // gestionado arriba (creación en varios pasos)
    }
    emit toolCreated(geometry);
}

// Flechas de «quién usa a quién» (X0/X1/X2). Una herramienta que mide contra un
// datum es invisible sin esto: en el lienzo se ve una recta construida y las dos
// rectas de las que sale, sin nada que las relacione, y borrar la equivocada
// rompe la medida sin que nada lo hubiera avisado.
//
// Van DEBAJO de las herramientas y en trazo fino: son estructura, no medida, y
// no pueden competir con lo que se está midiendo.
void EditorCanvas::paintDependencies(QPainter& painter) const {
    if (tools_ == nullptr) {
        return;
    }
    // Punto por el que se agarra visualmente una herramienta: el promedio de sus
    // puntos de referencia, que es lo más parecido a "dónde está" sin tener que
    // decidirlo tipo por tipo.
    const auto anchorOf = [this](const EditedTool& tool) {
        const auto points = referencePoints(tool.geometry);
        cv::Point2f sum(0.0F, 0.0F);
        for (const auto& p : points) {
            sum += p;
        }
        const cv::Point2f mean =
            points.empty() ? sum : sum / static_cast<float>(points.size());
        return imageToWidget(toImg(mean));
    };
    const auto findByName = [this](const std::string& name) -> const EditedTool* {
        for (const auto& tool : *tools_) {
            if (!tool.deleted && tool.config.name == name) {
                return &tool;
            }
        }
        return nullptr;
    };

    QPen pen(QColor(150, 255, 255, 130));
    pen.setStyle(Qt::DotLine);
    pen.setWidthF(1.2);
    pen.setCosmetic(true);
    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const auto& tool : *tools_) {
        if (tool.deleted) {
            continue;
        }
        for (const std::string* name : {&tool.config.reference, &tool.config.reference2}) {
            if (name->empty()) {
                continue;
            }
            const EditedTool* source = findByName(*name);
            // Una referencia rota no se dibuja: no hay a dónde llevar la flecha,
            // y el motivo se lo dirá la medición, que es donde importa.
            if (source == nullptr || source == &tool) {
                continue;
            }
            const QPointF from = anchorOf(*source);
            const QPointF to = anchorOf(tool);
            const QPointF delta = to - from;
            const double length = std::hypot(delta.x(), delta.y());
            if (length < 12.0) {
                continue;  // pegadas: la flecha sería un borrón
            }
            const QPointF unit = delta / length;
            const QPointF normal(-unit.y(), unit.x());
            // Se corta antes de llegar para no tapar la herramienta de destino.
            const QPointF tip = to - unit * 10.0;
            painter.drawLine(from + unit * 10.0, tip);
            // Punta de flecha en el extremo del que USA la referencia: el
            // sentido es "de dónde sale el dato" hacia "quién lo consume".
            painter.drawLine(tip, tip - unit * 7.0 + normal * 3.5);
            painter.drawLine(tip, tip - unit * 7.0 - normal * 3.5);
        }
    }
    painter.restore();
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
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, EdgeDefectsGeometry> ||
                          std::is_same_v<T, StraightnessGeometry> ||
                          std::is_same_v<T, OrientationGeometry>) {
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
            } else if constexpr (std::is_same_v<T, CircleGeometry> ||
                                 std::is_same_v<T, RoundnessGeometry>) {
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
            } else if constexpr (std::is_same_v<T, BlobGeometry> ||
                                 std::is_same_v<T, RegionGeometry> ||
                                 std::is_same_v<T, SymmetryGeometry> ||
                                 std::is_same_v<T, PolygonGeometry> ||
                                 std::is_same_v<T, ClearanceGeometry> ||
                                 std::is_same_v<T, BoltPatternGeometry> ||
                                 std::is_same_v<T, ExtremesGeometry> ||
                                 std::is_same_v<T, ChamferGeometry> ||
                                 std::is_same_v<T, FilletGeometry>) {
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
            } else if constexpr (std::is_same_v<T, ProfileGeometry>) {
                // El nominal, a trazo fino: es lo que la pieza DEBERÍA ser, así
                // que no puede competir visualmente con lo que se está midiendo.
                QPolygonF poly;
                for (const auto& v : g.nominal) {
                    poly << imageToWidget(toImg(v));
                }
                if (!poly.isEmpty()) {
                    QPen ghost = painter.pen();
                    ghost.setStyle(Qt::DashLine);
                    ghost.setWidthF(1.2);
                    painter.setPen(ghost);
                    painter.drawPolygon(poly);
                    labelPos = poly.boundingRect().topLeft() + QPointF(2, -4);
                }
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                QPolygonF poly;
                for (const auto& vtx : g.vertices) {
                    poly << imageToWidget(toImg(vtx));
                }
                painter.drawPolygon(poly);
                labelPos = poly.boundingRect().topLeft() + QPointF(2, -4);
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                // Diana sobre el rasgo vigilado y línea punteada hasta el cero
                // del tablero: se ve de un vistazo QUÉ desviación se está
                // midiendo y respecto a qué origen.
                const QPointF p = imageToWidget(toImg(g.point));
                painter.drawEllipse(p, 7.0, 7.0);
                painter.drawLine(p + QPointF(-11, 0), p + QPointF(11, 0));
                painter.drawLine(p + QPointF(0, -11), p + QPointF(0, 11));
                if (boardVisible_) {
                    const vision::BoardFrame board = boardFrame();
                    const QPointF zero = imageToWidget(board.origin);
                    QPen link = painter.pen();
                    link.setStyle(Qt::DashLine);
                    link.setWidthF(1.2);
                    painter.setPen(link);
                    painter.drawLine(zero, p);
                }
                labelPos = p + QPointF(8, -10);
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                const cv::Point2f s0 = toImg(g.start);
                const cv::Point2f sm = toImg(g.mid);
                const cv::Point2f s1 = toImg(g.end);
                const vision::ArcSpan arc = vision::circleThroughThreePoints(s0, sm, s1);
                const QPointF w0 = imageToWidget(s0);
                const QPointF w1 = imageToWidget(s1);
                if (arc.valid) {
                    // Solo el TRAMO marcado, no la circunferencia entera: dibujar
                    // el aro completo haría creer que se mide todo el contorno.
                    const double scale = targetRect().width() / image_.width();
                    const QPointF c = imageToWidget(arc.center);
                    const double r = arc.radius * scale;
                    const QRectF box(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
                    // Qt mide los ángulos en 1/16 de grado y con +Y hacia ARRIBA,
                    // al revés que las coordenadas de imagen: de ahí los signos.
                    painter.drawArc(box, static_cast<int>(-arc.startAngleDeg * 16.0),
                                    static_cast<int>(-arc.sweepDeg * 16.0));
                    QPen radiusPen = painter.pen();
                    radiusPen.setStyle(Qt::DashLine);
                    const QPen solid = painter.pen();
                    painter.setPen(radiusPen);
                    painter.drawLine(c, imageToWidget(sm));  // el radio, a la vista
                    painter.setPen(solid);
                    labelPos = imageToWidget(sm);
                } else {
                    // Tres puntos alineados: se dibuja lo trazado para que se
                    // pueda corregir en vez de desaparecer.
                    painter.drawLine(w0, w1);
                    labelPos = (w0 + w1) / 2.0;
                }
                painter.drawEllipse(w0, 3.0, 3.0);
                painter.drawEllipse(w1, 3.0, 3.0);
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry> ||
                                 std::is_same_v<T, GrooveGeometry> ||
                                 std::is_same_v<T, MedianAxisGeometry>) {
                // El eje trazado y, a rayas, hasta dónde busca el borde a cada
                // lado. Sin la banda, un "no encuentro bordes" no se entiende:
                // el operador no ve que su alcance se queda corto.
                const cv::Point2f from = toImg(g.axisFrom);
                const cv::Point2f to = toImg(g.axisTo);
                const QPointF a = imageToWidget(from);
                const QPointF b = imageToWidget(to);
                painter.drawLine(a, b);
                painter.drawEllipse(a, 3.0, 3.0);
                painter.drawEllipse(b, 3.0, 3.0);
                const cv::Point2f delta = to - from;
                const float length = static_cast<float>(cv::norm(delta));
                if (length > 1.0F) {
                    const cv::Point2f u = delta / length;
                    const cv::Point2f n(-u.y * g.searchBand, u.x * g.searchBand);
                    QPen dashed = painter.pen();
                    dashed.setStyle(Qt::DashLine);
                    dashed.setWidthF(1.0);
                    painter.save();
                    painter.setPen(dashed);
                    painter.drawLine(imageToWidget(from + n), imageToWidget(to + n));
                    painter.drawLine(imageToWidget(from - n), imageToWidget(to - n));
                    painter.restore();
                }
                labelPos = (a + b) / 2.0;
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                // Los dos aros entre los que se buscan los dientes.
                const QPointF c = imageToWidget(toImg(g.center));
                const double scale = displayScale();
                painter.drawEllipse(c, g.innerRadius * scale, g.innerRadius * scale);
                painter.drawEllipse(c, g.outerRadius * scale, g.outerRadius * scale);
                painter.drawLine(c + QPointF(-4, 0), c + QPointF(4, 0));
                painter.drawLine(c + QPointF(0, -4), c + QPointF(0, 4));
                labelPos = c + QPointF(0, -g.outerRadius * scale);
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry> ||
                                 std::is_same_v<T, ConstructedLineGeometry> ||
                                 std::is_same_v<T, CentreOffsetGeometry>) {
                // Aquí solo se puede dibujar el ANCLA. El elemento construido
                // depende de las referencias y no existe hasta que se mide: lo
                // pinta paintResults con el resultado. Se marca con trazo
                // discontinuo justamente para que no se confunda con una
                // herramienta trazada, que sí está donde se ve.
                const QPointF p = imageToWidget(toImg(g.anchor));
                QPen ghost = painter.pen();
                ghost.setStyle(Qt::DashLine);
                painter.setPen(ghost);
                painter.drawEllipse(p, 8.0, 8.0);
                painter.setPen(QPen(color, painter.pen().widthF()));
                if constexpr (std::is_same_v<T, ConstructedPointGeometry> ||
                              std::is_same_v<T, CentreOffsetGeometry>) {
                    painter.setBrush(color);
                    painter.drawEllipse(p, 2.5, 2.5);
                    painter.setBrush(Qt::NoBrush);
                } else {
                    painter.drawLine(p + QPointF(-8, 5), p + QPointF(8, -5));
                }
                labelPos = p + QPointF(0, -10);
            } else {
                // TRAMPA SILENCIOSA cerrada: esta cadena tampoco tenía `else`.
                // Eje, Rosca y Engranaje llevaban desde su entrega SIN rama de
                // dibujo: solo se veían cuando ya habían medido, y antes de eso
                // eran invisibles en el lienzo. Nada avisó.
                static_assert(alwaysFalse<T>,
                              "Falta el caso de esta geometría en paintTool: la "
                              "herramienta sería invisible en el lienzo.");
            }
        },
        tool.geometry);

    // El nombre solo se pinta si NO hay resultado para esta herramienta: cuando
    // lo hay, paintResults dibuja "nombre: medida" en el mismo sitio y las dos
    // etiquetas quedaban una encima de otra, ilegibles.
    if (!hasResultFor(tool.config)) {
        painter.setPen(selected ? Qt::white : color);
        painter.drawText(labelPos + QPointF(6, -4),
                         QString::fromStdString(tool.config.name));
    }

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

bool EditorCanvas::hasResultFor(const ToolConfig& config) const {
    return std::any_of(results_.begin(), results_.end(), [&config](const ToolRunResult& r) {
        return r.toolId == config.id && r.name == config.name;
    });
}

QString EditorCanvas::measureText(const ToolRunResult& result) const {
    // Quién decide la unidad NO es esta pantalla. Antes lo hacía, y la regla que
    // aplicaba —«todo lo que no sea un ángulo va en milímetros»— pintaba un
    // hexágono como «Lados (6): 6,00 mm» y un área en px² multiplicada por la
    // escala lineal. La lista de excepciones nunca se acababa, porque el
    // problema no era la lista: era preguntárselo al TIPO de herramienta en vez
    // de a la medida.
    if (result.kind != MeasuredKind::Length) {
        return QString::fromStdString(formatMeasure(result, mmPerPixel_, unit_, true));
    }
    if (mmPerPixel_ > 0.0 && unit_ != LengthUnit::Pixels) {
        const double mm = result.measured * mmPerPixel_;
        const bool useCm =
            unit_ == LengthUnit::Centimeters || (unit_ == LengthUnit::Auto && mm >= 100.0);
        return useCm ? QStringLiteral("%1 cm").arg(mm / 10.0, 0, 'f', 2)
                     : QStringLiteral("%1 mm").arg(mm, 0, 'f', 2);
    }
    return QStringLiteral("%1 px").arg(result.measured, 0, 'f', 1);
}

void EditorCanvas::paintResults(QPainter& painter) const {
    painter.save();
    QFont measureFont = painter.font();
    measureFont.setBold(true);
    painter.setFont(measureFont);

    // Rectángulos ya ocupados por otras etiquetas: sin esto, dos herramientas
    // cercanas escriben una encima de otra y no se lee ninguna medida.
    std::vector<ViewRect> taken;
    const ViewRect visible{0.0, 0.0, static_cast<double>(width()),
                           static_cast<double>(height())};
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

        // Ancla de la etiqueta: el ÚLTIMO punto del overlay, que es el que
        // pertenece a la herramienta. Con el primero, la herramienta Posición
        // (cuyo overlay empieza en el cero del tablero) apilaba todas sus
        // etiquetas sobre el mismo punto.
        QPointF labelPos;
        if (!result.overlayPoints.empty()) {
            labelPos = imageToWidget(result.overlayPoints.back());
        } else if (!result.overlaySegments.empty()) {
            labelPos = imageToWidget(result.overlaySegments.front()[0]);
        } else {
            continue;
        }

        // Las marcas de todas las piezas se ven; el texto solo de la enfocada.
        // Con seis piezas y cinco herramientas serían treinta etiquetas encima
        // del vídeo, y eso no se lee: se tapa la pieza y no se entiende nada.
        if (focusedPiece_ >= 0 && result.pieceIndex != focusedPiece_) {
            continue;
        }

        const QString text = QString::fromStdString(result.name) + QStringLiteral(": ") +
                             measureText(result);
        const QFontMetricsF metrics(painter.font());
        const QRectF preferred = metrics.boundingRect(text).adjusted(-4, -2, 4, 2)
                                     .translated(labelPos + QPointF(8, -10));
        const ViewRect placed = placeLabel(
            {preferred.x(), preferred.y(), preferred.width(), preferred.height()}, taken,
            visible);
        taken.push_back(placed);
        const QRectF box(placed.x, placed.y, placed.width, placed.height);

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
    if (pendingArc_.has_value()) {
        // Los dos extremos ya fijados, a la espera del punto intermedio.
        const QPointF s0 = imageToWidget(toImg((*pendingArc_)[0]));
        const QPointF s1 = imageToWidget(toImg((*pendingArc_)[1]));
        painter.drawLine(s0, s1);
        painter.drawEllipse(s0, 4.0, 4.0);
        painter.drawEllipse(s1, 4.0, 4.0);
    }
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

void EditorCanvas::paintBoard(QPainter& painter) const {
    if (!boardVisible_ || image_.isNull()) {
        return;
    }
    const QRectF view = QRectF(rect()).intersected(targetRect());
    if (view.isEmpty()) {
        return;
    }
    const vision::BoardFrame frame = boardFrame();

    // Extensión visible en coordenadas del tablero (px de imagen): basta con
    // las cuatro esquinas de la vista, aunque los ejes estén girados.
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    bool first = true;
    for (const QPointF& corner : {view.topLeft(), view.topRight(), view.bottomLeft(),
                                  view.bottomRight()}) {
        const vision::BoardReading r = vision::readPoint(frame, widgetToImage(corner));
        if (first) {
            minX = maxX = r.dx;
            minY = maxY = r.dy;
            first = false;
            continue;
        }
        minX = std::min(minX, r.dx);
        maxX = std::max(maxX, r.dx);
        minY = std::min(minY, r.dy);
        maxY = std::max(maxY, r.dy);
    }

    // Paso adaptativo: se elige en la unidad activa (mm si hay escala, px si
    // no) para que las etiquetas sean números redondos, apuntando a una línea
    // cada ~90 px de pantalla.
    const double unitsPerPx = mmPerPixel_ > 0.0 ? mmPerPixel_ : 1.0;
    const int divisions =
        std::max(2, static_cast<int>(std::lround(std::min(view.width(), view.height()) / 90.0)));
    const double spanUnits = std::max(maxX - minX, maxY - minY) * unitsPerPx;
    const double stepUnits = vision::niceGridStep(spanUnits, divisions);
    const double stepPx = stepUnits / unitsPerPx;
    if (!(stepPx > 0.0) || !std::isfinite(stepPx)) {
        return;
    }
    const double linesX = (maxX - minX) / stepPx;
    const double linesY = (maxY - minY) / stepPx;
    if (linesX > 500.0 || linesY > 500.0) {
        return;  // salvaguarda: nunca pintar una grilla ilegible
    }

    painter.save();
    painter.setClipRect(view);
    QFont font = painter.font();
    font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1.0));
    painter.setFont(font);

    QPen gridPen(QColor(120, 200, 255, 60));
    gridPen.setCosmetic(true);
    QPen axisPen(QColor(0, 220, 255, 200));
    axisPen.setWidthF(1.6);
    axisPen.setCosmetic(true);

    // Etiqueta compacta: la de las herramientas (formatLength) añade el
    // equivalente en px entre paréntesis y aquí satura la grilla.
    const auto tickLabel = [this](double valuePx) { return boardValueText(valuePx, true); };
    // Posición de los ejes en pantalla, para colgar de ellos las etiquetas; si
    // el eje queda fuera de la vista, se pegan al borde.
    const QPointF originWidget = imageToWidget(vision::toImagePoint(frame, 0.0, 0.0));
    const double labelY = std::clamp(originWidget.y() + 12.0, view.top() + 12.0, view.bottom() - 3.0);
    const double labelX = std::clamp(originWidget.x() + 5.0, view.left() + 3.0, view.right() - 55.0);

    // Líneas verticales (dx constante) y horizontales (dy constante).
    for (int axis = 0; axis < 2; ++axis) {
        const double lo = (axis == 0) ? minX : minY;
        const double hi = (axis == 0) ? maxX : maxY;
        const double otherLo = (axis == 0) ? minY : minX;
        const double otherHi = (axis == 0) ? maxY : maxX;
        const long long kFirst = static_cast<long long>(std::ceil(lo / stepPx));
        const long long kLast = static_cast<long long>(std::floor(hi / stepPx));
        for (long long k = kFirst; k <= kLast; ++k) {
            const double value = static_cast<double>(k) * stepPx;
            const bool isAxis = (k == 0);
            const QPointF a = imageToWidget(axis == 0 ? vision::toImagePoint(frame, value, otherLo)
                                                      : vision::toImagePoint(frame, otherLo, value));
            const QPointF b = imageToWidget(axis == 0 ? vision::toImagePoint(frame, value, otherHi)
                                                      : vision::toImagePoint(frame, otherHi, value));
            painter.setPen(isAxis ? axisPen : gridPen);
            painter.drawLine(a, b);
            if (isAxis) {
                continue;  // el 0 se rotula en el origen
            }
            // La etiqueta cuelga del eje correspondiente (o del borde si el eje
            // quedó fuera de la vista); el valor va en px de imagen y
            // formatLength lo pasa a la unidad activa.
            painter.setPen(QColor(170, 220, 255, 200));
            if (axis == 0) {
                const double x = imageToWidget(vision::toImagePoint(frame, value, 0.0)).x();
                painter.drawText(QPointF(x + 3.0, labelY), tickLabel(value));
            } else {
                const double y = imageToWidget(vision::toImagePoint(frame, 0.0, value)).y();
                painter.drawText(QPointF(labelX, y - 3.0), tickLabel(value));
            }
        }
    }

    // Origen y cuadrante: +X a la derecha y +Y hacia arriba, como en metrología.
    painter.setPen(axisPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(originWidget, 4.0, 4.0);
    painter.setPen(QColor(0, 220, 255, 230));
    painter.drawText(originWidget + QPointF(6.0, 14.0), QStringLiteral("0"));
    // Marcas de cuadrante: se colocan donde cada semieje positivo abandona la
    // vista, así siguen visibles aunque los ejes estén girados. Si el origen
    // quedó fuera de la vista no se dibujan (no habría de dónde partir).
    if (view.contains(originWidget)) {
        const QRectF inset = view.adjusted(26.0, 22.0, -26.0, -10.0);
        const auto edgePoint = [&inset, &originWidget](const QPointF& direction) {
            double t = std::numeric_limits<double>::max();
            if (direction.x() > 1e-9) {
                t = std::min(t, (inset.right() - originWidget.x()) / direction.x());
            } else if (direction.x() < -1e-9) {
                t = std::min(t, (inset.left() - originWidget.x()) / direction.x());
            }
            if (direction.y() > 1e-9) {
                t = std::min(t, (inset.bottom() - originWidget.y()) / direction.y());
            } else if (direction.y() < -1e-9) {
                t = std::min(t, (inset.top() - originWidget.y()) / direction.y());
            }
            if (!std::isfinite(t) || t < 0.0) {
                t = 0.0;
            }
            return originWidget + direction * t;
        };
        const auto unitDir = [&originWidget](const QPointF& along) {
            const QPointF d = along - originWidget;
            const double len = std::hypot(d.x(), d.y());
            return len > 1e-9 ? QPointF(d.x() / len, d.y() / len) : QPointF(1.0, 0.0);
        };
        const QPointF dirX =
            unitDir(imageToWidget(vision::toImagePoint(frame, stepPx, 0.0)));
        const QPointF dirY =
            unitDir(imageToWidget(vision::toImagePoint(frame, 0.0, stepPx)));
        // Pequeño desplazamiento perpendicular para no chocar con la etiqueta
        // de la última división de cada eje.
        painter.drawText(edgePoint(dirX) + QPointF(0.0, -8.0), QStringLiteral("+X"));
        painter.drawText(edgePoint(dirY) + QPointF(12.0, 0.0), QStringLiteral("+Y"));
    }

    // Lectura del punto bajo el cursor en el sistema centrado (T4).
    if (cursorWidget_.has_value() && view.contains(*cursorWidget_)) {
        const vision::BoardReading at = vision::readPoint(frame, widgetToImage(*cursorWidget_));
        const QString text = QStringLiteral("x %1   y %2")
                                 .arg(boardValueText(at.dx, true), boardValueText(at.dy, true));
        QRectF box = painter.fontMetrics().boundingRect(text).adjusted(-5.0, -3.0, 5.0, 3.0);
        box.moveTopLeft(*cursorWidget_ + QPointF(14.0, -box.height() - 8.0));
        // Si la caja se saldría de la vista, se voltea al otro lado del cursor.
        if (box.right() > view.right()) {
            box.moveRight(cursorWidget_->x() - 14.0);
        }
        if (box.top() < view.top()) {
            box.moveTop(cursorWidget_->y() + 16.0);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 34, 43, 210));
        painter.drawRoundedRect(box, 3.0, 3.0);
        painter.setPen(QColor(160, 225, 255));
        painter.drawText(box, Qt::AlignCenter, text);
        painter.setBrush(Qt::NoBrush);
    }

    painter.restore();
}


void EditorCanvas::paintContourReport(QPainter& painter) const {
    if (!contourVisible_ || !contourReport_.valid || image_.isNull()) {
        return;
    }
    constexpr double kPi = 3.14159265358979323846;
    const QColor lineColor(90, 180, 255);
    const QColor arcColor(255, 165, 40);
    const QColor holeColor(230, 110, 230);

    painter.save();
    const auto toWidget = [this](const cv::Point& p) {
        return imageToWidget(cv::Point2f(static_cast<float>(p.x), static_cast<float>(p.y)));
    };
    const auto polygonOf = [&toWidget](const std::vector<cv::Point>& points) {
        QPolygonF poly;
        poly.reserve(static_cast<int>(points.size()));
        for (const auto& p : points) {
            poly << toWidget(p);
        }
        return poly;
    };

    // El contorno crudo va DEBAJO y en blanco tenue: es la referencia contra la
    // que se lee la descomposición. Sin él, un arco mal ajustado se ve como un
    // arco perfecto y nadie nota que no sigue a la pieza.
    QPen rawPen(QColor(255, 255, 255, 110));
    rawPen.setWidthF(1.0);
    rawPen.setCosmetic(true);
    painter.setPen(rawPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygonOf(contourReport_.outer));

    QPen holePen(holeColor);
    holePen.setWidthF(1.8);
    holePen.setCosmetic(true);
    painter.setPen(holePen);
    for (const auto& hole : contourReport_.holes) {
        painter.drawPolygon(polygonOf(hole));
    }

    QFont labelFont = painter.font();
    labelFont.setBold(true);
    painter.setFont(labelFont);

    for (const auto& primitive : contourReport_.primitives) {
        QPen pen(primitive.kind == vision::PrimitiveKind::Arc ? arcColor : lineColor);
        pen.setWidthF(2.5);
        pen.setCosmetic(true);
        painter.setPen(pen);

        if (primitive.kind == vision::PrimitiveKind::Line) {
            painter.drawLine(imageToWidget(primitive.start), imageToWidget(primitive.end));
        } else {
            // El arco se dibuja a partir del círculo AJUSTADO, no de los puntos
            // del contorno: así se ve de un vistazo dónde se despega del borde.
            // El sentido lo decide el punto intermedio, que es lo único que
            // distingue el arco corto del largo entre los mismos extremos.
            const cv::Point2f c = primitive.center;
            const auto angleAt = [&c](const cv::Point2f& p) {
                return std::atan2(static_cast<double>(p.y - c.y),
                                  static_cast<double>(p.x - c.x));
            };
            const auto wrap = [](double a) {
                while (a < 0.0) {
                    a += 2.0 * kPi;
                }
                while (a >= 2.0 * kPi) {
                    a -= 2.0 * kPi;
                }
                return a;
            };
            const double a0 = angleAt(primitive.start);
            const double ccwSweep = wrap(angleAt(primitive.end) - a0);
            const bool ccw = wrap(angleAt(primitive.mid) - a0) <= ccwSweep;
            const double sweep = ccw ? ccwSweep : ccwSweep - 2.0 * kPi;
            const int steps =
                std::max(8, static_cast<int>(std::abs(sweep) * 180.0 / kPi / 3.0));
            QPolygonF arc;
            arc.reserve(steps + 1);
            for (int i = 0; i <= steps; ++i) {
                const double a = a0 + sweep * i / steps;
                arc << imageToWidget(cv::Point2f(
                    c.x + static_cast<float>(std::cos(a) * primitive.radius),
                    c.y + static_cast<float>(std::sin(a) * primitive.radius)));
            }
            painter.drawPolyline(arc);

            // El radio es el dato que se viene a buscar en un redondeo; los
            // tramos cortos no se etiquetan para no tapar la pieza de números.
            if (primitive.length > 20.0) {
                painter.drawText(imageToWidget(primitive.mid) + QPointF(6.0, -6.0),
                                 QStringLiteral("R %1").arg(
                                     boardValueText(primitive.radius, false)));
            }
        }

        // Punto de corte entre tramos: hace visible la descomposición aunque dos
        // tramos vecinos sean del mismo tipo.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(imageToWidget(primitive.start), 2.5, 2.5);
        painter.setBrush(Qt::NoBrush);
    }

    // Resumen abajo a la izquierda: arriba están la regla y el estado en vivo.
    const QStringList lines = contourSummaryLines();
    if (!lines.isEmpty()) {
        const QFontMetrics metrics(painter.font());
        int textWidth = 0;
        for (const auto& line : lines) {
            textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
        }
        const double lineHeight = metrics.height();
        const QRectF box(8.0, height() - 8.0 - lineHeight * lines.size() - 8.0,
                         textWidth + 16.0, lineHeight * lines.size() + 8.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRect(box);
        painter.setPen(QColor(235, 235, 235));
        for (int i = 0; i < lines.size(); ++i) {
            painter.drawText(QRectF(box.left() + 8.0, box.top() + 4.0 + lineHeight * i,
                                    box.width() - 16.0, lineHeight),
                             Qt::AlignVCenter | Qt::AlignLeft, lines[i]);
        }
    }
    painter.restore();
}

void EditorCanvas::paintRuler(QPainter& painter) const {
    if (!rulerVisible_ || image_.isNull()) {
        return;
    }
    const QRectF view = QRectF(rect()).intersected(targetRect());
    if (view.isEmpty()) {
        return;
    }

    // Las reglas miden en el MISMO sistema que el resto: si el tablero está
    // encendido, el 0 es su cero y la Y crece hacia arriba (convenio del
    // tablero); si está apagado, se miden coordenadas de imagen desde la
    // esquina, con la Y hacia abajo — que es lo que espera quien mira una regla
    // sin tablero (si no, todos los números de la regla vertical salían
    // negativos).
    const bool boardMode = boardVisible_;
    const vision::BoardFrame frame = boardMode ? boardFrame() : vision::BoardFrame{};
    const double unitsPerPx = mmPerPixel_ > 0.0 ? mmPerPixel_ : 1.0;
    const QString suffix = (mmPerPixel_ > 0.0 && unit_ != LengthUnit::Pixels)
                               ? (unit_ == LengthUnit::Centimeters ? QStringLiteral("cm")
                                                                   : QStringLiteral("mm"))
                               : QStringLiteral("px");
    const double unitScale =
        (suffix == QStringLiteral("cm")) ? unitsPerPx / 10.0 : unitsPerPx;

    constexpr double kBand = 18.0;  // grosor de la banda de la regla
    const QRectF top(view.left(), view.top(), view.width(), kBand);
    const QRectF left(view.left(), view.top(), kBand, view.height());

    painter.save();
    painter.setClipRect(view);
    QFont small = painter.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    small.setBold(false);
    painter.setFont(small);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 18, 18, 205));
    painter.drawRect(top);
    painter.drawRect(left);

    // Paso: el mismo criterio 1-2-5 del tablero, calculado sobre lo que se ve.
    // Conversión punto de imagen -> par de valores de la regla, y su inversa.
    const auto readAt = [this, boardMode, &frame](const cv::Point2f& imagePoint) {
        if (boardMode) {
            const vision::BoardReading r = vision::readPoint(frame, imagePoint);
            return QPointF(r.dx, r.dy);
        }
        return QPointF(imagePoint.x, imagePoint.y);
    };
    const auto pointAt = [this, boardMode, &frame](double vx, double vy) {
        if (boardMode) {
            return imageToWidget(vision::toImagePoint(frame, vx, vy));
        }
        return imageToWidget(cv::Point2f(static_cast<float>(vx), static_cast<float>(vy)));
    };

    const QPointF readA = readAt(widgetToImage(view.topLeft()));
    const QPointF readB = readAt(widgetToImage(view.bottomRight()));
    const double spanX = std::abs(readB.x() - readA.x()) * unitScale;
    const double spanY = std::abs(readB.y() - readA.y()) * unitScale;
    const int divisions =
        std::max(2, static_cast<int>(std::lround(std::min(view.width(), view.height()) / 80.0)));
    const double stepUnits = vision::niceGridStep(std::max(spanX, spanY), divisions);
    const double stepPx = stepUnits / (unitScale > 0.0 ? unitScale : 1.0);
    if (!(stepPx > 0.0) || !std::isfinite(stepPx)) {
        painter.restore();
        return;
    }

    QPen tickPen(QColor(200, 200, 200, 200));
    tickPen.setCosmetic(true);
    const QPen textPen(QColor(225, 225, 225));

    // Regla horizontal: se recorren los múltiplos del paso en X del sistema
    // activo y se marcan donde caen en pantalla.
    const double minDx = std::min(readA.x(), readB.x());
    const double maxDx = std::max(readA.x(), readB.x());
    const long long firstX = static_cast<long long>(std::ceil(minDx / stepPx));
    const long long lastX = static_cast<long long>(std::floor(maxDx / stepPx));
    if (lastX - firstX < 500) {
        for (long long k = firstX; k <= lastX; ++k) {
            const double value = static_cast<double>(k) * stepPx;
            const double x = pointAt(value, readA.y()).x();
            painter.setPen(tickPen);
            painter.drawLine(QPointF(x, top.top()), QPointF(x, top.bottom()));
            painter.setPen(textPen);
            painter.drawText(QPointF(x + 2.0, top.top() + 11.0),
                             QStringLiteral("%1").arg(value * unitScale, 0, 'f',
                                                      stepUnits < 1.0 ? 1 : 0));
        }
    }

    const double minDy = std::min(readA.y(), readB.y());
    const double maxDy = std::max(readA.y(), readB.y());
    const long long firstY = static_cast<long long>(std::ceil(minDy / stepPx));
    const long long lastY = static_cast<long long>(std::floor(maxDy / stepPx));
    if (lastY - firstY < 500) {
        for (long long k = firstY; k <= lastY; ++k) {
            const double value = static_cast<double>(k) * stepPx;
            const double y = pointAt(readA.x(), value).y();
            painter.setPen(tickPen);
            painter.drawLine(QPointF(left.left(), y), QPointF(left.right(), y));
            painter.save();
            painter.setPen(textPen);
            // Texto girado para que quepa en la banda vertical.
            painter.translate(left.left() + 12.0, y - 2.0);
            painter.rotate(-90.0);
            painter.drawText(QPointF(0.0, 0.0),
                             QStringLiteral("%1").arg(value * unitScale, 0, 'f',
                                                      stepUnits < 1.0 ? 1 : 0));
            painter.restore();
        }
    }

    // Barra de escala: un segmento rotulado con lo que mide de verdad. Es lo
    // que permite juzgar un tamaño de un vistazo, incluso en una captura.
    const QPointF zero = pointAt(0.0, 0.0);
    const QPointF oneStep = pointAt(stepPx, 0.0);
    const double barLength = std::abs(oneStep.x() - zero.x());
    if (barLength > 8.0 && barLength < view.width() * 0.8) {
        const double barY = view.bottom() - 14.0;
        const double barX = view.left() + kBand + 10.0;
        QPen barPen(QColor(255, 255, 255, 230));
        barPen.setWidthF(2.0);
        barPen.setCosmetic(true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 18, 18, 205));
        painter.drawRect(QRectF(barX - 6.0, barY - 14.0, barLength + 12.0, 22.0));
        painter.setPen(barPen);
        painter.drawLine(QPointF(barX, barY), QPointF(barX + barLength, barY));
        painter.drawLine(QPointF(barX, barY - 4.0), QPointF(barX, barY + 4.0));
        painter.drawLine(QPointF(barX + barLength, barY - 4.0),
                         QPointF(barX + barLength, barY + 4.0));
        painter.drawText(QPointF(barX, barY - 5.0),
                         QStringLiteral("%1 %2")
                             .arg(stepUnits, 0, 'f', stepUnits < 1.0 ? 2 : 0)
                             .arg(suffix));
    }

    // Marca de la posición del cursor sobre ambas reglas: ubica al operador sin
    // tener que leer números.
    if (cursorWidget_.has_value() && view.contains(*cursorWidget_)) {
        QPen cursorPen(QColor(255, 200, 0));
        cursorPen.setWidthF(1.5);
        cursorPen.setCosmetic(true);
        painter.setPen(cursorPen);
        painter.drawLine(QPointF(cursorWidget_->x(), top.top()),
                         QPointF(cursorWidget_->x(), top.bottom()));
        painter.drawLine(QPointF(left.left(), cursorWidget_->y()),
                         QPointF(left.right(), cursorWidget_->y()));
    }
    painter.restore();
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

    paintBoard(painter);  // por debajo de la pieza y de las herramientas
    paintLiveOverlay(painter);
    paintContourReport(painter);  // capa de consulta, siempre bajo las herramientas

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
        paintDependencies(painter);
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

    paintRuler(painter);

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
    paintFreeZone(painter);
    paintEdgeCorrection(painter);
}

// Lo que el operador ha marcado a mano, encima de la imagen.
//
// Sin esto, el pincel sería un gesto a ciegas: se pinta, la detección cambia, y
// no hay forma de saber QUÉ se marcó ni de encontrar la pincelada que sobra.
//
// Verde lo añadido y rojo lo quitado, translúcidos para no tapar el borde que
// se está corrigiendo — que es justo lo que hay que mirar mientras se pinta.
void EditorCanvas::paintEdgeCorrection(QPainter& painter) const {
    const auto tint = [&](const cv::Mat& mask, QColor colour) {
        if (mask.empty() || mask.type() != CV_8UC1 || image_.isNull()) {
            return;
        }
        if (mask.cols != image_.width() || mask.rows != image_.height()) {
            return;  // de otra resolución: se ignora, como en el pipeline
        }
        QImage layer(mask.cols, mask.rows, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        const QRgb rgba = qPremultiply(qRgba(colour.red(), colour.green(), colour.blue(), 90));
        for (int y = 0; y < mask.rows; ++y) {
            const uchar* row = mask.ptr<uchar>(y);
            auto* out = reinterpret_cast<QRgb*>(layer.scanLine(y));
            for (int x = 0; x < mask.cols; ++x) {
                if (row[x] != 0) {
                    out[x] = rgba;
                }
            }
        }
        painter.drawImage(targetRect(), layer);
    };
    tint(forcePiece_, QColor(0, 210, 90));
    tint(forceBackground_, QColor(230, 60, 60));

    // El tamaño del pincel bajo el cursor: sin verlo hay que pintar para
    // descubrir cuánto abarca, y eso ya es una pincelada que deshacer.
    if (brush_ != EdgeBrush::Off && cursorWidget_.has_value()) {
        QPen pen(brush_ == EdgeBrush::AddPiece ? QColor(0, 210, 90) : QColor(230, 60, 60));
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(*cursorWidget_, brushRadius_ * displayScale(),
                            brushRadius_ * displayScale());
    }
}

// La zona libre: la activa y la que se está trazando.
//
// Mismo ámbar que la rectangular, a propósito: son la misma idea —dónde mira el
// programa— y darles colores distintos las convertiría en dos cosas que hay que
// aprender por separado.
void EditorCanvas::paintFreeZone(QPainter& painter) const {
    const QColor zoneColor(255, 210, 0);
    const auto toWidget = [this](const cv::Point& point) {
        return imageToWidget(
            {static_cast<float>(point.x), static_cast<float>(point.y)});
    };

    if (freeZoneVisible_ && freeZone_.size() >= 3) {
        QPolygonF poly;
        for (const auto& point : freeZone_) {
            poly << toWidget(point);
        }
        // Además del contorno se APAGA lo de fuera. En un rectángulo el dentro y
        // el fuera se leen solos; en un contorno irregular no, y confundirlos es
        // creer que se está midiendo algo que el programa ni siquiera mira.
        QPainterPath outside;
        outside.addRect(QRectF(rect()));
        QPainterPath inside;
        inside.addPolygon(poly);
        painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 70));

        QPen pen(zoneColor);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(poly);
    }

    if (!freeZonePick_) {
        return;
    }
    QPen pen(zoneColor);
    pen.setWidthF(2.0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (freeLasso_ && freeTrace_.size() >= 2) {
        QPolygonF trace;
        for (const auto& point : freeTrace_) {
            trace << toWidget(point);
        }
        painter.drawPolyline(trace);
        // El cierre se insinúa desde el principio: la zona será el trazo MÁS
        // esta línea, y verla evita la sorpresa de un cierre por donde no se
        // esperaba.
        QPen closing(zoneColor);
        closing.setStyle(Qt::DotLine);
        closing.setCosmetic(true);
        painter.setPen(closing);
        painter.drawLine(trace.back(), trace.front());
        painter.setPen(pen);
    }

    if (!freeVertices_.empty()) {
        QPolygonF marks;
        for (const auto& point : freeVertices_) {
            marks << toWidget(point);
        }
        if (marks.size() >= 2) {
            painter.drawPolyline(marks);
        }
        // Línea elástica hasta el cursor: enseña el lado que se está a punto de
        // fijar antes de fijarlo.
        if (cursorWidget_.has_value()) {
            QPen rubber(zoneColor);
            rubber.setStyle(Qt::DotLine);
            rubber.setCosmetic(true);
            painter.setPen(rubber);
            painter.drawLine(marks.back(), *cursorWidget_);
            if (freeVertices_.size() >= 2) {
                painter.drawLine(*cursorWidget_, marks.front());
            }
            painter.setPen(pen);
        }
        painter.setBrush(zoneColor);
        for (const auto& mark : marks) {
            painter.drawEllipse(mark, 2.5, 2.5);
        }
        // El primero se resalta solo cuando cerrar es posible: un blanco verde
        // desde el primer vértice prometería un cierre que aún no existe.
        if (freeVertices_.size() >= 3) {
            painter.setBrush(QColor(0, 220, 0));
            painter.drawEllipse(marks.front(), 4.5, 4.5);
        }
        painter.setBrush(Qt::NoBrush);
    }
}

}  // namespace pci::inspection
