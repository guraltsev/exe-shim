cmake_minimum_required(VERSION 3.23)
project(exe_shim VERSION 0.1.0 LANGUAGES CXX)

find_package(fmt CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# Keep generated binaries in the repository's distribution directory while
# leaving intermediate libraries and object files under the selected build tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.artifacts")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# The core library owns all configuration parsing and process-launch behavior;
# the console and GUI targets below provide only subsystem-specific entrypoints.
add_library(shim-core STATIC src/shim.cpp)
target_compile_features(shim-core PRIVATE cxx_std_23)
target_include_directories(shim-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shim-core PRIVATE fmt::fmt tomlplusplus::tomlplusplus shell32)
target_compile_options(shim-core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wconversion>
  $<$<CXX_COMPILER_ID:GNU>:-Os>
  $<$<CXX_COMPILER_ID:GNU>:-ffunction-sections>
  $<$<CXX_COMPILER_ID:GNU>:-fdata-sections>)

# Build the normal names dynamically. Static variants remain available for
# single-file distribution, with an explicit suffix so the choice is visible.
function(add_shim target main_source)
  if(ARGN)
    add_executable(${target} WIN32 ${main_source})
  else()
    add_executable(${target} ${main_source})
  endif()
  target_compile_features(${target} PRIVATE cxx_std_23)
  target_link_libraries(${target} PRIVATE shim-core)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Os>
    $<$<CXX_COMPILER_ID:GNU>:-ffunction-sections>
    $<$<CXX_COMPILER_ID:GNU>:-fdata-sections>)
  target_link_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-municode>
    $<$<CXX_COMPILER_ID:GNU>:-Wl,--gc-sections>
    $<$<CXX_COMPILER_ID:GNU>:-Wl,--strip-all>)
endfunction()

add_shim(shim-console src/console_main.cpp)
add_shim(shim-gui src/gui_main.cpp GUI)
add_shim(shim-console-static src/console_main.cpp)
set_target_properties(shim-console-static PROPERTIES OUTPUT_NAME "shim-console.static")
target_link_options(shim-console-static PRIVATE $<$<CXX_COMPILER_ID:GNU>:-static>)
add_shim(shim-gui-static src/gui_main.cpp GUI)
set_target_properties(shim-gui-static PROPERTIES OUTPUT_NAME "shim-gui.static")
target_link_options(shim-gui-static PRIVATE $<$<CXX_COMPILER_ID:GNU>:-static>)

# Discover MinGW's actual runtime imports after linking. This is more reliable
# than TARGET_RUNTIME_DLLS for MinGW and excludes Windows system DLLs.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_runtime_dlls.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/copy_runtime_dlls.cmake
  @ONLY)
add_custom_target(runtime-dlls ALL
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/copy_runtime_dlls.cmake
  DEPENDS shim-console shim-gui
  COMMENT "Copying dynamic launcher runtime DLLs and their license notices")

# Copy the shim-generation helper next to the built launchers for distribution.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/make-shim.cmd
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/src/make-shim.cmd
          ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/make-shim.cmd
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/make-shim.cmd)
add_custom_target(make-shim-script ALL
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/make-shim.cmd)

# Keep the launcher's own dual-license terms distinct from third-party runtime
# notices copied by runtime-dlls.
add_custom_target(project-licenses ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-MIT
          ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/LICENSE-exe-shim-MIT.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-UNLICENSE
          ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/LICENSE-exe-shim-UNLICENSE.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/README.md
          ${CMAKE_CURRENT_SOURCE_DIR}/.artifacts/README-exe-shim.md
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-MIT
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-UNLICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
  COMMENT "Copying exe-shim license texts")

include(CTest)
if(BUILD_TESTING)
  # The recorder is a tiny target process that writes argv, cwd, and selected
  # environment values for the Python integration suite to inspect.
  add_executable(argument_recorder tests/helpers/argument_recorder.cpp)
  target_compile_features(argument_recorder PRIVATE cxx_std_23)
  target_link_options(argument_recorder PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)
  target_link_options(argument_recorder PRIVATE $<$<CXX_COMPILER_ID:GNU>:-static>)
  set_target_properties(argument_recorder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # Pass concrete built paths so the tests do not depend on PATH or install steps.
    add_test(NAME shim-integration
      COMMAND ${CMAKE_COMMAND} -E env EXE_SHIM_BINARY=$<TARGET_FILE:shim-console>
              EXE_SHIM_RECORDER=$<TARGET_FILE:argument_recorder>
              ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shim.py)
  endif()
endif()
