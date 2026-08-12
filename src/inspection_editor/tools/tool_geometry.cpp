#include "inspection_editor/tools/tool_geometry.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

namespace pci::inspection {

const char* toolTypeName(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return "caliper";
        case ToolType::Circle: return "circle";
        case ToolType::PointToLine: return "point_to_line";
        case ToolType::EdgeFlaw: return "edge_flaw";
        case ToolType::Blob: return "blob";
        case ToolType::Ruler: return "ruler";
        case ToolType::LineToLine: return "line_to_line";
        case ToolType::Angle: return "angle";
        case ToolType::PolyBlob: return "poly_blob";
        case ToolType::Position: return "position";
        case ToolType::Arc: return "arc";
        case ToolType::Shaft: return "shaft";
        case ToolType::Thread: return "thread";
        case ToolType::Gear: return "gear";
        case ToolType::ConstructedPoint: return "constructed_point";
        case ToolType::ConstructedLine: return "constructed_line";
        case ToolType::MedianAxis: return "median_axis";
        case ToolType::Region: return "region";
        case ToolType::Symmetry: return "symmetry";
        case ToolType::Polygon: return "polygon";
        case ToolType::EdgeDefects: return "edge_defects";
        case ToolType::Clearance: return "clearance";
        case ToolType::Straightness: return "straightness";
        case ToolType::Roundness: return "roundness";
        case ToolType::Orientation: return "orientation";
        case ToolType::CentreOffset: return "centre_offset";
        case ToolType::BoltPattern: return "bolt_pattern";
        case ToolType::Profile: return "profile";
        case ToolType::Extremes: return "extremes";
    }
    return "unknown";
}

const std::array<ToolType, 29>& allToolTypes() {
    static const std::array<ToolType, 29> kTypes{
        ToolType::Caliper,  ToolType::Circle,   ToolType::PointToLine, ToolType::EdgeFlaw,
        ToolType::Blob,     ToolType::Ruler,    ToolType::LineToLine,  ToolType::Angle,
        ToolType::PolyBlob, ToolType::Position, ToolType::Arc,         ToolType::Shaft,
        ToolType::Thread,   ToolType::Gear,     ToolType::ConstructedPoint,
        ToolType::ConstructedLine, ToolType::MedianAxis, ToolType::Region,
        ToolType::Symmetry, ToolType::Polygon, ToolType::EdgeDefects,
        ToolType::Clearance, ToolType::Straightness, ToolType::Roundness,
        ToolType::Orientation, ToolType::CentreOffset, ToolType::BoltPattern,
        ToolType::Profile, ToolType::Extremes};
    return kTypes;
}

core::Result<ToolType> toolTypeFromName(const std::string& name) {
    for (const ToolType type : allToolTypes()) {
        if (name == toolTypeName(type)) {
            return core::Result<ToolType>::ok(type);
        }
    }
    return core::Result<ToolType>::err("Tipo de herramienta desconocido: '" + name + "'");
}

const std::array<ToolCategory, 5>& allToolCategories() {
    static const std::array<ToolCategory, 5> kCategories{
        ToolCategory::BasicShape, ToolCategory::InLine, ToolCategory::Construction,
        ToolCategory::Gdt, ToolCategory::TurnedAndExtremes};
    return kCategories;
}

ToolCategory categoryOf(ToolType type) {
    switch (type) {
        // Forma y región: lo que describe la silueta o cuenta lo que hay dentro.
        case ToolType::Blob:
        case ToolType::PolyBlob:
        case ToolType::EdgeFlaw:
        case ToolType::Region:
        case ToolType::Symmetry:
        case ToolType::Polygon:
        case ToolType::EdgeDefects:
            return ToolCategory::BasicShape;
        // La cota directa, que es lo que se pide en un plano.
        case ToolType::Caliper:
        case ToolType::Circle:
        case ToolType::PointToLine:
        case ToolType::Ruler:
        case ToolType::LineToLine:
        case ToolType::Angle:
        case ToolType::Arc:
        case ToolType::Clearance:
            return ToolCategory::InLine;
        // Posición es una tolerancia geométrica: mide contra una referencia.
        case ToolType::Position:
        case ToolType::Straightness:
        case ToolType::Roundness:
        case ToolType::Orientation:
        case ToolType::CentreOffset:
        case ToolType::BoltPattern:
        case ToolType::Profile:
            return ToolCategory::Gdt;
        // No miden: se calculan a partir de otras y existen para ser el datum
        // contra el que miden las de GD&T.
        case ToolType::ConstructedPoint:
        case ToolType::ConstructedLine:
        // El eje medio SÍ mira la imagen, pero lo que entrega es lo mismo que
        // las otras dos: una recta para referenciar.
        case ToolType::MedianAxis:
            return ToolCategory::Construction;
        case ToolType::Shaft:
        case ToolType::Thread:
        case ToolType::Gear:
        case ToolType::Extremes:
            return ToolCategory::TurnedAndExtremes;
    }
    return ToolCategory::InLine;
}

const char* categoryLabel(ToolCategory category) {
    switch (category) {
        case ToolCategory::BasicShape: return "Figuras básicas";
        case ToolCategory::InLine: return "Medición en línea";
        case ToolCategory::Construction: return "Construcciones";
        case ToolCategory::Gdt: return "GD&T";
        case ToolCategory::TurnedAndExtremes: return "Máx./mín. y torneadas";
    }
    return "?";
}

const char* categoryDescription(ToolCategory category) {
    switch (category) {
        case ToolCategory::BasicShape:
            return "Figuras básicas — qué forma tiene la pieza y qué hay dentro:\n"
                   "área, perímetro, simetría, lados, manchas y defectos del borde.";
        case ToolCategory::InLine:
            return "Medición en línea — la cota directa, la que aparece en un plano:\n"
                   "distancias, diámetros, radios y ángulos.";
        case ToolCategory::Construction:
            return "Construcciones — puntos y rectas derivados de otras herramientas\n"
                   "(punto medio, intersección, bisectriz, eje medio). No miden por sí\n"
                   "solas: existen para servir de referencia a las de GD&T.";
        case ToolCategory::Gdt:
            return "GD&T — tolerancias geométricas medidas contra una referencia\n"
                   "declarada: rectitud, redondez, paralelismo, posición verdadera.\n"
                   "Sobre una silueta 2D hay cotas que no se pueden dar, y se dice.";
        case ToolCategory::TurnedAndExtremes:
            return "Máximos, mínimos y piezas torneadas — la medida más grande y la\n"
                   "más pequeña en cualquier dirección, y lo propio del torno:\n"
                   "diámetros, chaflanes, roscas y engranajes.";
    }
    return "";
}

std::vector<ToolType> toolsInCategory(ToolCategory category) {
    std::vector<ToolType> tools;
    for (const ToolType type : allToolTypes()) {
        if (categoryOf(type) == category) {
            tools.push_back(type);
        }
    }
    return tools;
}

