# Lógica de herramientas y ejecución: Qt-free, testeable sin GUI.
add_library(pci_inspection STATIC
  execution/edge_detection.cpp
  execution/tool_executor.cpp
  tools/tool_geometry.cpp
)
target_include_directories(pci_inspection PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_inspection
  PUBLIC pci_core pci_vision ${OpenCV_LIBS}
  PRIVATE project_warnings
)

# Editor visual (canvas + ventana): la única parte con Qt.
add_library(pci_editor STATIC
  canvas/editor_canvas.cpp
  canvas/tool_icons.cpp
  editor_window.cpp
)
target_include_directories(pci_editor PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_editor
  PUBLIC Qt6::Widgets pci_inspection pci_repositories
  PRIVATE pci_core pci_camera project_warnings
)
