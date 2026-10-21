add_executable(analog_semantics semantics.cpp)
target_link_libraries(analog_semantics PRIVATE cpphdl::analog)
add_test(NAME analog.semantics COMMAND analog_semantics)

add_executable(analog_cpphdl_interop cpphdl_interop.cpp include_order_analog_first.cpp include_order_digital_first.cpp)
target_link_libraries(analog_cpphdl_interop PRIVATE cpphdl::analog)
add_test(NAME analog.cpphdl_interop COMMAND analog_cpphdl_interop)

add_executable(analog_digital_ports digital_ports.cpp)
target_link_libraries(analog_digital_ports PRIVATE cpphdl::runtime)
set_target_properties(analog_digital_ports PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME analog.digital_ports COMMAND analog_digital_ports)

add_executable(analog_core_isolation core_isolation.cpp)
target_link_libraries(analog_core_isolation PRIVATE cpphdl::analog)
add_test(NAME analog.core_isolation COMMAND analog_core_isolation)

add_executable(analog_feedback_refinement feedback_refinement.cpp)
target_link_libraries(analog_feedback_refinement PRIVATE cpphdl::analog)

add_executable(analog_trace_requirements trace_requirements.cpp)
target_link_libraries(analog_trace_requirements PRIVATE cpphdl::analog)
add_test(NAME analog.trace_requirements COMMAND analog_trace_requirements)

set(analog_regression_tests analog.trace_requirements analog.semantics analog.cpphdl_interop analog.digital_ports analog.core_isolation)
foreach(example_target IN LISTS analog_example_targets)
  string(REGEX REPLACE "^analog_" "" example_name "${example_target}")
  set(example_test analog.examples.${example_name})
  if(example_name STREQUAL "mixed_signal")
    set(example_test analog.mixed_signal)
  endif()
  set(expected_status sampled_pass)
  set(expected_exit 0)
  if(example_name STREQUAL "mutual_influence")
    set(expected_status failed)
    set(expected_exit 1)
  elseif(example_name STREQUAL "diagnostics")
    set(expected_status expected_diagnostics)
  endif()
  add_test(NAME ${example_test} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:${example_target}>"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/example-results/${example_name}"
    "-DEXPECTED_STATUS=${expected_status}" "-DEXPECTED_EXIT=${expected_exit}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/RunExample.cmake")
  list(APPEND analog_regression_tests ${example_test})
endforeach()
# Each stress case checks the expected physical behavior, including loss of regulation.
foreach(example ldo pll agc)
  set(stress dropout)
  if(example STREQUAL "pll")
    set(stress loaded)
  elseif(example STREQUAL "agc")
    set(stress weak)
  endif()
  set_tests_properties(analog.examples.${example} PROPERTIES FIXTURES_SETUP analog_${example}_nominal)
  foreach(scenario ${stress} refined)
    set(test_name analog.feedback.${example}.${scenario})
    add_test(NAME ${test_name} COMMAND "${CMAKE_COMMAND}"
      "-DEXECUTABLE=$<TARGET_FILE:analog_${example}>"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/example-results/${example}-${scenario}"
      "-DSCENARIO=${scenario}" "-DEXPECTED_STATUS=sampled_pass" "-DEXPECTED_EXIT=0"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/RunExample.cmake")
    if(scenario STREQUAL "refined")
      set_tests_properties(${test_name} PROPERTIES FIXTURES_SETUP analog_${example}_refined)
    endif()
    list(APPEND analog_regression_tests ${test_name})
  endforeach()
  set(comparison analog.feedback.${example}.convergence)
  add_test(NAME ${comparison} COMMAND analog_feedback_refinement "${example}"
    "${CMAKE_CURRENT_BINARY_DIR}/example-results/${example}/trace.csv"
    "${CMAKE_CURRENT_BINARY_DIR}/example-results/${example}-refined/trace.csv")
  set_tests_properties(${comparison} PROPERTIES
    FIXTURES_REQUIRED "analog_${example}_nominal;analog_${example}_refined")
  list(APPEND analog_regression_tests ${comparison})
endforeach()
foreach(fixture wrong_units wide_bridge static_ports)
  add_test(NAME analog.reject_${fixture} COMMAND "${CMAKE_COMMAND}"
    "-DCXX=${CMAKE_CXX_COMPILER}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/negative/${fixture}.cpp"
    "-DPACKAGE_ROOT=${CPPHDL_ANALOG_PACKAGE_ROOT}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/negative/${fixture}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/CompileFail.cmake")
  list(APPEND analog_regression_tests analog.reject_${fixture})
endforeach()
set_tests_properties(${analog_regression_tests} PROPERTIES LABELS "analog" TIMEOUT 120)
add_custom_target(check-analog
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -L analog
  DEPENDS analog_veriloga_generate_models analog_veriloga_exporter analog_veriloga_cross_simulation cpphdl-veriloga analog_feedback_refinement analog_trace_requirements analog_semantics analog_cpphdl_interop analog_digital_ports analog_core_isolation ${analog_example_targets}
  USES_TERMINAL)

add_custom_target(check-veriloga
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -L veriloga
  DEPENDS analog_veriloga_generate_models analog_veriloga_exporter analog_veriloga_cross_simulation cpphdl-veriloga ${analog_example_targets}
  USES_TERMINAL)