std::string paramsWithReferences(const ToolReferences& references) {
    if (references.first.empty() && references.second.empty()) {
        return "{}";
    }
    cv::FileStorage fs(".json", cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
    // Se escriben siempre las dos claves cuando hay alguna: leer una clave
    // ausente ya se trata como vacía, pero un fichero con la mitad de las
    // claves invita a suponer que la otra "no aplica" en vez de "está vacía".
    fs << "ref" << references.first;
    fs << "ref2" << references.second;
    return fs.releaseAndGetString();
}

ToolReferences referencesFromParams(const std::string& paramsJson) {
    if (paramsJson.empty() || paramsJson == "{}") {
        return {};
    }
    try {
        cv::FileStorage fs(paramsJson, cv::FileStorage::READ | cv::FileStorage::MEMORY);
        if (!fs.isOpened()) {
            return {};
        }
        const auto read = [&fs](const char* key) -> std::string {
            const cv::FileNode node = fs[key];
            if (node.empty() || !node.isString()) {
                return {};
            }
            return static_cast<std::string>(node);
        };
        return ToolReferences{read("ref"), read("ref2")};
    } catch (const cv::Exception&) {
        // Parámetros corruptos: se ignoran. Una referencia ilegible hace que la
        // herramienta se comporte como si no la tuviera, y eso se nota al
        // medir; reventar aquí tumbaría la carga de la plantilla entera.
        return {};
    }
}

const std::array<ExtremeMeasure, 2>& allExtremeMeasures() {
    static const std::array<ExtremeMeasure, 2> kMeasures{ExtremeMeasure::MinWidth,
                                                         ExtremeMeasure::MaxSpan};
    return kMeasures;
}

const char* extremeMeasureLabel(ExtremeMeasure measure) {
    switch (measure) {
        case ExtremeMeasure::MinWidth: return "Anchura mínima";
        case ExtremeMeasure::MaxSpan: return "Diámetro máximo";
    }
    return "?";
}

const std::array<RegionMeasure, 6>& allRegionMeasures() {
    static const std::array<RegionMeasure, 6> kMeasures{
        RegionMeasure::Area,        RegionMeasure::Perimeter,   RegionMeasure::Solidity,
        RegionMeasure::Circularity, RegionMeasure::AspectRatio, RegionMeasure::HoleCount};
    return kMeasures;
}

const char* regionMeasureLabel(RegionMeasure measure) {
    switch (measure) {
        case RegionMeasure::Area: return "Área";
        case RegionMeasure::Perimeter: return "Perímetro";
        case RegionMeasure::Solidity: return "Solidez";
        case RegionMeasure::Circularity: return "Circularidad";
        case RegionMeasure::AspectRatio: return "Relación de aspecto";
        case RegionMeasure::HoleCount: return "Número de agujeros";
    }
    return "?";
}

const std::array<PointConstruction, 4>& allPointConstructions() {
    static const std::array<PointConstruction, 4> kModes{
        PointConstruction::Midpoint, PointConstruction::Intersection,
        PointConstruction::Projection, PointConstruction::CircleCenter};
    return kModes;
}

const std::array<LineConstruction, 4>& allLineConstructions() {
    static const std::array<LineConstruction, 4> kModes{
        LineConstruction::ThroughTwoPoints, LineConstruction::Bisector,
        LineConstruction::ParallelThrough, LineConstruction::PerpendicularThrough};
    return kModes;
}

const char* constructionLabel(PointConstruction mode) {
    switch (mode) {
        case PointConstruction::Midpoint: return "Punto medio de dos";
        case PointConstruction::Intersection: return "Intersección de dos rectas";
        case PointConstruction::Projection: return "Proyección de un punto sobre una recta";
        case PointConstruction::CircleCenter: return "Centro de un círculo";
    }
    return "?";
}

const char* constructionLabel(LineConstruction mode) {
    switch (mode) {
        case LineConstruction::ThroughTwoPoints: return "Recta por dos puntos";
        case LineConstruction::Bisector: return "Bisectriz de dos rectas";
        case LineConstruction::ParallelThrough: return "Paralela por un punto";
        case LineConstruction::PerpendicularThrough: return "Perpendicular por un punto";
    }
    return "?";
}

std::array<OperandKind, 2> operandsOf(PointConstruction mode) {
    switch (mode) {
        case PointConstruction::Midpoint:
            return {OperandKind::Point, OperandKind::Point};
        case PointConstruction::Intersection:
            return {OperandKind::Line, OperandKind::Line};
        case PointConstruction::Projection:
            return {OperandKind::Point, OperandKind::Line};
        case PointConstruction::CircleCenter:
            return {OperandKind::Circle, OperandKind::Unused};
    }
    return {OperandKind::Unused, OperandKind::Unused};
}

std::array<OperandKind, 2> operandsOf(LineConstruction mode) {
    switch (mode) {
        case LineConstruction::ThroughTwoPoints:
            return {OperandKind::Point, OperandKind::Point};
        case LineConstruction::Bisector:
            return {OperandKind::Line, OperandKind::Line};
        case LineConstruction::ParallelThrough:
        case LineConstruction::PerpendicularThrough:
            return {OperandKind::Line, OperandKind::Point};
    }
    return {OperandKind::Unused, OperandKind::Unused};
}

const char* operandKindLabel(OperandKind kind) {
    switch (kind) {
        // "con punto" y no "punto" porque un Círculo vale donde se pide un
        // punto: aporta su centro. Decir "punto" a secas haría pensar que no.
        case OperandKind::Point: return "una herramienta con punto (o un círculo)";
        case OperandKind::Line: return "una herramienta con recta";
        case OperandKind::Circle: return "un círculo";
        case OperandKind::Unused: return "nada";
    }
    return "?";
}

const char* toolTypeLabel(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return "Caliper";
        case ToolType::Circle: return "Círculo";
        case ToolType::PointToLine: return "Punto-Línea";
        case ToolType::EdgeFlaw: return "Borde liso";
        case ToolType::Blob: return "Blob";
        case ToolType::Ruler: return "Regla";
        case ToolType::LineToLine: return "Línea-Línea";
        case ToolType::Angle: return "Ángulo";
        case ToolType::PolyBlob: return "Blob poligonal";
        case ToolType::Position: return "Posición";
        case ToolType::Arc: return "Arco";
        case ToolType::Shaft: return "Eje / Diámetro";
        case ToolType::Thread: return "Rosca";
        case ToolType::Gear: return "Engranaje";
        case ToolType::ConstructedPoint: return "Punto construido";
        case ToolType::ConstructedLine: return "Recta construida";
        case ToolType::MedianAxis: return "Eje medio";
        case ToolType::Region: return "Región";
        case ToolType::Symmetry: return "Simetría";
        case ToolType::Polygon: return "Lados";
        case ToolType::EdgeDefects: return "Rebabas y mellas";
        case ToolType::Clearance: return "Holgura";
        case ToolType::Straightness: return "Rectitud (zona mínima)";
        case ToolType::Roundness: return "Redondez (zona mínima)";
        case ToolType::Orientation: return "Orientación";
        case ToolType::CentreOffset: return "Desviación de centros";
        case ToolType::BoltPattern: return "Patrón de agujeros";
        case ToolType::Profile: return "Perfil de línea";
        case ToolType::Extremes: return "Máx./mín.";
    }
    return "?";
}

