add_test(NAME analog.pdk.1g_repeater.native COMMAND analog_1g_repeater_core_test
  "${CMAKE_CURRENT_BINARY_DIR}/native_vectors.csv")
set_tests_properties(analog.pdk.1g_repeater.native PROPERTIES LABELS "analog;pdk;repeater;cpp" TIMEOUT 30
  FIXTURES_SETUP repeater_native_vectors)
if(Python3_Interpreter_FOUND)
  add_test(NAME analog.pdk.1g_repeater.frames COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_vectors.py" "${CMAKE_CURRENT_BINARY_DIR}/native_vectors.csv")
  set_tests_properties(analog.pdk.1g_repeater.frames PROPERTIES LABELS "analog;pdk;repeater;cpp" TIMEOUT 30
    FIXTURES_REQUIRED repeater_native_vectors)
  add_test(NAME analog.pdk.1g_repeater.guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flow.py")
  set_tests_properties(analog.pdk.1g_repeater.guards PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 30)
  add_test(NAME analog.pdk.1g_repeater.physical_guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/physical_guards.py")
  set_tests_properties(analog.pdk.1g_repeater.physical_guards PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 30)
  set(repeater_pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
  if(EXISTS "${repeater_pdk}/.cpphdl/venv/bin/python")
    add_test(NAME analog.pdk.1g_repeater.padframe COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/padframe.py" --pdk "${repeater_pdk}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/padframe")
    set_tests_properties(analog.pdk.1g_repeater.padframe PROPERTIES LABELS "analog;pdk;repeater;padframe" TIMEOUT 120)
  endif()
  option(CPPHDL_REPEATER_PHYSICAL_TESTS "Run the complete IHP repeater-core implementation flow" OFF)
  if(CPPHDL_REPEATER_PHYSICAL_TESTS)
    set(CPPHDL_REPEATER_PHYSICAL_WORK "${CMAKE_CURRENT_BINARY_DIR}/physical" CACHE PATH "Repeater physical regression artifacts")
    add_test(NAME analog.pdk.1g_repeater.physical COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../flow.py" --target core
      --work "${CPPHDL_REPEATER_PHYSICAL_WORK}")
    set_tests_properties(analog.pdk.1g_repeater.physical PROPERTIES LABELS "analog;pdk;repeater;physical"
      TIMEOUT 1200 RUN_SERIAL TRUE)
  endif()
endif()
