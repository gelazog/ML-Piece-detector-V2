add_library(pci_vision STATIC
  contour_analysis.cpp
  fixture_stabilizer.cpp
  orientation.cpp
  orientation_anchor.cpp
  pipeline.cpp
  position_fixture.cpp
  quality_metrics.cpp
  segmentation.cpp
)
target_include_directories(pci_vision PUBLIC ${CMAKE_SOURCE_DIR}/src)
# OpenCV, core y domain son PUBLIC: los headers públicos exponen cv::Mat,
# Result<T> y QualityMetrics.
target_link_libraries(pci_vision
  PUBLIC pci_core pci_domain ${OpenCV_LIBS}
  PRIVATE project_warnings
)