const char* toolTypeDescription(ToolType type) {
    switch (type) {
        case ToolType::Caliper:
            return "Caliper — mide la distancia entre dos bordes (px).\n"
                   "Dibuja una línea que CRUCE perpendicularmente los dos bordes a medir\n"
                   "(p. ej. de lado a lado del ancho de un brazo o una ranura).";
        case ToolType::Circle:
            return "Círculo — mide el diámetro y la redondez de un contorno circular.\n"
                   "Arrastra desde el CENTRO del círculo (o agujero) hasta su borde;\n"
                   "el borde se busca en una banda alrededor de ese radio.";
        case ToolType::PointToLine:
            return "Punto-Línea — mide la distancia perpendicular de un borde a una\n"
                   "línea de referencia. Dibuja la línea de referencia; el escaneo que\n"
                   "localiza el borde queda perpendicular en su punto medio\n"
                   "(muévelo con Mover/Elegir si hace falta).";
        case ToolType::EdgeFlaw:
            return "Borde liso — detecta irregularidades (muescas, rebabas, golpes) en\n"
                   "un borde que debería ser recto. Dibuja una línea SOBRE el borde a\n"
                   "vigilar; se mide la desviación máxima respecto a la recta ideal.\n"
                   "OJO: solo ve lo que cae dentro del largo de escaneo (campo\n"
                   "Escaneos/largo); una muesca más profunda que esa ventana pasa\n"
                   "desapercibida — súbelo si esperas defectos grandes.";
        case ToolType::Blob:
            return "Blob — cuenta manchas, agujeros o elementos dentro de una región.\n"
                   "Arrastra un rectángulo sobre la zona a vigilar; por defecto busca\n"
                   "elementos oscuros sobre fondo claro (área mínima 20 px²).";
        case ToolType::Ruler:
            return "Regla — distancia directa entre dos puntos fijos de la pieza\n"
                   "(no busca bordes: mide exactamente lo que trazas). Con la escala\n"
                   "calibrada, la medida sale en mm/cm. Ideal para medir al vuelo.";
        case ToolType::LineToLine:
            return "Línea-Línea — ángulo entre dos líneas de referencia.\n"
                   "Traza la primera línea y luego la segunda (dos arrastres); mide el\n"
                   "ángulo entre ambas en grados y también su separación. Útil para\n"
                   "verificar paralelismo o el ángulo entre dos bordes de la pieza.";
        case ToolType::Angle:
            return "Ángulo — mide el ángulo de una esquina en grados.\n"
                   "Arrastra del VÉRTICE al extremo del primer lado y luego marca el\n"
                   "extremo del segundo lado; se mide el ángulo interior (0°..180°)\n"
                   "con tolerancia en grados. Ideal para chaflanes y esquinas.";
        case ToolType::PolyBlob:
            return "Blob poligonal — cuenta manchas dentro de una región de forma\n"
                   "libre. Haz clic para ir marcando los vértices del polígono y\n"
                   "cierra haciendo clic sobre el primero. Igual que el Blob pero\n"
                   "para zonas irregulares que un rectángulo no cubre bien.";
        case ToolType::Position:
            return "Posición — vigila DÓNDE cae un rasgo respecto al cero del tablero\n"
                   "de referencia (Ver ▸ Tablero). Marca el rasgo con un clic-arrastre;\n"
                   "se mide su desviación (radial, en X o en Y) y se compara con las\n"
                   "tolerancias. Con el cero en la pieza la desviación es fija: usa el\n"
                   "centro de la imagen o un punto fijado para que signifique algo.\n"
                   "CON REFERENCIA es la posición verdadera de la norma: elige el datum\n"
                   "primario en Referencia (una recta: orienta el marco) y el secundario\n"
                   "en 2ª referencia (fija el origen). Entonces la medida es el DIÁMETRO\n"
                   "DE ZONA, 2·raíz(dx²+dy²), medido en ese marco — y no cambia aunque\n"
                   "la pieza llegue girada, porque todo se mide dentro del marco.\n"
                   "Solo es honesta si los datums se resuelven en el plano de la imagen:\n"
                   "una cara perpendicular a la cámara no da datum, y entonces no mide.";
        case ToolType::Arc:
            return "Arco — mide el RADIO de una esquina redondeada o un redondeo.\n"
                   "Marca tres puntos SOBRE el arco: los dos extremos y uno\n"
                   "intermedio, igual que al comprobarlo con una plantilla de radios.\n"
                   "El Círculo no sirve aquí: pide un centro y un contorno cerrado,\n"
                   "y en una esquina no hay ninguno de los dos.";
        case ToolType::Shaft:
            return "Eje / Diámetro — para piezas de torno vistas de perfil.\n"
                   "Traza el EJE a lo largo de la pieza, por el medio; se exploran\n"
                   "los dos bordes y se miden de una vez el DIÁMETRO, la CONICIDAD\n"
                   "(si no es cilíndrica) y la RECTITUD. Un calíper mide en un solo\n"
                   "punto y ahí no se distingue un cilindro de un cono.";
        case ToolType::Thread:
            return "Rosca — mide PASO, diámetro exterior y de fondo, y ángulo de\n"
                   "flanco de un tornillo visto DE PERFIL. Traza el eje a lo largo\n"
                   "de la parte roscada: el perfil se repite una vez por vuelta, y\n"
                   "de ese periodo sale el paso. Con la escala calibrada propone\n"
                   "además la designación métrica (M6×1, M8×1.25...).\n"
                   "Necesita ver varias vueltas y buen contraste de borde.";
        case ToolType::Gear:
            return "Engranaje — cuenta los DIENTES y mide diámetro de cabeza, de\n"
                   "raíz, módulo y excentricidad. La rueda debe verse DE CARA.\n"
                   "Arrastra del centro del engranaje hacia fuera, pasando la punta\n"
                   "de los dientes; el perfil radial se repite una vez por diente.\n"
                   "El módulo exige calibración px→mm: sin escala real no existe.";
        case ToolType::ConstructedPoint:
            return "Punto construido — NO mide: calcula un punto a partir de otras\n"
                   "herramientas para que sirva de referencia. Punto medio de dos,\n"
                   "corte de dos rectas, proyección de un punto sobre una recta o\n"
                   "centro de un círculo. Colócalo con un clic (solo fija dónde se\n"
                   "escribe) y elige la construcción y sus dos referencias.";
        case ToolType::ConstructedLine:
            return "Recta construida — NO mide: calcula una recta a partir de otras\n"
                   "herramientas para usarla como DATUM. Por dos puntos, bisectriz de\n"
                   "dos rectas (si son paralelas, la recta media), o paralela y\n"
                   "perpendicular a una recta por un punto. Es lo que le falta al\n"
                   "GD&T para medir contra algo declarado y no contra un supuesto.";
        case ToolType::MedianAxis:
            return "Eje medio — la línea que va por el CENTRO de una pieza alargada,\n"
                   "a media distancia entre sus dos flancos. Es el datum natural de una\n"
                   "pieza de torno. Traza el eje a lo largo de la pieza, por el medio:\n"
                   "da igual que quede descentrado, porque lo que se calcula es el punto\n"
                   "medio entre los bordes reales, no la línea que dibujaste. Mide su\n"
                   "RECTITUD y avisa de la desalineación entre la mitad de un tramo y la\n"
                   "otra, que es lo que delata dos diámetros que no son coaxiales.";
        case ToolType::Region:
            return "Región — describe la FORMA de lo que hay dentro del recuadro.\n"
                   "Arrastra un rectángulo sobre la pieza y elige qué medir: área,\n"
                   "perímetro, solidez, circularidad, relación de aspecto o número de\n"
                   "agujeros. Cada Región vigila UNA cosa con su tolerancia, así que\n"
                   "pon una por cada medida que te importe y deja fuera las demás.\n"
                   "Referencias medidas de la circularidad: un círculo da 0,99 y un\n"
                   "cuadrado 0,82 (el valor exacto de un cuadrado es 0,785; la\n"
                   "diferencia es el sesgo conocido de medir un borde recto sobre una\n"
                   "rejilla de píxeles). Lo que separa una forma de otra es de sobra.";
        case ToolType::Symmetry:
            return "Simetría — busca el mejor EJE DE SIMETRÍA de la silueta y da un\n"
                   "grado de 0 a 1 (1 = perfectamente simétrica). Arrastra un\n"
                   "rectángulo sobre la pieza. Sirve para pillar una pieza montada del\n"
                   "revés o con un rasgo que no debería estar: son los casos en los que\n"
                   "la simetría cae y ninguna cota se entera.\n"
                   "NO es la simetría de GD&T —esa se retiró de la norma en 2018—,\n"
                   "es un descriptor de forma; y el eje que encuentra se puede usar\n"
                   "como referencia de otras herramientas.";
        case ToolType::Polygon:
            return "Lados — cuenta los LADOS de un perfil poligonal y mide cada uno\n"
                   "y sus ángulos interiores. Para el hexágono de una tuerca o un\n"
                   "perfil recto. Arrastra un rectángulo sobre la pieza.\n"
                   "El campo Epsilon (en milésimas del perímetro) decide cuánto se\n"
                   "simplifica el contorno: súbelo si cuenta lados de más, bájalo si\n"
                   "se come alguno. Va en fracción del perímetro y no en píxeles a\n"
                   "propósito, para que el recuento no cambie al acercar la cámara.\n"
                   "Si el recuento no aguanta al doblar y al partir ese valor, la\n"
                   "figura no es un polígono claro (un círculo, por ejemplo) y lo dice\n"
                   "en vez de dar un número que cambiaría solo.";
        case ToolType::EdgeDefects:
            return "Rebabas y mellas — cuenta y mide los defectos de un borde UNO A\n"
                   "UNO, en vez de dar una sola desviación máxima como el Borde liso.\n"
                   "Traza una línea SOBRE el borde a vigilar. De cada defecto da su\n"
                   "altura, su extensión y si es rebaba (material de más, hacia fuera)\n"
                   "o mella (material de menos, hacia dentro).\n"
                   "El campo Altura mínima (px) dice a partir de qué desviación algo\n"
                   "cuenta como defecto: la medida es «cuántos defectos mayores que\n"
                   "esto», que es una pregunta con respuesta.\n"
                   "Un borde con una mella grande y otro con veinte pequeñas dan la\n"
                   "misma lectura con el Borde liso, y no son la misma pieza.";
        case ToolType::Clearance:
            return "Holgura — la separación MÁS CORTA entre dos figuras, y dónde\n"
                   "está. Arrastra un recuadro que abarque las dos: se miden las dos\n"
                   "figuras mayores que haya dentro.\n"
                   "No es lo que da un Caliper: el calíper mide donde cruzaste tú, y\n"
                   "el sitio donde la pieza está más apretada casi nunca es ese.\n"
                   "Si solo se ve una figura, puede que las dos se estén TOCANDO:\n"
                   "en cuanto se tocan, la silueta las une y ya no son dos. Cuánto se\n"
                   "solapan dos piezas no es una medida que una silueta 2D contenga.";
        case ToolType::Straightness:
            return "Rectitud (zona mínima) — el valor DE LA NORMA: la anchura de la\n"
                   "banda más estrecha de dos rectas paralelas que contiene todo el\n"
                   "borde. Traza una línea sobre el borde a vigilar.\n"
                   "OJO al comparar con el Borde liso: aquel da la desviación máxima\n"
                   "respecto a la recta media, que es media banda. Este número saldrá\n"
                   "MAYOR sin que la pieza haya empeorado — son dos cosas distintas, y\n"
                   "la que aparece en un plano es esta.\n"
                   "Límite de la óptica: esto es la rectitud PROYECTADA en el plano de\n"
                   "la imagen. Lo que se tuerza hacia la cámara o en contra no se ve, y\n"
                   "ninguna cámara sola puede verlo.";
        case ToolType::Roundness:
            return "Redondez (zona mínima) — el valor DE LA NORMA: la separación\n"
                   "radial entre los dos círculos CONCÉNTRICOS más juntos que\n"
                   "contienen el borde. Arrastra del centro al borde, como el Círculo.\n"
                   "Se dan los dos números: el de zona mínima (el del plano) y el de\n"
                   "mínimos cuadrados (el que dan casi todas las máquinas de medir, y\n"
                   "con el que el operador va a comparar). El primero nunca es mayor.\n"
                   "SOLO VALE DE FRENTE. La silueta de un cilindro visto de perfil no\n"
                   "es un círculo: son dos tangentes, y ahí no hay redondez que medir\n"
                   "por mucho que la herramienta se deje dibujar encima.";
        case ToolType::Orientation:
            return "Orientación — paralelismo, perpendicularidad y angularidad, que\n"
                   "son la misma medida con distinto ángulo nominal (0, 90 o el que\n"
                   "pongas en el campo Ángulo).\n"
                   "NO devuelve un ángulo: devuelve una DISTANCIA, la anchura de la\n"
                   "banda —orientada según el datum— que contiene todo el borde. Un\n"
                   "borde puede ir paralelo de media y estar tan ondulado que no quepa\n"
                   "en la banda del plano; el ángulo no lo vería.\n"
                   "Necesita un DATUM: elige en Referencia la herramienta que da la\n"
                   "recta contra la que se mide. Sin datum no mide, porque una\n"
                   "orientación sin decir respecto a qué no significa nada.";
        case ToolType::CentreOffset:
            return "Desviación de centros — la distancia entre los centros de dos\n"
                   "elementos circulares. Responde a «¿están estos dos agujeros\n"
                   "centrados uno con otro?», que es una pregunta legítima.\n"
                   "ESTO NO ES CONCENTRICIDAD ISO/ASME. La concentricidad se retiró de\n"
                   "la norma en 2018 por inverificable de forma repetible; para la cota\n"
                   "formal usa Posición verdadera con su marco de referencia.\n"
                   "Elige los dos círculos en Referencia y 2ª referencia. Vale también\n"
                   "un punto construido. Los dos tienen que verse DE FRENTE: el centro\n"
                   "de un cilindro visto de perfil no está donde parece.";
        case ToolType::BoltPattern:
            return "Patrón de agujeros — la cota de una brida. Arrastra un recuadro\n"
                   "que abarque la pieza entera: se encuentran los agujeros, se ajusta\n"
                   "el círculo primitivo y se mide cuánto se sale cada uno de su sitio.\n"
                   "La medida es la desviación del PEOR agujero, en diámetro de zona, y\n"
                   "el detalle dice cuál es. Con Agujeros esperados puesto, que falte\n"
                   "uno es el defecto y se dice.\n"
                   "La referencia es el propio patrón: su círculo primitivo ajustado y\n"
                   "su reparto angular. Girar la brida entera no cambia nada, que es lo\n"
                   "que se quiere aquí. Para medir contra un datum de fuera, usa\n"
                   "Posición verdadera en el agujero que te interese.";
        case ToolType::Profile:
            return "Perfil de línea — cuánto se separa el contorno de la pieza del que\n"
                   "DEBERÍA tener. Es la tolerancia GD&T más honesta para una silueta,\n"
                   "porque está definida sobre una línea y no sobre una superficie.\n"
                   "El nominal se captura del contorno de la pieza BUENA al dibujar la\n"
                   "herramienta, y se queda guardado dentro de la plantilla. Colócala\n"
                   "con un clic sobre la pieza de referencia; si la pieza que tienes\n"
                   "delante no es la buena, el nominal que captures tampoco lo será.\n"
                   "Da la zona bilateral 2·máx|d| y, por separado, cuánto material\n"
                   "sobra y cuánto falta — que son dos averías distintas.\n"
                   "No necesita alinear nada: la pieza ya viene alineada por su fixture.";
        case ToolType::Extremes:
            return "Máx./mín. — la medida más grande y la más pequeña de la pieza EN\n"
                   "CUALQUIER DIRECCIÓN, no en la que acertaras a trazar. Arrastra un\n"
                   "recuadro sobre la pieza y elige en Medida cuál vigilar.\n"
                   "Anchura mínima: la banda más estrecha que contiene la pieza. Es la\n"
                   "cota de «¿pasa por la ranura?».\n"
                   "Diámetro máximo: los dos puntos más separados. Es «¿cuánto hueco\n"
                   "necesita?».\n"
                   "Las dos se dan siempre en el detalle, con su dirección. No salen de\n"
                   "minAreaRect: ese minimiza el ÁREA, y ni su lado corto es la anchura\n"
                   "mínima ni su diagonal es el diámetro.";
    }
    return "";
}

