add_executable(analog_semantics semantics.cpp)
add_executable(analog_linear_accuracy linear_accuracy.cpp)
target_link_libraries(analog_linear_accuracy PRIVATE cpphdl::analog)
add_test(NAME analog.linear_accuracy COMMAND analog_linear_accuracy)
set_tests_properties(analog.linear_accuracy PROPERTIES LABELS "analog;solver" TIMEOUT 30)
add_executable(analog_nand_latch nand_latch.cpp)
target_link_libraries(analog_nand_latch PRIVATE cpphdl::analog)
add_executable(analog_buffered_nand_latch buffered_nand_latch.cpp)
target_link_libraries(analog_buffered_nand_latch PRIVATE cpphdl::analog)
add_executable(analog_differential_source_follower differential_source_follower.cpp)
target_link_libraries(analog_differential_source_follower PRIVATE cpphdl::analog)
add_executable(analog_cmos_storage cmos_storage.cpp)
target_link_libraries(analog_cmos_storage PRIVATE cpphdl::analog)
add_executable(analog_phase_frequency_detector phase_frequency_detector.cpp)
target_link_libraries(analog_phase_frequency_detector PRIVATE cpphdl::analog)
if(Python3_Interpreter_FOUND)
  set(pfd_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND pfd_flows veriloga)
  endif()
  foreach(flow IN LISTS pfd_flows)
    add_test(NAME analog.basic.phase_frequency_detector.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/phase_frequency_detector.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_phase_frequency_detector>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/phase-frequency-detector/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.basic.phase_frequency_detector.${flow} PROPERTIES LABELS "analog;basic;${flow}"
      TIMEOUT 3600 FIXTURES_SETUP pfd_${flow})
  endforeach()
  if("veriloga" IN_LIST pfd_flows)
    add_test(NAME analog.basic.phase_frequency_detector.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/phase_frequency_detector.py" --flow compare
      --work "${CMAKE_CURRENT_BINARY_DIR}/phase-frequency-detector")
    set_tests_properties(analog.basic.phase_frequency_detector.compare PROPERTIES LABELS "analog;basic;veriloga"
      TIMEOUT 90 FIXTURES_REQUIRED "pfd_cpp;pfd_veriloga")
  endif()
endif()
if(Python3_Interpreter_FOUND)
  set(storage_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND storage_flows veriloga)
  endif()
  foreach(flow IN LISTS storage_flows)
    add_test(NAME analog.basic.cmos_storage.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmos_storage.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_cmos_storage>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/cmos-storage/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.basic.cmos_storage.${flow} PROPERTIES LABELS "analog;basic;${flow}"
      TIMEOUT 1800 FIXTURES_SETUP cmos_storage_${flow})
  endforeach()
  if("veriloga" IN_LIST storage_flows)
    add_test(NAME analog.basic.cmos_storage.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmos_storage.py" --flow compare
      --work "${CMAKE_CURRENT_BINARY_DIR}/cmos-storage")
    set_tests_properties(analog.basic.cmos_storage.compare PROPERTIES LABELS "analog;basic;veriloga"
      TIMEOUT 60 FIXTURES_REQUIRED "cmos_storage_cpp;cmos_storage_veriloga")
  endif()
endif()
if(Python3_Interpreter_FOUND AND OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE
    AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.basic.nand_latch COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/nand_latch.py" --bench "$<TARGET_FILE:analog_nand_latch>"
    --work "${CMAKE_CURRENT_BINARY_DIR}/nand-latch"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.basic.nand_latch PROPERTIES LABELS "analog;basic;veriloga" TIMEOUT 600)
  add_test(NAME analog.basic.buffered_nand_latch COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/nand_latch.py" --bench "$<TARGET_FILE:analog_buffered_nand_latch>"
    --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/buffered_nand_latch.cpp" --cxx "${CMAKE_CXX_COMPILER}"
    --step-ps 0.5
    --work "${CMAKE_CURRENT_BINARY_DIR}/buffered-nand-latch"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.basic.buffered_nand_latch PROPERTIES LABELS "analog;basic;veriloga" TIMEOUT 600)
  add_test(NAME analog.basic.differential_source_follower COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/differential_source_follower.py"
    --bench "$<TARGET_FILE:analog_differential_source_follower>"
    --work "${CMAKE_CURRENT_BINARY_DIR}/differential-source-follower"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}")
  set_tests_properties(analog.basic.differential_source_follower PROPERTIES LABELS "analog;basic;veriloga" TIMEOUT 2400)
