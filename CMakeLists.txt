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
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio objdetect)

# Windows trae su propio onnxruntime.dll en System32 (Windows ML) y System32
# precede al PATH en la búsqueda de DLLs; solo el directorio del ejecutable le
# gana. Por eso la DLL correcta se copia junto a cada binario que la usa.
if(WIN32)
  find_file(ONNXRUNTIME_DLL_PATH onnxruntime.dll PATH_SUFFIXES bin)
  find_file(ONNXRUNTIME_PROVIDERS_DLL_PATH onnxruntime_providers_shared.dll
            PATH_SUFFIXES bin)
endif()

add_subdirectory(src/core)
add_subdirectory(src/domain)
add_subdirectory(src/camera)
add_subdirectory(src/vision)
add_subdirectory(src/ml)
add_subdirectory(src/database)
add_subdirectory(src/inspection_editor)
add_subdirectory(src/repositories)
add_subdirectory(src/engine)
add_subdirectory(src/ui)

add_executable(pc_inspector src/main.cpp)
target_link_libraries(pc_inspector
  PRIVATE pci_ui pci_repositories pci_database pci_camera pci_core project_warnings)
# Sin ventana de consola en Release; en Debug se conserva para ver el log en vivo.
set_target_properties(pc_inspector PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

if(WIN32 AND ONNXRUNTIME_DLL_PATH)
  add_custom_command(TARGET pc_inspector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ONNXRUNTIME_DLL_PATH} ${ONNXRUNTIME_PROVIDERS_DLL_PATH}
            $<TARGET_FILE_DIR:pc_inspector>)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Herramienta de setup para preparar el modelo de embeddings. El código
# protobuf de ONNX se genera localmente con el protoc instalado para evitar
# desajustes de versión entre gencode y runtime.
find_program(PROTOC_EXECUTABLE protoc)
find_library(PROTOBUF_LIBRARY protobuf)
if(PROTOC_EXECUTABLE AND PROTOBUF_LIBRARY)
  set(ONNX_GEN_DIR ${CMAKE_BINARY_DIR}/onnx_gen)
  file(MAKE_DIRECTORY ${ONNX_GEN_DIR})
  add_custom_command(
    OUTPUT ${ONNX_GEN_DIR}/onnx-ml.pb.cc ${ONNX_GEN_DIR}/onnx-ml.pb.h
    COMMAND ${PROTOC_EXECUTABLE} --cpp_out=${ONNX_GEN_DIR}
            --proto_path=${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/tools/onnx-ml.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/onnx-ml.proto
    COMMENT "Generando código protobuf de ONNX"
  )
  add_executable(prepare_model tools/prepare_model.cpp ${ONNX_GEN_DIR}/onnx-ml.pb.cc)
  target_include_directories(prepare_model PRIVATE ${ONNX_GEN_DIR})
  # protobuf de MSYS2 es DLL: sin este define el linker intenta resolver
  # variables TLS internas de la librería y falla.
  target_compile_definitions(prepare_model PRIVATE PROTOBUF_USE_DLLS)
  target_link_libraries(prepare_model PRIVATE ${PROTOBUF_LIBRARY})
endif()
