cmake_minimum_required(VERSION 3.25)

project(DesktopTodoList LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(CTest)
add_subdirectory(native)
