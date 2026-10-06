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
  test_gray.cpp
  test_tool_for_the_piece.cpp
  test_background_colour.cpp
  test_coloured_table.cpp
  test_brush_snap.cpp
  test_camera_profile.cpp
  test_calibration_images.cpp
  test_canvas_geometry.cpp
  test_crash_guard.cpp
  test_database.cpp
  test_shared_connection.cpp
  test_detection_profiles.cpp
  test_calibration_trace.cpp
  test_auto_zone_hides_pieces.cpp
  test_background_patch.cpp
  test_smoke_flag.cpp
  test_measured_piece.cpp
  test_measure_recipes.cpp
  test_outlined_piece.cpp
  test_recipe_is_remembered.cpp
  test_own_recipes.cpp
  test_pass_trigger.cpp
  test_no_silent_tool.cpp
  test_reference_advice.cpp
  test_thread_shows_the_thread.cpp
  test_proposals_are_checkable.cpp
  test_lookups_by_name.cpp
  test_pieces_dropped_for_being_small.cpp
  test_measurement_stability.cpp
  test_brush_reaches_the_contour.cpp
  test_whole_piece_dimensions.cpp
  test_arc_guards.cpp
  test_plateau_rules.cpp
  test_same_piece_everywhere.cpp
  test_option_labels.cpp
  test_csv_for_excel.cpp
  test_domain.cpp
  test_units.cpp
  test_synthetic_measures.cpp
  test_split_touching.cpp
  test_threshold_clipping.cpp
  test_caliper_ambiguity.cpp
  test_delete_scope.cpp
  test_glare_recovery.cpp
  test_new_photos.cpp
  test_region_own_otsu.cpp
  test_edges_on_reflections.cpp
  test_sibling_measures.cpp
  test_difference_map.cpp
  test_diverse_images.cpp
  test_edge_segmentation.cpp
  test_engine.cpp
  test_exit_does_not_hang.cpp
  test_fitting.cpp
  test_geometry_features.cpp
  test_fps_counter.cpp
  test_frame_utils.cpp
  test_inspection.cpp
  test_lens_distortion.cpp
  test_marker_corners_are_subpixel.cpp
  test_measure_units.cpp
  test_measurement_report.cpp
  test_ml.cpp
  test_nut_probe.cpp
  test_periodicity.cpp
  test_piece_order.cpp
  test_piece_report.cpp
  test_profiles.cpp
  test_probe.cpp
  test_result.cpp
  test_shape_class.cpp
  test_shift_report.cpp
  test_shape_measure.cpp
  test_shape_proposals.cpp
  test_tool_options.cpp
  test_point_count_tuning.cpp
  test_view_enhance.cpp
  test_variants.cpp
  test_vision.cpp
  test_detection_tuning.cpp
  test_documented_numbers.cpp
  test_real_photos.cpp
  test_real_video.cpp
  test_working_zone.cpp
  test_one_segmentation.cpp
  test_silhouette_helpers.cpp
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

# El hijo del banco ExitDoesNotHang.BenchManyShortProcessesAllExit: un
# `parallel_for_` de OpenCV y salir (ver tests/exit_bench_child.cpp). Va aparte
# para que lanzar decenas a la vez no cueste la memoria de decenas de pci_tests.
add_executable(pci_exit_bench_child exit_bench_child.cpp)
target_link_libraries(pci_exit_bench_child PRIVATE pci_vision ${OpenCV_LIBS} project_warnings)
add_dependencies(pci_tests pci_exit_bench_child)

include(GoogleTest)

# LAS PRUEBAS QUE MIDEN TIEMPO VAN EN SERIE.
#
# Comparan milisegundos (o cocientes de milisegundos) y con `ctest -j` compiten
# por los núcleos con el resto de la suite: fallaban solas de vez en cuando sin
# que nada estuviera mal. Pasó tres veces seguidas —EngineTest...InColour...,
# DetectionTuning.ItIsFastEnough..., EdgesOnReflections.TheAdviceCosts...— con
# una compilación en marcha, y las tres pasaban al repetirlas solas.
#
# Por eso se registran aparte, con RUN_SERIAL (ctest no ejecuta nada más a la
# vez) y la etiqueta `timing` (`ctest -LE timing` las salta en el ciclo diario).
# La lista va por NOMBRE y no por palabras del nombre: «ShrinksSlowly» es una
# prueba de lógica, no de tiempo. Una prueba nueva que mida tiempo se añade
# aquí.
set(PCI_TIMING_TESTS
  "AutoRoi.CroppingIsAtLeastTwiceAsFastOnASmallPiece"
  "DetectionTuning.ItIsFastEnoughToRunWhileTheOperatorWaits"
  "EdgesOnReflections.TheAdviceCostsWhatSegmentingTwiceCosts"
  "FixtureSpeedup.*"
  "MultiPiece.CountingManyPiecesIsNotAbsurdlySlower"
  "NormalizeSpeedup.*"
  "NutProbe.WhatItCostsToLookAtEveryPiece"
  "PointCountTuning.CostOn*"
  "SiblingMeasures.OpeningAToolCostsWhatSixReadingsCost"
  "ToolsCost.*"
  "ParallelTools.*"
  "EngineTest.AHundredPiecesInColourCostAboutTheSameAsInGrey"
  "StageTimings.*"
  "OneSegmentation.MeasuredCostOfSegmentingTwice"
)
list(JOIN PCI_TIMING_TESTS ":" PCI_TIMING_FILTER)

