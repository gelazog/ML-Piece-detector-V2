add_library(pci_ui STATIC
  main_window.cpp
  video_widget.cpp
)
target_include_directories(pci_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_ui
  PUBLIC Qt6::Widgets pci_camera
  PRIVATE Qt6::Concurrent pci_core pci_editor pci_vision pci_repositories project_warnings
)
