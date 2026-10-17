set(CPPHDL_IHP_PDK_ROOT "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2" CACHE PATH "Prepared IHP SG13G2 checkout")
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103_nqs.osdi"
    AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  foreach(corner mos_tt mos_ss mos_ff)
    add_test(NAME analog.1g.ihp_driver.${corner} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/driver.py" --bench "$<TARGET_FILE:analog_1g_bench>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --corner "${corner}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/${corner}")
    set_tests_properties(analog.1g.ihp_driver.${corner} PROPERTIES LABELS "analog;1g;ihp;veriloga" TIMEOUT 180)
  endforeach()
  add_test(NAME analog.1g.ihp_biased_driver.mos_tt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/driver.py" --bench "$<TARGET_FILE:analog_1g_bench>"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --physical-bias
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/biased_tt")
  set_tests_properties(analog.1g.ihp_biased_driver.mos_tt PROPERTIES LABELS "analog;1g;ihp;veriloga" TIMEOUT 180)
  foreach(corner mos_tt mos_ss mos_ff)
    if(corner STREQUAL "mos_tt")
      set(trim 30)
    elseif(corner STREQUAL "mos_ss")
      set(trim 42)
    else()
      set(trim 20)
    endif()
    add_test(NAME analog.1g.ihp_trimmed_driver.${corner} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/driver.py" --bench "$<TARGET_FILE:analog_1g_bench>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --corner "${corner}" --trim-code "${trim}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/trimmed_${corner}")
    set_tests_properties(analog.1g.ihp_trimmed_driver.${corner} PROPERTIES LABELS "analog;1g;ihp;veriloga" TIMEOUT 180)
  endforeach()
endif()
