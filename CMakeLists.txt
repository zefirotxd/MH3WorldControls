cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(MH3WorldControls VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
  message(FATAL_ERROR "MH3WorldControls currently supports Windows only.")
endif()

add_executable(MH3WorldControls WIN32 MH3WorldControls.cpp)
target_compile_features(MH3WorldControls PRIVATE cxx_std_17)
target_link_libraries(MH3WorldControls PRIVATE user32 gdi32 shell32)

if(MSVC)
  target_compile_options(MH3WorldControls PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_custom_command(TARGET MH3WorldControls POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/MH3WorldControls.ini"
    "$<TARGET_FILE_DIR:MH3WorldControls>/MH3WorldControls.ini"
  VERBATIM)
