add_library(pci_repositories STATIC
  piece_repository.cpp
  settings_repository.cpp
)
target_include_directories(pci_repositories PUBLIC ${CMAKE_SOURCE_DIR}/src)
# database y ml son PUBLIC: los headers exponen Db& y ml::Reference.
target_link_libraries(pci_repositories
  PUBLIC pci_database pci_ml
  PRIVATE pci_core project_warnings
)
