find_package(SQLite3 REQUIRED)

add_library(pci_database STATIC
  db.cpp
  schema.cpp
  statement.cpp
)
target_include_directories(pci_database PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_database
  PUBLIC pci_core
  PRIVATE SQLite::SQLite3 project_warnings
)
