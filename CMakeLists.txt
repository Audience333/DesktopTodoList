cmake_minimum_required(VERSION 3.25)

set(DESKTOP_TODO_VERSION "2.0.0" CACHE STRING "DesktopTodoList semantic version")
include(cmake/Version.cmake)

project(DesktopTodoList VERSION "${DESKTOP_TODO_VERSION}" LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(CTest)
add_subdirectory(native)
