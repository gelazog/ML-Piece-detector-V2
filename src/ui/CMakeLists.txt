add_library(pci_ui STATIC
  calibration_dialog.cpp
  detection_dialog.cpp
  history_dialog.cpp
  inspection_result_dialog.cpp
  main_window.cpp
  piece_manager_dialog.cpp
  preferences_dialog.cpp
  registration_wizard.cpp
  shortcuts_dialog.cpp
  stats_bar_chart.cpp
  template_manager_dialog.cpp
  video_widget.cpp
)
target_include_directories(pci_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_ui
  PUBLIC Qt6::Widgets pci_camera pci_engine
  PRIVATE Qt6::Concurrent pci_core pci_editor pci_vision pci_repositories project_warnings
)
