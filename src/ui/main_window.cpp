#include "ui/main_window.h"
#include "ui/main_window_internal.h"

#include "core/logging.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "inspection_editor/canvas/tool_palette.h"
#include "repositories/settings_repository.h"
#include "ui/measurements_panel.h"
#include "ui/piece_mosaic.h"
#include "ui/theme.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <string>
#include <vector>

namespace pci::ui {

namespace {

// Separador vertical entre grupos de la barra.
//
// Trece botones repartidos en tres filas, todos del mismo peso y a la misma
// distancia unos de otros, se leen como una lista de trece cosas sin
// relación. Con una línea entre grupos se leen como tres decisiones: qué
// miro, qué mido y qué hago. No es adorno — es lo único que dice dónde
// acaba un grupo y empieza el siguiente.
QFrame* barSeparator(QWidget* central) {
    auto* line = new QFrame(central);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

}  // namespace

MainWindow::MainWindow(AppRepositories repositories, QWidget* parent)
    : QMainWindow(parent), repos_(repositories) {
    setWindowTitle(tr("PC Inspector: demo de inspección visual"));
    resize(1100, 760);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    buildSourceRow(central, rootLayout);
    buildPieceRow(central, rootLayout);
    buildPieceToolsRow(central, rootLayout);
    buildNoticeBands(central, rootLayout);
    buildVideoCanvas(central, rootLayout);
    buildVideoBar(central, rootLayout);

    setCentralWidget(central);

    buildCompareAndToolsDocks();
    buildMeasurementsAndMosaicDocks();
    buildStatusBar();
    connectSignalsAndTimers();

    restoreCalibrationAndPreferences();
    restoreDetectionSettings();
    restoreCameraAndViewSettings();
    updateModeChip();  // el indicador arranca con el modo por defecto (M3)
    updateBoardReadout();

    buildCaptureDock();  // antes de restaurar la disposición, o no se colocaría
    buildMenusAndShortcuts();

    // Restaurar tamaño, posición, pantalla, maximizada y disposición de
    // paneles: la ventana se abre donde el operador la dejó (S3).
    restoreWindowLayout();

    placeDocksMissingFromSavedLayout();

    refreshCameras();

    restoreLastSession();

    // Al arrancar con una pieza ya seleccionada, su modo y su tablero mandan
    // sobre el ajuste global (M2); loadPieceList puede no disparar la señal.
    loadMeasurementForSelectedPiece();
    loadDetectionProfileForSelectedPiece();

    // LO ÚLTIMO: qué comandos se pueden usar de verdad ahora mismo.
    //
    // Va al final del constructor porque necesita las acciones ya creadas y sus
    // tooltips ya repartidos —guarda el texto de siempre para devolverlo cuando
    // el comando vuelva a poder usarse—, y porque la pieza y la fuente
    // recordadas acaban de cargarse: con eso ya se sabe qué hay delante.
    registerGatedCommands();
}

void MainWindow::buildSourceRow(QWidget* central, QVBoxLayout* rootLayout) {
    // --- Fila 1: cámara ---
    auto* cameraLayout = new QHBoxLayout();
    buildSourceControls(central, cameraLayout);
    buildEdgeBrushMenu(central, cameraLayout);
    connectEdgeBrushMenu();
    cameraLayout->addStretch(0);
    rootLayout->addLayout(cameraLayout);
}

void MainWindow::buildSourceControls(QWidget* central, QHBoxLayout* cameraLayout) {
    cameraLayout->addWidget(new QLabel(tr("Fuente:"), central));
    cameraCombo_ = new QComboBox(central);
    cameraCombo_->setObjectName(QStringLiteral("sourceCombo"));
    cameraCombo_->setMinimumWidth(200);
    // Ancho ACOTADO, y no estirado hasta donde llegue.
    //
    // Con factor de estiramiento, el desplegable se quedaba con todo el hueco
    // sobrante: «Integrated Camera» ocupaba media ventana y empujaba los
    // botones contra el borde derecho, lejos del combo al que se refieren. Un
    // desplegable no se lee mejor por ser cuatro veces más ancho que su texto;
    // los botones sí se encuentran mejor si están juntos.
    cameraCombo_->setMaximumWidth(320);
    cameraCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    cameraLayout->addWidget(cameraCombo_);
    startStopButton_ = new QPushButton(tr("Iniciar"), central);
    startStopButton_->setObjectName(QStringLiteral("startStopButton"));
    // El botón MÁS pulsado de la ventana y no decía nada. Su rótulo además
    // cambia solo —«Iniciar», «Detener», «Abrir…»— según la fuente elegida, así
    // que leerlo no basta para saber qué va a pasar.
    startStopButton_->setToolTip(tr("Arranca o detiene la fuente elegida en la lista de al lado."));
    startStopButton_->setWhatsThis(
        tr("Con una cámara, empieza o para el vídeo en directo. Con «Abrir imagen…» o "
           "«Abrir vídeo…», pide el fichero. Mientras está parado se puede seguir "
           "dibujando sobre el último fotograma."));
    cameraLayout->addWidget(startStopButton_);
    // El botón dice lo que va a hacer. Con «Abrir imagen…» elegido, «Iniciar»
    // no describe la acción —lo siguiente que pasa es que se abre un diálogo de
    // fichero— y un botón que no anuncia su efecto se pulsa con recelo.
    connect(cameraCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (streaming_) {
            // Con una fuente en marcha, elegir otra la CAMBIA. Antes esto no
            // podía pasar porque el desplegable estaba apagado, y para abrir una
            // imagen había que saber que primero hay que pulsar «Detener».
            //
            // El cambio no es inmediato: parar la fuente es asíncrono (une el
            // hilo de captura), así que se apunta la elección y se aplica en
            // `onStreamStopped`. Arrancar la nueva antes de que la anterior
            // suelte la cámara es la forma más rápida de quedarse sin ninguna.
            const QVariant choice = cameraCombo_->currentData();
            if (!choice.isValid() || choice.toInt() == kSourceOpenedFile) {
                return;  // el propio fichero abierto: no hay nada que cambiar
            }
            pendingSourceChoice_ = choice.toInt();
            statusBar()->showMessage(tr("Cambiando de fuente…"));
            onStartStopClicked();  // detiene; la nueva arranca al terminar
            return;
        }
        updateStartButtonText();
    });

    // Congelar. Va junto al de arrancar porque es la misma decisión —«qué estoy
    // mirando»— y porque es lo que se pulsa justo después de ver pasar la pieza
    // buena.
    freezeButton_ = new QPushButton(tr("Capturar foto"), central);
    freezeButton_->setObjectName(QStringLiteral("freezeButton"));
    freezeButton_->setEnabled(false);
    freezeButton_->setToolTip(tr("Congela la imagen para dibujar y medir con calma."));
    freezeButton_->setWhatsThis(
        tr("Con el vídeo en vivo la pieza tiembla y dibujar encima es cuestión de "
           "puntería. La cámara no se cierra: vuelves al vídeo con el mismo botón."));
    connect(freezeButton_, &QPushButton::clicked, this, &MainWindow::toggleFrozenPhoto);
    cameraLayout->addWidget(freezeButton_);

    cameraLayout->addWidget(barSeparator(central));

