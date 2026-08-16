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
  test_auto_measure.cpp
  test_camera_profile.cpp
  test_calibration_images.cpp
  test_canvas_geometry.cpp
  test_crash_guard.cpp
  test_database.cpp
  test_domain.cpp
  test_diverse_images.cpp
  test_engine.cpp
  test_fitting.cpp
  test_geometry_features.cpp
  test_fps_counter.cpp
  test_frame_utils.cpp
  test_inspection.cpp
  test_ml.cpp
  test_periodicity.cpp
  test_profiles.cpp
  test_probe.cpp
  test_result.cpp
  test_shape_class.cpp
  test_shape_measure.cpp
  test_shape_proposals.cpp
  test_tool_options.cpp
  test_vision.cpp
  test_working_zone.cpp
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

# Pruebas de gesto sobre el widget: necesitan Qt y una plataforma sin pantalla,
# por eso van aparte de la suite principal.
#
# Las fuentes de archivo van aquí y no en la suite general aunque no dibujen
# nada: necesitan un bucle de eventos vivo, porque el temporizador de la imagen
# y las señales encoladas del hilo del vídeo no llegan sin él.
add_executable(pci_gui_tests
  test_canvas_gestures.cpp
  test_configure_dialog.cpp
  test_frame_sources.cpp
)
set_target_properties(pci_gui_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pci_gui_tests
  PRIVATE pci_camera pci_editor pci_inspection pci_ui pci_vision pci_core
          pci_repositories pci_database Qt6::Widgets Qt6::Test
          GTest::gtest ${OpenCV_LIBS} project_warnings
)
gtest_discover_tests(pci_gui_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
