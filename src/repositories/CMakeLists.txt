add_library(pci_repositories STATIC
  config_io.cpp
  detection_profile_repository.cpp
  inspection_repository.cpp
  piece_repository.cpp
  settings_repository.cpp
  tool_repository.cpp
)
target_include_directories(pci_repositories PUBLIC ${CMAKE_SOURCE_DIR}/src)
# database, ml e inspection son PUBLIC: los headers exponen Db&, ml::Reference
# y ToolConfig.
target_link_libraries(pci_repositories
  # pci_vision es PUBLIC: piece_repository.h expone vision::BoardConfig.
  PUBLIC pci_database pci_domain pci_ml pci_inspection pci_vision
  # Qt6::Core (solo QtCore, sin widgets) para leer/escribir el JSON de
  # configuración: escribirlo a mano seria reinventar un parser.
  PRIVATE pci_core Qt6::Core project_warnings
)
