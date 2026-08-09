cmake_minimum_required(VERSION 3.23)
project(exe_shim VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_link_options(-static)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/artifacts")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_library(shim-core STATIC src/shim.cpp)
target_compile_features(shim-core PRIVATE cxx_std_23)
target_include_directories(shim-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shim-core PRIVATE fmt::fmt tomlplusplus::tomlplusplus shell32)
target_compile_options(shim-core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wconversion>)

add_executable(shim-console src/console_main.cpp)
target_compile_features(shim-console PRIVATE cxx_std_23)
target_link_libraries(shim-console PRIVATE shim-core)
target_link_options(shim-console PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)

add_executable(shim-gui WIN32 src/gui_main.cpp)
target_compile_features(shim-gui PRIVATE cxx_std_23)
target_link_libraries(shim-gui PRIVATE shim-core)
target_link_options(shim-gui PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/make-shim.cmd
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/artifacts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/src/make-shim.cmd
          ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/make-shim.cmd
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/make-shim.cmd)
add_custom_target(make-shim-script ALL
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/make-shim.cmd)

include(CTest)
if(BUILD_TESTING)
  add_executable(argument_recorder tests/helpers/argument_recorder.cpp)
  target_compile_features(argument_recorder PRIVATE cxx_std_23)
  target_link_options(argument_recorder PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)
  set_target_properties(argument_recorder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME shim-integration
      COMMAND ${CMAKE_COMMAND} -E env EXE_SHIM_BINARY=$<TARGET_FILE:shim-console>
              EXE_SHIM_RECORDER=$<TARGET_FILE:argument_recorder>
              ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shim.py)
  endif()
endif()