endif()
target_link_libraries(analog_semantics PRIVATE cpphdl::analog)
add_test(NAME analog.semantics COMMAND analog_semantics)
add_executable(analog_capacitor_loops capacitor_loops.cpp)
target_link_libraries(analog_capacitor_loops PRIVATE cpphdl::analog)
add_test(NAME analog.capacitor_loops COMMAND analog_capacitor_loops)
set_tests_properties(analog.capacitor_loops PROPERTIES LABELS "analog;solver" TIMEOUT 120)
find_package(Eigen3 3.3 QUIET NO_MODULE)
if(Eigen3_FOUND)
  add_executable(analog_sparse_rank sparse_rank.cpp)
  target_link_libraries(analog_sparse_rank PRIVATE cpphdl::analog Eigen3::Eigen)
  add_test(NAME analog.sparse_rank COMMAND analog_sparse_rank)
  set_tests_properties(analog.sparse_rank PROPERTIES LABELS "analog;solver" TIMEOUT 30)
  add_executable(analog_sparse_solver sparse_solver.cpp)
  target_link_libraries(analog_sparse_solver PRIVATE cpphdl::analog Eigen3::Eigen)
  add_test(NAME analog.sparse_solver COMMAND analog_sparse_solver)
  set_tests_properties(analog.sparse_solver PROPERTIES LABELS "analog;solver" TIMEOUT 300)
  add_executable(analog_capacitor_loops_sparse capacitor_loops.cpp)
  target_compile_definitions(analog_capacitor_loops_sparse PRIVATE CPPHDL_TEST_SPARSE)
  target_link_libraries(analog_capacitor_loops_sparse PRIVATE cpphdl::analog Eigen3::Eigen)
  add_test(NAME analog.capacitor_loops_sparse COMMAND analog_capacitor_loops_sparse)
  set_tests_properties(analog.capacitor_loops_sparse PROPERTIES LABELS "analog;solver" TIMEOUT 120)
endif()

add_executable(analog_cpphdl_interop cpphdl_interop.cpp include_order_analog_first.cpp include_order_digital_first.cpp)
target_link_libraries(analog_cpphdl_interop PRIVATE cpphdl::analog)
add_test(NAME analog.cpphdl_interop COMMAND analog_cpphdl_interop)
add_executable(analog_schmitt_sense schmitt_sense.cpp)
target_link_libraries(analog_schmitt_sense PRIVATE cpphdl::analog)
add_test(NAME analog.schmitt_sense COMMAND analog_schmitt_sense)
set_tests_properties(analog.schmitt_sense PROPERTIES LABELS "analog;solver;interop" TIMEOUT 30)
add_executable(analog_continuation continuation.cpp)
target_link_libraries(analog_continuation PRIVATE cpphdl::analog)
add_test(NAME analog.continuation COMMAND analog_continuation)
set_tests_properties(analog.continuation PROPERTIES LABELS "analog;solver;interop" TIMEOUT 90)

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

set(analog_regression_tests analog.trace_requirements analog.semantics analog.cpphdl_interop analog.schmitt_sense analog.continuation analog.digital_ports analog.core_isolation)
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
  DEPENDS analog_linear_accuracy analog_nand_latch analog_buffered_nand_latch analog_cmos_storage analog_phase_frequency_detector analog_capacitor_loops analog_veriloga_generate_models analog_veriloga_exporter analog_veriloga_cross_simulation cpphdl-veriloga analog_feedback_refinement analog_trace_requirements analog_semantics analog_cpphdl_interop analog_schmitt_sense analog_continuation analog_digital_ports analog_core_isolation ${analog_example_targets}
  USES_TERMINAL)
if(TARGET analog_sparse_solver)
  add_dependencies(check-analog analog_sparse_solver analog_sparse_rank analog_capacitor_loops_sparse)
endif()

add_custom_target(check-veriloga
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -L veriloga
  DEPENDS analog_nand_latch analog_buffered_nand_latch analog_cmos_storage analog_phase_frequency_detector analog_veriloga_generate_models analog_veriloga_exporter analog_veriloga_cross_simulation cpphdl-veriloga ${analog_example_targets}
  USES_TERMINAL)
add_dependencies(check-analog analog_differential_source_follower)
add_dependencies(check-veriloga analog_differential_source_follower)
add_custom_target(check-analog-transient
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 2
    -R "^analog[.](continuation|semantics|trace_requirements|cpphdl_interop|linear_accuracy|schmitt_sense|sparse_solver|sparse_rank)$"
  DEPENDS analog_continuation analog_semantics analog_trace_requirements analog_cpphdl_interop
    analog_linear_accuracy analog_schmitt_sense USES_TERMINAL VERBATIM)
if(TARGET analog_sparse_solver)
  add_dependencies(check-analog-transient analog_sparse_solver analog_sparse_rank)
endif()

add_dependencies(check-analog analog_veriloga_hierarchy)
add_dependencies(check-veriloga analog_veriloga_hierarchy)