bool measuresFraction(ToolType type) {
    // La Region puede medir una fraccion (solidez, circularidad) o no (area,
    // perimetro, agujeros): eso lo decide su geometria, no su tipo, y lo
    // resuelve la sobrecarga de `suggestTolerances` que la mira.
    return type == ToolType::Symmetry;
}

void suggestTolerances(ToolType type, double measured, double& toleranceMin,
                       double& toleranceMax) {
    switch (type) {
        case ToolType::EdgeDefects:
        case ToolType::Polygon:
        case ToolType::Blob:
        case ToolType::PolyBlob:
        case ToolType::Gear:
            // Conteo: se exige exactamente lo que hay en la pieza buena. En el
            // engranaje eso son los dientes, que o son los que toca o la rueda
            // es otra.
            toleranceMin = measured;
            toleranceMax = measured;
            return;
        case ToolType::Profile:
        case ToolType::BoltPattern:
        case ToolType::CentreOffset:
            // Una desviación: la pieza buena define el piso y el techo va
            // holgado, como el resto de las desviaciones.
            toleranceMin = 0.0;
            toleranceMax = std::max(measured * 1.5, 2.0);
            return;
        case ToolType::Clearance: {
            // Una holgura puede salir NEGATIVA: eso es interferencia. Proponer
            // una banda a partir de una pieza que ya interfiere no significa
            // nada, así que se deja abierta y que el operador ponga el mínimo
            // que de verdad admite.
            if (measured <= 0.0) {
                toleranceMin = 0.0;
                toleranceMax = 1e9;
                return;
            }
            const double band = std::max(measured * 0.10, 2.0);
            toleranceMin = std::max(0.0, measured - band);
            toleranceMax = measured + band;
            return;
        }
        case ToolType::Symmetry:
            // Vive entre 0 y 1: una banda relativa sería ridícula cerca de 1. El
            // techo se corta ahí porque una simetría mayor que 1 no existe, y
            // dejarlo abierto haría pasar por bueno un valor imposible.
            toleranceMin = std::max(0.0, measured - 0.05);
            toleranceMax = std::min(1.0, measured + 0.05);
            return;
        // El Eje medio va con el Borde liso y NO con la Simetría, aunque estén
        // seguidos en el enum: lo que mide es una rectitud en píxeles, no una
        // fracción. Meterlo en la rama de arriba le recortaba el techo a 1 y
        // dejaba fuera de tolerancia cualquier eje con más de 1 px de
        // desviación. Lo cazó el barrido de coherencia.
        case ToolType::Orientation:
        case ToolType::Roundness:
        case ToolType::Straightness:
        case ToolType::MedianAxis:
        case ToolType::EdgeFlaw:
            // Desviación: la pieza buena define el piso; techo holgado.
            toleranceMin = 0.0;
            toleranceMax = std::max(measured * 1.5, 2.0);
            return;
        case ToolType::Position:
            // Desviación respecto al cero: se admite desde 0 hasta un margen
            // alrededor de donde cayó la pieza buena.
            toleranceMin = 0.0;
            toleranceMax = std::max(measured * 1.25, 5.0);
            return;
        case ToolType::ConstructedPoint:
        case ToolType::ConstructedLine:
            // Una construcción no se juzga: no hay medida que pueda estar fuera
            // de tolerancia, solo un elemento que se pudo calcular o no. Se deja
            // la banda abierta para que nunca sea la causa de un NG; lo que sí
            // es NG es que no se pueda construir, y eso lo dice `ok=false`.
            toleranceMin = 0.0;
            toleranceMax = 1e9;
            return;
        case ToolType::LineToLine:
        case ToolType::Angle: {
            // Ángulo en grados: banda de ±2° alrededor del valor de la pieza buena.
            const double band = 2.0;
            toleranceMin = std::max(0.0, measured - band);
            toleranceMax = measured + band;
            return;
        }
        // La Región mide seis cosas con escalas distintas, así que el tipo por
        // sí solo no basta: quien tenga la geometría a mano debe llamar a la
        // sobrecarga que la mira. Sin ella, la banda relativa es lo menos malo
        // que se puede decir — nunca el conteo exacto, que dejaría un área
        // fuera de tolerancia por un píxel.
        case ToolType::Extremes:
        case ToolType::Region:
        case ToolType::Caliper:
        case ToolType::Circle:
        case ToolType::PointToLine:
        case ToolType::Arc:
        case ToolType::Shaft:
        case ToolType::Thread:
        case ToolType::Ruler: {
            // Banda de ±10% con un mínimo de ±2 px para medidas pequeñas.
            const double band = std::max(measured * 0.10, 2.0);
            toleranceMin = std::max(0.0, measured - band);
            toleranceMax = measured + band;
            return;
        }
    }
}

