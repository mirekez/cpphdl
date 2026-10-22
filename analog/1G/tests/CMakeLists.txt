if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "1G independent checks require Python3")
endif()
find_program(ONE_G_VERILATOR verilator)
set(CPPHDL_1G_CONVERTER "${CPPHDL_ANALOG_PACKAGE_ROOT}/build/cpphdl" CACHE FILEPATH "C++HDL hardware converter")
set(one_g_hardware_args "")
if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
  list(APPEND one_g_hardware_args --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
    --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
endif()
foreach(block alignment pcs scrambler)
  add_executable(analog_1g_${block} "${block}.cpp")
  target_link_libraries(analog_1g_${block} PRIVATE cpphdl::runtime)
  if(block STREQUAL "pcs")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_SOURCE_DIR}")
  else()
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block})
  endif()
  set_tests_properties(analog.1g.${block}.native PROPERTIES LABELS "analog;1g;hardware;cpp" TIMEOUT 30)
  if(ONE_G_VERILATOR AND EXISTS "${CPPHDL_1G_CONVERTER}")
    add_test(NAME analog.1g.${block}.rtl COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/hardware.py" --case "${block}" --converter "${CPPHDL_1G_CONVERTER}"
      --native "$<TARGET_FILE:analog_1g_${block}>"
      --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
      ${one_g_hardware_args}
      --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${block}")
    set_tests_properties(analog.1g.${block}.rtl PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 300)
  endif()
endforeach()
set(one_g_cases dac dac_loaded driver driver_loaded mos_driver biased_driver trimmed_driver memory pulse_shaper slicer
  hybrid hybrid_bad channel channel_loaded channel_reverse ctle_low ctle_nyquist
  dfe dfe_bypass transmitter receiver pair_link link link_loaded link_noisy link_uncancelled pair_link_refined link_refined)
foreach(circuit IN LISTS one_g_cases)
  string(REGEX REPLACE "_refined$" "" bench_case "${circuit}")
  if(circuit MATCHES "_refined$")
    set(refinement 2)
  else()
    set(refinement 1)
  endif()
  set(work "${CMAKE_CURRENT_BINARY_DIR}/results/${circuit}")
  add_test(NAME analog.1g.${circuit}.cpp COMMAND "${CMAKE_COMMAND}"
    "-DBENCH=$<TARGET_FILE:analog_1g_bench>" "-DDSP=$<TARGET_FILE:analog_1g_process>"
    "-DCASE=${bench_case}" "-DREFINEMENT=${refinement}" "-DFLOW=cpp" "-DWORK=${work}/cpp" "-DPYTHON=${Python3_EXECUTABLE}"
    "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/verify.py" -P "${CMAKE_CURRENT_SOURCE_DIR}/RunFlow.cmake")
  set_tests_properties(analog.1g.${circuit}.cpp PROPERTIES LABELS "analog;1g;cpp" TIMEOUT 900
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1" FIXTURES_SETUP one_g_${circuit}_cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.1g.${circuit}.veriloga COMMAND "${CMAKE_COMMAND}"
      "-DBENCH=$<TARGET_FILE:analog_1g_bench>" "-DDSP=$<TARGET_FILE:analog_1g_process>"
      "-DCASE=${bench_case}" "-DREFINEMENT=${refinement}" "-DFLOW=veriloga" "-DWORK=${work}/veriloga" "-DPYTHON=${Python3_EXECUTABLE}"
      "-DOPENVAF=${OPENVAF_EXECUTABLE}" "-DNGSPICE=${NGSPICE_EXECUTABLE}"
      "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/verify.py" -P "${CMAKE_CURRENT_SOURCE_DIR}/RunFlow.cmake")
    set_tests_properties(analog.1g.${circuit}.veriloga PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 900
      ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1" FIXTURES_SETUP one_g_${circuit}_veriloga)
    add_test(NAME analog.1g.${circuit}.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verify.py" compare "${work}/cpp" "${work}/veriloga")
    set_tests_properties(analog.1g.${circuit}.compare PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_${circuit}_cpp;one_g_${circuit}_veriloga")
  endif()
endforeach()
set(guard_args "${CMAKE_CURRENT_BINARY_DIR}/results/dac/cpp")
set(guard_fixtures one_g_dac_cpp)
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  list(APPEND guard_args --veriloga "${CMAKE_CURRENT_BINARY_DIR}/results/dac/veriloga")
  list(APPEND guard_fixtures one_g_dac_veriloga)
  foreach(circuit pair_link link)
    add_test(NAME analog.1g.${circuit}.refinement COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/refinement.py" "${CMAKE_CURRENT_BINARY_DIR}/results" "${circuit}")
    set_tests_properties(analog.1g.${circuit}.refinement PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_${circuit}_cpp;one_g_${circuit}_veriloga;one_g_${circuit}_refined_cpp;one_g_${circuit}_refined_veriloga")
  endforeach()
endif()
add_test(NAME analog.1g.checker_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/integrity.py" ${guard_args})
set_tests_properties(analog.1g.checker_guards PROPERTIES LABELS "analog;1g;cpp" TIMEOUT 30
  FIXTURES_REQUIRED "${guard_fixtures}")
add_test(NAME analog.1g.algorithms COMMAND analog_1g_algorithms)
set_tests_properties(analog.1g.algorithms PROPERTIES LABELS "analog;1g;cpp" TIMEOUT 30)
add_test(NAME analog.1g.generated COMMAND "${CMAKE_COMMAND}"
  "-DGENERATOR=$<TARGET_FILE:analog_1g_generate>" "-DROOT=${CPPHDL_ANALOG_PACKAGE_ROOT}"
  "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generated"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/Generated.cmake")
set_tests_properties(analog.1g.generated PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 60)
if(OPENVAF_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.generated_compile COMMAND "${CMAKE_COMMAND}"
    "-DGENERATOR=$<TARGET_FILE:analog_1g_generate>" "-DROOT=${CPPHDL_ANALOG_PACKAGE_ROOT}"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generated-compile"
    "-DOPENVAF=${OPENVAF_EXECUTABLE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Generated.cmake")
  set_tests_properties(analog.1g.generated_compile PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 240)
endif()
add_custom_target(check-1g
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -L 1g --output-on-failure -j 2
  DEPENDS analog_1g_bench analog_1g_generate analog_1g_process analog_1g_algorithms
    analog_1g_alignment analog_1g_pcs analog_1g_scrambler USES_TERMINAL)
