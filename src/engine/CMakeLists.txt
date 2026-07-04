# Orquestador del registro e inspección: une vision + ml + herramientas + BD.
add_library(pci_engine STATIC
  inspection_engine.cpp
  registration_session.cpp
)
target_include_directories(pci_engine PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_engine
  PUBLIC pci_core pci_domain pci_ml pci_inspection pci_repositories pci_vision
         ${OpenCV_LIBS}
  PRIVATE project_warnings
)