void suggestTolerances(const ToolGeometry& geometry, double measured, double& toleranceMin,
                       double& toleranceMax) {
    if (const auto* region = std::get_if<RegionGeometry>(&geometry)) {
        switch (region->measure) {
            case RegionMeasure::HoleCount:
                // Un recuento es exacto: la pieza buena tiene los agujeros que
                // tiene, y uno de más o de menos es otra pieza.
                toleranceMin = measured;
                toleranceMax = measured;
                return;
            case RegionMeasure::Solidity:
            case RegionMeasure::Circularity: {
                // Viven entre 0 y 1, así que una banda de ±10 % del valor sería
                // ridícula cerca de 1 y enorme cerca de 0. Se usa una banda
                // ABSOLUTA de ±0,05, y el techo se corta en 1: una circularidad
                // mayor que 1 no existe, y dejar el margen abierto por arriba
                // haría que un valor imposible pasara como bueno.
                toleranceMin = std::max(0.0, measured - 0.05);
                toleranceMax = std::min(1.0, measured + 0.05);
                return;
            }
            case RegionMeasure::AspectRatio: {
                const double band = std::max(measured * 0.05, 0.05);
                toleranceMin = std::max(1.0, measured - band);
                toleranceMax = measured + band;
                return;
            }
            case RegionMeasure::Area:
            case RegionMeasure::Perimeter:
                break;  // banda relativa, como cualquier otra medida de tamaño
        }
    }
    suggestTolerances(typeOf(geometry), measured, toleranceMin, toleranceMax);
}

ToolType typeOf(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> ToolType {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry>) {
                return ToolType::Caliper;
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return ToolType::Circle;
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return ToolType::PointToLine;
            } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                return ToolType::EdgeFlaw;
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                return ToolType::Blob;
            } else if constexpr (std::is_same_v<T, RulerGeometry>) {
                return ToolType::Ruler;
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return ToolType::LineToLine;
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return ToolType::Angle;
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                return ToolType::PolyBlob;
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                return ToolType::Position;
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                return ToolType::Arc;
            } else if constexpr (std::is_same_v<T, ShaftGeometry>) {
                return ToolType::Shaft;
            } else if constexpr (std::is_same_v<T, ThreadGeometry>) {
                return ToolType::Thread;
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                return ToolType::Gear;
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry>) {
                return ToolType::ConstructedPoint;
            } else if constexpr (std::is_same_v<T, ConstructedLineGeometry>) {
                return ToolType::ConstructedLine;
            } else if constexpr (std::is_same_v<T, MedianAxisGeometry>) {
                return ToolType::MedianAxis;
            } else if constexpr (std::is_same_v<T, RegionGeometry>) {
                return ToolType::Region;
            } else if constexpr (std::is_same_v<T, SymmetryGeometry>) {
                return ToolType::Symmetry;
            } else if constexpr (std::is_same_v<T, PolygonGeometry>) {
                return ToolType::Polygon;
            } else if constexpr (std::is_same_v<T, EdgeDefectsGeometry>) {
                return ToolType::EdgeDefects;
            } else if constexpr (std::is_same_v<T, ClearanceGeometry>) {
                return ToolType::Clearance;
            } else if constexpr (std::is_same_v<T, StraightnessGeometry>) {
                return ToolType::Straightness;
            } else if constexpr (std::is_same_v<T, RoundnessGeometry>) {
                return ToolType::Roundness;
            } else if constexpr (std::is_same_v<T, OrientationGeometry>) {
                return ToolType::Orientation;
            } else if constexpr (std::is_same_v<T, CentreOffsetGeometry>) {
                return ToolType::CentreOffset;
            } else if constexpr (std::is_same_v<T, BoltPatternGeometry>) {
                return ToolType::BoltPattern;
            } else if constexpr (std::is_same_v<T, ProfileGeometry>) {
                return ToolType::Profile;
            } else if constexpr (std::is_same_v<T, ExtremesGeometry>) {
                return ToolType::Extremes;
            } else {
                // Sin rama genérica a propósito. Antes esta cadena acababa en un
                // `else` que devolvía Position, así que al añadir un tipo nuevo
                // la herramienta se reportaba como Posición sin que nada
                // fallara al compilar. Ahora no compila hasta que se le asigne
                // su ToolType.
                static_assert(alwaysFalse<T>, "geometría sin ToolType asignado");
            }
        },
        geometry);
}

