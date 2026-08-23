# Lógica de negocio pura: sin Qt y sin OpenCV (regla estricta del proyecto).
add_library(pci_domain STATIC
  shift_report.cpp
  calibration.cpp
  capture_quality.cpp
  measurement_mode.cpp
  verdict.cpp
)
target_include_directories(pci_domain PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_domain
  PUBLIC pci_core
  PRIVATE project_warnings
)