    // UN solo control para la zona, con menú, en vez de dos botones.
    //
    // Había dos, y cada uno cambiaba de texto según el estado: «Zona de
    // detección» pasaba a «Quitar zona», y «Zona libre» a «Quitar zona libre».
    // En la barra se leía «Zona de detección | Quitar zona libre», que es un
    // botón diciendo lo que dibuja al lado de otro diciendo lo que borra — dos
    // verbos distintos para la misma decisión. Para saber qué había puesto
    // había que leer los dos y deducirlo.
    //
    // Ahora el botón dice SIEMPRE lo mismo («Zona») y el menú ofrece las tres
    // acciones por su nombre, con la activa marcada. El estado se lee de un
    // vistazo en vez de deducirse de dos etiquetas que se mueven.
    zoneButton_ = new QToolButton(central);
    // El rótulo alterna entre «Zona» y «Zona fija» según cuál esté en uso.
    zoneButton_->setObjectName(QStringLiteral("zoneButton"));
    zoneButton_->setText(tr("Zona"));
    zoneButton_->setIcon(inspection::regionIcon());
    zoneButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    zoneButton_->setPopupMode(QToolButton::InstantPopup);
    zoneButton_->setToolTip(
        tr("Limita dónde busca el programa la pieza, para que sombras y piezas vecinas "
           "no estorben."));
    auto* zoneMenu = new QMenu(zoneButton_);
    rectZoneAction_ = zoneMenu->addAction(tr("Dibujar zona rectangular"));
    rectZoneAction_->setIcon(inspection::regionIcon());
    rectZoneAction_->setToolTip(
        tr("Arrastra un rectángulo sobre el vídeo: el contorno solo se buscará ahí."));
    freeZoneAction_ = zoneMenu->addAction(tr("Dibujar zona libre"));
    freeZoneAction_->setIcon(inspection::freeZoneIcon());
    freeZoneAction_->setToolTip(
        tr("Rodea el área a mano, arrastrando o marcando esquinas a clics."));
    freeZoneAction_->setWhatsThis(
        tr("A diferencia del rectángulo, sirve para separar formas irregulares: el "
           "borde del útil pegado a la pieza, o una pieza vecina en diagonal. Cierra el "
           "trazo sobre el primer punto."));
    zoneMenu->addSeparator();
    clearZoneAction_ = zoneMenu->addAction(tr("Quitar la zona"));
    zoneButton_->setMenu(zoneMenu);
    cameraLayout->addWidget(zoneButton_);
}

void MainWindow::buildEdgeBrushMenu(QWidget* central, QHBoxLayout* cameraLayout) {
    // Pincel para corregir el borde detectado.
    edgeBrushButton_ = new QToolButton(central);
    edgeBrushButton_->setObjectName(QStringLiteral("edgeBrushButton"));
    edgeBrushButton_->setText(tr("Corregir borde"));
    edgeBrushButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    edgeBrushButton_->setPopupMode(QToolButton::InstantPopup);
    auto* brushMenu = new QMenu(edgeBrushButton_);
    brushAddAction_ = brushMenu->addAction(tr("Pincel: añadir a la pieza"));
    brushAddAction_->setObjectName(QStringLiteral("brushAddAction"));
    brushAddAction_->setCheckable(true);
    brushRemoveAction_ = brushMenu->addAction(tr("Pincel: quitar de la pieza"));
    brushRemoveAction_->setObjectName(QStringLiteral("brushRemoveAction"));
    brushRemoveAction_->setCheckable(true);
    brushMenu->addSeparator();

    // RODEAR, que es otra cosa que pintar.
    //
    // Petición de uso: «añadir pieza dibujando un contorno manualmente, y que
    // detecte o intente detectar la pieza (igual para quitarlo), por si en un
    // lote no la detecta, o detecta algo que no debe».
    //
    // El pincel ya servía para las dos cosas, y a mano: una pieza entera son
    // decenas de pinceladas, y lo que queda es una silueta dibujada a pulso —de
    // la que no se pueden sacar cotas—. Rodear es un gesto y el borde lo busca
    // el programa dentro del trazo.
    outlineAddAction_ = brushMenu->addAction(tr("Marcar una pieza rodeándola…"));
    outlineAddAction_->setObjectName(QStringLiteral("outlineAddAction"));
    outlineAddAction_->setCheckable(true);
    outlineAddAction_->setToolTip(
        tr("Rodea con el ratón una pieza que la detección no ve."));
    outlineAddAction_->setWhatsThis(
        tr("Dentro del trazo se vuelve a buscar el borde real, así que la pieza se mide "
           "de verdad y no con el pulso de tu mano. Si ahí dentro no hay nada que "
           "detectar, la pieza se marca igual pero sus cotas serían las del trazo."));
    outlineDropAction_ = brushMenu->addAction(tr("Descartar lo que no es una pieza…"));
    outlineDropAction_->setObjectName(QStringLiteral("outlineDropAction"));
    outlineDropAction_->setCheckable(true);
    outlineDropAction_->setToolTip(
        tr("Rodea una mancha que la detección cuenta como pieza sin serlo: una sombra, "
           "un reflejo, un rótulo en la mesa."));
    outlineDropAction_->setWhatsThis(
        tr("Todo lo que quede dentro del trazo pasa a ser fondo."));
    brushMenu->addSeparator();

    // LAS OPCIONES DEL PINCEL, EN SU SUBMENÚ.
    //
    // El tamaño y las tres ayudas iban sueltos en este desplegable, entre las
    // órdenes: catorce entradas con un rótulo falso («Ayuda del pincel», una
    // entrada apagada haciendo de título). Pintar, rodear y deshacer se usan en
    // cada corrección; el tamaño y las ayudas se ajustan una vez y se quedan.
    // Van en «Pincel», que es UN solo QMenu colgado en dos sitios: aquí y en la
    // barra, en «Medida». Las acciones son las mismas, no copias, así que
    // marcar una en un sitio se ve marcada en el otro.
    brushOptionsMenu_ = new QMenu(tr("Pincel"), this);
    brushOptionsMenu_->setObjectName(QStringLiteral("brushOptionsMenu"));
    brushOptionsMenu_->setToolTipsVisible(true);
    brushOptionsMenu_->menuAction()->setToolTip(
        tr("Tamaño del pincel y ayudas para trazar."));

    // EL TAMAÑO, A LA VISTA.
    //
    // Antes solo se podia cambiar con la rueda del raton, y la rueda no la
    // encuentra quien no sabe ya que esta ahi. Peor: el unico sitio donde se
    // veia el tamaño era el anillo bajo el cursor, y ese anillo se dejaba de
    // dibujar justo al terminar la primera pincelada. El resultado era un ajuste
    // que existia, no se veia y ademas se reiniciaba solo.
    auto* sizeRow = new QWidget(brushOptionsMenu_);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(12, 4, 12, 4);
    sizeLayout->addWidget(new QLabel(tr("Tamaño:"), sizeRow));
    brushSizeSlider_ = new QSlider(Qt::Horizontal, sizeRow);
    brushSizeSlider_->setObjectName(QStringLiteral("brushSizeSlider"));
    brushSizeSlider_->setRange(2, 120);  // los mismos topes que el lienzo
    brushSizeSlider_->setMinimumWidth(150);
    brushSizeSlider_->setToolTip(
        tr("Radio del pincel, en píxeles de la imagen.\n"
           "Sobre la imagen, las teclas [ y ] o Mayús+rueda hacen lo mismo."));
    sizeLayout->addWidget(brushSizeSlider_);
    brushSizeLabel_ = new QLabel(sizeRow);
    brushSizeLabel_->setMinimumWidth(52);
    sizeLayout->addWidget(brushSizeLabel_);
    auto* sizeAction = new QWidgetAction(brushOptionsMenu_);
    sizeAction->setDefaultWidget(sizeRow);
    brushOptionsMenu_->addAction(sizeAction);
    brushOptionsMenu_->addSeparator();

    // LAS TRES AYUDAS.
    //
    // Tres y no una porque son tres problemas distintos, y quien pide «que el
    // pincel ayude» no siempre quiere las tres a la vez: una arregla la mano que
    // dibuja, otra restringe lo que se puede dibujar, y la tercera arregla el
    // resultado. Cada una se puede apagar por su cuenta.
    brushSteadyAction_ = brushOptionsMenu_->addAction(tr("Pulso estable"));
    brushSteadyAction_->setObjectName(QStringLiteral("brushSteadyAction"));
    brushSteadyAction_->setCheckable(true);
    brushSteadyAction_->setToolTip(
        tr("Suaviza el temblor de la mano; la intención del trazo llega igual."));
    brushStraightAction_ = brushOptionsMenu_->addAction(tr("Trazo recto"));
    brushStraightAction_->setObjectName(QStringLiteral("brushStraightAction"));
    brushStraightAction_->setCheckable(true);
    brushStraightAction_->setToolTip(
        tr("La pincelada va en línea recta del principio al final."));
    brushStraightAction_->setWhatsThis(
        tr("El rodeo que dé la mano por el camino no cuenta. Mantener Mayús mientras se "
           "pinta invierte este interruptor solo para ese trazo."));
    brushSnapAction_ = brushOptionsMenu_->addAction(tr("Ceñir al borde"));
    brushSnapAction_->setObjectName(QStringLiteral("brushSnapAction"));
    brushSnapAction_->setCheckable(true);
    brushSnapAction_->setToolTip(
        tr("El trazo se pega al contraste real de la imagen, más ceñido que el ancho "
           "del pincel."));
    brushSnapAction_->setWhatsThis(
        tr("Se queda con la mitad de la pincelada más cercana a donde empezó el trazo, "
           "así que empieza encima de lo que quieres marcar. Donde no hay contraste que "
           "seguir, pinta como el pincel normal."));
    brushMenu->addMenu(brushOptionsMenu_);
    brushMenu->addSeparator();
    // UN solo deshacer, no dos.
    //
    // La aplicación ya tiene Ctrl+Z para las herramientas dibujadas. Darle al
    // pincel su propio atajo obligaría a saber cuál de los dos deshaceres está
    // uno usando, y a acertar — que es peor que no tener deshacer.
    //
    // La regla es la que espera cualquiera con un pincel en la mano: mientras
    // el pincel está activo, Ctrl+Z deshace la pincelada; con el pincel
    // apagado, Ctrl+Z sigue siendo el de las herramientas, como siempre. El
    // reparto lo hace `onUndo`, y los nombres del menú lo dicen para que no
    // haya que descubrirlo probando.
    brushUndoAction_ = brushMenu->addAction(
        tr("Deshacer la última pincelada	Ctrl+Z con el pincel activo"));
    brushUndoAction_->setEnabled(false);
    brushRedoAction_ = brushMenu->addAction(
        tr("Rehacer la pincelada	Ctrl+Y con el pincel activo"));
    brushRedoAction_->setEnabled(false);
    brushMenu->addSeparator();
    // La segunda mitad de corregir el borde: la corrección no sólo arregla ESTA
    // imagen, también dice dónde se equivoca la detección y con qué signo.
    brushTuneAction_ = brushMenu->addAction(tr("Afinar la detección con esta corrección…"));
    brushTuneAction_->setObjectName(QStringLiteral("brushTuneAction"));
    brushTuneAction_->setEnabled(false);  // hasta que haya algo que aprender
    brushClearAction_ = brushMenu->addAction(tr("Quitar las correcciones"));
    edgeBrushButton_->setMenu(brushMenu);
    cameraLayout->addWidget(edgeBrushButton_);
}

void MainWindow::connectEdgeBrushMenu() {
    connect(brushAddAction_, &QAction::triggered, this, [this](bool on) {
        brushRemoveAction_->setChecked(false);
        video_->setEdgeBrush(on ? inspection::EditorCanvas::EdgeBrush::AddPiece
                                : inspection::EditorCanvas::EdgeBrush::Off);
        statusBar()->showMessage(
            on ? tr("Pinta sobre lo que la detección se dejó fuera y forma parte de la pieza.")
               : tr("Pincel apagado."));
    });
    connect(brushRemoveAction_, &QAction::triggered, this, [this](bool on) {
        brushAddAction_->setChecked(false);
        video_->setEdgeBrush(on ? inspection::EditorCanvas::EdgeBrush::RemovePiece
                                : inspection::EditorCanvas::EdgeBrush::Off);
        statusBar()->showMessage(
            on ? tr("Pinta sobre lo que la detección metió y no es la pieza: sombras, "
                    "reflejos, la pieza de al lado.")
               : tr("Pincel apagado."));
    });
    // Rodear: los dos modos son exclusivos entre sí y con el pincel, porque el
    // gesto es el mismo arrastre y sólo puede significar una cosa.
    connect(outlineAddAction_, &QAction::triggered, this, [this](bool on) {
        outlineDropAction_->setChecked(false);
        brushAddAction_->setChecked(false);
        brushRemoveAction_->setChecked(false);
        video_->setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
        video_->setOutlinePickMode(on ? inspection::EditorCanvas::TracePurpose::MarkPiece
                                     : inspection::EditorCanvas::TracePurpose::WorkZone);
        statusBar()->showMessage(
            on ? tr("Rodea la pieza que falta: arrastra para trazarla a pulso, o marca "
                    "esquinas a clics y cierra sobre la primera.")
               : tr("Marcar piezas: apagado."));
    });
    connect(outlineDropAction_, &QAction::triggered, this, [this](bool on) {
        outlineAddAction_->setChecked(false);
        brushAddAction_->setChecked(false);
        brushRemoveAction_->setChecked(false);
        video_->setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
        video_->setOutlinePickMode(on ? inspection::EditorCanvas::TracePurpose::DropPiece
                                     : inspection::EditorCanvas::TracePurpose::WorkZone);
        statusBar()->showMessage(
            on ? tr("Rodea lo que no es una pieza: todo lo que quede dentro pasa a ser "
                    "fondo.")
               : tr("Descartar manchas: apagado."));
    });
    connect(video_, &inspection::EditorCanvas::pieceOutlined, this,
            &MainWindow::onPieceOutlined);
    connect(brushClearAction_, &QAction::triggered, this, [this] {
        video_->clearEdgeCorrection();
        statusBar()->showMessage(tr("Correcciones del borde quitadas."));
    });
    connect(brushTuneAction_, &QAction::triggered, this, &MainWindow::onTuneDetectionFromEdge);
    connect(brushUndoAction_, &QAction::triggered, this, [this] {
        if (!video_->undoEdgeCorrection()) {
            statusBar()->showMessage(tr("No hay ninguna pincelada que deshacer."));
        }
    });
    connect(brushRedoAction_, &QAction::triggered, this, [this] {
        if (!video_->redoEdgeCorrection()) {
            statusBar()->showMessage(tr("No hay ninguna pincelada que rehacer."));
        }
    });
}

void MainWindow::buildPieceRow(QWidget* central, QVBoxLayout* rootLayout) {
    // --- Fila 2: pieza y flujo ---
    auto* pieceLayout = new QHBoxLayout();
    pieceLayout->addWidget(new QLabel(tr("Pieza:"), central));
    pieceCombo_ = new QComboBox(central);
    pieceCombo_->setMinimumWidth(140);
    // Acotado, por lo mismo que el de la fuente: estirado hasta el final
    // separaba la pieza de las acciones que se le aplican.
    pieceCombo_->setMaximumWidth(260);
    pieceLayout->addWidget(pieceCombo_);

    // Indicador del modo de medición (M3): junto al combo de pieza, que es
    // donde se decide. El operador nunca debe dudar en qué modo está.
    modeChip_ = new QLabel(central);
    modeChip_->setObjectName(QStringLiteral("modeChip"));
    modeChip_->setAlignment(Qt::AlignCenter);
    pieceLayout->addWidget(modeChip_);

    // Cuántas piezas se están viendo. Estaba SÓLO dentro de Configurar ▸ Piezas,
    // y ahí no lo ve quien está trabajando: con seis piezas en el encuadre la
    // ventana no decía nada y se medía la mayor en silencio.
    piecesChip_ = new QLabel(central);
    // Nombre estable: las pruebas lo buscaban por su texto, y en cuanto apareció
    // otra etiqueta que también dice «pieza» empezaron a leer la equivocada.
    piecesChip_->setObjectName(QStringLiteral("piecesChip"));
    piecesChip_->setAlignment(Qt::AlignCenter);
    piecesChip_->setVisible(false);  // sin recuento no ocupa sitio
    pieceLayout->addWidget(piecesChip_);

    // El navegador de piezas: solo aparece cuando hay mas de una, porque con una
    // sola no hay nada entre lo que elegir y un control apagado permanente es
    // ruido en una barra que ya esta llena.
    pieceNav_ = new QWidget(central);
    auto* navLayout = new QHBoxLayout(pieceNav_);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(2);
    piecePrevButton_ = new QToolButton(pieceNav_);
    piecePrevButton_->setObjectName(QStringLiteral("piecePrevButton"));
    piecePrevButton_->setText(QStringLiteral("\u2039"));
    // Una ayuda de partida: la de verdad la escribe `updatePieceNavigator`
    // cuando ya sabe cuántas piezas hay, y hasta entonces estas flechas eran
    // dos símbolos sin una palabra.
    piecePrevButton_->setToolTip(tr("Pieza anterior del encuadre, en orden de lectura."));
    piecePrevButton_->setAutoRaise(true);
    piecePrevButton_->setFocusPolicy(Qt::NoFocus);
    navLayout->addWidget(piecePrevButton_);
    pieceNavLabel_ = new QLabel(pieceNav_);
    pieceNavLabel_->setObjectName(QStringLiteral("pieceNavLabel"));
    pieceNavLabel_->setAlignment(Qt::AlignCenter);
    navLayout->addWidget(pieceNavLabel_);
    pieceNextButton_ = new QToolButton(pieceNav_);
    pieceNextButton_->setObjectName(QStringLiteral("pieceNextButton"));
    pieceNextButton_->setText(QStringLiteral("\u203a"));
    pieceNextButton_->setToolTip(tr("Pieza siguiente del encuadre, en orden de lectura."));
    pieceNextButton_->setAutoRaise(true);
    pieceNextButton_->setFocusPolicy(Qt::NoFocus);
    navLayout->addWidget(pieceNextButton_);
    pieceNav_->setVisible(false);
    pieceLayout->addWidget(pieceNav_);
    connect(piecePrevButton_, &QToolButton::clicked, this, [this] { stepFocusedPiece(-1); });
    connect(pieceNextButton_, &QToolButton::clicked, this, [this] { stepFocusedPiece(1); });

    // Que hay una correccion a mano puesta.
    //
    // Hace falta PORQUE el trazo se retira: sin este aviso, una correccion
    // activa seria estado invisible, y el operador podria estar mirando un
    // contorno que no sale de la deteccion sin tener forma de saberlo.
    edgeChip_ = new QLabel(central);
    edgeChip_->setObjectName(QStringLiteral("edgeChip"));
    edgeChip_->setAlignment(Qt::AlignCenter);
    edgeChip_->setVisible(false);
    pieceLayout->addWidget(edgeChip_);

    pieceLayout->addWidget(new QLabel(tr("Plantilla:"), central));
    templateCombo_ = new QComboBox(central);
    templateCombo_->setMinimumWidth(110);
    templateCombo_->setToolTip(
        tr("Una pieza puede tener varias plantillas de herramientas; se inspecciona "
           "con la activa."));
    pieceLayout->addWidget(templateCombo_);
    newTemplateButton_ = new QPushButton(tr("+"), central);
    newTemplateButton_->setToolTip(tr("Crear una plantilla nueva para esta pieza"));
    newTemplateButton_->setMaximumWidth(32);
    pieceLayout->addWidget(newTemplateButton_);
    manageTemplatesButton_ = new QPushButton(tr("Gestionar…"), central);
    manageTemplatesButton_->setToolTip(
        tr("Renombrar, duplicar o eliminar plantillas de esta pieza"));
    connect(manageTemplatesButton_, &QPushButton::clicked, this,
            &MainWindow::onManageTemplatesClicked);
    pieceLayout->addWidget(manageTemplatesButton_);

    // Aquí cambia la pregunta: hasta este punto la fila dice QUÉ se mide —la
    // pieza y su plantilla—, y a partir de aquí QUÉ SE HACE con ello. Sin la
    // línea, las siete cosas se leían como una lista sin relación.
    pieceLayout->addWidget(barSeparator(central));

    registerLiveButton_ = new QPushButton(tr("Registrar y activar"), central);
    registerLiveButton_->setToolTip(
        tr("Captura %1 referencias de la pieza, guarda las herramientas y arranca la "
           "auto-inspección.")
            .arg(kCaptureTarget));
    pieceLayout->addWidget(registerLiveButton_);

    autoInspectButton_ = new QPushButton(tr("Auto-inspección"), central);
    autoInspectButton_->setObjectName(QStringLiteral("autoInspectButton"));
    autoInspectButton_->setCheckable(true);
    autoInspectButton_->setToolTip(
        tr("Inspecciona continuamente el video contra la pieza seleccionada"));
    pieceLayout->addWidget(autoInspectButton_);

    inspectButton_ = new QPushButton(tr("Inspeccionar"), central);
    inspectButton_->setObjectName(QStringLiteral("inspectButton"));
    inspectButton_->setToolTip(tr("Inspección única con reporte detallado"));
    // LA acción de esta pantalla, y la única destacada. Con trece botones del
    // mismo peso, el que se pulsa cien veces al día parecía tan importante como
    // «Gestionar…», que se abre una vez al mes. Un solo elemento distinto llama
    // la atención; dos o tres destacados no destacan ninguno.
    //
    // OJO CON ESTA LÍNEA: está aquí por el MARCO, no por el Enter.
    //
    // Esto es un `QMainWindow` y no un `QDialog`, y la documentación de Qt es
    // explícita: «the default button behavior is provided only in dialogs». Así
    // que la propiedad NO hace que Enter inspeccione, y quien la lea esperando
    // eso se equivoca. Para inspeccionar con el teclado está la tecla `I`, que
    // el menú ya enseña.
    //
    // Y no se quita, aunque el nombre engañe: está medido que es lo que pinta el
    // realce. Comparando el mismo botón con y sin la propiedad,
    // `tests/test_default_on_main_window.cpp` da **1645 de 1680 píxeles
    // distintos, el 97,9 %**. Quitarla dejaría el botón principal igual que los
    // otros doce.
    //
    // La negrita de abajo es el realce que NO depende de estar en un diálogo, y
    // esa es la que vigila la prueba.
    inspectButton_->setDefault(true);
    QFont emphasis = inspectButton_->font();
    emphasis.setBold(true);
    inspectButton_->setFont(emphasis);
    pieceLayout->addWidget(inspectButton_);

    // Medir la pieza NO es inspeccionarla, y por eso es un botón aparte aunque
    // estén juntos: inspeccionar compara contra una referencia y da un
    // veredicto; esto solo contesta cuánto mide lo que hay delante. Se puede
    // usar sin pieza registrada, sin plantilla y sin calibrar — dando píxeles y
    // diciéndolo.
    measurePieceButton_ = new QPushButton(tr("Medir pieza"), central);
    measurePieceButton_->setToolTip(
        tr("Mide la pieza entera a partir de su contorno: perímetro, área, agujeros y "
           "las cotas propias de su forma."));
    measurePieceButton_->setWhatsThis(
        tr("No hace falta pieza registrada ni plantilla. Sin calibrar da píxeles y lo "
           "dice. Desde el informe se puede copiar, exportar a CSV o convertir cotas en "
           "herramientas vigiladas."));
    pieceLayout->addWidget(measurePieceButton_);
    pieceLayout->addStretch(0);
    rootLayout->addLayout(pieceLayout);
}

void MainWindow::buildPieceToolsRow(QWidget* central, QVBoxLayout* rootLayout) {
    // --- Fila 3: lo que actúa sobre la PIEZA y la PLANTILLA ---
    //
    // El dibujo se fue al dock de la derecha (P5) y con él lo que actúa sobre
    // la herramienta seleccionada. Aquí se queda lo demás, y el reparto no es
    // por hacer sitio: es por significado. «Rasgo distintivo», «Fijar escala» y
    // «Guardar plantilla» no son herramientas de dibujo — meterlas en el dock
    // sería ordenar por tamaño en vez de por lo que hace cada cosa.
    auto* toolsLayout = new QHBoxLayout();

    anchorButton_ = new QPushButton(tr("Rasgo distintivo"), central);
    anchorButton_->setIcon(inspection::anchorIcon());
    anchorButton_->setCheckable(true);
    anchorButton_->setToolTip(
        tr("Marca un punto único de la pieza (un agujero, una marca) para fijar su "
           "orientación."));
    anchorButton_->setWhatsThis(
        tr("Con la pieza siendo simétrica, se detecta igual en cualquier rotación, "
           "incluso girada 180°."));
    toolsLayout->addWidget(anchorButton_);

    calibrateFromToolButton_ = new QPushButton(tr("Fijar escala con esta medida…"), central);
    calibrateFromToolButton_->setEnabled(false);
    calibrateFromToolButton_->setToolTip(
        tr("Traza una herramienta sobre algo de tamaño conocido y escribe cuánto mide "
           "de verdad."));
    calibrateFromToolButton_->setWhatsThis(
        tr("La escala px→mm sale de esa medida y todas las cotas quedan en unidades "
           "reales."));
    toolsLayout->addWidget(calibrateFromToolButton_);

    saveTemplateButton_ = new QPushButton(tr("Guardar plantilla (Ctrl+S)"), central);
    saveTemplateButton_->setToolTip(
        tr("Guarda las herramientas dibujadas en la plantilla activa de la pieza."));
    saveTemplateButton_->setWhatsThis(
        tr("No hace falta volver a registrar la pieza. Si no hay ninguna seleccionada, "
           "pide crear una."));
    connect(saveTemplateButton_, &QPushButton::clicked, this,
            &MainWindow::onSaveTemplateClicked);
    toolsLayout->addWidget(saveTemplateButton_);

    toolsLayout->addStretch(1);
    // «ATAJOS (F1)» SE VA DE LA BARRA.
    //
    // Estaba en la fila que se mira cien veces al día, compitiendo por la misma
    // mirada que «Inspeccionar», y no es trabajo: es ayuda. Y ya se alcanza por
    // dos caminos —la tecla F1, que el propio botón anunciaba en su rótulo, y
    // *Ayuda ▸ Atajos de teclado…*—, así que quitarlo no le quita nada a nadie.
    //
    // El criterio, por si hay que aplicarlo otra vez: un control de la barra que
    // (1) no forma parte del trabajo de cada pieza, (2) tiene tecla y (3) tiene
    // entrada de menú, no necesita estar ahí. Los otros trece la pasan.
    rootLayout->addLayout(toolsLayout);
}

void MainWindow::buildNoticeBands(QWidget* central, QVBoxLayout* rootLayout) {
    // LO QUE IMPIDE MEDIR, lo primero encima de la imagen. Va antes que la guía
    // y que el veredicto porque manda sobre los dos: con la cámara caída, un
    // «OK» debajo sería el del último fotograma, no el de la pieza de ahora.
    blockingNotice_ = new BlockingNotice(central);
    connect(blockingNotice_, &BlockingNotice::actionRequested, this,
            &MainWindow::onBlockerAction);
    rootLayout->addWidget(blockingNotice_);

    // Guía del primer arranque (I3). Va donde el veredicto y no en un diálogo
    // a propósito: un asistente modal se cierra sin leer y encima tapa la
    // ventana que hay que mirar para hacer el primer paso.
    setupBanner_ = new QWidget(central);
    {
        auto* row = new QHBoxLayout(setupBanner_);
        row->setContentsMargins(8, 4, 4, 4);
        setupHintLabel_ = new QLabel(setupBanner_);
        setupHintLabel_->setWordWrap(true);
        row->addWidget(setupHintLabel_, 1);
        auto* dismiss = new QPushButton(tr("Entendido"), setupBanner_);
        dismiss->setToolTip(tr("No vuelve a mostrarse; el estado sigue en los "
                               "indicadores de la barra de abajo."));
        row->addWidget(dismiss);
        connect(dismiss, &QPushButton::clicked, this, &MainWindow::dismissSetupGuide);
    }
    setupBanner_->setStyleSheet(theme::bandStyle(theme::kProseOnBand, theme::kBandField));
    setupBanner_->setVisible(false);
    rootLayout->addWidget(setupBanner_);

    // El veredicto, grande: el de la auto-inspección y, fuera de ella, el de
    // las herramientas dibujadas sobre lo que se ve.
    verdictBoard_ = new VerdictBoard(central);
    rootLayout->addWidget(verdictBoard_);

    // Lectura continua de la pieza respecto al tablero (T3): solo visible con
    // el tablero encendido, junto al banner y nunca en un diálogo.
    boardReadoutLabel_ = new QLabel(central);
    boardReadoutLabel_->setAlignment(Qt::AlignCenter);
    boardReadoutLabel_->setStyleSheet(
        theme::bandStyle(theme::kInkOnBand, theme::kBandField));
    boardReadoutLabel_->setVisible(false);
    rootLayout->addWidget(boardReadoutLabel_);
}

void MainWindow::buildVideoCanvas(QWidget* central, QVBoxLayout* rootLayout) {
    // Video (canvas de edición) como área central de la ventana.
    video_ = new inspection::EditorCanvas(central);
    video_->setTools(&liveTools_);
    // AQUÍ, y no arriba con el resto del pincel.
    //
    // El botón y su menú se construyen mucho antes que el lienzo, y este
    // `connect` estaba con ellos — sobre `video_` todavía NULO. Qt no puede
    // conectar nada a un puntero nulo: avisa por consola y sigue. El resultado
    // era un pincel que pintaba en pantalla, marcaba verde y rojo, y cuya
    // corrección NO LLEGABA A NINGUNA PARTE: ni se reanalizaba, ni salía el
    // mensaje de «+N px, −M px», ni se movía el contorno.
    //
    // Las lambdas del menú sí funcionaban porque se ejecutan después, con
    // `video_` ya creado; sólo esta línea se evaluaba en el momento equivocado.
    // Por eso el fallo parecía «el pincel no hace su función» y no «falta una
    // conexión».
    // TODO ESTO VIVE AQUI Y NO JUNTO AL MENU, y no es orden estetico.
    //
    // `video_` se construye unas lineas mas arriba. Conectar o llamar al lienzo
    // desde donde se arma el menu del pincel seria hacerlo sobre un puntero
    // nulo: `connect` sobre nulo no conecta nada y solo avisa por consola —el
    // fallo que ya costo cuatro rondas con `edgeCorrected`— y una llamada como
    // `video_->brushRadius()` directamente tumba el programa al arrancar.
    connect(brushSizeSlider_, &QSlider::valueChanged, this,
            [this](int value) { applyBrushRadius(value, false); });
    // La rueda del raton sobre la imagen y el deslizador son el MISMO ajuste, y
    // tienen que enseñar el mismo numero. Antes el lienzo emitia este aviso y no
    // lo escuchaba nadie: se podia cambiar el tamaño sin que nada lo dijera.
    connect(video_, &inspection::EditorCanvas::brushRadiusChanged, this,
            [this](int radiusPx) { applyBrushRadius(radiusPx, true); });
    // Que ha hecho la ultima pincelada. Una ayuda que unas veces actua y otras
    // no, sin decir cual de las dos ha pasado, se vive como que el programa va a
    // rachas.
    connect(video_, &inspection::EditorCanvas::edgeStrokeFinished, this,
            [this](bool snapped, double contrast, int kept, int band) {
                if (!video_->brushSnap() || band <= 0) {
                    return;
                }
                if (snapped) {
                    statusBar()->showMessage(
                        tr("Pincelada ceñida al borde: se queda con %1 de %2 px "
                           "(contraste %3).")
                            .arg(kept)
                            .arg(band)
                            .arg(contrast, 0, 'f', 0));
                } else {
                    statusBar()->showMessage(
                        tr("Sin borde que seguir bajo la pincelada (contraste %1, hace "
                           "falta 12): se marcó entera, como el pincel de siempre.")
                            .arg(contrast, 0, 'f', 0));
                }
            });
    const auto rememberAssist = [this](const char* key, QAction* action,
                                       void (inspection::EditorCanvas::*apply)(bool)) {
        connect(action, &QAction::toggled, this, [this, key, apply](bool on) {
            (video_->*apply)(on);
            if (repos_.settings != nullptr) {
                repos_.settings->setInt(key, on ? 1 : 0);
            }
        });
    };
    rememberAssist("brush_steady", brushSteadyAction_,
                   &inspection::EditorCanvas::setBrushSteady);
    rememberAssist("brush_straight", brushStraightAction_,
                   &inspection::EditorCanvas::setBrushStraight);
    rememberAssist("brush_snap", brushSnapAction_, &inspection::EditorCanvas::setBrushSnap);

    // RECUPERAR NO PUEDE DEPENDER DE UNA SEÑAL QUE SOLO SALTA AL CAMBIAR.
    //
    // Antes esto era `action->setChecked(guardado)` y punto, confiando en que
    // `toggled` llevara el valor al lienzo. Una QAction empieza SIN MARCAR, así
    // que con un «apagado» guardado, `setChecked(false)` no emitía nada y el
    // lienzo se quedaba con su valor de fábrica.
    //
    // «Pulso estable» viene de fábrica ENCENDIDO, o sea que el operador lo
    // apagaba, reiniciaba, y el menú se lo enseñaba apagado mientras el pincel
    // lo seguía aplicando. No es que se olvide el ajuste: es que la pantalla
    // afirma una cosa y el programa hace otra, y no hay forma de descubrirlo
    // salvo notando que el trazo no obedece.
    //
    // Ahora el valor se empuja al lienzo SIEMPRE, haya cambiado la casilla o no.
    const auto restoreAssist = [this](const char* key, QAction* action,
                                      void (inspection::EditorCanvas::*apply)(bool),
                                      bool factory) {
        const auto saved = repos_.settings->getInt(key, factory ? 1 : 0);
        const bool wanted = saved.isOk() ? saved.value() != 0 : factory;
        action->setChecked(wanted);
        (video_->*apply)(wanted);
    };

    // Lo guardado, o lo que trae el lienzo de fabrica. Los valores por defecto
    // viven en UN solo sitio —el lienzo— para que restablecer los ajustes
    // devuelva exactamente lo que hace una instalacion nueva.
    if (repos_.settings != nullptr) {
        const auto saved = repos_.settings->getInt("brush_radius", video_->brushRadius());
        applyBrushRadius(saved.isOk() ? saved.value() : video_->brushRadius(), true);
        restoreAssist("brush_steady", brushSteadyAction_,
                      &inspection::EditorCanvas::setBrushSteady, video_->brushSteady());
        restoreAssist("brush_straight", brushStraightAction_,
                      &inspection::EditorCanvas::setBrushStraight, video_->brushStraight());
        restoreAssist("brush_snap", brushSnapAction_,
                      &inspection::EditorCanvas::setBrushSnap, video_->brushSnap());
    } else {
        applyBrushRadius(video_->brushRadius(), true);
        brushSteadyAction_->setChecked(video_->brushSteady());
        brushStraightAction_->setChecked(video_->brushStraight());
        brushSnapAction_->setChecked(video_->brushSnap());
    }

    connect(video_, &inspection::EditorCanvas::edgeCorrected, this,
            &MainWindow::onEdgeCorrected);
    rootLayout->addWidget(video_, 1);
}

void MainWindow::buildCompareAndToolsDocks() {
    // Panel de comparación "registrada vs actual" en un dock reubicable (S3):
    // el operador lo puede mover, flotar o cerrar, y su posición se guarda.
    auto* compareWidget = new QWidget(this);
    auto* compareLayout = new QVBoxLayout(compareWidget);
    auto makeThumb = [compareWidget]() {
        auto* label = new QLabel(compareWidget);
        label->setFixedSize(170, 170);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(
            theme::placeholderStyle());
        label->setText(QStringLiteral("—"));
        return label;
    };
    compareLayout->addWidget(new QLabel(tr("Pieza registrada:"), compareWidget));
    refThumbLabel_ = makeThumb();
    compareLayout->addWidget(refThumbLabel_);
    compareLayout->addWidget(new QLabel(tr("Pieza actual:"), compareWidget));
    currentThumbLabel_ = makeThumb();
    compareLayout->addWidget(currentThumbLabel_);

    // Rotar la vista de la pieza a gusto del usuario (gira la orientación de
    // la pieza seleccionada; persiste y aplica en registro e inspección).
    auto* rotateLayout = new QHBoxLayout();
    auto* rotateLeft = new QPushButton(QStringLiteral("⟲ 90°"), compareWidget);
    auto* rotateRight = new QPushButton(QStringLiteral("⟳ 90°"), compareWidget);
    const QString rotateTip =
        tr("Gira cómo se ve la pieza (su recorte normalizado). Con una pieza\n"
           "seleccionada el giro se guarda con ella.");
    rotateLeft->setToolTip(rotateTip);
    rotateRight->setToolTip(rotateTip);
    rotateLayout->addWidget(rotateLeft);
    rotateLayout->addWidget(rotateRight);
    compareLayout->addLayout(rotateLayout);
    connect(rotateLeft, &QPushButton::clicked, this, [this] { rotatePieceView(-90.0); });
    connect(rotateRight, &QPushButton::clicked, this, [this] { rotatePieceView(90.0); });
    similarityLabel_ = new QLabel(compareWidget);
    similarityLabel_->setWordWrap(true);
    compareLayout->addWidget(similarityLabel_);
    compareLayout->addStretch(1);

    // --- Dock «Herramientas» (P5) ---
    //
    // La paleta y lo que actúa sobre la herramienta seleccionada: «Borrar» y el
    // parámetro de muestreo. Su sitio natural es junto a la herramienta, no
    // suelto en una barra donde ya no cabía nada.
    {
        auto* toolsPanel = new QWidget(this);
        auto* panelLayout = new QVBoxLayout(toolsPanel);
        panelLayout->setContentsMargins(0, 0, 0, 0);

        toolPalette_ = new inspection::ToolPalette(toolsPanel);
        panelLayout->addWidget(toolPalette_);

        // Parámetro de muestreo de la herramienta seleccionada, sin abrir el
        // editor: banda del Caliper, rayos del Círculo, escaneos del Borde,
        // área mínima del Blob.
        auto* paramRow = new QHBoxLayout();
        liveParamLabel_ = new QLabel(tr("Puntos:"), toolsPanel);
        paramRow->addWidget(liveParamLabel_);
        liveParamSpin_ = new QSpinBox(toolsPanel);
        liveParamSpin_->setRange(1, 1000);
        liveParamSpin_->setEnabled(false);
        liveParamSpin_->setToolTip(
            tr("Parámetro de muestreo de la herramienta seleccionada."));
        liveParamSpin_->setWhatsThis(
            tr("En la mayoría de herramientas son los puntos de medida: más puntos dan "
               "una medida más estable pero más lenta. Calibre usa la banda y Blob el "
               "área mínima, que no son puntos."));
        paramRow->addWidget(liveParamSpin_, 1);
        panelLayout->addLayout(paramRow);

        // «Borrar» ya no vive aquí: se mudó DENTRO de la paleta, junto a
        // Mover/Elegir, que es donde se elige la herramienta sobre la que actúa.
        // Tenerlo al final del panel obligaba a un viaje de ida y vuelta con el
        // ratón para el gesto más encadenado que hay: elegir y quitar.
        panelLayout->addStretch(1);

        toolsDock_ = new QDockWidget(tr("Herramientas"), this);
        // El nombre TIENE que ser estable: `saveState`/`restoreState` guardan la
        // disposición por `objectName`, así que cambiarlo perdería la
        // colocación que el operador dejó.
        toolsDock_->setObjectName(QStringLiteral("toolsDock"));
        toolsDock_->setWidget(toolsPanel);
        addDockWidget(Qt::RightDockWidgetArea, toolsDock_);
    }

    compareDock_ = new QDockWidget(tr("Comparación registrada / actual"), this);
    compareDock_->setObjectName(QStringLiteral("compareDock"));
    compareDock_->setWidget(compareWidget);
    addDockWidget(Qt::RightDockWidgetArea, compareDock_);
}

void MainWindow::buildMeasurementsAndMosaicDocks() {
    // LA TABLA DE MEDIDAS EN VIVO.
    //
    // Petición de uso: «falta la parte en donde te resume las medidas, la
    // ventana/pestaña para verlas». Los números existían y se pintaban sobre el
    // vídeo, pero con catorce cotas se pisan y, con varias piezas, sólo se
    // escriben los de UNA — que es lo correcto para el vídeo y deja las demás
    // sin poder leerse en ningún sitio.
    measurements_ = new MeasurementsPanel(this);
    measurementsDock_ = new QDockWidget(tr("Medidas en vivo"), this);
    measurementsDock_->setObjectName(QStringLiteral("measurementsDock"));
    measurementsDock_->setWidget(measurements_);
    addDockWidget(Qt::LeftDockWidgetArea, measurementsDock_);
    // A LA IZQUIERDA, y no a la derecha con los demás.
    //
    // Queja de uso: «las medidas en vivo quedan mejor del lado izquierdo,
    // porque estás saturando de opciones». Es cierto y se cuenta: a la derecha
    // ya viven la paleta de herramientas, la comparación y el mosaico —tres
    // paneles— mientras que a la izquierda sólo estaba la tira de capturas.
    //
    // Y encaja con cómo se lee la pantalla: la vista occidental barre de
    // izquierda a derecha, así que la columna de datos que se consulta mientras
    // se mira la pieza gana estando en el primer barrido, no en el último.
    // COMO UNA PESTAÑA MÁS, y no cerrado.
    //
    // Nació cerrado con el razonamiento de que «un panel vacío ocupando sitio
    // enseña a cerrarlo», y el razonamiento era bueno para el mosaico y malo
    // para éste: la primera respuesta del taller al entregarlo fue «no agregaste
    // el apartado de mediciones, como los de herramientas o comparación o
    // capturar». Un panel que arranca cerrado no se encuentra, y punto.
    //
    // Compartiendo sitio con la comparación no quita espacio a nadie: es una
    // pestaña visible al lado de las otras. La comparación sigue delante porque
    // es la que estaba, y cambiar de golpe lo que el operador ve al abrir sería
    // otra forma de decidir por él.
    // El emparejado con la tira de capturas se hace más tarde, cuando esa
    // existe: aquí todavía no está construida.
    // Al abrirlo se vuelve a analizar: con una imagen fija —que no genera
    // análisis nuevos— el panel aparecería vacío y el operador concluiría que
    // no funciona.
    // LO QUE EL PANEL PIDE, LO HACE LA VENTANA.
    //
    // El panel enseña y avisa; no toca ni una herramienta. Borrar desde ahí pasa
    // por el mismo camino que borrar en el lienzo —con su deshacer—, y elegir
    // una pieza mueve la MISMA elección que las flechas y el mosaico, no un
    // estado paralelo que pueda discrepar.
    connect(measurements_, &MeasurementsPanel::toolChosen, this,
            [this](std::int64_t toolId) {
                for (std::size_t i = 0; i < liveTools_.size(); ++i) {
                    if (liveTools_[i].config.id == toolId) {
                        video_->setSelectedIndex(static_cast<int>(i));
                        onLiveSelectionChanged(static_cast<int>(i));
                        break;
                    }
                }
            });
    connect(measurements_, &MeasurementsPanel::overlayVisibilityChanged, this,
            [this](std::int64_t toolId, bool visible) {
                auto at = std::find(hiddenOverlays_.begin(), hiddenOverlays_.end(), toolId);
                if (visible && at != hiddenOverlays_.end()) {
                    hiddenOverlays_.erase(at);
                } else if (!visible && at == hiddenOverlays_.end()) {
                    hiddenOverlays_.push_back(toolId);
                }
                // Se repinta con lo último que se midió: esperar al siguiente
                // análisis dejaría el ojo sin efecto visible durante un segundo,
                // y un control que tarda en responder se pulsa dos veces.
                video_->setResults(visibleResults(lastToolResults_));
                video_->update();
            });
    connect(measurements_, &MeasurementsPanel::deleteRequested, this,
            [this](std::int64_t toolId) {
                for (std::size_t i = 0; i < liveTools_.size(); ++i) {
                    if (liveTools_[i].config.id == toolId) {
                        video_->setSelectedIndex(static_cast<int>(i));
                        onDeleteToolClicked();
                        break;
                    }
                }
            });
    connect(measurements_, &MeasurementsPanel::pieceChosen, this, [this](int piece) {
        // OJO CON LAS DOS NUMERACIONES, que ya costaron un fallo aquí: el panel
        // habla en índices desde 0 —como `ToolRunResult::pieceIndex`— y
        // `focusedPiece_` va desde 1, con el 0 reservado para «la mayor», que es
        // lo que la aplicación ha hecho siempre. «Todas» se lee como ese cero.
        focusedPiece_ = piece < 0 ? 0 : piece + 1;
        video_->setFocusedPiece(piece < 0 ? 0 : piece);
        video_->update();
    });

    connect(measurementsDock_, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown) {
            reanalyseCurrentFrame();
        }
    });

    mosaic_ = new PieceMosaic(this);
    mosaicDock_ = new QDockWidget(tr("Piezas del encuadre"), this);
    mosaicDock_->setObjectName(QStringLiteral("mosaicDock"));
    mosaicDock_->setWidget(mosaic_);
    addDockWidget(Qt::RightDockWidgetArea, mosaicDock_);
    // Arranca cerrado: con una sola pieza no tiene nada que enseñar, y un panel
    // vacío ocupando sitio desde el primer arranque enseña a cerrarlo y a no
    // volver a abrirlo.
    mosaicDock_->setVisible(false);
    // Al reabrirlo se vuelve a analizar. Sin esto, con una imagen fija cargada
    // —que no genera análisis nuevos— el panel reaparecería con lo que hubiera
    // dentro la última vez, o vacío, y el operador concluiría que no funciona.
    connect(mosaicDock_, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown) {
            reanalyseCurrentFrame();
        }
    });
    connect(mosaic_, &PieceMosaic::pieceChosen, this, [this](int number) {
        // Pulsar una baldosa es ELEGIRLA: pasa a ser la que miden las
        // herramientas y la que el vídeo remarca. Es el mismo enfoque que mueven
        // las flechas del selector, no un estado aparte del panel.
        focusedPiece_ = number;
        updatePieceNavigator();
        reanalyseCurrentFrame();
    });
}

