add_library(pci_vision STATIC
  contour_analysis.cpp
  orientation.cpp
  pipeline.cpp
  position_fixture.cpp
  segmentation.cpp
)
target_include_directories(pci_vision PUBLIC ${CMAKE_SOURCE_DIR}/src)
# OpenCV y pci_core son PUBLIC: los headers públicos exponen cv::Mat y Result<T>.
target_link_libraries(pci_vision
  PUBLIC pci_core ${OpenCV_LIBS}
  PRIVATE project_warnings
)
