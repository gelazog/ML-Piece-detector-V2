add_library(pci_camera STATIC
  camera_controller.cpp
  camera_enumerator.cpp
  native_cameras.cpp
  frame_utils.cpp
)
target_include_directories(pci_camera PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_camera
  PUBLIC Qt6::Core Qt6::Gui
  PRIVATE pci_core ${OpenCV_LIBS} project_warnings
)

# Enumeración nativa por DirectShow en Windows: COM + GUIDs de strmiids.
if(WIN32)
  target_link_libraries(pci_camera PRIVATE strmiids ole32 oleaut32 uuid)
endif()
