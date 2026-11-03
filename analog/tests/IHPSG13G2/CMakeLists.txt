if(Python3_Interpreter_FOUND)
  add_test(NAME analog.ihp.process_contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_contract.py")
  set_tests_properties(analog.ihp.process_contract PROPERTIES LABELS "analog;solver;ihp;smoke" TIMEOUT 15)
  add_test(NAME analog.ihp.inverter_contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_inverter.py")
  set_tests_properties(analog.ihp.inverter_contract PROPERTIES LABELS "analog;solver;ihp;smoke" TIMEOUT 15)
endif()
if(TARGET cpphdl::osdi)
  add_executable(analog_ihp_process_bench process_bench.cpp)
  target_link_libraries(analog_ihp_process_bench PRIVATE cpphdl::osdi)
  add_executable(analog_ihp_inverter_bench inverter_bench.cpp)
  target_link_libraries(analog_ihp_inverter_bench PRIVATE cpphdl::osdi)
  set(CPPHDL_ANALOG_IHP_PDK "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2" CACHE PATH "Installed IHP PDK")
  if(EXISTS "${CPPHDL_ANALOG_IHP_PDK}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103.osdi")
    add_test(NAME analog.ihp.process_abi COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/abi.py" --bench "$<TARGET_FILE:analog_ihp_process_bench>"
      --pdk "${CPPHDL_ANALOG_IHP_PDK}" --work "${CMAKE_CURRENT_BINARY_DIR}/abi")
    set_tests_properties(analog.ihp.process_abi PROPERTIES LABELS "analog;solver;ihp;smoke" TIMEOUT 90)
    foreach(flow native reference)
      set(reference_args "")
      if(flow STREQUAL "reference")
        if(NOT CPPHDL_ANALOG_REFERENCE_NGSPICE)
          continue()
        endif()
        list(APPEND reference_args --ngspice "${CPPHDL_ANALOG_REFERENCE_NGSPICE}")
      endif()
      add_test(NAME analog.ihp.process_${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --bench "$<TARGET_FILE:analog_ihp_process_bench>"
        --pdk "${CPPHDL_ANALOG_IHP_PDK}" --work "${CMAKE_CURRENT_BINARY_DIR}/${flow}" ${reference_args})
      set_tests_properties(analog.ihp.process_${flow} PROPERTIES
        LABELS "analog;solver;ihp;smoke;${flow}" TIMEOUT 190 RUN_SERIAL TRUE)
      add_test(NAME analog.ihp.inverter_${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/inverter.py" --bench "$<TARGET_FILE:analog_ihp_inverter_bench>"
        --pdk "${CPPHDL_ANALOG_IHP_PDK}" --work "${CMAKE_CURRENT_BINARY_DIR}/inverter_${flow}" ${reference_args})
      set_tests_properties(analog.ihp.inverter_${flow} PROPERTIES
        LABELS "analog;solver;ihp;smoke;${flow}" TIMEOUT 190 RUN_SERIAL TRUE)
    endforeach()
  else()
    message(STATUS "IHP process simulations disabled: install the PDK with pdk/.prepare_IHPSG13G2.sh")
  endif()
endif()
