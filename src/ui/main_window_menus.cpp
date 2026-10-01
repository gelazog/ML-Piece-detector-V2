#include "ui/main_window.h"
#include "ui/main_window_internal.h"

namespace pci::ui {

// Barra de menú: agrupa las acciones de baja frecuencia que antes saturaban
// las filas de botones. Las combos y botones de uso constante siguen visibles.
// QUÉ HACE CADA ENTRADA, dicho donde el operador la lee.
//
// Sale de una queja directa: «no son intuitivos ni coherentes; debería decirle
// al usuario qué hace cada cosa». Medido: 25 de las 40 entradas de menú no
// explicaban nada.
//
// Y algo peor — Qt NO ENSEÑA las ayudas de los menús salvo que se pida con
// `setToolTipsVisible`, y nadie lo había pedido. Así que las quince que SÍ
// estaban escritas tampoco se veían. Escribir explicaciones que el programa
// esconde es peor que no escribirlas: cuesta lo mismo y no ayuda a nadie.
//
// Las explicaciones van juntas y no repartidas por los sitios de construcción
// a propósito: así se leen todas de una vez y se ve si dos entradas se pisan o
// si una dice lo contrario que otra, que es de donde sale la sensación de
// incoherencia.
void MainWindow::explainMenus() {
    struct MenuHelp {
        QString nombre;
        QString ayuda;    // tooltip: una frase corta
        QString detalle;  // opcional: setWhatsThis, para lo que no cabe en el tooltip
    };
    const MenuHelp ayudas[] = {
        {tr("Exportar configuración…"),
         tr("Guarda en un fichero los ajustes de esta máquina, para copiarlos en otra."),
         tr("Incluye cámara, detección, escala, tablero y preferencias. No lleva piezas "
            "ni su historial.")},
        {tr("Importar configuración…"),
         tr("Carga los ajustes guardados de otra máquina y sustituye los actuales."),
         tr("Las piezas registradas y su historial no cambian. Si esta cámara está a "
            "otra altura, vuelve a calibrar la escala.")},
        {tr("Restablecer configuración de fábrica…"),
         tr("Devuelve todos los ajustes a como venían de fábrica."),
         tr("Pide confirmación antes de aplicarse. No borra piezas, herramientas ni "
            "historial: solo los ajustes.")},
        {tr("Buscar cámaras de nuevo"),
         tr("Vuelve a preguntar qué cámaras hay conectadas; útil si enchufaste una con "
            "el programa abierto."),
         QString()},
        {tr("Calibrar escala (mm)…"),
         tr("Marca con dos clics una distancia conocida y di cuánto mide, para fijar la "
            "escala."),
         tr("Sin calibrar, todas las medidas salen en píxeles. Repite la calibración si "
            "cambia la altura de la cámara o la resolución.")},
        {tr("Calibrar la lente…"),
         tr("Corrige la deformación del objetivo con fotos de un tablero de ajedrez."),
         tr("Hace falta cuando la misma pieza mide distinto en el centro que en una "
            "esquina.")},
        {tr("Unidad de medida"),
         tr("En qué unidad se enseñan las medidas: milímetros, centímetros, píxeles o "
            "automática."),
         tr("Para ver milímetros o centímetros hace falta haber calibrado la escala "
            "antes.")},
        {tr("Medir pieza"),
         tr("Mide la pieza de delante con las herramientas dibujadas, sin guardar nada."),
         tr("Sirve para comprobar antes de inspeccionar que las herramientas están bien "
            "puestas.")},
        {tr("Modo de medición de la pieza…"),
         tr("Elige si esta pieza se juzga por sus medidas o por su posición respecto al "
            "tablero."),
         tr("El modo se guarda con la pieza, no con la máquina.")},
        {tr("Automática (mm/cm)"),
         tr("Enseña milímetros en las medidas pequeñas y centímetros en las grandes."),
         QString()},
        {tr("Milímetros"),
         tr("Todas las medidas en milímetros. Necesita la escala calibrada."), QString()},
        {tr("Centímetros"),
         tr("Todas las medidas en centímetros. Necesita la escala calibrada."), QString()},
        {tr("Píxeles"),
         tr("Todas las medidas en píxeles de la imagen: lo que hay sin calibrar."),
         QString()},
        {tr("Pulgadas"),
         tr("Todas las medidas en pulgadas, con tres decimales. Necesita la escala "
            "calibrada."),
         tr("Una pulgada son 25,4 mm; con menos decimales se perdería precisión.")},
        {tr("Registrar con asistente…"),
         tr("Da de alta una pieza nueva paso a paso, capturando varias fotos buenas."),
         tr("Con esas fotos el programa aprende a avisar de piezas raras aunque no "
            "midas nada.")},
        {tr("Registrar otro acabado de esta pieza…"),
         tr("Añade un acabado admisible a esta misma pieza: otro proveedor, otro lote, "
            "otro brillo."),
         tr("No uses el asistente para esto: crearía una pieza distinta. Mezclar dos "
            "acabados en la misma referencia deja de detectar defectos que antes sí se "
            "veían.")},
        {tr("Gestionar piezas…"),
         tr("Renombra, duplica o borra piezas registradas."),
         tr("También enseña cuántas herramientas e inspecciones tiene cada una.")},
        {tr("Gestionar plantillas…"),
         tr("Crea, renombra o borra plantillas de herramientas de esta pieza."),
         tr("Las plantillas son juegos de herramientas de la misma pieza: una por cara, "
            "o una rápida y otra completa.")},
        {tr("Guardar plantilla"),
         tr("Guarda las herramientas dibujadas ahora como plantilla de esta pieza."),
         QString()},
        {tr("Inspeccionar"),
         tr("Mide la pieza, da el veredicto OK/NG y lo guarda en el historial con su "
            "foto."),
         tr("Es lo que diferencia una inspección de una prueba: queda registrada.")},
        {tr("Editor de plantilla…"),
         tr("Abre la pieza a tamaño completo para dibujar sus herramientas sin la "
            "cámara en marcha."),
         QString()},
        {tr("Ver historial…"),
         tr("Todas las inspecciones guardadas de esta pieza, con foto, veredicto y "
            "motivo."),
         tr("Desde ahí se saca el informe del turno.")},
        {tr("Panel de herramientas"),
         tr("Muestra u oculta el panel con las herramientas dibujadas y sus "
            "tolerancias."),
         QString()},
        {tr("Panel de comparación"),
         tr("Muestra u oculta el panel que compara la pieza registrada con la actual."),
         QString()},
        {tr("Origen del tablero"),
         tr("Dónde está el cero desde el que se miden las posiciones."),
         tr("Puede ser el centro de la pieza, un punto fijo de la imagen o un rasgo "
            "marcado. Solo afecta a las herramientas de Posición.")},
        {tr("Atajos de teclado…"),
         tr("La lista de teclas: zoom, paso a paso, cambiar de pieza, medir."), QString()},
    };
    for (auto* menu : menuBar()->findChildren<QMenu*>()) {
        // Sin esto no se ve ninguna, ni las que ya estaban escritas.
        menu->setToolTipsVisible(true);
        for (auto* action : menu->actions()) {
            for (const auto& entry : ayudas) {
                if (action->text() == entry.nombre) {
                    action->setToolTip(entry.ayuda);
                    if (!entry.detalle.isEmpty()) {
                        action->setWhatsThis(entry.detalle);
                    }
                }
            }
        }
    }
}

void MainWindow::buildMenuBar() {
    // --- Archivo ---
    //
    // Vuelve, y esta vez con ficheros. Abrir una imagen o un vídeo solo se podía
    // desde el desplegable de fuente —elegir «Abrir imagen…» y luego pulsar el
    // botón—, sin Ctrl+O ni lista de recientes. Todo el mundo lo busca aquí, así
    // que aquí está, delante de todo. Por debajo es el mismo camino que el
    // desplegable: `startFileSourceAtPath`.
    auto* fileMenu = menuBar()->addMenu(tr("&Archivo"));
    QAction* openAction = shortcutAction(QStringLiteral("open_file"),
                                         tr("Abrir imagen o vídeo…"));
    if (openAction != nullptr) {
        fileMenu->addAction(openAction);
    } else {
        openAction = fileMenu->addAction(tr("Abrir imagen o vídeo…"), this,
                                         &MainWindow::onOpenFileClicked);
    }
    openAction->setObjectName(QStringLiteral("openFileAction"));
    openAction->setToolTip(
        tr("Carga una foto o un vídeo del disco. También puedes arrastrarlo a la ventana."));
    recentMenu_ = fileMenu->addMenu(tr("Abrir reciente"));
    recentMenu_->setObjectName(QStringLiteral("recentFilesMenu"));
    recentMenu_->menuAction()->setToolTip(
        tr("Los últimos ficheros abiertos, el más nuevo arriba."));
    rebuildRecentMenu();

    // --- Configurar ---
    //
    // Dos quejas de uso que apuntan al mismo sitio: «el menú de configurar/escala
    // debería de ser ahí y no dentro de otra sección», y «la parte de
    // archivo-fuente-medida me parece fea, a algunas cosas les falta coherencia,
    // las palabras y/o secciones mal retratadas».
    //
    // Había DOS menús mal nombrados y casi vacíos:
    //
    //   - «Archivo» no tenía ningún fichero. Lo que tenía era exportar, importar
    //     y restablecer la CONFIGURACIÓN. Abrir una imagen o un vídeo nunca
    //     estuvo ahí: eso se hace con el desplegable de fuente de la barra.
    //   - «Fuente» tenía dos cosas que no se parecen en nada: buscar cámaras, y
    //     «Configurar…», que es la puerta a cámara, detección, piezas,
    //     rendimiento, ESCALA, preferencias y atajos — casi todos los ajustes de
    //     la aplicación, metidos dentro de un menú que habla de de dónde viene la
    //     imagen.
    //
    // Los dos contestan a la misma pregunta —cómo está puesta a punto esta
    // máquina— así que van juntos y con el nombre que les toca. «Configurar…» es
    // la primera entrada porque es la que se busca, y la escala vive dentro, que
    // es justo donde el taller la pidió.
    auto* setupMenu = menuBar()->addMenu(tr("&Configurar"));
    configureAction_ = setupMenu->addAction(tr("Configurar…"), this,
                                            &MainWindow::onConfigureClicked);
    configureAction_->setObjectName(QStringLiteral("configureAction"));
    configureAction_->setToolTip(
        tr("Cámara, detección, piezas, rendimiento, escala, preferencias y atajos, "
           "todo en el mismo sitio."));
    configureAction_->setWhatsThis(
        tr("Se abre sin bloquear el vídeo: lo que ajustes se ve al momento sobre la "
           "pieza."));
    refreshAction_ = setupMenu->addAction(tr("Buscar cámaras de nuevo"), this,
                                          &MainWindow::refreshCameras);
    setupMenu->addSeparator();
    // Clonar la puesta a punto a otra PC de la línea (O4).
    setupMenu->addAction(tr("Exportar configuración…"), this,
                         &MainWindow::onExportConfigClicked);
    setupMenu->addAction(tr("Importar configuración…"), this,
                         &MainWindow::onImportConfigClicked);
    setupMenu->addSeparator();
    // Separada de las otras dos por lo que hace, no por estética: exportar e
    // importar copian ajustes de una máquina a otra; ésta los borra.
    setupMenu->addAction(tr("Restablecer configuración de fábrica…"), this,
                         &MainWindow::onResetConfigClicked);

    // --- Las calibraciones, aquí y no en «Medida» ---
    //
    // Segunda parte de la misma queja: «el menú de configurar/escala debería de
    // ser ahí y no dentro de otra sección».
    //
    // Calibrar no es medir. Se hace UNA vez —cuando se monta la cámara, cuando
    // se cambia el objetivo— y luego se queda puesto durante turnos enteros;
    // medir es lo que se hace cien veces al día. Tenerlas juntas obligaba a
    // pasar por delante de tres entradas que nadie va a tocar cada vez que se
    // buscaba «Medir pieza».
    //
    // Y son exactamente lo mismo que ya vive aquí: cómo está puesta a punto esta
    // máquina. La pestaña «Escala» de Configurar ya abría este mismo asistente,
    // así que el atajo estaba en un menú y el sitio de verdad en otro.
    setupMenu->addSeparator();
    if (auto* calibrate = shortcutAction(QStringLiteral("calibrate"),
                                         tr("Calibrar escala (mm)…"))) {
        setupMenu->addAction(calibrate);
    } else {
        setupMenu->addAction(tr("Calibrar escala (mm)…"), this,
                             &MainWindow::onCalibrateClicked);
    }
    setupMenu->addAction(tr("Calibrar la lente…"), this,
                         &MainWindow::onCalibrateLensClicked);
    lensCorrectionAction_ = setupMenu->addAction(tr("Corregir la distorsión de la lente"));
    lensCorrectionAction_->setCheckable(true);
    lensCorrectionAction_->setEnabled(false);  // hasta que haya un modelo
    lensCorrectionAction_->setToolTip(
        tr("Endereza lo que curva la lente, antes de medir. Cambia las medidas."));
    lensCorrectionAction_->setWhatsThis(
        tr("Una pieza ya registrada tiene sus tolerancias ajustadas contra el borde de "
           "antes, así que al encenderlo hay que revisarlas."));
    connect(lensCorrectionAction_, &QAction::toggled, this, [this](bool on) {
        lensCorrectionOn_ = on && lensCorrector_.isReady();
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("lens_enabled", lensCorrectionOn_ ? 1 : 0);
        }
        statusBar()->showMessage(
            lensCorrectionOn_
                ? tr("Distorsión de la lente corregida. Las medidas han cambiado: vuelve "
                     "a comprobar las tolerancias de las piezas registradas.")
                : tr("Corrección de la lente apagada."));
        reanalyseCurrentFrame();
    });
    auto* arucoAction = setupMenu->addAction(tr("Escala por marcador ArUco (en vivo)"));
    arucoAction->setCheckable(true);
    arucoAction->setChecked(arucoLiveScale_);
    arucoAction->setToolTip(
        tr("Pon un marcador ArUco de tamaño conocido junto a la pieza para calcular la "
           "escala en cada frame."));
    connect(arucoAction, &QAction::toggled, this, [this](bool on) {
        if (on) {
            bool ok = false;
            const double mm = QInputDialog::getDouble(
                this, tr("Marcador ArUco"), tr("Lado real del marcador (mm):"),
                markerSizeMm_, 1.0, 10000.0, 1, &ok);
            if (!ok) {
                // Revertir sin re-disparar la señal.
                QSignalBlocker blocker(sender());
                qobject_cast<QAction*>(sender())->setChecked(false);
                return;
            }
            markerSizeMm_ = mm;
        }
        arucoLiveScale_ = on;
        // Sin escala por marcador, que no se vea el marcador deja de ser un
        // problema.
        markerStreak_.reset();
        blockingNotice_->resolve(Blocker::Marker);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("aruco_live", on ? 1 : 0);
            repos_.settings->setDouble("aruco_marker_mm", markerSizeMm_);
        }
        statusBar()->showMessage(
            on ? tr("Escala por marcador ArUco activa (lado %1 mm).").arg(markerSizeMm_, 0, 'f', 1)
               : tr("Escala por marcador ArUco desactivada."));
        reanalyseCurrentFrame();
    });

    // --- Medida ---
    //
    // Lo que queda aquí es lo que se hace CON UNA PIEZA DELANTE: medirla, decir
    // en qué unidad se lee y con qué criterio se juzga. Las calibraciones se han
    // ido a «Configurar» porque se hacen una vez y duran turnos enteros — pasar
    // por delante de ellas cien veces al día para llegar a «Medir pieza» era el
    // recorrido al revés.
    //
    // Este menú nació porque «Calibrar escala» vivía en *Fuente*, junto a
    // «Buscar cámaras», y «Unidad de medida» en *Ver*, junto a «Mostrar
    // contorno» — como si elegir milímetros o píxeles fuera una cuestión de
    // aspecto, cuando cambia el número que se apunta en el parte.
    auto* measureMenu = menuBar()->addMenu(tr("&Medida"));
    auto* unitMenu = measureMenu->addMenu(tr("Unidad de medida"));
    unitGroup_ = new QActionGroup(this);
    // El número es el valor del enum `LengthUnit`, no la posición en la lista.
    // Se guarda tal cual en los ajustes, así que tiene que seguir significando
    // lo mismo aunque la lista se reordene.
    const std::pair<QString, int> units[] = {
        {tr("Automática (mm/cm)"), 0}, {tr("Milímetros"), 1},
        {tr("Centímetros"), 2}, {tr("Píxeles"), 3}, {tr("Pulgadas"), 4}};
    for (const auto& [label, value] : units) {
        auto* action = unitMenu->addAction(label);
        action->setCheckable(true);
        action->setData(value);
        unitGroup_->addAction(action);
        if (value == 0) {
            action->setChecked(true);
        }
    }
    measureMenu->addSeparator();
    // La acción de la barra, también aquí: un botón que solo existe en la barra
    // no lo encuentra quien navega con el teclado, y a los menús se va justo
    // cuando no se reconoce el icono.
    if (auto* measure = shortcutAction(QStringLiteral("measure_piece"),
                                       tr("Medir pieza"))) {
        measureMenu->addAction(measure);
    } else {
        measureMenu->addAction(tr("Medir pieza"), this,
                               &MainWindow::onMeasurePieceClicked);
    }
    measurementModeAction_ = measureMenu->addAction(
        tr("Modo de medición de la pieza…"), this,
        &MainWindow::onMeasurementModeClicked);
    // EL PINCEL, TAMBIÉN AQUÍ.
    //
    // Su tamaño y sus ayudas solo se encontraban abriendo «Corregir borde», un
    // botón que está apagado mientras no haya una imagen quieta: con la cámara
    // en marcha no había forma de dejarlos preparados, ni de llegar a ellos
    // con el teclado. Va en «Medida» porque corregir el borde cambia lo que se
    // mide de la pieza que hay delante.
    //
    // Es el MISMO submenú que cuelga del botón, no una copia: dos juegos de
    // acciones acabarían diciendo cosas distintas del mismo ajuste.
    if (brushOptionsMenu_ != nullptr) {
        measureMenu->addSeparator();
        measureMenu->addMenu(brushOptionsMenu_);
    }

    auto* pieceMenu = menuBar()->addMenu(tr("&Pieza"));
    pieceMenu->addAction(tr("Registrar con asistente…"), this,
                                                 &MainWindow::onRegisterWizardClicked);
    // OTRO ACABADO DE LA MISMA PIEZA, y no una pieza nueva.
    //
    // Va justo debajo de «Registrar con asistente…» porque es la confusión que
    // hay que evitar: quien tiene delante la misma pieza con otro acabado
    // acabaría registrándola otra vez, y eso crea una pieza distinta con sus
    // herramientas y su historial aparte.
    registerVariantAction_ = pieceMenu->addAction(
        tr("Registrar otro acabado de esta pieza…"), this,
        &MainWindow::onRegisterVariantClicked);
    pieceMenu->addAction(tr("Gestionar piezas…"), this,
                                               &MainWindow::onManagePiecesClicked);
    pieceMenu->addSeparator();
    // Las plantillas son de la pieza, así que sus acciones viven aquí y no solo
    // en la barra.
    pieceMenu->addAction(tr("Gestionar plantillas…"), this,
                         &MainWindow::onManageTemplatesClicked);
    if (auto* save = shortcutAction(QStringLiteral("save_template"),
                                    tr("Guardar plantilla"))) {
        pieceMenu->addAction(save);
    } else {
        pieceMenu->addAction(tr("Guardar plantilla"), this,
                             &MainWindow::onSaveTemplateClicked);
    }

    auto* inspectionMenu = menuBar()->addMenu(tr("&Inspección"));
    if (auto* inspect = shortcutAction(QStringLiteral("inspect_once"),
                                       tr("Inspeccionar"))) {
        inspectionMenu->addAction(inspect);
    } else {
        inspectionMenu->addAction(tr("Inspeccionar"), this, &MainWindow::onInspectClicked);
    }
    autoInspectAction_ = inspectionMenu->addAction(tr("Auto-inspección"));
    autoInspectAction_->setCheckable(true);
    // Espejo del botón de la barra, en los dos sentidos: si el menú dijera una
    // cosa y el botón otra, el operador no sabría cuál se cree.
    connect(autoInspectAction_, &QAction::toggled, this, [this](bool on) {
        if (autoInspectButton_ != nullptr && autoInspectButton_->isChecked() != on) {
            autoInspectButton_->setChecked(on);
        }
    });
    inspectionMenu->addSeparator();
    if (auto* editor = shortcutAction(QStringLiteral("template_editor"),
                                      tr("Editor de plantilla…"))) {
        inspectionMenu->addAction(editor);
    } else {
        inspectionMenu->addAction(tr("Editor de plantilla…"), this,
                                  &MainWindow::onOpenEditorClicked);
    }
    historyAction_ = inspectionMenu->addAction(tr("Ver historial…"), this,
                                              &MainWindow::onShowHistoryClicked);

    auto* viewMenu = menuBar()->addMenu(tr("&Ver"));
    showContourAction_ = viewMenu->addAction(tr("Mostrar contorno"));
    showContourAction_->setObjectName(QStringLiteral("showContourAction"));
    showContourAction_->setCheckable(true);
    // Era la ÚNICA capa del menú Ver que no se recordaba: el tablero, la regla
    // y el resto sí. Quien lo apagaba para inspeccionar con la pieza congelada
    // se lo encontraba encendido en cada arranque.
    //
    // Y el menú lo recordaba, pero el LIENZO no se enteraba, por dos motivos
    // encadenados: `setChecked(false)` sobre una acción que ya está en `false`
    // no emite `toggled`, y además el `connect` que lleva el valor al lienzo se
    // hacía DESPUÉS, así que aunque emitiera no había nadie escuchando. Como el
    // lienzo trae el contorno visible de fábrica, el menú decía «oculto» y el
    // contorno se seguía pintando encima del vídeo.
    //
    // Se arregla igual que las ayudas del pincel: el valor se empuja al lienzo
    // SIEMPRE. Recuperar un ajuste no puede depender de una señal que solo
    // salta cuando algo cambia.
    const bool contourVisible = repos_.settings == nullptr ||
                                repos_.settings->getInt("show_contour", 1).valueOr(1) != 0;
    showContourAction_->setChecked(contourVisible);
    video_->setLiveContourVisible(contourVisible);
    showContourAction_->setToolTip(
        tr("Al ocultarlo, las herramientas se congelan en su sitio (la pieza se "
           "inspecciona fija, sin que nada se mueva)."));
    connect(showContourAction_, &QAction::toggled, video_,
            &inspection::EditorCanvas::setLiveContourVisible);
    connect(showContourAction_, &QAction::toggled, this, [this](bool on) {
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("show_contour", on ? 1 : 0);
        }
        reanalyseCurrentFrame();
    });

    // LOS PANELES, EN SU GRUPO.
    //
    // «Ver» tenía diez entradas seguidas con un solo separador: lo que se pinta
    // encima de la imagen y los paneles que se abren y se cierran, mezclados en
    // una lista que hay que leer entera. Son dos cosas distintas —una cambia lo
    // que se dibuja sobre la pieza, la otra qué ventanas hay alrededor— y desde
    // aquí se ven separadas.
    viewMenu->addSeparator();

    // Un panel que se cierra sin forma de recuperarlo es una herramienta
    // perdida, así que el dock tiene su entrada en el menú igual que el de
    // comparación.
    if (toolsDock_ != nullptr) {
        auto* toggle = toolsDock_->toggleViewAction();
        toggle->setText(tr("Panel de herramientas"));
        viewMenu->addAction(toggle);
    }

    // Mostrar/ocultar el panel de comparación reubicable (S3).
    if (compareDock_ != nullptr) {
        auto* toggle = compareDock_->toggleViewAction();
        toggle->setText(tr("Panel de comparación"));
        viewMenu->addAction(toggle);
    }

    // El mosaico se ofrece solo la primera vez que hay varias piezas. Si el
    // operador lo cierra, no se le vuelve a abrir — por eso hace falta esta
    // entrada: sin ella, cerrarlo una vez sería cerrarlo para siempre.
    if (mosaicDock_ != nullptr) {
        auto* toggle = mosaicDock_->toggleViewAction();
        toggle->setText(tr("Piezas del encuadre (mosaico)"));
        toggle->setToolTip(
            tr("Enseña cada pieza recortada y numerada, todas al mismo tamaño. "
               "Pulsa una para medir esa."));
        viewMenu->addAction(toggle);
    }

    // La tabla de medidas en vivo. Sin esta entrada el panel existiría y no se
    // podría encontrar: arranca cerrado a propósito.
    if (measurementsDock_ != nullptr) {
        auto* toggle = measurementsDock_->toggleViewAction();
        toggle->setObjectName(QStringLiteral("measurementsToggle"));
        toggle->setText(tr("Medidas en vivo (tabla)"));
        toggle->setToolTip(
            tr("Lo que mide cada herramienta de cada pieza, con su banda y su "
               "veredicto."));
        toggle->setWhatsThis(
            tr("Sobre el vídeo solo caben los números de una pieza; aquí se leen todos, "
               "y las que no llegan a medir dicen por qué."));
        viewMenu->addAction(toggle);
    }

    // Y lo que cambia CÓMO SE VE la pieza, en el suyo: seguir su giro y realzar
    // el contraste no abren ni cierran nada, cambian lo que hay en pantalla.
    viewMenu->addSeparator();
    trackRotationAction_ = viewMenu->addAction(tr("Seguir rotación de la pieza"));
    trackRotationAction_->setCheckable(true);
    trackRotationAction_->setChecked(pipelineConfig_.autoOrient);
    trackRotationAction_->setToolTip(
        tr("Hace que las herramientas sigan a la pieza cuando llega girada."));
    connect(trackRotationAction_, &QAction::toggled, this, [this](bool on) {
        pipelineConfig_.autoOrient = on;
        persistPipelineConfig();
        statusBar()->showMessage(on ? tr("Siguiendo la rotación de la pieza.")
                                    : tr("Pieza mostrada vertical (orientación fija)."));
    });

    // Tablero de referencia centrado (T2): overlay + elección de origen.
    viewMenu->addSeparator();
    boardAction_ = viewMenu->addAction(tr("Tablero de referencia (centro = 0)"));
    boardAction_->setCheckable(true);
    boardAction_->setChecked(boardVisible_);
    boardAction_->setToolTip(
        tr("Dibuja ejes y grilla con el cero en el origen elegido, para medir la "
           "posición de la pieza."));
    boardAction_->setWhatsThis(
        tr("Enseña la desviación en X/Y y el ángulo en vez de solo distancias sueltas. "
           "+X a la derecha, +Y hacia arriba."));
    connect(boardAction_, &QAction::toggled, this, [this](bool on) {
        boardVisible_ = on;
        video_->setBoardVisible(on);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("board_visible", on ? 1 : 0);
        }
        updateBoardReadout();
        statusBar()->showMessage(on ? tr("Tablero de referencia activo (centro = 0).")
                                    : tr("Tablero de referencia oculto."));
    });

    rulerAction_ = viewMenu->addAction(tr("Regla graduada"));
    rulerAction_->setCheckable(true);
    rulerAction_->setChecked(rulerVisible_);
    rulerAction_->setToolTip(
        tr("Reglas en los bordes con marcas, barra de escala y posición del cursor."));
    rulerAction_->setWhatsThis(
        tr("Sirve para leer una medida de un vistazo sin dibujar una herramienta."));
    connect(rulerAction_, &QAction::toggled, this, [this](bool on) {
        rulerVisible_ = on;
        video_->setRulerVisible(on);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("ruler_visible", on ? 1 : 0);
        }
    });

    // REALZAR PARA VER, que no es lo mismo que subir el brillo de la camara.
    //
    // La diferencia esta escrita en el propio texto de ayuda porque es la que
    // importa: los controles de «Camara e imagen» cambian el fotograma que se
    // ANALIZA —y con el, el umbral, la polaridad y todas las cotas—. Esto solo
    // cambia lo que se pinta. Un operador que no vea la pieza va a tocar lo
    // primero que encuentre, y conviene que lo primero que encuentre sea lo que
    // no le mueve las medidas.
    viewEnhanceAction_ = viewMenu->addAction(tr("Realzar la imagen para verla"));
    viewEnhanceAction_->setObjectName(QStringLiteral("viewEnhanceAction"));
    viewEnhanceAction_->setCheckable(true);
    viewEnhanceAction_->setToolTip(
        tr("Estira el contraste en pantalla para distinguir una pieza oscura sobre "
           "fondo oscuro."));
    viewEnhanceAction_->setWhatsThis(
        tr("Solo cambia lo que se pinta: las medidas siguen saliendo del fotograma "
           "original. Para arreglar la iluminación de verdad, usa Configurar ▸ "
           "Configurar…, pestaña Cámara e imagen."));
    connect(viewEnhanceAction_, &QAction::toggled, this, [this](bool on) {
        video_->setViewEnhance(on);
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("view_enhance", on ? 1 : 0);
        }
        if (!on) {
            statusBar()->showMessage(tr("Realce de vista apagado."));
        } else if (video_->viewEnhanceActive()) {
            statusBar()->showMessage(
                tr("Realce de vista activo. Solo cambia lo que se ve: las medidas "
                   "salen del fotograma original."));
        } else {
            // Decirlo, en vez de dejar al operador dudando de si el interruptor
            // hace algo.
            statusBar()->showMessage(
                tr("Realce de vista activo, pero esta imagen ya usa todo el rango: "
                   "no hay nada que estirar."));
        }
    });

    auto* boardMenu = viewMenu->addMenu(tr("Origen del tablero"));
    boardOriginGroup_ = new QActionGroup(this);
    const struct {
        QString label;
        QString tip;
        vision::BoardOrigin origin;
    } origins[] = {
        {tr("Automático: centro del contorno"),
         tr("Centra el cero en el centro geométrico de la pieza, el que se ve "
            "centrado."),
         vision::BoardOrigin::PieceBounds},
        {tr("Automático: centro de masa"),
         tr("Centro de masa del contorno. En piezas asimétricas queda desplazado "
            "respecto al centro que se ve."),
         vision::BoardOrigin::PieceCenter},
        {tr("Automático: centro de la imagen"),
         tr("El cero queda fijo en pantalla: mide cuánto se desvía la pieza del centro "
            "del campo de visión."),
         vision::BoardOrigin::ImageCenter},
        {tr("Manual: punto fijado a mano…"),
         tr("Marca un punto de la imagen con el ratón y todo se mide respecto a él."),
         vision::BoardOrigin::FixedPoint},
    };
    for (const auto& entry : origins) {
        auto* action = boardMenu->addAction(entry.label);
        action->setCheckable(true);
        action->setToolTip(entry.tip);
        action->setData(static_cast<int>(entry.origin));
        action->setChecked(entry.origin == boardConfig_.origin);
        boardOriginGroup_->addAction(action);
        // El punto a mano se marca CON EL RATÓN SOBRE LA IMAGEN, así que sin
        // imagen no se puede elegir: al pulsarlo el lienzo se queda esperando
        // un clic que no puede llegar, y el tablero se queda en un origen que
        // nadie fijó. Los tres automáticos sí son ajustes y se pueden dejar
        // puestos de antemano.
        if (entry.origin == vision::BoardOrigin::FixedPoint) {
            boardFixedPointAction_ = action;
        }
    }
    connect(boardOriginGroup_, &QActionGroup::triggered, this, &MainWindow::onBoardOriginChanged);

    boardFollowAction_ = boardMenu->addAction(tr("Ejes girados con la pieza"));
    boardFollowAction_->setCheckable(true);
    boardFollowAction_->setChecked(boardConfig_.followPieceAngle);
    boardFollowAction_->setToolTip(
        tr("Los ejes giran con la pieza en vez de quedar alineados con la imagen."));
    connect(boardFollowAction_, &QAction::toggled, this, [this](bool on) {
        boardConfig_.followPieceAngle = on;
        video_->setBoardConfig(boardConfig_);
        persistBoardConfig();
        updateBoardReadout();
    });

    connect(unitGroup_, &QActionGroup::triggered, this, &MainWindow::onUnitChanged);

    auto* helpMenu = menuBar()->addMenu(tr("A&yuda"));
    if (auto* guide = shortcutAction(QStringLiteral("shortcuts_help"),
                                     tr("Atajos de teclado…"))) {
        helpMenu->addAction(guide);
    } else {
        helpMenu->addAction(tr("Atajos de teclado…"), this, &MainWindow::onShowShortcuts);
    }
    // Lo último: las explicaciones se ponen cuando ya existen todas las
    // entradas, y así vale con un solo sitio en vez de veinticinco.
    explainMenus();
}

