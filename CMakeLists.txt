cmake_minimum_required(VERSION 3.23)
project(exe_shim VERSION 1.0.0 LANGUAGES CXX)

find_package(fmt CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

add_library(shim-core STATIC shim.cpp)
target_compile_features(shim-core PRIVATE cxx_std_23)
target_include_directories(shim-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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

include(CTest)
if(BUILD_TESTING)
  add_executable(argument_recorder tests/argument_recorder.cpp)
  target_compile_features(argument_recorder PRIVATE cxx_std_23)
  target_link_options(argument_recorder PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME shim-integration
      COMMAND ${CMAKE_COMMAND} -E env EXE_SHIM_BINARY=$<TARGET_FILE:shim-console>
              EXE_SHIM_RECORDER=$<TARGET_FILE:argument_recorder>
              ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shim.py)
  endif()
endif()
