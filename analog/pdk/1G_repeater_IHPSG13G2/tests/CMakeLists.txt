add_test(NAME analog.pdk.1g_repeater.native COMMAND analog_1g_repeater_core_test
  "${CMAKE_CURRENT_BINARY_DIR}/native_vectors.csv")
set_tests_properties(analog.pdk.1g_repeater.native PROPERTIES LABELS "analog;pdk;repeater;cpp" TIMEOUT 30
  FIXTURES_SETUP repeater_native_vectors)
if(Python3_Interpreter_FOUND)
  add_test(NAME analog.pdk.1g_repeater.shared_memory COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared_memory.py")
  set_tests_properties(analog.pdk.1g_repeater.shared_memory PROPERTIES
    LABELS "analog;pdk;repeater;integration" TIMEOUT 15)
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad" AND
     EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef")
    add_test(NAME analog.pdk.1g_repeater.shared_route_grid COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/shared_route_grid.py")
    set_tests_properties(analog.pdk.1g_repeater.shared_route_grid PROPERTIES
      LABELS "analog;pdk;repeater;integration" TIMEOUT 30)
  endif()
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/sv2v")
    add_test(NAME analog.pdk.1g_repeater.shared_physical COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/shared_physical.py")
    set_tests_properties(analog.pdk.1g_repeater.shared_physical PROPERTIES
      LABELS "analog;pdk;repeater;integration" TIMEOUT 30)
  endif()
  add_test(NAME analog.pdk.1g_repeater.unified_archive_recipe COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/unified_archive_recipe.py")
  set_tests_properties(analog.pdk.1g_repeater.unified_archive_recipe PROPERTIES
    LABELS "analog;pdk;repeater;integration" TIMEOUT 15)
  add_test(NAME analog.pdk.1g_repeater.unified_checkpoint COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/unified_checkpoint.py")
  set_tests_properties(analog.pdk.1g_repeater.unified_checkpoint PROPERTIES
    LABELS "analog;pdk;repeater;integration" TIMEOUT 15)
  add_test(NAME analog.pdk.1g_repeater.unified_inventory COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/unified_synthesis.py")
  set_tests_properties(analog.pdk.1g_repeater.unified_inventory PROPERTIES
    LABELS "analog;pdk;repeater;integration" TIMEOUT 15)
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys")
    add_test(NAME analog.pdk.1g_repeater.unified_parameters COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/unified_parameters.py"
      --yosys "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
      --work "${CMAKE_CURRENT_BINARY_DIR}/unified-parameters")
    set_tests_properties(analog.pdk.1g_repeater.unified_parameters PROPERTIES
      LABELS "analog;pdk;repeater;integration;formal" TIMEOUT 90 RESOURCE_LOCK one_g_eda_memory)
  endif()
  add_test(NAME analog.pdk.1g_repeater.package_clock_results COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/package_clock_results.py")
  set_tests_properties(analog.pdk.1g_repeater.package_clock_results PROPERTIES
    LABELS "analog;pdk;repeater" TIMEOUT 15)
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
    add_test(NAME analog.pdk.1g_repeater.clock_control_guards
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/clock_control_guards.py")
    set_tests_properties(analog.pdk.1g_repeater.clock_control_guards PROPERTIES
      LABELS "analog;pdk;repeater;geometry" TIMEOUT 30)
    add_test(NAME analog.pdk.1g_repeater.compact_analog_rows
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/compact_analog_rows.py")
    set_tests_properties(analog.pdk.1g_repeater.compact_analog_rows PROPERTIES
      LABELS "analog;pdk;repeater;geometry" TIMEOUT 30)
    add_test(NAME analog.pdk.1g_repeater.development_floorplan
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/development_floorplan.py")
    set_tests_properties(analog.pdk.1g_repeater.development_floorplan PROPERTIES
      LABELS "analog;pdk;repeater;geometry" TIMEOUT 60)
    add_test(NAME analog.pdk.1g_repeater.package_development_layout
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/package_development_layout.py")
    set_tests_properties(analog.pdk.1g_repeater.package_development_layout PROPERTIES
      LABELS "analog;pdk;repeater;geometry" TIMEOUT 90 SKIP_RETURN_CODE 77)
    add_test(NAME analog.pdk.1g_repeater.fill_remainder COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/fill_remainder.py")
    set_tests_properties(analog.pdk.1g_repeater.fill_remainder PROPERTIES LABELS "analog;pdk;repeater;geometry" TIMEOUT 30)
    if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout")
      add_test(NAME analog.pdk.1g_repeater.compact_processing_clock
        COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/compact_processing_clock.py"
        --work "${CMAKE_CURRENT_BINARY_DIR}/compact-processing-clock")
      set_tests_properties(analog.pdk.1g_repeater.compact_processing_clock PROPERTIES
        LABELS "analog;pdk;repeater;geometry;lvs" TIMEOUT 600 RUN_SERIAL TRUE)
      add_test(NAME analog.pdk.1g_repeater.processing_clock_integration
        COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_integration.py"
        --work "${CMAKE_CURRENT_BINARY_DIR}/processing-clock-integration")
      set_tests_properties(analog.pdk.1g_repeater.processing_clock_integration PROPERTIES
        LABELS "analog;pdk;repeater;geometry;lvs" TIMEOUT 420)
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
      add_test(NAME analog.pdk.1g_repeater.unified_net_repair COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/unified_net_repair.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.unified_net_repair PROPERTIES
        LABELS "analog;pdk;repeater;timing;formal" TIMEOUT 120 RESOURCE_LOCK one_g_eda_memory)
      add_test(NAME analog.pdk.1g_repeater.routing_rc_margin COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/routing_rc_margin.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.routing_rc_margin PROPERTIES LABELS "analog;pdk;repeater;timing" TIMEOUT 45)
      add_test(NAME analog.pdk.1g_repeater.finish_timing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/finish_timing.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
      set_tests_properties(analog.pdk.1g_repeater.finish_timing PROPERTIES LABELS "analog;pdk;repeater;timing" TIMEOUT 45)
      add_test(NAME analog.pdk.1g_repeater.finish_receiver_timing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/finish_timing.py" --pdk "${repeater_pdk}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --receiver)
      set_tests_properties(analog.pdk.1g_repeater.finish_receiver_timing PROPERTIES LABELS "analog;pdk;repeater;timing" TIMEOUT 45)
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

if(Python3_Interpreter_FOUND AND ONE_G_VERILATOR AND EXISTS "${CPPHDL_1G_CONVERTER}")
  add_test(NAME analog.pdk.1g_repeater.digital_integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../flow.py" --target digital
    --cpphdl "${CPPHDL_1G_CONVERTER}" --cxx "${CPPHDL_TEST_CXX}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/digital-integration")
  set_tests_properties(analog.pdk.1g_repeater.digital_integration PROPERTIES
    LABELS "analog;pdk;repeater;rtl;integration" TIMEOUT 2100 RESOURCE_LOCK one_g_eda_memory
    FIXTURES_SETUP repeater_digital_checked)
  add_test(NAME analog.pdk.1g_repeater.electrical_integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../electrical_flow.py"
    --digital "${CMAKE_CURRENT_BINARY_DIR}/digital-integration/digital"
    --work "${CMAKE_CURRENT_BINARY_DIR}/electrical-integration" --cxx "${CPPHDL_TEST_CXX}")
  set_tests_properties(analog.pdk.1g_repeater.electrical_integration PROPERTIES
    LABELS "analog;pdk;repeater;integration;electrical" TIMEOUT 600 RESOURCE_LOCK one_g_eda_memory
    FIXTURES_REQUIRED repeater_digital_checked FIXTURES_SETUP repeater_electrical_checked)
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2/.cpphdl/venv/bin/python")
    add_test(NAME analog.pdk.1g_repeater.pad_integration COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../pad_integration.py"
      --electrical "${CMAKE_CURRENT_BINARY_DIR}/electrical-integration"
      --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
      --work "${CMAKE_CURRENT_BINARY_DIR}/pad-integration" --core-size 6000)
    set_tests_properties(analog.pdk.1g_repeater.pad_integration PROPERTIES
      LABELS "analog;pdk;repeater;integration;electrical;padframe" TIMEOUT 420
      RESOURCE_LOCK one_g_eda_memory FIXTURES_REQUIRED repeater_electrical_checked)
  endif()
endif()
