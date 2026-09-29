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
  platform/windows/tray_icon.cpp
  platform/windows/hotkey_service.cpp
  platform/windows/window_behavior.cpp
  platform/windows/notification_service.cpp
  platform/windows/autostart_service.cpp
  presentation/details_panel.cpp
  presentation/editor_session.cpp
  presentation/layout.cpp
  presentation/pointer_controller.cpp
  presentation/selection_toolbar.cpp
  presentation/settings_panel.cpp
  presentation/data_transfer_dialog.cpp
  presentation/accessibility_provider.cpp
  presentation/task_list_view.cpp
  presentation/theme.cpp
  presentation/view_model.cpp
)
target_include_directories(desktop_todo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(desktop_todo_core PUBLIC desktop_todo_options Advapi32 Comctl32 Shell32 Comdlg32 WindowsApp Uiautomationcore)

set(DESKTOP_TODO_GENERATED_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DESKTOP_TODO_GENERATED_RESOURCES}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
  "${DESKTOP_TODO_GENERATED_RESOURCES}/app.manifest" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
  "${DESKTOP_TODO_GENERATED_RESOURCES}/app.ico" COPYONLY)
file(TO_CMAKE_PATH "${DESKTOP_TODO_GENERATED_RESOURCES}/app.ico"
  DESKTOP_TODO_ICON_RESOURCE_PATH)
file(TO_CMAKE_PATH "${DESKTOP_TODO_GENERATED_RESOURCES}/app.manifest"
  DESKTOP_TODO_MANIFEST_RESOURCE_PATH)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/version.rc.in"
  "${DESKTOP_TODO_GENERATED_RESOURCES}/version.rc" @ONLY)

add_executable(DesktopTodoList WIN32
  app/application.cpp
  app/main.cpp
  platform/windows/window_class.cpp
  presentation/renderer.cpp
  presentation/text_editor.cpp
  presentation/widget_window.cpp
  "${DESKTOP_TODO_GENERATED_RESOURCES}/version.rc"
)
target_link_libraries(DesktopTodoList PRIVATE desktop_todo_core)
target_link_libraries(DesktopTodoList PRIVATE Comctl32 D2d1 Dwrite Ole32 Shell32)
target_include_directories(DesktopTodoList PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
target_link_options(DesktopTodoList PRIVATE
  "$<$<CONFIG:Debug>:/INCREMENTAL:NO>"
  "/MANIFESTINPUT:${DESKTOP_TODO_GENERATED_RESOURCES}/app.manifest")
target_compile_definitions(DesktopTodoList PRIVATE
  "DESKTOP_TODO_VERSION_STRING=\"${DESKTOP_TODO_VERSION}\"")

add_subdirectory(tests)
