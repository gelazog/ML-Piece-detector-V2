include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Solo aplica en MSVC; inofensivo en MinGW.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(pci_tests
  test_database.cpp
  test_domain.cpp
  test_engine.cpp
  test_fps_counter.cpp
  test_frame_utils.cpp
  test_inspection.cpp
  test_ml.cpp
  test_result.cpp
  test_vision.cpp
)
target_link_libraries(pci_tests
  PRIVATE pci_camera pci_core pci_database pci_domain pci_engine pci_inspection
          pci_ml pci_repositories pci_vision GTest::gtest_main ${OpenCV_LIBS}
          project_warnings
)
target_compile_definitions(pci_tests PRIVATE PCI_MODELS_DIR="${CMAKE_SOURCE_DIR}/models")

if(WIN32 AND ONNXRUNTIME_DLL_PATH)
  add_custom_command(TARGET pci_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ONNXRUNTIME_DLL_PATH} ${ONNXRUNTIME_PROVIDERS_DLL_PATH}
            $<TARGET_FILE_DIR:pci_tests>)
endif()

include(GoogleTest)
gtest_discover_tests(pci_tests DISCOVERY_MODE PRE_TEST)
