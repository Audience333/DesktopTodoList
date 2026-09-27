add_executable(desktop_todo_tests
  test_main.cpp
)

target_compile_features(desktop_todo_tests PRIVATE cxx_std_20)
target_link_libraries(desktop_todo_tests PRIVATE desktop_todo_core)

add_test(NAME native.smoke COMMAND desktop_todo_tests)