// Acciones con atajo configurable: el valor por defecto puede sobreescribirse
// desde la guía (F1) y persiste en Settings ("key_<id>").
QAction* MainWindow::shortcutAction(const QString& id, const QString& menuText) {
    // LA MISMA ACCIÓN, NO UNA GEMELA.
    //
    // Ninguna de las 58 entradas de menú enseñaba su atajo, y el arreglo obvio
    // —`setShortcut` en la entrada— es el equivocado: los atajos ya son
    // `QAction` invisibles colgadas de la ventana, así que poner la misma tecla
    // en la entrada del menú da DOS acciones con la misma secuencia en la misma
    // ventana. Eso es `ambiguousActivate`: Qt no dispara ninguna de forma
    // fiable. Este proyecto ya se comió ese fallo con Ctrl+1 y Ctrl+2.
    //
    // Así que la entrada del menú no se crea: se cuelga la que ya existe. Una
    // sola acción, un solo atajo, y el menú lo enseña solo porque Qt pinta la
    // secuencia de la acción que le den.
    //
    // Se le cambia el texto al del menú —«Calibrar escala (mm)…» en vez de
    // «Calibrar mm…»— y eso no toca la guía de atajos, que enseña
    // `ShortcutSpec::description` y no el texto de la acción. Son dos sitios
    // con dos públicos: el menú se lee de pasada, la guía se lee buscando.
    for (const auto& spec : shortcuts_) {
        if (spec.id == id && spec.action != nullptr) {
            if (!menuText.isEmpty()) {
                spec.action->setText(menuText);
            }
            return spec.action;
        }
    }
    // Sin la acción no se deja el menú cojo: el llamante pone la entrada de
    // siempre. Devolver nulo y que alguien lo cuelgue sería una entrada vacía.
    return nullptr;
}

