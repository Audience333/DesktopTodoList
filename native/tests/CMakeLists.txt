add_executable(desktop_todo_tests
  application/app_service_test.cpp
  test_main.cpp
  domain/reminder_engine_test.cpp
  domain/selection_model_test.cpp
  domain/task_store_test.cpp
  domain/task_query_test.cpp
  domain/validation_test.cpp
  persistence/import_export_test.cpp
  persistence/json_codec_test.cpp
  persistence/state_repository_test.cpp
  platform/single_instance_test.cpp
  presentation/layout_test.cpp
  presentation/renderer_smoke_test.cpp
  presentation/renderer_state_test.cpp
  presentation/task_list_view_test.cpp
  presentation/editing_test.cpp
  presentation/editor_host_smoke_test.cpp
  presentation/widget_window_smoke_test.cpp
  presentation/pointer_controller_test.cpp
)

target_compile_features(desktop_todo_tests PRIVATE cxx_std_20)
target_include_directories(desktop_todo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(desktop_todo_tests PRIVATE desktop_todo_core)
target_sources(desktop_todo_tests PRIVATE
  ../presentation/renderer.cpp
  ../presentation/text_editor.cpp
  ../platform/windows/window_class.cpp
  ../presentation/widget_window.cpp
)
target_link_libraries(desktop_todo_tests PRIVATE D2d1 Dwrite)
target_compile_definitions(desktop_todo_tests PRIVATE
  DESKTOP_TODO_SOURCE_DIR=L"${CMAKE_SOURCE_DIR}"
)

add_test(NAME native.smoke COMMAND desktop_todo_tests --filter build_marker_)
add_test(NAME native.reminder_engine COMMAND desktop_todo_tests --filter reminder_engine_)
add_test(NAME native.selection_model COMMAND desktop_todo_tests --filter selection_model_)
add_test(NAME native.task_store COMMAND desktop_todo_tests --filter task_store_)
add_test(NAME native.task_query COMMAND desktop_todo_tests --filter task_query_)
add_test(NAME native.validation COMMAND desktop_todo_tests --filter validation_)
add_test(NAME native.import_export COMMAND desktop_todo_tests --filter import_export_)
add_test(NAME native.json_codec COMMAND desktop_todo_tests --filter json_codec_)
add_test(NAME native.state_repository COMMAND desktop_todo_tests --filter state_repository_)
add_test(NAME native.app_service COMMAND desktop_todo_tests --filter app_service_)
add_test(NAME native.single_instance COMMAND desktop_todo_tests --filter single_instance_)
add_test(NAME native.layout COMMAND desktop_todo_tests --filter layout_)
add_test(NAME native.renderer_smoke COMMAND desktop_todo_tests --filter renderer_smoke_)
add_test(NAME native.renderer_state COMMAND desktop_todo_tests --filter renderer_state_)
add_test(NAME native.task_list_view COMMAND desktop_todo_tests --filter task_list_view_)
add_test(NAME native.editing COMMAND desktop_todo_tests --filter editing_)
add_test(NAME native.editor_host_smoke COMMAND desktop_todo_tests --filter editor_host_smoke_)
add_test(NAME native.widget_window_smoke COMMAND desktop_todo_tests --filter widget_window_smoke_)
add_test(NAME native.pointer_controller COMMAND desktop_todo_tests --filter pointer_controller_)
add_test(NAME native.selection_toolbar COMMAND desktop_todo_tests --filter selection_toolbar_)
