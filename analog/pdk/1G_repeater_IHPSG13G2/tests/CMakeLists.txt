add_test(NAME analog.pdk.1g_repeater.native COMMAND analog_1g_repeater_core_test
  "${CMAKE_CURRENT_BINARY_DIR}/native_vectors.csv")
set_tests_properties(analog.pdk.1g_repeater.native PROPERTIES LABELS "analog;pdk;repeater;cpp" TIMEOUT 30
  FIXTURES_SETUP repeater_native_vectors)
if(Python3_Interpreter_FOUND)
  add_test(NAME analog.pdk.1g_repeater.drc_reuse COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/drc_reuse.py")
  set_tests_properties(analog.pdk.1g_repeater.drc_reuse PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 15)
  add_test(NAME analog.pdk.1g_repeater.tool_process COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tool_process.py")
  set_tests_properties(analog.pdk.1g_repeater.tool_process PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 15)
  add_test(NAME analog.pdk.1g_repeater.route_checkpoints COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/route_checkpoints.py")
  set_tests_properties(analog.pdk.1g_repeater.route_checkpoints PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 15)
  add_test(NAME analog.pdk.1g_repeater.io_cdl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/io_lvs.py")
  set_tests_properties(analog.pdk.1g_repeater.io_cdl PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 30)
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
  if(NGSPICE_EXECUTABLE AND EXISTS "${repeater_pdk}/ihp-sg13g2/libs.ref/sg13g2_io/spice/sg13g2_io.spice")
    add_test(NAME analog.pdk.1g_repeater.pad_operating COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/pad_operating.py" --pdk "${repeater_pdk}"
      --ngspice "${NGSPICE_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/pad_operating")
    set_tests_properties(analog.pdk.1g_repeater.pad_operating PROPERTIES LABELS "analog;pdk;repeater;padframe" TIMEOUT 600)
  endif()
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout")
    add_test(NAME analog.pdk.1g_repeater.ideal_contacts COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/ideal_taps.py" --pdk "${repeater_pdk}"
      --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/ideal-contacts")
    set_tests_properties(analog.pdk.1g_repeater.ideal_contacts PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 90)
  endif()
  if(EXISTS "${repeater_pdk}/.cpphdl/venv/bin/python")
    add_test(NAME analog.pdk.1g_repeater.fill_remainder COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/fill_remainder.py")
    set_tests_properties(analog.pdk.1g_repeater.fill_remainder PROPERTIES LABELS "analog;pdk;repeater;geometry" TIMEOUT 30)
    if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout")
      add_test(NAME analog.pdk.1g_repeater.fill_active_hierarchy COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/fill_active_hierarchy.py" --pdk "${repeater_pdk}"
        --klayout "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout"
        --work "${CMAKE_CURRENT_BINARY_DIR}/fill-active-hierarchy")
      set_tests_properties(analog.pdk.1g_repeater.fill_active_hierarchy PROPERTIES LABELS "analog;pdk;repeater;geometry" TIMEOUT 180)
    endif()
    add_test(NAME analog.pdk.1g_repeater.padframe COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/padframe.py" --pdk "${repeater_pdk}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/padframe")
    set_tests_properties(analog.pdk.1g_repeater.padframe PROPERTIES LABELS "analog;pdk;repeater;padframe" TIMEOUT 120)
    if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
      add_test(NAME analog.pdk.1g_repeater.routing_rc_margin COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/routing_rc_margin.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.routing_rc_margin PROPERTIES LABELS "analog;pdk;repeater;timing" TIMEOUT 45)
      add_test(NAME analog.pdk.1g_repeater.finish_timing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/finish_timing.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.finish_timing PROPERTIES LABELS "analog;pdk;repeater;timing" TIMEOUT 45)
      add_test(NAME analog.pdk.1g_repeater.finish_clock_timing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/finish_timing.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --clock-finish "${CPPHDL_ANALOG_REAL_ROOT}/1G/IHPSG13G2/processing_clock_finish.tcl")
      set_tests_properties(analog.pdk.1g_repeater.finish_clock_timing PROPERTIES LABELS "analog;pdk;repeater;timing" TIMEOUT 45)
      add_test(NAME analog.pdk.1g_repeater.antenna_inputs COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/antenna_inputs.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.antenna_inputs PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 45)
      add_test(NAME analog.pdk.1g_repeater.empty_nets COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/empty_nets.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.empty_nets PROPERTIES LABELS "analog;pdk;repeater" TIMEOUT 45)
    endif()
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
