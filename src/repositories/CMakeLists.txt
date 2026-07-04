add_library(pci_repositories STATIC
  piece_repository.cpp
  settings_repository.cpp
  tool_repository.cpp
)
target_include_directories(pci_repositories PUBLIC ${CMAKE_SOURCE_DIR}/src)
# database, ml e inspection son PUBLIC: los headers exponen Db&, ml::Reference
# y ToolConfig.
target_link_libraries(pci_repositories
  PUBLIC pci_database pci_ml pci_inspection
  PRIVATE pci_core project_warnings
)
