add_library(pci_core STATIC
  logging.cpp
)
target_include_directories(pci_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_core PRIVATE project_warnings)