void translateGeometry(ToolGeometry& geometry, const cv::Point2f& delta) {
    std::visit(
        [&delta](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, EdgeDefectsGeometry> ||
                          std::is_same_v<T, StraightnessGeometry> ||
                          std::is_same_v<T, OrientationGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                g.p0 += delta;
                g.p1 += delta;
            } else if constexpr (std::is_same_v<T, CircleGeometry> ||
                                 std::is_same_v<T, RoundnessGeometry> ||
                                 std::is_same_v<T, BlobGeometry> ||
                                 std::is_same_v<T, RegionGeometry> ||
                                 std::is_same_v<T, BoltPatternGeometry> ||
                                 std::is_same_v<T, ExtremesGeometry> ||
                                 std::is_same_v<T, SymmetryGeometry> ||
                                 std::is_same_v<T, PolygonGeometry> ||
                                 std::is_same_v<T, ClearanceGeometry>) {
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
            } else if constexpr (std::is_same_v<T, ProfileGeometry>) {
                for (auto& v : g.nominal) {
                    v += delta;
                }
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                g.point += delta;
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                g.start += delta;
                g.mid += delta;
                g.end += delta;
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry> ||
                                 std::is_same_v<T, MedianAxisGeometry>) {
                g.axisFrom += delta;
                g.axisTo += delta;
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                g.center += delta;
            } else if constexpr (std::is_same_v<T, CentreOffsetGeometry>) {
                g.anchor += delta;
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry> ||
                                 std::is_same_v<T, ConstructedLineGeometry>) {
                // Solo se mueve la etiqueta: el elemento lo dictan las
                // referencias, y arrastrarlo no puede cambiar dónde cae. Que se
                // deje mover es a propósito — la etiqueta estorba a menudo.
                g.anchor += delta;
            } else {
                // Igual que en typeOf: esta cadena no puede acabar sin rama. Al
                // no tener `else`, un tipo nuevo simplemente NO se trasladaba —
                // la herramienta se quedaba clavada al arrastrarla y nada
                // fallaba al compilar.
                static_assert(alwaysFalse<T>, "geometría que no sabe trasladarse");
            }
        },
        geometry);
}

namespace {

std::string writeJson(const std::function<void(cv::FileStorage&)>& body) {
    cv::FileStorage fs("{}", cv::FileStorage::WRITE | cv::FileStorage::MEMORY |
                                 cv::FileStorage::FORMAT_JSON);
    body(fs);
    return fs.releaseAndGetString();
}

// Lectura con validación: una clave ausente es un error controlado, no un 0.
class JsonReader {
public:
    explicit JsonReader(const std::string& json)
        : fs_(json,
              cv::FileStorage::READ | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON) {}

    core::Result<double> number(const char* key) {
        const cv::FileNode node = fs_[key];
        if (node.empty() || !node.isReal()) {
            if (node.empty() || !node.isInt()) {
                return core::Result<double>::err(std::string("Geometría corrupta: falta '") +
                                                 key + "'");
            }
        }
        const double value = static_cast<double>(node.real());
        // Un JSON con 1e400 desborda a infinito y OpenCV lo acepta sin rechistar.
        // Si se dejara pasar, la herramienta quedaría en el infinito y todas sus
        // medidas saldrían NaN: es preferible rechazar la geometría entera.
        if (!std::isfinite(value)) {
            return core::Result<double>::err(std::string("Geometría corrupta: '") + key +
                                             "' no es un número finito");
        }
        return core::Result<double>::ok(value);
    }

    // Clave opcional (campos añadidos después de la v1 del formato).
    double numberOr(const char* key, double fallback) {
        const cv::FileNode node = fs_[key];
        if (node.empty() || (!node.isReal() && !node.isInt())) {
            return fallback;
        }
        const double value = static_cast<double>(node.real());
        return std::isfinite(value) ? value : fallback;
    }

    // Secuencia plana [x0,y0,x1,y1,...] como lista de puntos.
    std::vector<cv::Point2f> points(const char* key) {
        std::vector<cv::Point2f> pts;
        const cv::FileNode node = fs_[key];
        if (node.empty() || !node.isSeq()) {
            return pts;
        }
        std::vector<double> flat;
        for (const auto& n : node) {
            if (n.isReal() || n.isInt()) {
                flat.push_back(static_cast<double>(n.real()));
            }
        }
        for (std::size_t i = 0; i + 1 < flat.size(); i += 2) {
            if (!std::isfinite(flat[i]) || !std::isfinite(flat[i + 1])) {
                return {};  // un vértice no finito invalida el polígono entero
            }
            pts.emplace_back(static_cast<float>(flat[i]), static_cast<float>(flat[i + 1]));
        }
        return pts;
    }

private:
    cv::FileStorage fs_;
};

// Lee el modo de una construcción comprobando que el número guardado sea uno de
// los que existen. `numberOr` con un valor por defecto no sirve aquí: un modo
// desconocido —una plantilla escrita por una versión posterior, unos params
// tocados a mano— se degradaría en silencio al primero de la lista, y la
// herramienta calcularía una cosa distinta de la que el operador configuró sin
// que nada lo dijera.
template <typename Mode, std::size_t N>
core::Result<Mode> readConstruction(JsonReader& reader, const std::array<Mode, N>& modes) {
    const auto raw = reader.number("mode");
    if (!raw.isOk()) {
        return core::Result<Mode>::err(raw.error().message);
    }
    const int value = static_cast<int>(raw.value());
    for (const Mode mode : modes) {
        if (static_cast<int>(mode) == value) {
            return core::Result<Mode>::ok(mode);
        }
    }
    return core::Result<Mode>::err("Construcción desconocida: " + std::to_string(value));
}

}  // namespace

