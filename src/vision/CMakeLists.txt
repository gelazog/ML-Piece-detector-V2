add_library(pci_vision STATIC
  auto_roi.cpp
  board_frame.cpp
  contour_analysis.cpp
  fitting.cpp
  geometry_features.cpp
  fixture_stabilizer.cpp
  frame_geometry.cpp
  orientation.cpp
  orientation_anchor.cpp
  periodicity.cpp
  pipeline.cpp
  plane_scale.cpp
  position_fixture.cpp
  quality_metrics.cpp
  segmentation.cpp
  shape_class.cpp
)
target_include_directories(pci_vision PUBLIC ${CMAKE_SOURCE_DIR}/src)
# OpenCV, core y domain son PUBLIC: los headers públicos exponen cv::Mat,
# Result<T> y QualityMetrics.
target_link_libraries(pci_vision
  PUBLIC pci_core pci_domain ${OpenCV_LIBS}
  PRIVATE project_warnings
)