void MainWindow::buildShortcuts() {
    auto addShortcut = [this](const QString& id, const QString& description,
                              const QKeySequence& defaultKey, auto slot) {
        auto* action = new QAction(description, this);
        QKeySequence key = defaultKey;
        if (repos_.settings != nullptr) {
            const auto saved =
                repos_.settings->getString(("key_" + id).toStdString(), std::string());
            if (saved.isOk() && !saved.value().empty()) {
                key = QKeySequence(QString::fromStdString(saved.value()));
            }
        }
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        shortcuts_.push_back({id, description, defaultKey, action});
    };

    addShortcut("undo", tr("Deshacer (herramientas dibujadas)"), QKeySequence::Undo,
                &MainWindow::onUndo);
    addShortcut("redo", tr("Rehacer"), QKeySequence::Redo, &MainWindow::onRedo);
    addShortcut("delete_tool", tr("Borrar la herramienta seleccionada"),
                QKeySequence(Qt::Key_Delete), &MainWindow::onDeleteToolClicked);
    addShortcut("select_mode", tr("Modo Mover/Elegir (cancela dibujo y rasgo)"),
                QKeySequence(Qt::Key_Escape), [this] {
                    anchorButton_->setChecked(false);
                    toolPalette_->activate(std::nullopt);
                });

    // Atajos por FAMILIA + dígito, generados de las propias familias. Antes
    // había una tabla escrita a mano con un dígito por herramienta, y se quedó
    // corta: con catorce herramientas y diez dígitos, Arco, Eje, Rosca y
    // Engranaje no tenían tecla.
    int familyNumber = 0;
    for (const inspection::ToolCategory category : inspection::allToolCategories()) {
        if (inspection::toolsInCategory(category).empty()) {
            continue;
        }
        ++familyNumber;
        addShortcut(QStringLiteral("tool_family_%1").arg(familyNumber),
                    tr("Familia: %1").arg(QString::fromUtf8(
                        inspection::categoryLabel(category))),
                    QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + familyNumber)),
                    [this, category] { toolPalette_->activateCategory(category); });
    }
    for (int slot = 1; slot <= 9; ++slot) {
        addShortcut(QStringLiteral("tool_slot_%1").arg(slot),
                    tr("Herramienta %1 de la familia activa").arg(slot),
                    QKeySequence(static_cast<Qt::Key>(Qt::Key_0 + slot)),
                    [this, slot] { toolPalette_->activateInCurrentCategory(slot - 1); });
    }

    addShortcut("camera_toggle", tr("Iniciar/Detener cámara"), QKeySequence(Qt::Key_V),
                [this] {
                    if (startStopButton_->isEnabled()) {
                        onStartStopClicked();
                    }
                });
    addShortcut("register_live", tr("Registrar y activar"), QKeySequence(Qt::Key_R),
                &MainWindow::onRegisterLiveClicked);
    addShortcut("auto_inspect", tr("Auto-inspección (alternar)"), QKeySequence(Qt::Key_A),
                [this] { autoInspectButton_->toggle(); });
    addShortcut("inspect_once", tr("Inspeccionar una vez"), QKeySequence(Qt::Key_I),
                &MainWindow::onInspectClicked);
    addShortcut("template_editor", tr("Abrir Plantilla…"), QKeySequence(Qt::Key_P),
                &MainWindow::onOpenEditorClicked);
    addShortcut("calibrate", tr("Calibrar mm…"), QKeySequence(Qt::Key_C),
                &MainWindow::onCalibrateClicked);
    addShortcut("anchor", tr("Marcar rasgo distintivo"), QKeySequence(Qt::Key_D),
                [this] { anchorButton_->toggle(); });
    addShortcut("duplicate_tool", tr("Duplicar la herramienta seleccionada"),
                QKeySequence(Qt::CTRL | Qt::Key_D), &MainWindow::onDuplicateToolClicked);
    addShortcut("save_template", tr("Guardar la plantilla (herramientas en vivo)"),
                QKeySequence::Save, &MainWindow::onSaveTemplateClicked);
    // MEDIR PIEZA TIENE TECLA, y la tiene por una razón concreta.
    //
    // El taller pidió «otra ventana en donde al momento de realizar las
    // mediciones te mostrara los cálculos, como área, perímetro, etc.». Esa
    // ventana existe desde hace tiempo: es este botón, y da perímetro, área,
    // agujeros, circularidad, diámetros y las cotas dibujadas, con copiar y
    // exportar.
    //
    // Que alguien pida algo que ya está es la mejor prueba posible de que no se
    // encuentra. Y no se encuentra porque es uno de trece botones del mismo
    // peso: destacarlo también sería quitarle el sitio al de inspeccionar, que
    // es el único destacado a propósito.
    //
    // Así que se hace descubrible por donde se descubren las cosas: el menú, que
    // desde hace poco enseña la tecla de cada entrada. La M es la letra natural
    // y estaba libre — A, C, D, I, P, R y V ya tienen dueño.
    addShortcut("measure_piece", tr("Medir la pieza (área, perímetro, cotas)"),
                QKeySequence(Qt::Key_M), &MainWindow::onMeasurePieceClicked);
    addShortcut("shortcuts_help", tr("Guía de atajos"), QKeySequence(Qt::Key_F1),
                &MainWindow::onShowShortcuts);
    // Ctrl+O, la de cualquier programa. Sin ella, abrir un fichero pedía elegir
    // «Abrir imagen…» en el desplegable y luego pulsar el botón de al lado.
    addShortcut("open_file", tr("Abrir imagen o vídeo…"), QKeySequence::Open,
                &MainWindow::onOpenFileClicked);

    // Vista (Z3). ZoomIn cubre Ctrl++ y Ctrl+= (la misma tecla sin Shift).
    addShortcut("zoom_in", tr("Acercar la vista"), QKeySequence::ZoomIn,
                [this] { video_->zoomIn(); });
    addShortcut("zoom_out", tr("Alejar la vista"), QKeySequence::ZoomOut,
                [this] { video_->zoomOut(); });
    addShortcut("zoom_fit", tr("Ajustar la vista a la ventana (zoom mínimo)"),
                QKeySequence(Qt::CTRL | Qt::Key_0), [this] { video_->zoomToMin(); });
    // CTRL+1 Y CTRL+2 YA ESTABAN COGIDOS.
    //
    // Las cinco familias de herramientas se reparten Ctrl+1 … Ctrl+5 más arriba,
    // y aquí se volvían a pedir Ctrl+1 para «vista al 100 %» y Ctrl+2 para
    // «zoom máximo». Dos acciones con la misma secuencia en la misma ventana no
    // se reparten el turno: Qt emite `ambiguousActivate` y NO dispara ninguna de
    // forma fiable. Los dos atajos estaban documentados en F1 y ninguno de los
    // cuatro hacía lo que decía.
    //
    // Se mueven a Ctrl+Alt, que no choca con nada, y se dejan al lado de
    // Ctrl+0 —que sí está libre— en vez de repartirlos por otras teclas: los
    // tres son «encuadre», y encuadrar con tres modificadores distintos sería
    // otra forma de lo mismo.
    addShortcut("zoom_actual", tr("Vista al 100 % (píxeles reales)"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1),
                [this] { video_->zoomToActualPixels(); });
    addShortcut("zoom_max", tr("Zoom máximo"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2),
                [this] { video_->zoomToMax(); });
}

