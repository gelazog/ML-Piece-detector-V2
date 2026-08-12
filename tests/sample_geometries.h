#pragma once

// Una geometría de ejemplo por cada tipo de herramienta, para las pruebas de
// coherencia que recorren TODAS las herramientas.
//
// Es un `switch` sin `default` a propósito: con `-Werror` un tipo nuevo en el
// enum rompe la compilación de las pruebas, así que no se puede añadir una
// herramienta decimoquinta y dejarla fuera del repaso sin enterarse.

#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection::testing_support {

inline ToolGeometry sampleGeometry(ToolType type) {
    switch (type) {
        case ToolType::Caliper:
            return CaliperGeometry{{10.0F, 20.0F}, {90.0F, 40.0F}, 10.0F};
        case ToolType::Circle:
            return CircleGeometry{{100.0F, 110.0F}, 50.0F, 12.0F, 36};
        case ToolType::PointToLine:
            return PointToLineGeometry{
                {0.0F, 0.0F}, {50.0F, 0.0F}, {25.0F, -20.0F}, {25.0F, 20.0F}};
        case ToolType::EdgeFlaw:
            return EdgeFlawGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 20};
        case ToolType::Blob:
            return BlobGeometry{{40.0F, 50.0F}, 80.0F, 60.0F, 20.0F, true};
        case ToolType::Ruler:
            return RulerGeometry{{1.0F, 2.0F}, {73.0F, 44.0F}};
        case ToolType::LineToLine:
            return LineToLineGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, {0.0F, 30.0F},
                                      {40.0F, 35.0F}};
        case ToolType::Angle:
            return AngleGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, {0.0F, 40.0F}};
        case ToolType::PolyBlob:
            return PolyBlobGeometry{
                {{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}}, 20.0F, true};
        case ToolType::Position:
            return PositionGeometry{{12.0F, 34.0F}, PositionAxis::Radial};
        case ToolType::Arc:
            return ArcGeometry{{-40.0F, 0.0F}, {0.0F, -40.0F}, {40.0F, 0.0F}, 12.0F, 24};
        case ToolType::Shaft:
            return ShaftGeometry{{-80.0F, 0.0F}, {80.0F, 0.0F}, 60.0F, 32};
        case ToolType::Thread:
            return ThreadGeometry{{-90.0F, 5.0F}, {90.0F, 5.0F}, 60.0F, 240};
        case ToolType::Gear:
            return GearGeometry{{0.0F, 0.0F}, 40.0F, 90.0F, 1440};
        case ToolType::Orientation:
            return OrientationGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 60, 0.0F};
        case ToolType::Roundness:
            return RoundnessGeometry{{100.0F, 110.0F}, 50.0F, 12.0F, 72};
        case ToolType::Straightness:
            return StraightnessGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 60};
        case ToolType::EdgeDefects:
            return EdgeDefectsGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 60, 1.5F,
                                       true};
        case ToolType::Clearance:
            return ClearanceGeometry{{0.0F, 0.0F}, 200.0F, 160.0F, true};
        case ToolType::Polygon:
            return PolygonGeometry{{0.0F, 0.0F}, 160.0F, 120.0F, 0.02F, true};
        case ToolType::Symmetry:
            return SymmetryGeometry{{0.0F, 0.0F}, 160.0F, 120.0F, true};
        case ToolType::Region:
            return RegionGeometry{{0.0F, 0.0F}, 160.0F, 120.0F, RegionMeasure::Area, true};
        case ToolType::MedianAxis:
            return MedianAxisGeometry{{-80.0F, 0.0F}, {80.0F, 0.0F}, 60.0F, 32};
        case ToolType::ConstructedPoint:
            return ConstructedPointGeometry{PointConstruction::Midpoint, {15.0F, 25.0F}};
        case ToolType::ConstructedLine:
            return ConstructedLineGeometry{LineConstruction::ThroughTwoPoints, {18.0F, 28.0F}};
    }
    return RulerGeometry{};  // inalcanzable: el switch de arriba es exhaustivo
}

}  // namespace pci::inspection::testing_support