std::string toJson(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::string {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y
                       << "band" << g.bandWidth;
                });
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "r" << g.radius << "band"
                       << g.searchBand << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "lax" << g.lineA.x << "lay" << g.lineA.y << "lbx" << g.lineB.x
                       << "lby" << g.lineB.y << "sax" << g.scanA.x << "say" << g.scanA.y
                       << "sbx" << g.scanB.x << "sby" << g.scanB.y;
                });
            } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y
                       << "scanLen" << g.scanLength << "scans" << g.scanCount;
                });
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "minArea" << g.minArea << "dark" << (g.darkBlobs ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, ClearanceGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "dark" << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, RoundnessGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "r" << g.radius
                       << "band" << g.searchBand << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, OrientationGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1"
                       << g.p1.y << "scanLen" << g.scanLength << "scans" << g.scanCount
                       << "nominal" << g.nominalAngleDeg;
                });
            } else if constexpr (std::is_same_v<T, StraightnessGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1"
                       << g.p1.y << "scanLen" << g.scanLength << "scans" << g.scanCount;
                });
            } else if constexpr (std::is_same_v<T, EdgeDefectsGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1"
                       << g.p1.y << "scanLen" << g.scanLength << "scans" << g.scanCount
                       << "minH" << g.minHeight << "dark" << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, PolygonGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "eps" << g.epsilonFraction << "dark"
                       << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, SymmetryGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "dark" << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, RegionGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    // La clave del selector se llama "mode" en TODAS las
                    // geometrías que tienen uno, no "measure" aquí y "mode"
                    // allí: escribir con un nombre y leer con otro ya costó un
                    // test en rojo, y con una sola clave no puede volver a
                    // pasar.
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "mode" << static_cast<int>(g.measure) << "dark"
                       << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, RulerGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y;
                });
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "ax0" << g.a0.x << "ay0" << g.a0.y << "ax1" << g.a1.x << "ay1"
                       << g.a1.y << "bx0" << g.b0.x << "by0" << g.b0.y << "bx1" << g.b1.x
                       << "by1" << g.b1.y;
                });
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "vx" << g.vertex.x << "vy" << g.vertex.y << "e0x" << g.end0.x
                       << "e0y" << g.end0.y << "e1x" << g.end1.x << "e1y" << g.end1.y;
                });
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    // Los vértices van como secuencia plana [x0,y0,x1,y1,...].
                    fs << "verts" << "[:";
                    for (const auto& v : g.vertices) {
                        fs << v.x << v.y;
                    }
                    fs << "]";
                    fs << "minArea" << g.minArea << "dark" << (g.darkBlobs ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "px" << g.point.x << "py" << g.point.y << "axis"
                       << static_cast<int>(g.axis) << "nx" << g.nominal.x << "ny"
                       << g.nominal.y;
                });
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "sx" << g.start.x << "sy" << g.start.y << "mx" << g.mid.x << "my"
                       << g.mid.y << "ex" << g.end.x << "ey" << g.end.y << "band"
                       << g.searchBand << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry> ||
                                 std::is_same_v<T, MedianAxisGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "ax" << g.axisFrom.x << "ay" << g.axisFrom.y << "bx" << g.axisTo.x
                       << "by" << g.axisTo.y << "band" << g.searchBand << "stations"
                       << g.stations;
                });
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "rin"
                       << g.innerRadius << "rout" << g.outerRadius << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, ExtremesGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "mode" << static_cast<int>(g.measure) << "dark"
                       << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, ProfileGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    // Igual que el blob poligonal: secuencia plana [x0,y0,...].
                    fs << "nominal" << "[:";
                    for (const auto& v : g.nominal) {
                        fs << v.x << v.y;
                    }
                    fs << "]";
                });
            } else if constexpr (std::is_same_v<T, BoltPatternGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "holes" << g.expectedHoles << "dark"
                       << (g.darkPiece ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, CentreOffsetGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "ax" << g.anchor.x << "ay" << g.anchor.y;
                });
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry> ||
                                 std::is_same_v<T, ConstructedLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "mode" << static_cast<int>(g.mode) << "ax" << g.anchor.x << "ay"
                       << g.anchor.y;
                });
            } else {
                static_assert(alwaysFalse<T>, "geometría que no sabe serializarse");
            }
        },
        geometry);
}

