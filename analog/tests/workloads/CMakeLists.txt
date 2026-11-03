add_executable(analog_workload_smoke bench.cpp)
target_link_libraries(analog_workload_smoke PRIVATE cpphdl::analog)
if(Python3_Interpreter_FOUND)
  add_test(NAME analog.workloads.native_smoke COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --bench "$<TARGET_FILE:analog_workload_smoke>"
    --work "${CMAKE_CURRENT_BINARY_DIR}/native")
  set_tests_properties(analog.workloads.native_smoke PROPERTIES
    LABELS "analog;solver;workloads;smoke;cpp" TIMEOUT 190)
  add_test(NAME analog.workloads.contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_contract.py")
  set_tests_properties(analog.workloads.contract PROPERTIES
    LABELS "analog;solver;workloads;smoke" TIMEOUT 15)
  find_program(CPPHDL_ANALOG_REFERENCE_NGSPICE NAMES ngspice
    HINTS "${CPPHDL_ANALOG_REAL_ROOT}/out/veriloga-toolchain/install/bin")
  if(CPPHDL_ANALOG_REFERENCE_NGSPICE)
    add_test(NAME analog.workloads.reference_smoke COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --bench "$<TARGET_FILE:analog_workload_smoke>"
      --ngspice "${CPPHDL_ANALOG_REFERENCE_NGSPICE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/reference")
    set_tests_properties(analog.workloads.reference_smoke PROPERTIES
      LABELS "analog;solver;workloads;smoke;reference" TIMEOUT 190)
  endif()
endif()
