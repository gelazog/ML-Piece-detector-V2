cmake_minimum_required(VERSION 3.21)
project(PCInspector VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

# Warnings estrictos solo para el código propio; los headers de Qt/OpenCV
# entran como SYSTEM vía sus imported targets y no disparan -Werror.
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

add_subdirectory(src/core)
add_subdirectory(src/camera)
add_subdirectory(src/ui)

add_executable(pc_inspector src/main.cpp)
target_link_libraries(pc_inspector PRIVATE pci_ui pci_camera pci_core project_warnings)
# Sin ventana de consola en Release; en Debug se conserva para ver el log en vivo.
set_target_properties(pc_inspector PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