// Porcentaje visible y estado de los botones de la barra de zoom.
void MainWindow::updateZoomIndicator() {
    const double scale = video_->displayScale();
    const bool hasImage = scale > 0.0;
    zoomLabel_->setText(hasImage ? tr("%1 %").arg(qRound(scale * 100.0))
                                 : QStringLiteral("—"));
    zoomLabel_->setEnabled(hasImage);
    zoomMinButton_->setEnabled(hasImage && !video_->atMinZoom());
    zoomOutButton_->setEnabled(hasImage && !video_->atMinZoom());
    zoomInButton_->setEnabled(hasImage && !video_->atMaxZoom());
    zoomMaxButton_->setEnabled(hasImage && !video_->atMaxZoom());
}

void MainWindow::onShowShortcuts() {
    ShortcutsDialog dialog(&shortcuts_, repos_.settings, this);
    keepDialogSize(dialog, repos_.settings, "shortcuts", 560, 620);
    dialog.exec();
}

// Si la auto-inspección se puede encender AHORA, y si no, por qué no.
//
// Antes esto se comprobaba DESPUÉS de pulsar y se contestaba con un
// `QMessageBox` modal: el operador encendía el conmutador, le saltaba un diálogo
// diciendo que no, y el conmutador se apagaba solo. Tres pasos para enterarse de
// algo que se podía ver antes de tocar nada.
//
// Y el modal tenía un segundo coste, que apareció al escribir su test: sin
// pantalla bloquea para siempre, así que el banco se colgó cinco minutos hasta
// que hubo que matar el proceso. Un control que no se puede probar es un control
// que nadie va a probar.
//
// Ahora está apagado con su motivo en el tooltip, que es lo que este proyecto ya
// hace en los botones de borrar: se lee antes de pulsar, y se puede comprobar.
// Los comandos que prometen algo, con lo que cada uno necesita.
//
// Se registran DESPUÉS de construir la interfaz y de repartir los tooltips,
// porque aquí se guarda el texto de siempre para poder devolverlo cuando el
// comando vuelva a poder usarse. Sin eso, apagar y encender una vez dejaría al
// operador con el motivo de la última vez que no se podía.
void MainWindow::registerGatedCommands() {
    const auto add = [this](QAction* action, QAbstractButton* button, bool needsImage,
                            bool needsPiece, bool needsEngine) {
        if (action == nullptr && button == nullptr) {
            return;
        }
        GatedCommand gate;
        gate.action = action;
        gate.button = button;
        gate.needsImage = needsImage;
        gate.needsPiece = needsPiece;
        gate.needsEngine = needsEngine;
        // El compilador no sabe que uno de los dos existe —lo garantiza la
        // guarda de arriba—, así que se escribe en dos pasos en vez de con un
        // ternario que él lee como «puede llamar a `toolTip` sobre nulo».
        if (action != nullptr) {
            gate.help = action->toolTip();
        } else if (button != nullptr) {
            gate.help = button->toolTip();
        }
        gated_.push_back(std::move(gate));
    };

    // Necesitan una IMAGEN delante: sin fotograma no hay nada que medir ni
    // sobre lo que hacer clic.
    add(shortcutAction(QStringLiteral("calibrate")), nullptr, true, false, false);
    add(shortcutAction(QStringLiteral("measure_piece")), measurePieceButton_, true, false,
        false);
    add(shortcutAction(QStringLiteral("template_editor")), nullptr, true, false, false);
    add(boardFixedPointAction_, nullptr, true, false, false);

    // Necesitan una PIEZA registrada seleccionada: todas guardan o consultan
    // algo suyo.
    add(measurementModeAction_, nullptr, false, true, false);
    add(registerVariantAction_, nullptr, false, true, false);
    add(historyAction_, nullptr, false, true, false);
    add(shortcutAction(QStringLiteral("save_template")), nullptr, false, true, false);

    // Y la inspección, las tres cosas: pieza que comparar, imagen que mirar y
    // motor que juzgue.
    add(shortcutAction(QStringLiteral("inspect_once")), inspectButton_, true, true, true);

    updateGatedCommands();
}

