add_library(desktop_todo_options INTERFACE)
target_compile_features(desktop_todo_options INTERFACE cxx_std_20)
target_compile_definitions(desktop_todo_options INTERFACE UNICODE _UNICODE)

if(MSVC)
  target_compile_options(desktop_todo_options INTERFACE /W4 /WX /permissive- /utf-8)
endif()

add_library(desktop_todo_core STATIC
  domain/build_info.cpp
  domain/validation.cpp
)
target_include_directories(desktop_todo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(desktop_todo_core PUBLIC desktop_todo_options)

add_executable(DesktopTodoList WIN32
  app/main.cpp
)
target_link_libraries(DesktopTodoList PRIVATE desktop_todo_core)

add_subdirectory(tests)
