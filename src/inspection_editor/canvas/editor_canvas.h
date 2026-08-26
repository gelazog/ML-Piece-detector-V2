#pragma once

#include <QImage>
#include <QPointF>
#include <QStringList>
#include <QWidget>

#include <array>
#include <optional>
#include <vector>

#include "inspection_editor/canvas/canvas_geometry.h"
#include "inspection_editor/execution/tool_executor.h"
#include "vision/board_frame.h"
#include "vision/geometry_features.h"
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
    // Con varias piezas medidas (C6), cuál de ellas enseña sus cotas. -1 = solo
    // la principal. Catorce etiquetas por seis piezas es ruido, no información:
    // se dibujan las marcas de todas pero los números de una sola.
    void setFocusedPiece(int pieceIndex);
    [[nodiscard]] int focusedPiece() const { return focusedPiece_; }
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

    // --- zona LIBRE (contorno a mano alzada) ---
    // El mismo trabajo que la zona rectangular, sin la obligación de que sea un
    // rectángulo. Con el modo activo se dibuja de dos maneras, y las dos son el
    // mismo gesto: **arrastrando** se traza a pulso, y **a clics** se van
    // marcando vértices que se cierran haciendo clic sobre el primero (o con un
    // doble clic). Se ofrecen las dos porque resuelven casos distintos —el pulso
    // es rápido para rodear una mesa, los clics son exactos para seguir el borde
    // de un útil— y elegir por el operador habría estorbado a la mitad de ellos.
    //
    // El botón derecho deshace el último vértice; sin vértices, cancela.
    void setFreeZonePickMode(bool enabled);
    [[nodiscard]] bool freeZonePickMode() const { return freeZonePick_; }
    void setFreeZone(bool visible, const std::vector<cv::Point>& imagePolygon = {});

    // --- pincel de borde ---
    //
    // Corregir a mano dónde está el borde de la pieza cuando la detección se
    // equivoca: una sombra que se come un lado, un reflejo que parte la pieza.
    // Se pinta sobre la MÁSCARA, no sobre la imagen.
    //
    // Solo tiene sentido con una imagen QUIETA. En vídeo en vivo el contorno se
    // recalcula en cada frame, así que un borde corregido a mano sería mentira
    // en cuanto la pieza se moviera un píxel — quien lo encienda debe ofrecerlo
    // solo con foto o fichero.
    enum class EdgeBrush { Off, AddPiece, RemovePiece };

    // El modo y el TAMAÑO se ponen por separado, y esa separacion es el arreglo
    // de un fallo, no una preferencia de estilo.
    //
    // Antes esto era `setEdgeBrush(mode, radiusPx = 12)`, y la ventana lo
    // llamaba sin radio cada vez que se encendia el pincel. El valor por defecto
    // del parametro pisaba en silencio el tamaño que el operador acababa de
    // elegir con la rueda: se ajustaba el pincel, se apagaba, se volvia a
    // encender y estaba otra vez en 12. Con dos funciones eso ya no se puede
    // escribir.
    void setEdgeBrush(EdgeBrush mode);
    void setBrushRadius(int radiusPx);
    [[nodiscard]] EdgeBrush edgeBrush() const { return brush_; }
    [[nodiscard]] int brushRadius() const { return brushRadius_; }

    // --- Las tres ayudas del pincel ---
    //
    // Son TRES interruptores y no un modo con tres posiciones porque atacan
    // tres problemas distintos y se combinan: uno arregla la entrada, otro la
    // restringe y el tercero arregla la salida.
    //
    //   - Pulso estable  : el trazo deja de copiar el temblor de la mano.
    //   - Trazo recto    : el trazo va en linea recta del principio al final.
    //   - Ceñir al borde : el resultado sigue el contraste real de la imagen en
    //                      vez de tener el ancho del pincel.
    //
    // DE FABRICA: «pulso estable» encendido, los otros dos apagados. La razon
    // por la que no son iguales importa.
    //
    // El pulso estable solo FILTRA el temblor de la mano: no hay ninguna
    // pincelada que se pudiera dar antes y no se pueda dar ahora, asi que puede
    // venir puesto sin quitarle nada a nadie.
    //
    // «Ceñir al borde» si quita algo, y se descubrio al romper una prueba de
    // punta a punta que llevaba tiempo verde: con el ceñido puesto, el pincel ya
    // no puede forzar una zona que NO se parece a lo que se señalo —un rebaje
    // oscuro, una pestaña de poco contraste, un trozo que uno quiere incluir
    // aunque la imagen no lo respalde—. Eso es un cambio de lo que la
    // herramienta PUEDE hacer, no un afinado de como lo hace, y en este proyecto
    // esas cosas se eligen (la misma regla que tiene el subpixel en
    // `vision/pipeline.h`). Esta a un clic, en el menu del propio pincel, y el
    // programa dice en la barra de estado que ha hecho cada pincelada.
    void setBrushSteady(bool on);
    void setBrushStraight(bool on);
    void setBrushSnap(bool on);
    [[nodiscard]] bool brushSteady() const { return brushSteady_; }
    [[nodiscard]] bool brushStraight() const { return brushStraight_; }
    [[nodiscard]] bool brushSnap() const { return brushSnap_; }

    // LO QUE SE ESCRIBE SOBRE CADA COTA EN EL VÍDEO.
    //
    // Se publica para poder comprobarlo: lo que esta etiqueta dice es lo único
    // que el operador lee mientras trabaja, y hasta hace poco el veredicto iba
    // solo en el COLOR de la letra. Un daltónico no lo veía, en blanco y negro
    // desaparecía, y sobre mesa blanca el rojo quedaba en 2,21:1 de contraste.
    [[nodiscard]] QString overlayLabel(const ToolRunResult& result) const;

    // Deshacer y rehacer las pinceladas.
    //
    // Con PARCHES y no con instantáneas del frame entero: cada máscara mide lo
    // que la imagen, así que un paso completo son 4 MB a 1920x1080 y cincuenta
    // pasos serían doscientos megas. Una pincelada toca una zona pequeña, y es
    // esa zona —antes y después— lo único que hace falta guardar.
    bool undoEdgeCorrection();
    bool redoEdgeCorrection();
    [[nodiscard]] bool canUndoEdgeCorrection() const { return !undoSteps_.empty(); }
    [[nodiscard]] bool canRedoEdgeCorrection() const { return !redoSteps_.empty(); }

    // REALCE DE VISTA: estirar el contraste de lo que se PINTA, nada mas.
    //
    // Viene de «si la pieza es negra, y el demas cuadro es negro no se alcanza a
    // ver correctamente». Una pieza mate oscura sobre fondo oscuro ocupa treinta
    // niveles de gris de 256: en pantalla es una mancha negra dentro de otra.
    //
    // NO TOCA `image_`. Ya existe otra forma de subir el brillo —los controles de
    // la camara— y esa si cambia el fotograma que se analiza: mueve el umbral,
    // mueve la polaridad y mueve todas las cotas. Son dos cosas distintas y
    // confundirlas haria que las medidas se movieran por mirar.
    void setViewEnhance(bool on);
    [[nodiscard]] bool viewEnhance() const { return viewEnhance_; }
    // Si el realce esta encendido Y la imagen de ahora lo necesitaba. Sirve para
    // poder decir «esta imagen no hace falta realzarla» en vez de dejar al
    // operador dudando de si el interruptor funciona.
    [[nodiscard]] bool viewEnhanceActive() const { return viewEnhance_ && enhancedUseful_; }

    // Si la pincelada se PINTA sobre la imagen.
    //
    // El trazo es un gesto, no un resultado. Una vez que la corrección se ha
    // aplicado y el contorno se ha movido, dejar la mancha encima confunde las
    // dos cosas: el operador ya no sabe si lo que ve es lo que el programa
    // detecta o lo que él pintó. Se retira, y la corrección SIGUE EN VIGOR.
    void setEdgeCorrectionVisible(bool visible);
    [[nodiscard]] bool edgeCorrectionVisible() const { return showCorrection_; }
    // Cuántos píxeles hay marcados, para poder decir que la corrección sigue
    // puesta aunque ya no se vea.
    [[nodiscard]] int correctedPixelCount() const;
    // Las dos máscaras acumuladas, para enseñarlas y para deshacerlas.
    void setEdgeCorrection(const cv::Mat& forcePiece, const cv::Mat& forceBackground);
    void clearEdgeCorrection();

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
    // Hace falta para poder comprobar que lo guardado LLEGA aquí y no se queda
    // en la casilla del menú: son dos estados distintos y ahí cabía un fallo.
    [[nodiscard]] bool liveContourVisible() const { return showLiveContour_; }

    // EL CONTORNO DE TODAS LAS PIEZAS DEL ENCUADRE, en orden de lectura, y cuál
    // de ellas es la que se está midiendo (numerada desde 1; 0 = ninguna).
    //
    // Sin esto, el programa decía «6 piezas» y dibujaba una sola línea verde: el
    // operador no podía saber cuáles eran las otras cinco ni si estaban donde él
    // las veía. Y con el selector de pieza —que numera en orden de lectura— hace
    // falta además poder LEER ese número encima de cada una, o «pieza 3» no se
    // puede comprobar contra la mesa.
    //
    // Lista vacía = no se contaron; se dibuja solo la medida, como siempre.
    // `chosen` distingue «esta es la que te ha tocado por ser la mayor» de «esta
    // la has elegido tú». No es lo mismo para quien mira: si la ha elegido, esta
    // trabajando con ella y quiere verla destacada del resto; si le ha tocado,
    // destacarla seria afirmar una decision que nadie tomo.
    // Cuántas piezas está dibujando ahora mismo. Para poder comprobar desde
    // fuera que lo que se ve en pantalla son las que hay.
    [[nodiscard]] int livePieceCount() const {
        return static_cast<int>(livePieceOutlines_.size());
    }
    void setLivePieceOutlines(const std::vector<QPolygonF>& outlines, int measured,
                              bool chosen = false);
    // El contorno detectado que se está pintando. Existe para poder comprobar
    // desde fuera que una corrección del borde LLEGA a mover la línea verde:
    // sin esto, «se corrige» solo se podía verificar mirando la pantalla.
    [[nodiscard]] const QPolygonF& liveContour() const { return liveContour_; }
    // El tamaño de la imagen que se está mostrando, para poder reconstruir desde
    // fuera la misma transformación vista↔imagen que usa el lienzo.
    [[nodiscard]] QSize imageSize() const { return image_.size(); }
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

    // --- contorno detectado (A4) ---
    // Superpone el contorno de la pieza con su descomposición (rectas y arcos
    // en colores distintos), los agujeros y un resumen numérico. Es una capa de
    // consulta: no se puede seleccionar ni arrastrar, para que no compita con
    // las herramientas.
    void setContourReport(bool visible, const vision::ContourReport& report = {});
    [[nodiscard]] bool contourReportVisible() const { return contourVisible_; }
    [[nodiscard]] const vision::ContourReport& contourReport() const { return contourReport_; }
    // Resumen del contorno (perímetro, área, agujeros, envolvente, tramos) en la
    // unidad activa, una línea por dato. Lo pinta el propio lienzo y además lo
    // lee la ventana para el panel de estado: un solo sitio donde se decide el
    // formato, o los dos acabarían diciendo cosas distintas.
    [[nodiscard]] QStringList contourSummaryLines() const;

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
    // Zona libre terminada, ya simplificada, en coordenadas de imagen.
    void freeZonePicked(const std::vector<cv::Point>& imagePolygon);
    // El operador se echó atrás. Existe porque quien encendió el modo tiene un
    // botón pulsado, y un botón que se queda hundido después de cancelar dice
    // que el programa sigue esperando un trazo que ya nadie va a hacer.
    void freeZoneCancelled();
    // El operador ha soltado una pincelada: las dos máscaras completas, en
    // coordenadas de imagen. Se emiten enteras y no el trazo suelto porque quien
    // las usa las necesita completas, y así no hay dos acumuladores que
    // mantener sincronizados.
    void edgeCorrected(const cv::Mat& forcePiece, const cv::Mat& forceBackground);
    // El tamaño del pincel cambió con la rueda: quien lo muestre debe seguirlo.
    void brushRadiusChanged(int radiusPx);

    // EL CLIC DERECHO PIDE OPCIONES; NO EJECUTA NINGUNA.
    //
    // Antes el clic derecho sobre una herramienta la BORRABA en el acto, sin
    // menu y sin preguntar. En cualquier otro programa el clic derecho es el
    // gesto de «enseñame que puedo hacer aqui», asi que el que mas se parece a
    // pedir informacion era el unico que destruia trabajo.
    //
    // La lona no monta el menu: dice DONDE se pulso —y sobre que, si es que hay
    // algo— y quien conoce las acciones lo monta. `tool` vale -1 cuando el clic
    // cae sobre el vacio, que tambien tiene cosas que ofrecer.
    void contextMenuRequested(int tool, const QPoint& globalPos,
                              const cv::Point2f& imagePoint);
    // Que ha hecho la ultima pincelada. Existe para poder DECIRLO: una ayuda que
    // unas veces actua y otras no, sin explicar cual de las dos ha pasado, se
    // vive como que el programa va a rachas.
    void edgeStrokeFinished(bool snapped, double contrast, int keptPixels, int bandPixels);
    void toolRightClicked(int index);
    // Un gesto claramente intencionado que no pudo convertirse en herramienta.
    // Existe para que nada se descarte en silencio: si el operador traza y no
    // aparece nada, tiene que saber por qué.
    void traceRejected(const QString& reason);

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
    // Estado de vista actual, como transformación probable sin ventana.
    [[nodiscard]] ViewTransform view() const;
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
    // Flechas de dependencia entre herramientas que se referencian (X0/X1/X2).
    void paintDependencies(QPainter& painter) const;
    void paintResults(QPainter& painter) const;
    // ¿Hay un resultado medido para esta herramienta? Si lo hay, su etiqueta ya
    // incluye el nombre y no hay que pintarlo por duplicado.
    [[nodiscard]] bool hasResultFor(const ToolConfig& config) const;
    // Medida formateada según su tipo (conteo, grados o longitud en la unidad
    // activa).
    [[nodiscard]] QString measureText(const ToolRunResult& result) const;
    void paintCreationPreview(QPainter& painter) const;
    void paintFreeZone(QPainter& painter) const;
    void paintEdgeCorrection(QPainter& painter) const;
    // Cierra el trazo: lo simplifica, y si no encierra área lo dice en vez de
    // descartarlo en silencio.
    void finishFreeZone(const std::vector<cv::Point>& trace);
    void paintLiveOverlay(QPainter& painter) const;
    void paintBoard(QPainter& painter) const;
    void paintContourReport(QPainter& painter) const;
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
    int focusedPiece_ = 0;
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
    // Contorno detectado (A4). Se guarda entero -no solo dibujado- porque el
    // botón de exportar necesita los mismos puntos que se están viendo.
    bool contourVisible_ = false;
    vision::ContourReport contourReport_;
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
    // Arco a medio crear: el primer trazo fija los dos extremos y el segundo,
    // por dónde pasa. Sin ese tercer punto los extremos admiten dos arcos -el
    // corto y el largo- y no se sabria cual se esta midiendo.
    std::optional<std::array<cv::Point2f, 2>> pendingArc_;
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
    std::vector<QPolygonF> livePieceOutlines_;
    int liveMeasuredPiece_ = 0;
    bool livePieceChosen_ = false;
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

    // Zona libre: la guardada y la que se está trazando ahora.
    // Pincel de borde.
    EdgeBrush brush_ = EdgeBrush::Off;
    int brushRadius_ = 12;
    bool painting_ = false;
    bool brushSteady_ = true;
    bool brushStraight_ = false;
    bool brushSnap_ = false;
    // Punto suavizado del trazo en curso: el pincel pinta AQUI y no donde esta
    // el raton. Es el estabilizador clasico de los programas de dibujo, y lo que
    // quita es el temblor, no la intencion.
    std::optional<cv::Point2f> steadyPoint_;
    // Trazo recto en curso: donde empezo, y si esta pincelada concreta va recta
    // (el interruptor y la tecla Mayus se combinan al pulsar, no despues).
    std::optional<cv::Point2f> straightStart_;
    bool straightStroke_ = false;
    // Gris del entorno del punto donde empezo el trazo: la semilla que decide
    // con que mitad de la banda se queda «ceñir al borde».
    double strokeSeed_ = 0.0;
    // Último punto del trazo en curso: el pincel une puntos consecutivos en vez
    // de pintar círculos sueltos, o un movimiento rápido dejaría huecos.
    std::optional<cv::Point> lastPaint_;
    cv::Mat forcePiece_;
    cv::Mat forceBackground_;
    bool showCorrection_ = true;

    // Realce de vista. `enhanced_` es una COPIA que solo se pinta; se recalcula
    // cuando cambia el fotograma y no en cada repintado, que si no seria una
    // pasada por toda la imagen por cada movimiento del raton.
    bool viewEnhance_ = false;
    QImage enhanced_;
    qint64 enhancedKey_ = 0;
    bool enhancedUseful_ = false;
    const QImage& displayImage();

    // Un paso de deshacer: la zona que tocó la pincelada, y su contenido antes
    // y después. Guardar las dos caras deja deshacer y rehacer simétricos, sin
    // tener que reconstruir nada.
    struct EdgeCorrectionStep {
        cv::Rect area;
        cv::Mat pieceBefore;
        cv::Mat backgroundBefore;
        cv::Mat pieceAfter;
        cv::Mat backgroundAfter;
    };
    std::vector<EdgeCorrectionStep> undoSteps_;
    std::vector<EdgeCorrectionStep> redoSteps_;
    // Copia del estado al EMPEZAR el trazo. Transitoria: en cuanto se suelta se
    // extrae de ella el parche de la zona tocada y se tira.
    cv::Mat strokeBeforePiece_;
    cv::Mat strokeBeforeBackground_;
    cv::Rect strokeArea_;
    void beginEdgeStroke();
    void commitEdgeStroke();
    void applyEdgeStep(const cv::Rect& area, const cv::Mat& piece, const cv::Mat& background);
    void forgetEdgeCorrection();
    void paintAt(const cv::Point2f& imagePoint);
    // Ciñe al borde lo que acaba de pintar este trazo. Se hace AL SOLTAR y no
    // durante el arrastre a proposito: durante el trazo el operador tiene que
    // ver por donde va pasando el pincel, y al soltar la pincelada se asienta
    // sobre el borde. Ademas, ceñir en cada movimiento del raton reevaluaria una
    // banda que aun esta creciendo, y el resultado dependeria de a que velocidad
    // se movio la mano.
    void snapStrokeToEdge();
    // El anillo del pincel bajo el cursor. FUERA de `paintEdgeCorrection`, y ese
    // cambio de sitio es el arreglo de «el tamaño desaparece despues de usarlo»:
    // estaba detras del mismo `return` que retira la mancha del trazo, asi que
    // al soltar la primera pincelada se dejaba de dibujar el indicador de
    // tamaño. El anillo es el CURSOR, no la correccion.
    void paintBrushCursor(QPainter& painter) const;

    bool freeZonePick_ = false;
    bool freeZoneVisible_ = false;
    std::vector<cv::Point> freeZone_;      // la activa, coords de imagen
    std::vector<cv::Point> freeVertices_;  // vértices marcados a clic, en curso
    std::vector<cv::Point> freeTrace_;     // trazo a pulso, en curso
    bool freeDragging_ = false;
    bool freeLasso_ = false;
    QPointF freePressWidget_;
};

}  // namespace pci::inspection
