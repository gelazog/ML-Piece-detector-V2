# ONNX Runtime del entorno UCRT64: intenta el paquete CMake y si no existe
# localiza librería y headers a mano.
find_package(onnxruntime CONFIG QUIET)
if(NOT onnxruntime_FOUND)
  find_library(ONNXRUNTIME_LIBRARY onnxruntime REQUIRED)
  find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h REQUIRED
            PATH_SUFFIXES onnxruntime)
  add_library(onnxruntime_interface INTERFACE)
  target_link_libraries(onnxruntime_interface INTERFACE ${ONNXRUNTIME_LIBRARY})
  target_include_directories(onnxruntime_interface INTERFACE ${ONNXRUNTIME_INCLUDE_DIR})
  add_library(onnxruntime::onnxruntime ALIAS onnxruntime_interface)
endif()

add_library(pci_ml STATIC
  embedding_extractor.cpp
  reference.cpp
)
target_include_directories(pci_ml PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pci_ml
  PUBLIC pci_core ${OpenCV_LIBS}
  PRIVATE onnxruntime::onnxruntime project_warnings
)