void MainWindow::buildStatusBar() {
    // Controles de vista (Z3): mínimo / − / porcentaje / + / máximo, siempre a
    // mano en la barra inferior para quien no use atajos ni rueda.
    auto* zoomBar = new QWidget(this);
    auto* zoomLayout = new QHBoxLayout(zoomBar);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(2);
    auto addZoomButton = [this, zoomBar, zoomLayout](const QString& text, const QString& tip,
                                                     auto slot) {
        auto* button = new QToolButton(zoomBar);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);  // no robar el foco al lienzo
        connect(button, &QToolButton::clicked, this, slot);
        zoomLayout->addWidget(button);
        return button;
    };
    zoomMinButton_ = addZoomButton(QStringLiteral("⤢"), tr("Zoom mínimo: ajustar a la ventana"),
                                   [this] { video_->zoomToMin(); });
    zoomOutButton_ = addZoomButton(QStringLiteral("−"), tr("Alejar (Ctrl+-)"),
                                   [this] { video_->zoomOut(); });
    zoomLabel_ = new QLabel(zoomBar);
    zoomLabel_->setMinimumWidth(52);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setToolTip(tr("Zoom actual."));
    zoomLabel_->setWhatsThis(
        tr("La rueda acerca o aleja hacia el cursor. El botón central o Ctrl y "
           "arrastrar mueven la vista. Doble clic ajusta a la ventana."));
    zoomLayout->addWidget(zoomLabel_);
    zoomInButton_ = addZoomButton(QStringLiteral("+"), tr("Acercar (Ctrl++)"),
                                  [this] { video_->zoomIn(); });
    zoomMaxButton_ = addZoomButton(QStringLiteral("⛶"), tr("Zoom máximo (20×)"),
                                   [this] { video_->zoomToMax(); });
    statusBar()->addPermanentWidget(zoomBar);
    connect(video_, &inspection::EditorCanvas::viewChanged, this, &MainWindow::updateZoomIndicator);
    updateZoomIndicator();

    // Tira de estado de la estación (I1). Los cuatro datos que deciden si una
    // medida vale estaban repartidos por las pestañas de «Configurar»: para
    // saber si estabas midiendo en condiciones había que abrirlo y recorrerlas,
    // que es justo lo que nadie hace antes de medir.
    //
    // Son botones planos y no etiquetas porque cada uno LLEVA a la pestaña que
    // lo arregla: enseñar un problema sin decir dónde se toca es media ayuda.
    for (int i = 0; i < 4; ++i) {
        auto* light = new QPushButton(this);
        light->setFlat(true);
        light->setCursor(Qt::PointingHandCursor);
        statusBar()->addPermanentWidget(light);
        stationLights_.push_back(light);
    }

    calibLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(calibLabel_);
    statsLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statsLabel_);

    // Indicadores de estado con punto verde/rojo (S4): cámara, BD y modelo ONNX.
    camIndicator_ = new QLabel(this);
    camIndicator_->setObjectName(QStringLiteral("camIndicator"));
    dbIndicator_ = new QLabel(this);
    dbIndicator_->setObjectName(QStringLiteral("dbIndicator"));
    modelIndicator_ = new QLabel(this);
    modelIndicator_->setObjectName(QStringLiteral("modelIndicator"));
    statusBar()->addPermanentWidget(camIndicator_);
    statusBar()->addPermanentWidget(dbIndicator_);
    statusBar()->addPermanentWidget(modelIndicator_);
    updateStatusIndicators();
    // SIN BASE DE DATOS NO SE INSPECCIONA NI SE GUARDA NADA, y hasta ahora eso
    // solo lo decía «BD ✕» en la esquina. No lleva botón: la base de datos se
    // abre al arrancar, así que lo único que la vuelve a intentar es arrancar
    // otra vez.
    if (repos_.pieces == nullptr) {
        blockingNotice_->report(
            Blocker::Database,
            tr("No hay base de datos: no se puede inspeccionar ni guardar piezas. "
               "Cierra el programa y vuelve a abrirlo."));
    }
}