core::Result<ToolGeometry> geometryFromJson(ToolType type, const std::string& json) {
    using ResultT = core::Result<ToolGeometry>;

    try {
        JsonReader reader(json);
        auto f = [&reader](const char* key) { return reader.number(key); };

        switch (type) {
            case ToolType::Caliper: {
                CaliperGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1"), band = f("band");
                for (const auto* r : {&x0, &y0, &x1, &y1, &band}) {
                    if (!r->isOk()) return ResultT::err(r->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.bandWidth = static_cast<float>(band.value());
                return ResultT::ok(g);
            }
            case ToolType::Circle: {
                CircleGeometry g;
                auto cx = f("cx"), cy = f("cy"), r = f("r"), band = f("band");
                for (const auto* v : {&cx, &cy, &r, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.radius = static_cast<float>(r.value());
                g.searchBand = static_cast<float>(band.value());
                // "rays" llegó después: los JSON viejos usan el valor por defecto.
                g.rayCount = static_cast<int>(reader.numberOr("rays", g.rayCount));
                return ResultT::ok(g);
            }
            case ToolType::PointToLine: {
                PointToLineGeometry g;
                auto lax = f("lax"), lay = f("lay"), lbx = f("lbx"), lby = f("lby");
                auto sax = f("sax"), say = f("say"), sbx = f("sbx"), sby = f("sby");
                for (const auto* v : {&lax, &lay, &lbx, &lby, &sax, &say, &sbx, &sby}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.lineA = {static_cast<float>(lax.value()), static_cast<float>(lay.value())};
                g.lineB = {static_cast<float>(lbx.value()), static_cast<float>(lby.value())};
                g.scanA = {static_cast<float>(sax.value()), static_cast<float>(say.value())};
                g.scanB = {static_cast<float>(sbx.value()), static_cast<float>(sby.value())};
                return ResultT::ok(g);
            }
            case ToolType::EdgeFlaw: {
                EdgeFlawGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                auto len = f("scanLen"), scans = f("scans");
                for (const auto* v : {&x0, &y0, &x1, &y1, &len, &scans}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.scanLength = static_cast<float>(len.value());
                g.scanCount = static_cast<int>(scans.value());
                return ResultT::ok(g);
            }
            case ToolType::Ruler: {
                RulerGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                for (const auto* v : {&x0, &y0, &x1, &y1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                return ResultT::ok(g);
            }
            case ToolType::Blob: {
                BlobGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                auto minArea = f("minArea"), dark = f("dark");
                for (const auto* v : {&cx, &cy, &w, &h, &minArea, &dark}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.minArea = static_cast<float>(minArea.value());
                g.darkBlobs = dark.value() != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::LineToLine: {
                LineToLineGeometry g;
                auto ax0 = f("ax0"), ay0 = f("ay0"), ax1 = f("ax1"), ay1 = f("ay1");
                auto bx0 = f("bx0"), by0 = f("by0"), bx1 = f("bx1"), by1 = f("by1");
                for (const auto* v : {&ax0, &ay0, &ax1, &ay1, &bx0, &by0, &bx1, &by1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.a0 = {static_cast<float>(ax0.value()), static_cast<float>(ay0.value())};
                g.a1 = {static_cast<float>(ax1.value()), static_cast<float>(ay1.value())};
                g.b0 = {static_cast<float>(bx0.value()), static_cast<float>(by0.value())};
                g.b1 = {static_cast<float>(bx1.value()), static_cast<float>(by1.value())};
                return ResultT::ok(g);
            }
            case ToolType::Angle: {
                AngleGeometry g;
                auto vx = f("vx"), vy = f("vy"), e0x = f("e0x"), e0y = f("e0y");
                auto e1x = f("e1x"), e1y = f("e1y");
                for (const auto* v : {&vx, &vy, &e0x, &e0y, &e1x, &e1y}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.vertex = {static_cast<float>(vx.value()), static_cast<float>(vy.value())};
                g.end0 = {static_cast<float>(e0x.value()), static_cast<float>(e0y.value())};
                g.end1 = {static_cast<float>(e1x.value()), static_cast<float>(e1y.value())};
                return ResultT::ok(g);
            }
            case ToolType::PolyBlob: {
                PolyBlobGeometry g;
                g.vertices = reader.points("verts");
                if (g.vertices.size() < 3) {
                    return ResultT::err("Blob poligonal: se necesitan al menos 3 vértices");
                }
                auto minArea = f("minArea"), dark = f("dark");
                for (const auto* v : {&minArea, &dark}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.minArea = static_cast<float>(minArea.value());
                g.darkBlobs = dark.value() != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::Position: {
                PositionGeometry g;
                auto px = f("px"), py = f("py");
                for (const auto* v : {&px, &py}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.point = {static_cast<float>(px.value()), static_cast<float>(py.value())};
                const int axis = static_cast<int>(reader.numberOr("axis", 0.0));
                g.axis = (axis == 1)   ? PositionAxis::X
                         : (axis == 2) ? PositionAxis::Y
                                       : PositionAxis::Radial;
                // "nx"/"ny" llegaron con G4: las plantillas anteriores no las
                // tienen y su punto teórico es el origen del marco.
                g.nominal = {static_cast<float>(reader.numberOr("nx", 0.0)),
                             static_cast<float>(reader.numberOr("ny", 0.0))};
                return ResultT::ok(g);
            }
            case ToolType::Arc: {
                ArcGeometry g;
                auto sx = f("sx"), sy = f("sy"), mx = f("mx"), my = f("my"), ex = f("ex"),
                     ey = f("ey"), band = f("band");
                for (const auto* v : {&sx, &sy, &mx, &my, &ex, &ey, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.start = {static_cast<float>(sx.value()), static_cast<float>(sy.value())};
                g.mid = {static_cast<float>(mx.value()), static_cast<float>(my.value())};
                g.end = {static_cast<float>(ex.value()), static_cast<float>(ey.value())};
                g.searchBand = static_cast<float>(band.value());
                g.rayCount = static_cast<int>(reader.numberOr("rays", 24.0));
                return ResultT::ok(g);
            }
            case ToolType::Shaft: {
                ShaftGeometry g;
                auto ax = f("ax"), ay = f("ay"), bx = f("bx"), by = f("by"), band = f("band");
                for (const auto* v : {&ax, &ay, &bx, &by, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.axisFrom = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                g.axisTo = {static_cast<float>(bx.value()), static_cast<float>(by.value())};
                g.searchBand = static_cast<float>(band.value());
                g.stations = static_cast<int>(reader.numberOr("stations", 32.0));
                return ResultT::ok(g);
            }
            case ToolType::Orientation: {
                OrientationGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                for (const auto* v : {&x0, &y0, &x1, &y1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.scanLength = static_cast<float>(reader.numberOr("scanLen", 16.0));
                g.scanCount = static_cast<int>(reader.numberOr("scans", 60.0));
                g.nominalAngleDeg = static_cast<float>(reader.numberOr("nominal", 0.0));
                return ResultT::ok(g);
            }
            case ToolType::Roundness: {
                RoundnessGeometry g;
                auto cx = f("cx"), cy = f("cy"), r = f("r"), band = f("band");
                for (const auto* v : {&cx, &cy, &r, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.radius = static_cast<float>(r.value());
                g.searchBand = static_cast<float>(band.value());
                g.rayCount = static_cast<int>(reader.numberOr("rays", 72.0));
                return ResultT::ok(g);
            }
            case ToolType::Straightness: {
                StraightnessGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                for (const auto* v : {&x0, &y0, &x1, &y1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.scanLength = static_cast<float>(reader.numberOr("scanLen", 16.0));
                g.scanCount = static_cast<int>(reader.numberOr("scans", 60.0));
                return ResultT::ok(g);
            }
            case ToolType::Clearance: {
                ClearanceGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                for (const auto* v : {&cx, &cy, &w, &h}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::EdgeDefects: {
                EdgeDefectsGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                for (const auto* v : {&x0, &y0, &x1, &y1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.scanLength = static_cast<float>(reader.numberOr("scanLen", 16.0));
                g.scanCount = static_cast<int>(reader.numberOr("scans", 60.0));
                g.minHeight = static_cast<float>(reader.numberOr("minH", 1.5));
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::Polygon: {
                PolygonGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                for (const auto* v : {&cx, &cy, &w, &h}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.epsilonFraction = static_cast<float>(reader.numberOr("eps", 0.02));
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::Symmetry: {
                SymmetryGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                for (const auto* v : {&cx, &cy, &w, &h}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::Region: {
                RegionGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                for (const auto* v : {&cx, &cy, &w, &h}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                // Igual que en las construcciones: una medida desconocida no se
                // degrada a la primera, porque daría un número creíble que no es
                // el que el operador configuró.
                const auto measure = readConstruction(reader, allRegionMeasures());
                if (!measure.isOk()) return ResultT::err(measure.error().message);
                g.measure = measure.value();
                return ResultT::ok(g);
            }
            case ToolType::MedianAxis: {
                MedianAxisGeometry g;
                auto ax = f("ax"), ay = f("ay"), bx = f("bx"), by = f("by"), band = f("band");
                for (const auto* v : {&ax, &ay, &bx, &by, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.axisFrom = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                g.axisTo = {static_cast<float>(bx.value()), static_cast<float>(by.value())};
                g.searchBand = static_cast<float>(band.value());
                g.stations = static_cast<int>(reader.numberOr("stations", 32.0));
                return ResultT::ok(g);
            }
            case ToolType::Thread: {
                ThreadGeometry g;
                auto ax = f("ax"), ay = f("ay"), bx = f("bx"), by = f("by"), band = f("band");
                for (const auto* v : {&ax, &ay, &bx, &by, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.axisFrom = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                g.axisTo = {static_cast<float>(bx.value()), static_cast<float>(by.value())};
                g.searchBand = static_cast<float>(band.value());
                g.stations = static_cast<int>(reader.numberOr("stations", 240.0));
                return ResultT::ok(g);
            }
            case ToolType::Gear: {
                GearGeometry g;
                auto cx = f("cx"), cy = f("cy"), rin = f("rin"), rout = f("rout");
                for (const auto* v : {&cx, &cy, &rin, &rout}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.innerRadius = static_cast<float>(rin.value());
                g.outerRadius = static_cast<float>(rout.value());
                g.rayCount = static_cast<int>(reader.numberOr("rays", 1440.0));
                return ResultT::ok(g);
            }
            case ToolType::Extremes: {
                ExtremesGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                for (const auto* v : {&cx, &cy, &w, &h}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                const auto measure = readConstruction(reader, allExtremeMeasures());
                if (!measure.isOk()) return ResultT::err(measure.error().message);
                g.measure = measure.value();
                return ResultT::ok(g);
            }
            case ToolType::Profile: {
                ProfileGeometry g;
                // Mismo lector que el blob poligonal: la secuencia plana ya
                // sabe leerse, y una segunda forma de leer lo mismo acabaría
                // divergiendo.
                g.nominal = reader.points("nominal");
                if (g.nominal.size() < 3) {
                    return ResultT::err("Perfil sin nominal: hacen falta al menos 3 puntos");
                }
                return ResultT::ok(g);
            }
            case ToolType::BoltPattern: {
                BoltPatternGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                for (const auto* v : {&cx, &cy, &w, &h}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.expectedHoles = static_cast<int>(reader.numberOr("holes", 0.0));
                g.darkPiece = reader.numberOr("dark", 1.0) != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::CentreOffset: {
                CentreOffsetGeometry g;
                auto ax = f("ax"), ay = f("ay");
                for (const auto* v : {&ax, &ay}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.anchor = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                return ResultT::ok(g);
            }
            case ToolType::ConstructedPoint: {
                ConstructedPointGeometry g;
                auto ax = f("ax"), ay = f("ay");
                for (const auto* v : {&ax, &ay}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.anchor = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                // Una construcción desconocida NO se degrada a la primera: eso
                // convertiría un fichero de otra versión en una medida creíble
                // que no es la que el operador configuró.
                const auto mode = readConstruction(reader, allPointConstructions());
                if (!mode.isOk()) return ResultT::err(mode.error().message);
                g.mode = mode.value();
                return ResultT::ok(g);
            }
            case ToolType::ConstructedLine: {
                ConstructedLineGeometry g;
                auto ax = f("ax"), ay = f("ay");
                for (const auto* v : {&ax, &ay}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.anchor = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                const auto mode = readConstruction(reader, allLineConstructions());
                if (!mode.isOk()) return ResultT::err(mode.error().message);
                g.mode = mode.value();
                return ResultT::ok(g);
            }
        }
        return ResultT::err("Tipo de herramienta no soportado");
    } catch (const cv::Exception& e) {
        return ResultT::err(std::string("JSON de geometría inválido: ") + e.what());
    }
}

}  // namespace pci::inspection
