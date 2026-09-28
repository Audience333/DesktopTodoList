add_library(desktop_todo_options INTERFACE)
target_compile_features(desktop_todo_options INTERFACE cxx_std_20)
target_compile_definitions(desktop_todo_options INTERFACE UNICODE _UNICODE)

if(MSVC)
  target_compile_options(desktop_todo_options INTERFACE /W4 /WX /permissive- /utf-8)
endif()

add_library(desktop_todo_core STATIC
  application/app_service.cpp
  domain/build_info.cpp
  domain/reminder_engine.cpp
  domain/selection_model.cpp
  domain/task_query.cpp
  domain/task_store.cpp
  domain/validation.cpp
  persistence/import_export.cpp
  persistence/json_codec.cpp
  persistence/state_repository.cpp
  persistence/win32_file_system.cpp
  platform/windows/single_instance.cpp
)
target_include_directories(desktop_todo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(desktop_todo_core PUBLIC desktop_todo_options Advapi32 WindowsApp)

add_executable(DesktopTodoList WIN32
  app/application.cpp
  app/main.cpp
  platform/windows/window_class.cpp
)
target_link_libraries(DesktopTodoList PRIVATE desktop_todo_core)
target_link_libraries(DesktopTodoList PRIVATE Ole32 Shell32)

add_subdirectory(tests)
