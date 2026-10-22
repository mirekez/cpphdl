add_library(cpphdl_veriloga_driver STATIC Driver.cpp)
target_compile_features(cpphdl_veriloga_driver PRIVATE cxx_std_20)
target_compile_definitions(cpphdl_veriloga_driver PRIVATE
  CPPHDL_ANALOG_PACKAGE_ROOT="${CPPHDL_ANALOG_PACKAGE_ROOT}"
  CPPHDL_ANALOG_CXX="${CMAKE_CXX_COMPILER}")
add_executable(cpphdl-veriloga "${CPPHDL_ANALOG_PACKAGE_ROOT}/cpphdl-veriloga.cpp")
target_link_libraries(cpphdl-veriloga PRIVATE cpphdl_veriloga_driver)

add_executable(analog_veriloga_generate_models generate_models.cpp)
target_link_libraries(analog_veriloga_generate_models PRIVATE cpphdl::analog)

if(BUILD_TESTING)
  add_executable(analog_veriloga_exporter tests/exporter.cpp)
  target_link_libraries(analog_veriloga_exporter PRIVATE cpphdl::analog)
  add_test(NAME analog.veriloga.exporter COMMAND analog_veriloga_exporter)
  add_test(NAME analog.veriloga.cli COMMAND "${CMAKE_COMMAND}"
    "-DCONVERTER=$<TARGET_FILE:cpphdl-veriloga>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_fixture.hpp"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-results"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunExport.cmake")
  set_tests_properties(analog.veriloga.exporter analog.veriloga.cli PROPERTIES LABELS "analog;veriloga" TIMEOUT 90)
  if(TARGET cpphdl)
    add_test(NAME analog.veriloga.root_cli COMMAND "${CMAKE_COMMAND}"
      "-DCONVERTER=$<TARGET_FILE:cpphdl>" "-DROOT_DRIVER=ON"
      "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_fixture.hpp"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/root-cli-results"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunExport.cmake")
    set_tests_properties(analog.veriloga.root_cli PROPERTIES LABELS "analog;veriloga" TIMEOUT 90)
  endif()
  add_executable(analog_veriloga_cross_simulation tests/cross_simulation.cpp)
  target_link_libraries(analog_veriloga_cross_simulation PRIVATE cpphdl::analog)
  set(CPPHDL_ANALOG_VERILOGA_TESTS "AUTO" CACHE STRING "Verilog-A independent simulation tests: AUTO, ON or OFF")
  set_property(CACHE CPPHDL_ANALOG_VERILOGA_TESTS PROPERTY STRINGS AUTO ON OFF)
  if(NOT CPPHDL_ANALOG_VERILOGA_TESTS MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "CPPHDL_ANALOG_VERILOGA_TESTS must be AUTO, ON or OFF")
  endif()
  if(NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    find_program(OPENVAF_EXECUTABLE NAMES openvaf openvaf-r)
    find_program(NGSPICE_EXECUTABLE NAMES ngspice)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND Python3_Interpreter_FOUND)
      add_test(NAME analog.veriloga.live_clock COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/live_clock.py" --ngspice "${NGSPICE_EXECUTABLE}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/results/live_clock")
      set_tests_properties(analog.veriloga.live_clock PROPERTIES LABELS "analog;veriloga;interop" TIMEOUT 60)
      add_test(NAME analog.veriloga.trace_codec COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/trace_codec.py")
      set_tests_properties(analog.veriloga.trace_codec PROPERTIES LABELS "analog;veriloga" TIMEOUT 30)
      foreach(circuit divider rc rc_initial floating_charge mos_regions inverter controlled_resistor driver opamp ldo ldo_dropout pll agc)
        add_test(NAME analog.veriloga.${circuit} COMMAND "${CMAKE_COMMAND}"
          "-DBENCH=$<TARGET_FILE:analog_veriloga_cross_simulation>" "-DCASE=${circuit}"
          "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/results/${circuit}"
          "-DOPENVAF=${OPENVAF_EXECUTABLE}" "-DNGSPICE=${NGSPICE_EXECUTABLE}"
          "-DPYTHON=${Python3_EXECUTABLE}" "-DCOMPARE=${CMAKE_CURRENT_SOURCE_DIR}/compare.py"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunComparison.cmake")
        set_tests_properties(analog.veriloga.${circuit} PROPERTIES LABELS "analog;veriloga" TIMEOUT 300)
      endforeach()
      add_test(NAME analog.veriloga.detect_resistor_fault COMMAND "${CMAKE_COMMAND}"
        "-DBENCH=$<TARGET_FILE:analog_veriloga_cross_simulation>" "-DCASE=rc" "-DINJECT_RESISTOR_FAULT=ON"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/results/detect-resistor-fault"
        "-DOPENVAF=${OPENVAF_EXECUTABLE}" "-DNGSPICE=${NGSPICE_EXECUTABLE}"
        "-DPYTHON=${Python3_EXECUTABLE}" "-DCOMPARE=${CMAKE_CURRENT_SOURCE_DIR}/compare.py"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunComparison.cmake")
      set_tests_properties(analog.veriloga.detect_resistor_fault PROPERTIES LABELS "analog;veriloga" TIMEOUT 120)
      message(STATUS "Verilog-A comparisons enabled: ${OPENVAF_EXECUTABLE}; ${NGSPICE_EXECUTABLE}")
    elseif(CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "ON")
      message(FATAL_ERROR "Verilog-A comparisons require OPENVAF_EXECUTABLE, NGSPICE_EXECUTABLE and Python3")
    else()
      message(STATUS "Verilog-A cross-simulator tests unavailable (need OpenVAF, ngspice/OSDI and Python3); exporter remains available")
    endif()
  endif()
endif()

add_executable(analog_veriloga_hierarchy tests/hierarchy.cpp)
if(Python3_Interpreter_FOUND)
  add_test(NAME analog.veriloga.trace_storage COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/trace_storage_contract.py")
  set_tests_properties(analog.veriloga.trace_storage PROPERTIES LABELS "analog;veriloga" TIMEOUT 30)
endif()
if(Python3_Interpreter_FOUND AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.veriloga.time_grid COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/time_grid_contract.py" --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/time_grid")
  set_tests_properties(analog.veriloga.time_grid PROPERTIES LABELS "analog;veriloga" TIMEOUT 180)
endif()
target_link_libraries(analog_veriloga_hierarchy PRIVATE cpphdl::analog)
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME analog.veriloga.hierarchy_structure COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/hierarchy.py")
  set_tests_properties(analog.veriloga.hierarchy_structure PROPERTIES LABELS "analog;veriloga" TIMEOUT 30)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.veriloga.hierarchy_equations COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/hierarchy_equations.py"
      --bench "$<TARGET_FILE:analog_veriloga_hierarchy>" --driver "$<TARGET_FILE:cpphdl-veriloga>"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/hierarchy")
    set_tests_properties(analog.veriloga.hierarchy_equations PROPERTIES LABELS "analog;veriloga" TIMEOUT 1200)
  endif()
endif()