# DISCOVERY_TIMEOUT 60: listar las pruebas tarda 0,2 s, pero con `ctest -j 6`
# arrancan a la vez varios procesos que reservan ~1 GB cada uno, y los 5 s por
# defecto de CMake se agotaron una vez y tumbaron el descubrimiento entero.
#
# OMP_NUM_THREADS=1: libopencv_core arrastra OpenBLAS, que al cargarse reserva
# memoria para su grupo de hilos: 1027 MB comprometidos por proceso, 130 MB con
# un hilo. Con `ctest -j 6` eran ~6 GB solo en reservas, y una vez la máquina se
# quedó sin memoria y 179 pruebas fallaron al arrancar. Ninguna prueba depende
# de que el álgebra de OpenBLAS vaya en paralelo.

gtest_discover_tests(pci_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
  TEST_FILTER "-${PCI_TIMING_FILTER}"
  PROPERTIES ENVIRONMENT "OMP_NUM_THREADS=1")
gtest_discover_tests(pci_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
  TEST_FILTER "${PCI_TIMING_FILTER}"
  TEST_LIST pci_tests_timing
  PROPERTIES ENVIRONMENT "OMP_NUM_THREADS=1" RUN_SERIAL TRUE LABELS timing)

# Pruebas de gesto sobre el widget: necesitan Qt y una plataforma sin pantalla,
# por eso van aparte de la suite principal.
#
# Las fuentes de archivo van aquí y no en la suite general aunque no dibujen
# nada: necesitan un bucle de eventos vivo, porque el temporizador de la imagen
# y las señales encoladas del hilo del vídeo no llegan sin él.
add_executable(pci_gui_tests
  test_canvas_gestures.cpp
  test_default_buttons.cpp
  test_enter_key_in_dialogs.cpp
  test_menus_show_shortcuts.cpp
  test_default_on_main_window.cpp
  test_menu_grouping.cpp
  test_mnemonics.cpp
  test_background_patch_window.cpp
  test_coloured_table_advice.cpp
  test_detection_groups.cpp
  test_detection_advanced.cpp
  test_same_detection_everywhere.cpp
  test_lens_wizard.cpp
  test_readme_paths.cpp
  test_variant_wizard.cpp
  test_result_dialog.cpp
  test_piece_mosaic.cpp
  test_measurements_panel.cpp
  test_dead_controls.cpp
  test_toolbar_weight.cpp
  test_measures_on_every_source.cpp
  test_bgra_is_not_gray.cpp
  test_holes_belong_to_the_piece.cpp
  test_shape_reason_shows_its_evidence.cpp
  test_the_drawing_brings_its_own_ruler.cpp
  test_how_many_pieces_are_there.cpp
  test_a_washer_seen_at_an_angle.cpp
  test_the_marker_straightens_the_measure.cpp
  test_mosaic_click_survives.cpp
  test_scale_dialog.cpp
  test_selfexplaining.cpp
  test_brush_persistence.cpp
  test_brush_comfort.cpp
  test_brush_size_from_menu.cpp
  test_brush_menu.cpp
  test_context_menu.cpp
  test_theme.cpp
  test_video_bands.cpp
  test_lens_grid_contrast.cpp
  test_secondary_text_contrast.cpp
  test_shape_and_proposals_agree.cpp
  test_angle_proposals_are_corners.cpp
  test_polygon_count_is_checkable.cpp
  test_palette_guard.cpp
  test_overlay_verdict.cpp
  test_overlay_halo.cpp
  test_two_pieces_ui.cpp
  test_report_tabs.cpp
  test_tools_by_family.cpp
  test_edge_advice_reaches_ui.cpp
  test_mosaic_numbering.cpp
  test_configure_dialog.cpp
  test_configure_roundtrip.cpp
  test_frame_sources.cpp
  test_source_remembers.cpp
  test_session_state.cpp
  test_dark_mode_palette.cpp
  test_theme_choice.cpp
  test_status_indicator_words.cpp
  test_outline_reaches_window.cpp
  test_open_file.cpp
  test_verdict_board.cpp
)
set_target_properties(pci_gui_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pci_gui_tests
  PRIVATE pci_camera pci_editor pci_inspection pci_ui pci_vision pci_core
          pci_repositories pci_database Qt6::Widgets Qt6::Test
          GTest::gtest ${OpenCV_LIBS} project_warnings
)
gtest_discover_tests(pci_gui_tests
  DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
  TEST_FILTER "-${PCI_TIMING_FILTER}"
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OMP_NUM_THREADS=1"
)
gtest_discover_tests(pci_gui_tests
  DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
  TEST_FILTER "${PCI_TIMING_FILTER}"
  TEST_LIST pci_gui_tests_timing
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OMP_NUM_THREADS=1" RUN_SERIAL TRUE LABELS timing
)
