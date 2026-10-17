cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(cpphdl_analog VERSION 0.3.0 LANGUAGES CXX)
  include(CTest)
endif()

get_filename_component(CPPHDL_ANALOG_REAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)
get_filename_component(CPPHDL_ANALOG_PACKAGE_ROOT "${CPPHDL_ANALOG_REAL_ROOT}/.." ABSOLUTE)
include("${CPPHDL_ANALOG_PACKAGE_ROOT}/cmake/CpphdlRuntime.cmake")
add_library(cpphdl_analog INTERFACE)
add_library(cpphdl::analog ALIAS cpphdl_analog)
add_library(analog ALIAS cpphdl_analog)
target_link_libraries(cpphdl_analog INTERFACE cpphdl::runtime)
target_include_directories(cpphdl_analog INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(cpphdl_analog INTERFACE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cpphdl_analog INTERFACE -Wall -Wextra)
endif()

add_subdirectory(VerilogA)

set(analog_example_targets "")
file(GLOB example_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*/main.cpp")
foreach(source IN LISTS example_sources)
  get_filename_component(directory "${source}" DIRECTORY)
  get_filename_component(folder "${directory}" NAME)
  set(example_target analog_${folder})
  add_executable(${example_target} "${source}")
  target_link_libraries(${example_target} PRIVATE cpphdl::analog)
  list(APPEND analog_example_targets ${example_target})
endforeach()

# Re-elaborate the actual examples so checked-in models cannot drift from their C++ definitions.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  set(analog_generation_args
    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/VerilogA/generate.py"
    --root "${CPPHDL_ANALOG_PACKAGE_ROOT}"
    --models "$<TARGET_FILE:analog_veriloga_generate_models>")
  foreach(example_target IN LISTS analog_example_targets)
    string(REGEX REPLACE "^analog_" "" example_name "${example_target}")
    list(APPEND analog_generation_args --example "${example_name}=$<TARGET_FILE:${example_target}>")
  endforeach()
  add_custom_target(generate-veriloga COMMAND ${analog_generation_args}
    DEPENDS analog_veriloga_generate_models ${analog_example_targets} VERBATIM USES_TERMINAL)
  if(BUILD_TESTING)
    add_test(NAME analog.veriloga.generated COMMAND ${analog_generation_args} --check)
    set_tests_properties(analog.veriloga.generated PROPERTIES LABELS "analog;veriloga" TIMEOUT 120)
    if(OPENVAF_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
      add_test(NAME analog.veriloga.generated_compile COMMAND ${analog_generation_args}
        --check --openvaf "${OPENVAF_EXECUTABLE}")
      set_tests_properties(analog.veriloga.generated_compile PROPERTIES LABELS "analog;veriloga" TIMEOUT 180)
    endif()
  endif()
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
