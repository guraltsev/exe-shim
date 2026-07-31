cmake_minimum_required(VERSION 3.23)
project(exe_shim VERSION 1.0.0 LANGUAGES CXX)

find_package(fmt CONFIG REQUIRED)

add_executable(shim shim.cpp)
target_compile_features(shim PRIVATE cxx_std_23)
target_link_libraries(shim PRIVATE fmt::fmt shell32)
target_link_options(shim PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)
target_compile_options(shim PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wconversion>)

include(CTest)
if(BUILD_TESTING)
  add_executable(argument_recorder tests/argument_recorder.cpp)
  target_compile_features(argument_recorder PRIVATE cxx_std_23)
  target_link_options(argument_recorder PRIVATE $<$<CXX_COMPILER_ID:GNU>:-municode>)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME shim-integration
      COMMAND ${CMAKE_COMMAND} -E env EXE_SHIM_BINARY=$<TARGET_FILE:shim>
              EXE_SHIM_RECORDER=$<TARGET_FILE:argument_recorder>
              ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shim.py)
  endif()
endif()