void MainWindow::updateGatedCommands() {
    const bool hasImage = !lastFrame_.isNull();
    const bool hasPiece = selectedPieceId() >= 0;
    const bool hasEngine = repos_.engine != nullptr;

    for (const auto& gate : gated_) {
        QStringList missing;
        if (gate.needsImage && !hasImage) {
            missing << tr("no hay ninguna imagen delante (abre una fuente o una foto)");
        }
        if (gate.needsPiece && !hasPiece) {
            missing << tr("no hay ninguna pieza registrada seleccionada");
        }
        if (gate.needsEngine && !hasEngine) {
            missing << tr("no hay motor de inspección");
        }
        const bool usable = missing.isEmpty();
        // El motivo SUSTITUYE al tooltip mientras está apagado, y se devuelve el
        // de siempre al reactivarse. Añadirlo al final dejaría al operador
        // leyendo un párrafo de ayuda para encontrar la única línea que le
        // importa: por qué no puede pulsar.
        const QString tip = usable ? gate.help
                                   : tr("Todavía no se puede: %1.")
                                         .arg(missing.join(tr("; ")));
        if (gate.action != nullptr) {
            gate.action->setEnabled(usable);
            gate.action->setToolTip(tip);
        }
        if (gate.button != nullptr) {
            gate.button->setEnabled(usable);
            gate.button->setToolTip(tip);
        }
    }
}

}  // namespace pci::ui
