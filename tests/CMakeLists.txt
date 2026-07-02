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
  test_fps_counter.cpp
  test_frame_utils.cpp
  test_result.cpp
  test_vision.cpp
)
target_link_libraries(pci_tests
  PRIVATE pci_camera pci_core pci_vision GTest::gtest_main ${OpenCV_LIBS} project_warnings
)

include(GoogleTest)
gtest_discover_tests(pci_tests DISCOVERY_MODE PRE_TEST)
