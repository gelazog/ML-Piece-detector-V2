add_library(pci_ui STATIC
  calibration_dialog.cpp
  lens_calibration_dialog.cpp
  camera_image_page.cpp
  capture_tray.cpp
  configure_dialog.cpp
  background_patch_dialog.cpp
  detection_page.cpp
  dialog_geometry.cpp
  history_dialog.cpp
  inspection_result_dialog.cpp
  main_window.cpp
  measurement_mode_dialog.cpp
  performance_page.cpp
  rate_readout.cpp
  setup_guide.cpp
  station_status.cpp
  pieces_page.cpp
  piece_manager_dialog.cpp
  dialog_buttons.cpp
  piece_mosaic.cpp
  piece_report_dialog.cpp
  preferences_page.cpp
  registration_wizard.cpp
  shortcuts_dialog.cpp
  stats_bar_chart.cpp
  template_manager_dialog.cpp
  video_widget.cpp
)
target_include_directories(pci_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_ui
  PUBLIC Qt6::Widgets pci_camera pci_engine pci_repositories
  PRIVATE Qt6::Concurrent pci_core pci_editor pci_vision project_warnings
)