void MainWindow::connectSignalsAndTimers() {
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(&enumerationWatcher_, &QFutureWatcher<std::vector<camera::CameraInfo>>::finished,
            this, &MainWindow::onCamerasEnumerated);
    connect(&analysisWatcher_, &QFutureWatcher<AnalysisOverlay>::finished, this,
            &MainWindow::onAnalysisFinished);

    cameraFrames_ =
        connect(&controller_, &camera::CameraController::frameReady, this, &MainWindow::onFrame);
    connect(&controller_, &camera::CameraController::statsUpdated, this, &MainWindow::onStats);
    connect(&controller_, &camera::CameraController::cameraError, this,
            &MainWindow::onCameraError);
    connect(&controller_, &camera::CameraController::stopped, this, &MainWindow::onStreamStopped);
    connect(&controller_, &camera::CameraController::controlsProbed, this,
            &MainWindow::onControlsProbed);
    connect(&controller_, &camera::CameraController::resolutionsProbed, this,
            &MainWindow::onResolutionsProbed);
    connect(&controller_, &camera::CameraController::exposureChosen, this,
            [this](double exposure, const std::vector<camera::ExposureFpsSample>& sweep) {
                // Se deja en el log lo que se PROBÓ y no solo lo que salió: si
                // algún día una cámara elige mal, la tabla dice por qué.
                core::logInfo("Exposición elegida por medida: " +
                              std::to_string(exposure) + " (de " +
                              std::to_string(sweep.size()) + " probadas)");
                autoExposureOn_ = false;
                updateCalibrationLabel();
            });
    connect(&controller_, &camera::CameraController::profileRejected, this,
            [this](const QString& reason) {
                // El perfil se deshizo: la cámara vuelve a automático, así que
                // las medidas NO son repetibles y hay que decirlo donde se
                // miran — que es la etiqueta de la escala, no un log.
                core::logWarning("Perfil rechazado: " + reason.toStdString());
                autoExposureOn_ = true;
                updateCalibrationLabel();
            });

    connect(toolPalette_, &inspection::ToolPalette::toolChosen, this,
            &MainWindow::onToolModeChanged);
    connect(video_, &inspection::EditorCanvas::toolCreated, this,
            &MainWindow::onLiveToolCreated);
    connect(video_, &inspection::EditorCanvas::toolModified, this,
            &MainWindow::onLiveToolModified);
    connect(video_, &inspection::EditorCanvas::selectionChanged, this,
            &MainWindow::onLiveSelectionChanged);
    connect(liveParamSpin_, &QSpinBox::valueChanged, this, &MainWindow::onLiveParamChanged);
    connect(calibrateFromToolButton_, &QPushButton::clicked, this,
            &MainWindow::onCalibrateFromToolClicked);
    connect(toolPalette_, &inspection::ToolPalette::deleteRequested, this,
            &MainWindow::onDeleteToolClicked);
    connect(toolPalette_, &inspection::ToolPalette::deleteAllRequested, this,
            &MainWindow::onDeleteAllToolsClicked);
    connect(anchorButton_, &QPushButton::toggled, this, &MainWindow::onAnchorButtonToggled);
    connect(video_, &inspection::EditorCanvas::pointPicked, this,
            &MainWindow::onAnchorPicked);
    connect(pieceCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onPieceSelectionChanged);
    connect(templateCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onTemplateChanged);
    connect(newTemplateButton_, &QPushButton::clicked, this,
            &MainWindow::onNewTemplateClicked);
    connect(video_, &inspection::EditorCanvas::contextMenuRequested, this,
            &MainWindow::onCanvasContextMenu);

    connect(registerLiveButton_, &QPushButton::clicked, this,
            &MainWindow::onRegisterLiveClicked);
    connect(&captureTimer_, &QTimer::timeout, this, &MainWindow::onCaptureTick);
    connect(&captureWatcher_,
            &QFutureWatcher<
                core::Result<engine::RegistrationSession::SampleFeedback>>::finished,
            this, &MainWindow::onCaptureProcessed);
    connect(autoInspectButton_, &QPushButton::toggled, this, &MainWindow::onAutoToggled);
    connect(&autoTimer_, &QTimer::timeout, this, &MainWindow::onAutoTick);

    connect(rectZoneAction_, &QAction::triggered, this,
            [this] { onRoiButtonToggled(true); });
    connect(clearZoneAction_, &QAction::triggered, this, &MainWindow::onClearZoneClicked);
    connect(video_, &inspection::EditorCanvas::regionPicked, this,
            &MainWindow::onRegionPicked);
    connect(measurePieceButton_, &QPushButton::clicked, this,
            &MainWindow::onMeasurePieceClicked);
    connect(freeZoneAction_, &QAction::triggered, this,
            [this] { onFreeZoneButtonToggled(true); });
    connect(video_, &inspection::EditorCanvas::freeZonePicked, this,
            &MainWindow::onFreeZonePicked);
    connect(video_, &inspection::EditorCanvas::freeZoneCancelled, this,
            &MainWindow::onFreeZoneCancelled);
    connect(inspectButton_, &QPushButton::clicked, this, &MainWindow::onInspectClicked);
    connect(&inspectionWatcher_,
            &QFutureWatcher<core::Result<engine::InspectionEngine::Outcome>>::finished, this,
            &MainWindow::onInspectionFinished);

    // Dos segundos después del último movimiento. Suficiente para que un
    // arrastre entero cueste una sola escritura, y corto para que un cierre
    // brusco no se lleve por delante lo que se acaba de colocar.
    layoutSaveTimer_.setSingleShot(true);
    layoutSaveTimer_.setInterval(2000);
    connect(&layoutSaveTimer_, &QTimer::timeout, this, &MainWindow::persistWindowLayout);

    captureTimer_.setInterval(350);
    // El reloj del disparo por paso de pieza: monótono y propio, para no
    // depender de la hora del sistema —que puede saltar hacia atrás— ni del
    // intervalo del temporizador.
    passClock_.start();
    autoTimer_.setInterval(autoIntervalMs_);  // se reajusta al cargar Preferencias
}

