set(ten_g_cases primitives gm_stage limiter rectifier sample_hold dff mux2 xor history deserializer
  serializer emphasis channel sfi sfi_decay ctle_low ctle_nyquist lock_monitor lock_monitor_bad hogge vco cdr cdr_phase cdr_offset cdr_offset_negative cdr_gap cdr_ppm cdr_noisy cdr_no_data
  dfe dfe_bypass transmitter receiver link link_loaded ctle_nyquist_refined cdr_refined link_refined)
if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "10G test checks require Python3")
endif()
foreach(circuit IN LISTS ten_g_cases)
  string(REGEX REPLACE "_refined$" "" bench_case "${circuit}")
  if(circuit MATCHES "_refined$")
    set(refinement 2)
  else()
    set(refinement 1)
  endif()
  set(work "${CMAKE_CURRENT_BINARY_DIR}/results/${circuit}")
  add_test(NAME analog.10g.${circuit}.cpp COMMAND "${CMAKE_COMMAND}"
    "-DBENCH=$<TARGET_FILE:analog_10g_bench>" "-DCASE=${bench_case}" "-DREFINEMENT=${refinement}" "-DFLOW=cpp"
    "-DWORK=${work}/cpp" "-DPYTHON=${Python3_EXECUTABLE}"
    "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/verify.py" -P "${CMAKE_CURRENT_SOURCE_DIR}/RunFlow.cmake")
  set_tests_properties(analog.10g.${circuit}.cpp PROPERTIES LABELS "analog;10g;cpp" TIMEOUT 600
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1" FIXTURES_SETUP ten_g_${circuit}_cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.10g.${circuit}.veriloga COMMAND "${CMAKE_COMMAND}"
      "-DBENCH=$<TARGET_FILE:analog_10g_bench>" "-DCASE=${bench_case}" "-DREFINEMENT=${refinement}" "-DFLOW=veriloga"
      "-DWORK=${work}/veriloga" "-DPYTHON=${Python3_EXECUTABLE}"
      "-DOPENVAF=${OPENVAF_EXECUTABLE}" "-DNGSPICE=${NGSPICE_EXECUTABLE}"
      "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/verify.py" -P "${CMAKE_CURRENT_SOURCE_DIR}/RunFlow.cmake")
    set_tests_properties(analog.10g.${circuit}.veriloga PROPERTIES LABELS "analog;10g;veriloga" TIMEOUT 600
      ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1" FIXTURES_SETUP ten_g_${circuit}_veriloga)
    add_test(NAME analog.10g.${circuit}.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verify.py" compare "${work}/cpp" "${work}/veriloga")
    set_tests_properties(analog.10g.${circuit}.compare PROPERTIES LABELS "analog;10g;veriloga" TIMEOUT 60
      FIXTURES_REQUIRED "ten_g_${circuit}_cpp;ten_g_${circuit}_veriloga")
  endif()
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.10g.loading COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/verify.py" loading
    "${CMAKE_CURRENT_BINARY_DIR}/results/link" "${CMAKE_CURRENT_BINARY_DIR}/results/link_loaded")
  set_tests_properties(analog.10g.loading PROPERTIES LABELS "analog;10g;veriloga" TIMEOUT 60
    FIXTURES_REQUIRED "ten_g_link_cpp;ten_g_link_veriloga;ten_g_link_loaded_cpp;ten_g_link_loaded_veriloga")
  foreach(circuit ctle_nyquist cdr link)
    add_test(NAME analog.10g.${circuit}.refinement COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verify.py" refine
      "${CMAKE_CURRENT_BINARY_DIR}/results/${circuit}" "${CMAKE_CURRENT_BINARY_DIR}/results/${circuit}_refined")
    set_tests_properties(analog.10g.${circuit}.refinement PROPERTIES LABELS "analog;10g;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "ten_g_${circuit}_cpp;ten_g_${circuit}_veriloga;ten_g_${circuit}_refined_cpp;ten_g_${circuit}_refined_veriloga")
  endforeach()
endif()
add_custom_target(check-10g
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -L 10g --output-on-failure -j 2
  DEPENDS analog_10g_bench USES_TERMINAL)

add_test(NAME analog.10g.solver COMMAND analog_10g_solver)
set_tests_properties(analog.10g.solver PROPERTIES LABELS "analog;10g;cpp" TIMEOUT 60)
add_dependencies(check-10g analog_10g_solver)

add_test(NAME analog.10g.generated COMMAND "${CMAKE_COMMAND}"
  "-DGENERATOR=$<TARGET_FILE:analog_10g_generate>" "-DROOT=${CPPHDL_ANALOG_PACKAGE_ROOT}"
  "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generated"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/Generated.cmake")
set_tests_properties(analog.10g.generated PROPERTIES LABELS "analog;10g;veriloga" TIMEOUT 60)
if(OPENVAF_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.10g.generated_compile COMMAND "${CMAKE_COMMAND}"
    "-DGENERATOR=$<TARGET_FILE:analog_10g_generate>" "-DROOT=${CPPHDL_ANALOG_PACKAGE_ROOT}"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generated-compile"
    "-DOPENVAF=${OPENVAF_EXECUTABLE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Generated.cmake")
  set_tests_properties(analog.10g.generated_compile PROPERTIES LABELS "analog;10g;veriloga" TIMEOUT 180)
endif()
add_dependencies(check-10g analog_10g_generate)
