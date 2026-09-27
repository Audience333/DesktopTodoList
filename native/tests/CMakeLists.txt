add_executable(desktop_todo_tests
  test_main.cpp
  domain/task_store_test.cpp
  domain/validation_test.cpp
)

target_compile_features(desktop_todo_tests PRIVATE cxx_std_20)
target_include_directories(desktop_todo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(desktop_todo_tests PRIVATE desktop_todo_core)

add_test(NAME native.smoke COMMAND desktop_todo_tests --filter build_marker_)
add_test(NAME native.task_store COMMAND desktop_todo_tests --filter task_store_)
add_test(NAME native.validation COMMAND desktop_todo_tests --filter validation_)
