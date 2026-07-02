add_library(pci_camera STATIC
  camera_controller.cpp
  camera_enumerator.cpp
  frame_utils.cpp
)
target_include_directories(pci_camera PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_camera
  PUBLIC Qt6::Core Qt6::Gui
  PRIVATE pci_core ${OpenCV_LIBS} project_warnings
)