inspection::LengthUnit MainWindow::currentUnit() const {
    const QAction* checked = unitGroup_->checkedAction();
    return static_cast<inspection::LengthUnit>(checked != nullptr ? checked->data().toInt()
                                                                  : 0);
}

std::string MainWindow::activeTemplate() const {
    const QString name = templateCombo_->currentText();
    return name.isEmpty() ? std::string("principal") : name.toStdString();
}

void MainWindow::onUnitChanged() {
    const inspection::LengthUnit unit = currentUnit();
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("length_unit", static_cast<int>(unit));
    }
    video_->setLengthUnit(unit);
    // Elegir mm/cm sin escala no hace nada visible: avisar una vez.
    if (unit != inspection::LengthUnit::Auto && unit != inspection::LengthUnit::Pixels &&
        !calibration_.valid()) {
        statusBar()->showMessage(
            tr("Para ver medidas en mm/cm primero calibra la escala "
               "(Medida ▸ Calibrar escala (mm)…)."));
    }
}

MainWindow::~MainWindow() {
    autoTimer_.stop();
    captureTimer_.stop();
    controller_.stop();
    enumerationWatcher_.waitForFinished();
    analysisWatcher_.waitForFinished();
    inspectionWatcher_.waitForFinished();
    captureWatcher_.waitForFinished();
}

void MainWindow::setControlsEnabled(bool enabled) {
    cameraCombo_->setEnabled(enabled);
    refreshAction_->setEnabled(enabled);
    // Sin cámaras el botón sigue vivo: el desplegable siempre ofrece abrir una
    // imagen o un vídeo.
    startStopButton_->setEnabled(enabled);
}

}  // namespace pci::ui
