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
  test_detection_profiles.cpp
  test_calibration_trace.cpp
  test_auto_zone_hides_pieces.cpp
  test_background_patch.cpp
  test_smoke_flag.cpp
  test_measured_piece.cpp
  test_measure_recipes.cpp
  test_outlined_piece.cpp
  test_recipe_is_remembered.cpp
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
  test_fitting.cpp
  test_geometry_features.cpp
  test_fps_counter.cpp
  test_frame_utils.cpp
  test_inspection.cpp
  test_lens_distortion.cpp
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
  test_view_enhance.cpp
  test_variants.cpp
  test_vision.cpp
  test_detection_tuning.cpp
  test_documented_numbers.cpp
  test_real_photos.cpp
  test_real_video.cpp
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
  test_default_buttons.cpp
  test_enter_key_in_dialogs.cpp
  test_menus_show_shortcuts.cpp
  test_default_on_main_window.cpp
  test_menu_grouping.cpp
  test_mnemonics.cpp
  test_background_patch_window.cpp
  test_coloured_table_advice.cpp
  test_detection_groups.cpp
  test_same_detection_everywhere.cpp
  test_lens_wizard.cpp
  test_readme_paths.cpp
  test_variant_wizard.cpp
  test_result_dialog.cpp
  test_piece_mosaic.cpp
  test_measurements_panel.cpp
  test_mosaic_click_survives.cpp
  test_scale_dialog.cpp
  test_selfexplaining.cpp
  test_brush_persistence.cpp
  test_brush_comfort.cpp
  test_brush_size_from_menu.cpp
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
