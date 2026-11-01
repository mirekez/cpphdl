add_test(NAME analog.1g.ihp_reference_clock.placement_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_placement.py")
set_tests_properties(analog.1g.ihp_reference_clock.placement_guards PROPERTIES
  LABELS "analog;1g;ihp;physical;clock" TIMEOUT 30)

add_custom_target(prepare-ihp-extraction
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_magic.py"
    --destination "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-toolchain"
  COMMENT "Build pinned Magic for IHP analog parasitic extraction")
add_custom_target(prepare-ihp-extraction-sidewall
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_magic.py"
    --destination "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-sidewall"
    --clip-tile-area --full-sidewall
  COMMENT "Build isolated Magic candidate with bounded tile area and full sidewall coefficients")
add_custom_target(prepare-ihp-extraction-precise
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_magic.py"
    --destination "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-precise"
    --clip-tile-area --full-sidewall --precise-values
  COMMENT "Build isolated Magic with precise SPICE value serialization")
add_custom_target(prepare-ihp-extraction-double
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_magic.py"
    --destination "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-double"
    --clip-tile-area --full-sidewall --precise-values --double-values
  COMMENT "Build isolated Magic retaining double precision through SPICE serialization")
if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-sidewall/source.tar.gz")
  add_test(NAME analog.1g.ihp_extraction.format
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/magic_format.py"
      --archive "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-sidewall/source.tar.gz")
  set_tests_properties(analog.1g.ihp_extraction.format PROPERTIES
    LABELS "analog;1g;ihp;extraction" TIMEOUT 120)
  add_test(NAME analog.1g.ihp_extraction.double_format
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/magic_double_format.py"
      --archive "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-sidewall/source.tar.gz")
  set_tests_properties(analog.1g.ihp_extraction.double_format PROPERTIES
    LABELS "analog;1g;ihp;extraction" TIMEOUT 120)
endif()
set(CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-sidewall/install/bin/magic"
  CACHE FILEPATH "Magic candidate for the independent sidewall coefficient oracle")
set(CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-precise/install/bin/magic"
  CACHE FILEPATH "Precise-value Magic for LV standard-cell clock macro extraction")
set(CPPHDL_IHP_PLL_MAGIC_EXECUTABLE "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-double/install/bin/magic"
  CACHE FILEPATH "Double-value Magic for composed PLL extraction; prepare-ihp-extraction-double")
if(EXISTS "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}" AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_extraction.sidewall
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/magic_sidewall.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/sidewall_coefficients")
  set_tests_properties(analog.1g.ihp_extraction.sidewall PROPERTIES
    LABELS "analog;1g;ihp;physical;extraction" TIMEOUT 300)
endif()
add_custom_target(prepare-ihp-field-solver
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_fastercap.py"
    --destination "${CPPHDL_ANALOG_REAL_ROOT}/out/fastercap-toolchain"
  COMMENT "Prepare pinned local FasterCap for independent capacitance calibration")
add_custom_target(prepare-ihp-field-solver-source
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_fastercap_source.py"
    --destination "${CPPHDL_ANALOG_REAL_ROOT}/out/fastercap-source-build"
  COMMENT "Build pinned FasterCap with the per-block disk offset correction")
set(CPPHDL_IHP_FIELD_SOURCE_PREPARATION "" CACHE FILEPATH "Corrected FasterCap source-build preparation receipt")
if(CPPHDL_IHP_FIELD_SOURCE_PREPARATION)
  add_test(NAME analog.1g.ihp_extraction.field_solver_disk COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/field_solver_disk.py"
    --preparation "${CPPHDL_IHP_FIELD_SOURCE_PREPARATION}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/field_solver_disk")
  set_tests_properties(analog.1g.ihp_extraction.field_solver_disk PROPERTIES
    LABELS "analog;1g;ihp;physical" TIMEOUT 2500)
endif()
set(CPPHDL_IHP_FIELD_SOLVER_PREPARATION "" CACHE FILEPATH "Pinned FasterCap preparation receipt")
add_test(NAME analog.1g.ihp_extraction.field_matrix_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/field_matrix.py")
set_tests_properties(analog.1g.ihp_extraction.field_matrix_guards PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
if(CPPHDL_IHP_FIELD_SOLVER_PREPARATION)
  add_test(NAME analog.1g.ihp_extraction.field_solver COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/field_solver.py"
    --preparation "${CPPHDL_IHP_FIELD_SOLVER_PREPARATION}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/field_solver")
  set_tests_properties(analog.1g.ihp_extraction.field_solver PROPERTIES
    LABELS "analog;1g;ihp;physical" TIMEOUT 800)
endif()
add_test(NAME analog.1g.ihp_extraction.capacitance_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/extraction_capacitance.py")
add_test(NAME analog.1g.ihp_extraction.wire_reduction COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/reduce_extracted_wires.py")
set_tests_properties(analog.1g.ihp_extraction.wire_reduction PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_extraction.connected_capacitors COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/extracted_connections.py")
set_tests_properties(analog.1g.ihp_extraction.connected_capacitors PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_extraction.resistor_body_connections COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/resistor_body_extraction.py")
add_test(NAME analog.1g.ihp_extraction.resistor_array_connections COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/resistor_array_extraction.py")
set_tests_properties(analog.1g.ihp_extraction.resistor_array_connections PROPERTIES
  LABELS "analog;1g;ihp;extraction;transmitter" TIMEOUT 30)
add_test(NAME analog.1g.ihp_transmit.switching_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_cell_switching_guards.py")
set_tests_properties(analog.1g.ihp_transmit.switching_guards PROPERTIES
  LABELS "analog;1g;ihp;transmitter" TIMEOUT 30)
set_tests_properties(analog.1g.ihp_extraction.resistor_body_connections PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_processing_clock_oscillator.acquisition_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/oscillator_acquisition_window_guards.py")
set_tests_properties(analog.1g.ihp_processing_clock_oscillator.acquisition_guards PROPERTIES
  LABELS "analog;1g;ihp;clock;physical" TIMEOUT 60)
set_tests_properties(analog.1g.ihp_extraction.capacitance_guards PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_processing_clock_gate.pex_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_gate_pex_guards.py")
set_tests_properties(analog.1g.ihp_processing_clock_gate.pex_guards PROPERTIES
  LABELS "analog;1g;ihp;clock;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_processing_clock.physical_input_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_physical.py")
add_test(NAME analog.1g.ihp_processing_clock.crossing_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_crossings.py")
set_tests_properties(analog.1g.ihp_processing_clock.crossing_guards PROPERTIES
  LABELS "analog;1g;ihp;clock;physical" TIMEOUT 30)
set_tests_properties(analog.1g.ihp_processing_clock.physical_input_guards PROPERTIES
  LABELS "analog;1g;ihp;clock;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive.load_profiles COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_profiles.py")
add_test(NAME analog.1g.ihp_adc_lane.interface_timing_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_interface_timing.py")
set_tests_properties(analog.1g.ihp_adc_lane.interface_timing_guards PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_adc_lane.interface_load_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_interface_loads_test.py")
set_tests_properties(analog.1g.ihp_adc_lane.interface_load_guards PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_lvs_ports.graph_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/lvs_disconnected_ports_test.py")
set_tests_properties(analog.1g.ihp_lvs_ports.graph_guards PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
set_tests_properties(analog.1g.ihp_receive.load_profiles PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_transmit.spice_vectors COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/spice_differential_vectors.py")
set_tests_properties(analog.1g.ihp_transmit.spice_vectors PROPERTIES LABELS "analog;1g;ihp;transmitter" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive.dc_surface_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_dc_surface.py")
set_tests_properties(analog.1g.ihp_receive.dc_surface_guards PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
set(CPPHDL_IHP_PDK_ROOT "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2" CACHE PATH "Prepared IHP SG13G2 checkout")
add_test(NAME analog.1g.ihp_flow_control.crossing_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/flow_control_crossings.py")
set_tests_properties(analog.1g.ihp_flow_control.crossing_guards PROPERTIES
  LABELS "analog;1g;ihp;physical;flow-control" TIMEOUT 30)
if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad"
   AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_stdcell.lef")
  add_test(NAME analog.1g.ihp_flow_control.clock_loads COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/flow_control_clock_loads.py"
    --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --openroad "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/flow_control_clock_loads")
  set_tests_properties(analog.1g.ihp_flow_control.clock_loads PROPERTIES
    LABELS "analog;1g;ihp;physical;flow-control" TIMEOUT 90)
endif()
set(CPPHDL_1G_FLOW_CONTROL_ROUTED_PROOF "" CACHE FILEPATH "Qualified routed SRAM/control timing and replay receipt")
set(CPPHDL_1G_FLOW_CONTROL_CELL_PROOF "" CACHE FILEPATH "Mapped SRAM/control proof used for routing")
if(CPPHDL_1G_FLOW_CONTROL_ROUTED_PROOF AND CPPHDL_1G_FLOW_CONTROL_CELL_PROOF)
  add_test(NAME analog.1g.ihp_flow_control.crossings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/flow_control_crossings.py"
    --routed "${CPPHDL_1G_FLOW_CONTROL_ROUTED_PROOF}"
    --cells "${CPPHDL_1G_FLOW_CONTROL_CELL_PROOF}"
    --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --openroad "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad"
    --yosys "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/flow_control_fifo_crossings")
  set_tests_properties(analog.1g.ihp_flow_control.crossings PROPERTIES
    LABELS "analog;1g;ihp;physical;flow-control" TIMEOUT 900)
endif()
set(CPPHDL_IHP_MAGIC_EXECUTABLE "${CPPHDL_ANALOG_REAL_ROOT}/out/magic-toolchain/install/bin/magic"
  CACHE FILEPATH "Pinned Magic executable for analog extraction")
if(EXISTS "${CPPHDL_IHP_MAGIC_EXECUTABLE}" AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_extraction.mim COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/analog_extraction.py"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_MAGIC_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/analog_extraction")
  set_tests_properties(analog.1g.ihp_extraction.mim PROPERTIES
    LABELS "analog;1g;ihp;physical" TIMEOUT 300)
endif()
add_test(NAME analog.1g.ihp_extraction.tile_area COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/magic_tile_area.py")
set_tests_properties(analog.1g.ihp_extraction.tile_area PROPERTIES
  LABELS "analog;1g;ihp;physical" TIMEOUT 30)
set(CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION "" CACHE PATH "Exactly replayed routed ProcessingClockControl implementation for extracted crossing checks")
if(NGSPICE_EXECUTABLE)
  add_test(NAME analog.1g.ihp_extraction.resistor_body
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resistor_body_model.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/resistor_body_model")
  set_tests_properties(analog.1g.ihp_extraction.resistor_body PROPERTIES
    LABELS "analog;1g;ihp;physical;psp" TIMEOUT 300)
endif()
set(CPPHDL_1G_PROCESSING_GATE_EXTRACTION "" CACHE PATH "Conserved ProcessingClockGate extraction for PSP corner tests")
set(CPPHDL_1G_PROCESSING_GATE_LAYOUT_PROOF "" CACHE FILEPATH "LVS/local-DRC proof for the physical admission gate")
set(CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF "" CACHE FILEPATH "LVS/local-DRC proof for the protected processing oscillator")
set(CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION "" CACHE PATH "Conserved oscillator extraction for physical clock tests")
if(CPPHDL_1G_PROCESSING_GATE_EXTRACTION AND CPPHDL_1G_PROCESSING_GATE_LAYOUT_PROOF AND
   CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION AND CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF AND
   NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  foreach(mode IN ITEMS startup complete)
    set(delivery_options)
    set(delivery_timeout 172800)
    if(mode STREQUAL "startup")
      list(APPEND delivery_options --startup-only)
      set(delivery_timeout 2400)
    endif()
    add_test(NAME analog.1g.ihp_processing_clock_delivery.extracted_${mode}
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_extracted_delivery.py"
        --oscillator "${CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION}"
        --oscillator-layout "${CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF}"
        --gate "${CPPHDL_1G_PROCESSING_GATE_EXTRACTION}"
        --gate-layout "${CPPHDL_1G_PROCESSING_GATE_LAYOUT_PROOF}"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
        --cxx "${CMAKE_CXX_COMPILER}" ${delivery_options}
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_extracted_${mode}")
    set_tests_properties(analog.1g.ihp_processing_clock_delivery.extracted_${mode} PROPERTIES
      LABELS "analog;1g;ihp;clock;physical;psp" TIMEOUT ${delivery_timeout} RESOURCE_LOCK one_g_closed_adc_memory)
  endforeach()
endif()
if(CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION AND CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF AND
   NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_processing_clock_oscillator.extracted
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_oscillator_extracted.py"
      --extraction "${CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION}"
      --layout-proof "${CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_oscillator_extracted")
  set_tests_properties(analog.1g.ihp_processing_clock_oscillator.extracted PROPERTIES
    LABELS "analog;1g;ihp;clock;physical;psp" TIMEOUT 14600 RESOURCE_LOCK one_g_closed_adc_memory)
  add_test(NAME analog.1g.ihp_processing_clock_oscillator.extracted_acquisition
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_oscillator_extracted.py"
      --acquisition-window
      --extraction "${CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION}"
      --layout-proof "${CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_oscillator_extracted_acquisition")
  set_tests_properties(analog.1g.ihp_processing_clock_oscillator.extracted_acquisition PROPERTIES
    LABELS "analog;1g;ihp;clock;physical;psp" TIMEOUT 14600 RESOURCE_LOCK one_g_closed_adc_memory)
endif()
if(CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF AND NGSPICE_EXECUTABLE AND
   EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_processing_clock_oscillator.physical_devices
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_oscillator_physical_devices.py"
      --layout-proof "${CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_oscillator_physical_devices")
  set_tests_properties(analog.1g.ihp_processing_clock_oscillator.physical_devices PROPERTIES
    LABELS "analog;1g;ihp;clock;physical;psp" TIMEOUT 11000 RESOURCE_LOCK one_g_closed_adc_memory)
endif()
if(CPPHDL_1G_PROCESSING_GATE_EXTRACTION AND CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION AND
   NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_processing_clock_gate.extracted
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_gate_extracted.py"
      --extraction "${CPPHDL_1G_PROCESSING_GATE_EXTRACTION}"
      --physical "${CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION}"
      --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_gate>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_gate_extracted")
  set_tests_properties(analog.1g.ihp_processing_clock_gate.extracted PROPERTIES
    LABELS "analog;1g;ihp;clock;physical;psp" TIMEOUT 7500 RESOURCE_LOCK one_g_closed_adc_memory)
  add_test(NAME analog.1g.ihp_processing_clock_gate.extracted_charge
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_gate_extracted_charge.py"
      --extraction "${CPPHDL_1G_PROCESSING_GATE_EXTRACTION}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_gate_extracted_charge")
  set_tests_properties(analog.1g.ihp_processing_clock_gate.extracted_charge PROPERTIES
    LABELS "analog;1g;ihp;clock;physical;psp" TIMEOUT 7500 RESOURCE_LOCK one_g_closed_adc_memory)
endif()
if(CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION AND
   EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_processing_clock.crossings COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_crossings.py"
    --implementation "${CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION}"
    --work-root "${CMAKE_CURRENT_BINARY_DIR}/tests/processing_clock_crossings"
    --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --openroad "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
  set_tests_properties(analog.1g.ihp_processing_clock.crossings PROPERTIES
    LABELS "analog;1g;ihp;clock;physical" TIMEOUT 180)
endif()
if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad" AND
   EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell")
  add_test(NAME analog.1g.ihp_processing_clock.clock_tree COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_tree.py"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
  set_tests_properties(analog.1g.ihp_processing_clock.clock_tree PROPERTIES
    LABELS "analog;1g;ihp;clock;physical" TIMEOUT 120)
  add_test(NAME analog.1g.ihp_processing_clock.output_logic COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_output.py"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain")
  set_tests_properties(analog.1g.ihp_processing_clock.output_logic PROPERTIES
    LABELS "analog;1g;ihp;clock;physical" TIMEOUT 120)
endif()
add_test(NAME analog.1g.ihp_processing_clock_delivery.compact_trace COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/compact_trace.py")
set_tests_properties(analog.1g.ihp_processing_clock_delivery.compact_trace PROPERTIES
  LABELS "analog;1g;ihp;clock" TIMEOUT 30)
add_test(NAME analog.1g.ihp_area.evidence_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/mapped_area.py")
set_tests_properties(analog.1g.ihp_area.evidence_guards PROPERTIES LABELS "analog;1g;ihp;area" TIMEOUT 30)
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_area.mim_budget
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mim_area_budget.py")
  set_tests_properties(analog.1g.ihp_area.mim_budget PROPERTIES LABELS "analog;1g;ihp;area" TIMEOUT 30)
endif()
add_test(NAME analog.1g.ihp_route.guide_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/route_eco.py")
set_tests_properties(analog.1g.ihp_route.guide_guards PROPERTIES LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_route.corner_repair COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/repair_route_corner.py")
set_tests_properties(analog.1g.ihp_route.corner_repair PROPERTIES LABELS "analog;1g;ihp;physical" TIMEOUT 30)
add_test(NAME analog.1g.ihp_mos.geometry COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/geometry_contract.py")
set_tests_properties(analog.1g.ihp_mos.geometry PROPERTIES LABELS "analog;1g;ihp" TIMEOUT 30)
add_test(NAME analog.1g.ihp_core_transmit.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_driver_process.py")
set_tests_properties(analog.1g.ihp_core_transmit.bindings PROPERTIES LABELS "analog;1g;ihp" TIMEOUT 30)
add_test(NAME analog.1g.ihp_level_down.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/level_down_bindings.py")
set_tests_properties(analog.1g.ihp_level_down.bindings PROPERTIES LABELS "analog;1g;ihp;interface" TIMEOUT 30)
add_test(NAME analog.1g.ihp_source_follower.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/follower_bindings.py")
set_tests_properties(analog.1g.ihp_source_follower.bindings PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_frontend.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_frontend_bindings.py")
set_tests_properties(analog.1g.ihp_receive_frontend.bindings PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_frontend.spice_vectors COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/spice_vectors.py")
set_tests_properties(analog.1g.ihp_receive_frontend.spice_vectors PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_conversion.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/conversion_bindings.py")
set_tests_properties(analog.1g.ihp_receive_conversion.bindings PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_conversion.replay_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_consistency.py")
set_tests_properties(analog.1g.ihp_receive_conversion.replay_guards PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_conversion.checkpoint_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/psp_live.py")
set_tests_properties(analog.1g.ihp_receive_conversion.checkpoint_guards PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_conversion.xspice_bridge_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/xspice_bridge.py")
set_tests_properties(analog.1g.ihp_receive_conversion.xspice_bridge_guards PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_conversion.xspice_vector_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/xspice_bridges.py")
set_tests_properties(analog.1g.ihp_receive_conversion.xspice_vector_guards PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_interface.input_charge_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/input_charge.py")
set_tests_properties(analog.1g.ihp_interface.input_charge_guards PROPERTIES LABELS "analog;1g;ihp;interface" TIMEOUT 30)
set(CPPHDL_1G_CLOCK_TRANSLATOR_PROOF "" CACHE FILEPATH "Passing 150-case PSP clock-translator checks.json for charge characterization")
if(CPPHDL_1G_CLOCK_TRANSLATOR_PROOF)
  add_test(NAME analog.1g.ihp_interface.input_charge COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/input_charge.py" --proof "${CPPHDL_1G_CLOCK_TRANSLATOR_PROOF}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/tests/results/clock_interface_input_charge/checks.json")
  set_tests_properties(analog.1g.ihp_interface.input_charge PROPERTIES LABELS "analog;1g;ihp;interface" TIMEOUT 180)
endif()

if(Eigen3_FOUND)
  add_executable(analog_1g_ihp_processing_clock_oscillator tests/processing_clock_oscillator.cpp)
  target_link_libraries(analog_1g_ihp_processing_clock_oscillator PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_processing_clock_oscillator)
  add_dependencies(check-analog analog_1g_ihp_processing_clock_oscillator)
  add_executable(analog_1g_ihp_processing_clock_closed tests/processing_clock_closed.cpp)
  add_executable(analog_1g_ihp_processing_clock_delivery tests/processing_clock_delivery.cpp)
  add_test(NAME analog.1g.ihp_processing_clock_delivery.guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_delivery_guards.py")
  set_tests_properties(analog.1g.ihp_processing_clock_delivery.guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 60)
  target_link_libraries(analog_1g_ihp_processing_clock_delivery PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_processing_clock_delivery)
  add_dependencies(check-analog analog_1g_ihp_processing_clock_delivery)
  add_executable(analog_1g_ihp_processing_clock_gate tests/processing_clock_gate.cpp)
  target_link_libraries(analog_1g_ihp_processing_clock_gate PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_processing_clock_gate)
  add_dependencies(check-analog analog_1g_ihp_processing_clock_gate)
  target_link_libraries(analog_1g_ihp_processing_clock_closed PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_processing_clock_closed)
  add_dependencies(check-analog analog_1g_ihp_processing_clock_closed)
  add_executable(analog_1g_ihp_reference_clock_generator tests/reference_clock_generator.cpp)
  add_executable(analog_1g_ihp_reference_clock_layout_export tests/reference_clock_layout_export.cpp)
  target_link_libraries(analog_1g_ihp_reference_clock_layout_export PRIVATE cpphdl::analog)
  add_executable(analog_1g_ihp_receive_sampler_export tests/receive_sampler_export.cpp)
  target_link_libraries(analog_1g_ihp_receive_sampler_export PRIVATE cpphdl::analog)
  add_dependencies(check-1g analog_1g_ihp_receive_sampler_export)
  add_dependencies(check-analog analog_1g_ihp_receive_sampler_export)
  add_executable(analog_1g_ihp_transmit_translator_export tests/transmit_translator_export.cpp)
  target_link_libraries(analog_1g_ihp_transmit_translator_export PRIVATE cpphdl::analog)
  add_dependencies(check-1g analog_1g_ihp_transmit_translator_export)
  add_dependencies(check-analog analog_1g_ihp_transmit_translator_export)
  add_executable(analog_1g_ihp_transmit_translation_bias_export tests/transmit_translation_bias_export.cpp)
  target_link_libraries(analog_1g_ihp_transmit_translation_bias_export PRIVATE cpphdl::analog)
  add_dependencies(check-1g analog_1g_ihp_transmit_translation_bias_export)
  add_dependencies(check-analog analog_1g_ihp_transmit_translation_bias_export)
  add_executable(analog_1g_ihp_transmit_translation_bank_export tests/transmit_translation_bank_export.cpp)
  target_link_libraries(analog_1g_ihp_transmit_translation_bank_export PRIVATE cpphdl::analog)
  add_dependencies(check-1g analog_1g_ihp_transmit_translation_bank_export)
  add_dependencies(check-analog analog_1g_ihp_transmit_translation_bank_export)
  add_executable(analog_1g_ihp_transmit_physical_export tests/transmit_physical_export.cpp)
  target_link_libraries(analog_1g_ihp_transmit_physical_export PRIVATE cpphdl::analog)
  add_dependencies(check-1g analog_1g_ihp_transmit_physical_export)
  add_dependencies(check-analog analog_1g_ihp_transmit_physical_export)
  add_test(NAME analog.1g.ihp_transmit.physical_partition
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_physical_partition.py"
      --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>")
  set_tests_properties(analog.1g.ihp_transmit.physical_partition PROPERTIES
    LABELS "analog;1g;ihp;transmitter;physical" TIMEOUT 60)
  add_test(NAME analog.1g.ihp_reference_clock.realization
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_realization.py"
      --exporter "$<TARGET_FILE:analog_1g_ihp_reference_clock_layout_export>"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_realization")
  set_tests_properties(analog.1g.ihp_reference_clock.realization PROPERTIES
    LABELS "analog;1g;ihp;clock;binding" TIMEOUT 120)
  target_link_libraries(analog_1g_ihp_reference_clock_generator PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_reference_clock_generator)
  add_dependencies(check-analog analog_1g_ihp_reference_clock_generator)
  add_test(NAME analog.1g.ihp_reference_clock.guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_guards.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_reference_clock_generator>")
  set_tests_properties(analog.1g.ihp_reference_clock.guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 120)
  add_test(NAME analog.1g.ihp_processing_clock.guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_guards.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_closed>" --pdk "${CPPHDL_IHP_PDK_ROOT}")
  set_tests_properties(analog.1g.ihp_processing_clock.guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 120)
  add_test(NAME analog.1g.ihp_processing_clock.compare_guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_compare_guards.py")
  set_tests_properties(analog.1g.ihp_processing_clock.compare_guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 30)
  add_test(NAME analog.1g.ihp_processing_clock_closed.compare_guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_closed_compare_guards.py")
  set_tests_properties(analog.1g.ihp_processing_clock_closed.compare_guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 30)
  add_test(NAME analog.1g.ihp_processing_clock_gate.guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_gate_guards.py")
  set_tests_properties(analog.1g.ihp_processing_clock_gate.guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 30)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND
     EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOSlv.lib")
    add_test(NAME analog.1g.ihp_processing_clock_interface.guards COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_interface.py" --pdk "${CPPHDL_IHP_PDK_ROOT}")
    set_tests_properties(analog.1g.ihp_processing_clock_interface.guards PROPERTIES
      LABELS "analog;1g;ihp;clock" TIMEOUT 30)
    if(CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION)
      add_test(NAME analog.1g.ihp_processing_clock_interface.psp COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_interface.py"
        --physical "${CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION}"
        --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_gate>" --pdk "${CPPHDL_IHP_PDK_ROOT}"
        --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_interface")
      set_tests_properties(analog.1g.ihp_processing_clock_interface.psp PROPERTIES
        LABELS "analog;1g;ihp;clock;psp" TIMEOUT 2400 RESOURCE_LOCK one_g_clock_simulation)
      add_test(NAME analog.1g.ihp_processing_clock_interface.handoff COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_interface.py" --registered
        --physical "${CPPHDL_1G_PROCESSING_CLOCK_IMPLEMENTATION}"
        --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_gate>" --pdk "${CPPHDL_IHP_PDK_ROOT}"
        --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_handoff")
      set_tests_properties(analog.1g.ihp_processing_clock_interface.handoff PROPERTIES
        LABELS "analog;1g;ihp;clock;psp" TIMEOUT 2400 RESOURCE_LOCK one_g_clock_simulation)
    endif()
    add_test(NAME analog.1g.ihp_processing_clock_gate.electrical COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_gate.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_gate>" --pdk "${CPPHDL_IHP_PDK_ROOT}"
      --ngspice "${NGSPICE_EXECUTABLE}" --openvaf "${OPENVAF_EXECUTABLE}"
      --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_gate")
    set_tests_properties(analog.1g.ihp_processing_clock_gate.electrical PROPERTIES
      LABELS "analog;1g;ihp;clock;cpp;veriloga;psp" TIMEOUT 3600 RESOURCE_LOCK one_g_clock_simulation
      FIXTURES_SETUP one_g_processing_gate_proof)
    add_test(NAME analog.1g.ihp_processing_clock.input_charge COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_input_charge.py"
      --gate-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_gate/checks.json"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_input_charge")
    set_tests_properties(analog.1g.ihp_processing_clock.input_charge PROPERTIES
      LABELS "analog;1g;ihp;clock;psp" TIMEOUT 4200 RESOURCE_LOCK one_g_clock_simulation
      FIXTURES_REQUIRED one_g_processing_gate_proof)
  endif()
  add_test(NAME analog.1g.ihp_processing_clock_limits.guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_limits_guards.py")
  set_tests_properties(analog.1g.ihp_processing_clock_limits.guards PROPERTIES
    LABELS "analog;1g;ihp;clock" TIMEOUT 30)
  if(NGSPICE_EXECUTABLE AND
     EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOSlv.lib")
    add_test(NAME analog.1g.ihp_processing_clock_limits.psp COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_limits.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_oscillator>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_limits")
    set_tests_properties(analog.1g.ihp_processing_clock_limits.psp PROPERTIES
      LABELS "analog;1g;ihp;clock;psp" TIMEOUT 21600 RESOURCE_LOCK one_g_clock_simulation)
  endif()
  foreach(clock_flow cpp veriloga psp)
    if(clock_flow STREQUAL "cpp" OR
       (clock_flow STREQUAL "veriloga" AND OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE) OR
       (clock_flow STREQUAL "psp" AND NGSPICE_EXECUTABLE AND
        EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOSlv.lib"))
      set(clock_tools)
      if(NGSPICE_EXECUTABLE)
        list(APPEND clock_tools --ngspice "${NGSPICE_EXECUTABLE}")
      endif()
      if(OPENVAF_EXECUTABLE)
        list(APPEND clock_tools --openvaf "${OPENVAF_EXECUTABLE}")
      endif()
      add_test(NAME analog.1g.ihp_reference_clock.${clock_flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_generator.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_reference_clock_generator>" --flow "${clock_flow}"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --cxx "${CMAKE_CXX_COMPILER}" ${clock_tools}
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_${clock_flow}")
      set_tests_properties(analog.1g.ihp_reference_clock.${clock_flow} PROPERTIES
        LABELS "analog;1g;ihp;clock;${clock_flow}" TIMEOUT 10800 RESOURCE_LOCK one_g_clock_simulation)
      add_test(NAME analog.1g.ihp_processing_clock.${clock_flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_oscillator.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_oscillator>" --flow "${clock_flow}"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --cxx "${CMAKE_CXX_COMPILER}" ${clock_tools}
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_${clock_flow}")
      set_tests_properties(analog.1g.ihp_processing_clock.${clock_flow} PROPERTIES
        LABELS "analog;1g;ihp;clock;${clock_flow}" TIMEOUT 14400 RESOURCE_LOCK one_g_clock_simulation)
      add_test(NAME analog.1g.ihp_processing_clock_closed.${clock_flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_closed.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_closed>" --flow "${clock_flow}"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --cxx "${CMAKE_CXX_COMPILER}" ${clock_tools}
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_closed_${clock_flow}")
      set_tests_properties(analog.1g.ihp_processing_clock_closed.${clock_flow} PROPERTIES
        LABELS "analog;1g;ihp;clock;${clock_flow}" TIMEOUT 45000 RESOURCE_LOCK one_g_clock_simulation)
      add_test(NAME analog.1g.ihp_processing_clock_delivery.${clock_flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_delivery.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_processing_clock_delivery>" --flow "${clock_flow}"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --cxx "${CMAKE_CXX_COMPILER}" ${clock_tools}
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_delivery_${clock_flow}")
      set_tests_properties(analog.1g.ihp_processing_clock_delivery.${clock_flow} PROPERTIES
        LABELS "analog;1g;ihp;clock;${clock_flow}" TIMEOUT 45000 RESOURCE_LOCK one_g_clock_simulation)
    endif()
  endforeach()
  if(TEST analog.1g.ihp_reference_clock.cpp AND TEST analog.1g.ihp_reference_clock.veriloga)
    set_tests_properties(analog.1g.ihp_reference_clock.cpp PROPERTIES FIXTURES_SETUP one_g_ihp_clock_cpp)
    set_tests_properties(analog.1g.ihp_reference_clock.veriloga PROPERTIES FIXTURES_SETUP one_g_ihp_clock_va)
    add_test(NAME analog.1g.ihp_reference_clock.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_compare.py"
      --cpp "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_cpp/checks.json"
      --veriloga "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_veriloga/checks.json"
      --output "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_compare/checks.json")
    set_tests_properties(analog.1g.ihp_reference_clock.compare PROPERTIES
      LABELS "analog;1g;ihp;clock;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_ihp_clock_cpp;one_g_ihp_clock_va")
  endif()
  if(TEST analog.1g.ihp_processing_clock.cpp AND TEST analog.1g.ihp_processing_clock.veriloga)
    set_tests_properties(analog.1g.ihp_processing_clock.cpp PROPERTIES FIXTURES_SETUP one_g_ihp_processing_cpp)
    set_tests_properties(analog.1g.ihp_processing_clock.veriloga PROPERTIES FIXTURES_SETUP one_g_ihp_processing_va)
    add_test(NAME analog.1g.ihp_processing_clock.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_compare.py"
      --cpp "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_cpp/checks.json"
      --veriloga "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_veriloga/checks.json"
      --output "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_compare/checks.json")
    set_tests_properties(analog.1g.ihp_processing_clock.compare PROPERTIES
      LABELS "analog;1g;ihp;clock;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_ihp_processing_cpp;one_g_ihp_processing_va")
  endif()
  if(TEST analog.1g.ihp_processing_clock_closed.cpp AND TEST analog.1g.ihp_processing_clock_closed.veriloga)
    set_tests_properties(analog.1g.ihp_processing_clock_closed.cpp PROPERTIES FIXTURES_SETUP one_g_ihp_processing_closed_cpp)
    set_tests_properties(analog.1g.ihp_processing_clock_closed.veriloga PROPERTIES FIXTURES_SETUP one_g_ihp_processing_closed_va)
    add_test(NAME analog.1g.ihp_processing_clock_closed.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_closed_compare.py"
      --cpp "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_closed_cpp/checks.json"
      --veriloga "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_closed_veriloga/checks.json"
      --output "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_closed_compare/checks.json")
    set_tests_properties(analog.1g.ihp_processing_clock_closed.compare PROPERTIES
      LABELS "analog;1g;ihp;clock;veriloga" TIMEOUT 180
      FIXTURES_REQUIRED "one_g_ihp_processing_closed_cpp;one_g_ihp_processing_closed_va")
  endif()
  if(TEST analog.1g.ihp_processing_clock_delivery.cpp AND TEST analog.1g.ihp_processing_clock_delivery.veriloga)
    set_tests_properties(analog.1g.ihp_processing_clock_delivery.cpp PROPERTIES FIXTURES_SETUP one_g_ihp_processing_delivery_cpp)
    set_tests_properties(analog.1g.ihp_processing_clock_delivery.veriloga PROPERTIES FIXTURES_SETUP one_g_ihp_processing_delivery_va)
    add_test(NAME analog.1g.ihp_processing_clock_delivery.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_delivery_compare.py"
      --cpp "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_delivery_cpp/checks.json"
      --veriloga "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_delivery_veriloga/checks.json"
      --output "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_delivery_compare/checks.json")
    set_tests_properties(analog.1g.ihp_processing_clock_delivery.compare PROPERTIES
      LABELS "analog;1g;ihp;clock;veriloga" TIMEOUT 45000
      FIXTURES_REQUIRED "one_g_ihp_processing_delivery_cpp;one_g_ihp_processing_delivery_va")
  endif()
  add_executable(analog_1g_ihp_programmable_receive_conversion tests/programmable_receive_conversion.cpp)
  target_link_libraries(analog_1g_ihp_programmable_receive_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  add_executable(analog_1g_ihp_wideband_receive_conversion tests/wideband_receive_conversion.cpp)
  target_link_libraries(analog_1g_ihp_wideband_receive_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  add_executable(analog_1g_ihp_charge_balanced_receive_conversion tests/charge_balanced_receive_conversion.cpp)
  target_link_libraries(analog_1g_ihp_charge_balanced_receive_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  add_executable(analog_1g_ihp_charge_balanced_receive_array tests/charge_balanced_receive_array.cpp)
  target_link_libraries(analog_1g_ihp_charge_balanced_receive_array PRIVATE cpphdl::analog Eigen3::Eigen)
  foreach(check_target check-1g check-analog check-veriloga)
    if(TARGET ${check_target})
      add_dependencies(${check_target} analog_1g_ihp_programmable_receive_conversion analog_1g_ihp_wideband_receive_conversion
        analog_1g_ihp_charge_balanced_receive_conversion analog_1g_ihp_charge_balanced_receive_array)
    endif()
  endforeach()
  add_test(NAME analog.1g.ihp_programmable_conversion.contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_conversion_contract.py")
  add_test(NAME analog.1g.ihp_programmable_conversion.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_conversion_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_conversion>")
  set_tests_properties(analog.1g.ihp_programmable_conversion.contract analog.1g.ihp_programmable_conversion.bindings
    PROPERTIES LABELS "analog;1g;ihp;converter" TIMEOUT 120)
  set(conversion_cosim_available FALSE)
  if(NGSPICE_EXECUTABLE)
    get_filename_component(conversion_ngspice "${NGSPICE_EXECUTABLE}" REALPATH)
    get_filename_component(conversion_bin "${conversion_ngspice}" DIRECTORY)
    if(EXISTS "${conversion_bin}/../share/ngspice/scripts/src/ngspice/cosim.h")
      set(conversion_cosim_available TRUE)
    endif()
  endif()
  foreach(conversion_model programmable wideband charge_balanced)
    set(conversion_target analog_1g_ihp_programmable_receive_conversion)
    set(conversion_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_conversion.cpp")
    if(conversion_model STREQUAL "wideband")
      set(conversion_target analog_1g_ihp_wideband_receive_conversion)
      set(conversion_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/wideband_receive_conversion.cpp")
    elseif(conversion_model STREQUAL "charge_balanced")
      set(conversion_target analog_1g_ihp_charge_balanced_receive_conversion)
      set(conversion_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/charge_balanced_receive_conversion.cpp")
    endif()
    foreach(count 1 4 16)
      set(bursts 1)
      if(count EQUAL 16)
        set(bursts 2)
      endif()
      foreach(gain 4 8)
        set(profile "${count}_gain${gain}")
        set(conversion_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/${conversion_model}_conversion_${profile}")
        set(conversion_flows cpp)
        if(conversion_cosim_available)
          if(OPENVAF_EXECUTABLE AND TARGET cpphdl-veriloga)
            list(APPEND conversion_flows veriloga)
          endif()
          if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
            list(APPEND conversion_flows psp)
          endif()
        endif()
        foreach(flow IN LISTS conversion_flows)
          set(conversion_tools)
          if(NOT flow STREQUAL "cpp")
            list(APPEND conversion_tools --ngspice "${NGSPICE_EXECUTABLE}")
          endif()
          if(flow STREQUAL "veriloga")
            list(APPEND conversion_tools --driver "$<TARGET_FILE:cpphdl-veriloga>" --openvaf "${OPENVAF_EXECUTABLE}")
          elseif(flow STREQUAL "psp")
            list(APPEND conversion_tools --pdk "${CPPHDL_IHP_PDK_ROOT}")
            if(conversion_model MATCHES "^(wideband|charge_balanced)$" AND count EQUAL 16)
              list(APPEND conversion_tools --simulation-timeout 18000)
            endif()
          endif()
          add_test(NAME analog.1g.ihp_${conversion_model}_conversion_${profile}.${flow} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_conversion.py" --flow "${flow}"
            --bench "$<TARGET_FILE:${conversion_target}>" --cxx "${CMAKE_CXX_COMPILER}"
            --fixture-source "${conversion_fixture}" --work "${conversion_work}" --slices "${count}" --gain "${gain}" --storage-pf 1.5 --bursts "${bursts}"
            ${conversion_tools})
          set_tests_properties(analog.1g.ihp_${conversion_model}_conversion_${profile}.${flow} PROPERTIES
            LABELS "analog;1g;ihp;converter;${flow}" TIMEOUT 21600 ENVIRONMENT "OMP_THREAD_LIMIT=1"
            RESOURCE_LOCK one_g_adc_array_memory FIXTURES_SETUP "one_g_${conversion_model}_conversion_${profile}_${flow}")
        endforeach()
        if("veriloga" IN_LIST conversion_flows)
          add_test(NAME analog.1g.ihp_${conversion_model}_conversion_${profile}.compare COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_conversion_compare.py" --work "${conversion_work}")
          set_tests_properties(analog.1g.ihp_${conversion_model}_conversion_${profile}.compare PROPERTIES
            LABELS "analog;1g;ihp;converter" TIMEOUT 300
            FIXTURES_REQUIRED "one_g_${conversion_model}_conversion_${profile}_cpp;one_g_${conversion_model}_conversion_${profile}_veriloga")
        endif()
      endforeach()
    endforeach()
  endforeach()
endif()

if(TARGET Eigen3::Eigen)
  add_executable(analog_1g_ihp_programmable_receive_array tests/programmable_receive_array.cpp)
  target_link_libraries(analog_1g_ihp_programmable_receive_array PRIVATE cpphdl::analog Eigen3::Eigen)
  foreach(check_target check-1g check-analog check-veriloga)
    add_dependencies(${check_target} analog_1g_ihp_programmable_receive_array)
  endforeach()
  set(programmable_array_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_receive_array")
  set(programmable_array_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND TARGET cpphdl-veriloga
      AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND programmable_array_flows veriloga)
  endif()
  foreach(flow IN LISTS programmable_array_flows)
    add_test(NAME analog.1g.ihp_programmable_array.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_array>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${programmable_array_work}/${flow}"
      --driver "$<TARGET_FILE:cpphdl-veriloga>"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.ihp_programmable_array.${flow} PROPERTIES
      LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 21600 RESOURCE_LOCK one_g_adc_array_memory
      ENVIRONMENT "OMP_THREAD_LIMIT=1"
      FIXTURES_SETUP "one_g_ihp_programmable_array_${flow}")
  endforeach()
  if("veriloga" IN_LIST programmable_array_flows)
    add_test(NAME analog.1g.ihp_programmable_array.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array.py" --flow compare
      --work "${programmable_array_work}")
    set_tests_properties(analog.1g.ihp_programmable_array.compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 180
      FIXTURES_REQUIRED "one_g_ihp_programmable_array_cpp;one_g_ihp_programmable_array_veriloga")
  endif()
endif()
add_test(NAME analog.1g.ihp_programmable_array.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array_contract.py")
set_tests_properties(analog.1g.ihp_programmable_array.contract PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
if(TARGET analog_1g_ihp_programmable_receive_array)
  add_test(NAME analog.1g.ihp_programmable_array.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_array>")
  add_test(NAME analog.1g.ihp_programmable_array.refinement_guards COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_refinement.py")
  set_tests_properties(analog.1g.ihp_programmable_array.bindings analog.1g.ihp_programmable_array.refinement_guards
    PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 120)
  if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
    foreach(profile process process_refined)
      if(profile STREQUAL "process")
        set(warmup_step 1e-9)
      else()
        set(warmup_step 5e-10)
      endif()
      add_test(NAME analog.1g.ihp_programmable_array.${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_process.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_array>" --cxx "${CMAKE_CXX_COMPILER}"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --warmup-step "${warmup_step}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_array_${profile}")
      set_tests_properties(analog.1g.ihp_programmable_array.${profile} PROPERTIES
        LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 21600 RESOURCE_LOCK one_g_adc_array_memory
        ENVIRONMENT "OMP_THREAD_LIMIT=1" FIXTURES_SETUP "one_g_ihp_programmable_array_${profile}")
    endforeach()
    add_test(NAME analog.1g.ihp_programmable_array.process_compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_process.py" --compare
      "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_array_process"
      "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_array_process_refined"
      --output "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_array_process_comparison.json")
    set_tests_properties(analog.1g.ihp_programmable_array.process_compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 180
      FIXTURES_REQUIRED "one_g_ihp_programmable_array_process;one_g_ihp_programmable_array_process_refined")
  endif()
endif()
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout")
  add_test(NAME analog.1g.ihp_capacitor.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capacitor_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/capacitor_layout")
  set_tests_properties(analog.1g.ihp_capacitor.layout PROPERTIES LABELS "analog;1g;ihp;geometry;area" TIMEOUT 1200)
  add_test(NAME analog.1g.ihp_capacitor.small_layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capacitor_layout.py"
      --small --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/capacitor_small_layout")
  set_tests_properties(analog.1g.ihp_capacitor.small_layout PROPERTIES LABELS "analog;1g;ihp;geometry;clock" TIMEOUT 1200)
  add_test(NAME analog.1g.ihp_processing_clock_gate.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_gate_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_gate_layout")
  set_tests_properties(analog.1g.ihp_processing_clock_gate.layout PROPERTIES LABELS "analog;1g;ihp;geometry;clock" TIMEOUT 1800)
  add_test(NAME analog.1g.ihp_processing_clock_oscillator.passive_layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oscillator_passive_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/oscillator_passive_layout")
  set_tests_properties(analog.1g.ihp_processing_clock_oscillator.passive_layout PROPERTIES
    LABELS "analog;1g;ihp;geometry;clock" TIMEOUT 1800)
  add_test(NAME analog.1g.ihp_processing_clock_oscillator.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/processing_clock_oscillator_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/processing_clock_oscillator_layout")
  set_tests_properties(analog.1g.ihp_processing_clock_oscillator.layout PROPERTIES
    LABELS "analog;1g;ihp;geometry;clock" TIMEOUT 2400)
  if(TARGET analog_1g_ihp_reference_clock_layout_export)
    add_test(NAME analog.1g.ihp_reference_clock.layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_layout.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_reference_clock_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_layout")
    set_tests_properties(analog.1g.ihp_reference_clock.layout PROPERTIES
      LABELS "analog;1g;ihp;geometry;clock" TIMEOUT 7200 FIXTURES_SETUP one_g_reference_clock_layout)
  endif()
  add_test(NAME analog.1g.ihp_reference_clock.lvs_view
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/lvs_view.py")
  set_tests_properties(analog.1g.ihp_reference_clock.lvs_view PROPERTIES
    LABELS "analog;1g;ihp;geometry;clock" TIMEOUT 60)
  if(TARGET analog_1g_ihp_reference_clock_layout_export AND NGSPICE_EXECUTABLE)
    add_test(NAME analog.1g.ihp_reference_clock.extracted_bindings
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_extracted_bindings.py")
    set_tests_properties(analog.1g.ihp_reference_clock.extracted_bindings PROPERTIES
      LABELS "analog;1g;ihp;physical;clock" TIMEOUT 30)
    if(EXISTS "${CPPHDL_IHP_MAGIC_EXECUTABLE}")
      add_test(NAME analog.1g.ihp_reference_clock.extracted
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_extracted.py"
          --layout-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_layout/checks.json"
          --exporter "$<TARGET_FILE:analog_1g_ihp_reference_clock_layout_export>"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
          --magic "${CPPHDL_IHP_MAGIC_EXECUTABLE}"
          --cxx "${CMAKE_CXX_COMPILER}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_extracted")
      set_tests_properties(analog.1g.ihp_reference_clock.extracted PROPERTIES
        LABELS "analog;1g;ihp;physical;clock;psp" TIMEOUT 12600
        FIXTURES_REQUIRED one_g_reference_clock_layout)
    endif()
    add_test(NAME analog.1g.ihp_reference_clock.physical_devices
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_physical_devices.py"
        --layout-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_layout/checks.json"
        --exporter "$<TARGET_FILE:analog_1g_ihp_reference_clock_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
        --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_physical_devices")
    set_tests_properties(analog.1g.ihp_reference_clock.physical_devices PROPERTIES
      LABELS "analog;1g;ihp;physical;clock;psp" TIMEOUT 7500
      FIXTURES_REQUIRED one_g_reference_clock_layout)
  endif()
  add_test(NAME analog.1g.ihp_capacitor_bank.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capacitor_bank_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/capacitor_bank_layout")
  set_tests_properties(analog.1g.ihp_capacitor_bank.layout PROPERTIES LABELS "analog;1g;ihp;geometry;area" TIMEOUT 1200)
  if(NGSPICE_EXECUTABLE)
    add_test(NAME analog.1g.ihp_resistor.layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resistor_layout.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --ngspice "${NGSPICE_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/resistor_layout")
    set_tests_properties(analog.1g.ihp_resistor.layout PROPERTIES LABELS "analog;1g;ihp;geometry;area" TIMEOUT 1200)
  endif()
endif()
if(TARGET Eigen3::Eigen)
  add_executable(analog_1g_ihp_physical_inverter tests/physical_inverter.cpp)
  target_link_libraries(analog_1g_ihp_physical_inverter PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_physical_inverter)
  add_dependencies(check-analog analog_1g_ihp_physical_inverter)
  add_dependencies(check-veriloga analog_1g_ihp_physical_inverter)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE
      AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout"
      AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.1g.ihp_inverter.physical
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/physical_inverter.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --bench "$<TARGET_FILE:analog_1g_ihp_physical_inverter>" --openvaf "${OPENVAF_EXECUTABLE}"
        --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/physical_inverter")
    set_tests_properties(analog.1g.ihp_inverter.physical PROPERTIES LABELS "analog;1g;ihp;geometry;veriloga" TIMEOUT 900)
  endif()
  add_executable(analog_1g_ihp_mdi_receive tests/mdi_receive.cpp)
  target_link_libraries(analog_1g_ihp_mdi_receive PRIVATE cpphdl::analog Eigen3::Eigen)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.1g.ihp_mdi_receive COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/mdi_receive.py" --bench "$<TARGET_FILE:analog_1g_ihp_mdi_receive>"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mdi_receive"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}")
    set_tests_properties(analog.1g.ihp_mdi_receive PROPERTIES LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 10800)
  endif()
  add_dependencies(check-1g analog_1g_ihp_mdi_receive)
  add_dependencies(check-analog analog_1g_ihp_mdi_receive)
  add_dependencies(check-veriloga analog_1g_ihp_mdi_receive)
  foreach(receiver_variant precision process wideband precise_wideband parallel balanced)
    set(receiver_load_profile standard)
    if(receiver_variant STREQUAL "parallel" OR receiver_variant STREQUAL "balanced")
      set(receiver_load_profile parallel)
    endif()
  add_executable(analog_1g_ihp_receive_${receiver_variant}_conversion tests/receive_${receiver_variant}_conversion.cpp)
  target_link_libraries(analog_1g_ihp_receive_${receiver_variant}_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  add_executable(analog_1g_ihp_receive_${receiver_variant} tests/receive_${receiver_variant}.cpp)
  target_link_libraries(analog_1g_ihp_receive_${receiver_variant} PRIVATE cpphdl::analog Eigen3::Eigen)
  set(precision_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND precision_flows veriloga)
  endif()
  foreach(flow IN LISTS precision_flows)
    add_test(NAME analog.1g.ihp_receive_${receiver_variant}.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_precision.py" --flow "${flow}" --load-profile "${receiver_load_profile}"
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_${receiver_variant}>"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_${receiver_variant}.cpp" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.ihp_receive_${receiver_variant}.${flow} PROPERTIES
      LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 3600
      FIXTURES_SETUP "one_g_ihp_receive_${receiver_variant}_${flow}")
  endforeach()
  if("veriloga" IN_LIST precision_flows)
    add_test(NAME analog.1g.ihp_receive_${receiver_variant}.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_precision.py" --flow compare --load-profile "${receiver_load_profile}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}")
    set_tests_properties(analog.1g.ihp_receive_${receiver_variant}.compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 180
      FIXTURES_REQUIRED "one_g_ihp_receive_${receiver_variant}_cpp;one_g_ihp_receive_${receiver_variant}_veriloga")
  endif()
  add_dependencies(check-1g analog_1g_ihp_receive_${receiver_variant})
  add_dependencies(check-analog analog_1g_ihp_receive_${receiver_variant})
  add_dependencies(check-veriloga analog_1g_ihp_receive_${receiver_variant})
  add_dependencies(check-1g analog_1g_ihp_receive_${receiver_variant}_conversion)
  add_dependencies(check-analog analog_1g_ihp_receive_${receiver_variant}_conversion)
  add_dependencies(check-veriloga analog_1g_ihp_receive_${receiver_variant}_conversion)
  foreach(count 1 4 16)
    if(count EQUAL 16)
      set(bursts 2)
    else()
      set(bursts 1)
    endif()
    foreach(flow IN LISTS precision_flows)
      add_test(NAME analog.1g.ihp_receive_${receiver_variant}_conversion_${count}.${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/receive_conversion.py" --flow "${flow}"
        --bench "$<TARGET_FILE:analog_1g_ihp_receive_${receiver_variant}_conversion>"
        --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_${receiver_variant}_conversion.cpp"
        --cxx "${CMAKE_CXX_COMPILER}" --slices "${count}" --bursts "${bursts}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}_conversion_${count}/${flow}"
        --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
      set_tests_properties(analog.1g.ihp_receive_${receiver_variant}_conversion_${count}.${flow} PROPERTIES
        LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 10800
        FIXTURES_SETUP "one_g_ihp_receive_${receiver_variant}_conversion_${count}_${flow}")
    endforeach()
    if("veriloga" IN_LIST precision_flows)
      add_test(NAME analog.1g.ihp_receive_${receiver_variant}_conversion_${count}.compare COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/receive_conversion.py" --flow compare --slices "${count}" --bursts "${bursts}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}_conversion_${count}")
      set_tests_properties(analog.1g.ihp_receive_${receiver_variant}_conversion_${count}.compare PROPERTIES
        LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 180
        FIXTURES_REQUIRED "one_g_ihp_receive_${receiver_variant}_conversion_${count}_cpp;one_g_ihp_receive_${receiver_variant}_conversion_${count}_veriloga")
    endif()
  endforeach()
  endforeach()
endif()
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout")
    add_test(NAME analog.1g.ihp_lvs_ports.netlist_guards
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../pdk/1G_repeater_IHPSG13G2/tool_process.py"
        --exclusive -- "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/klayout"
        -b -r "${CMAKE_CURRENT_SOURCE_DIR}/tests/disconnected_ports.rb")
    set_tests_properties(analog.1g.ihp_lvs_ports.netlist_guards PROPERTIES
      LABELS "analog;1g;ihp;physical" TIMEOUT 120)
  endif()
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
    add_test(NAME analog.1g.ihp_mos.layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_layout.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_layout")
    set_tests_properties(analog.1g.ihp_mos.layout PROPERTIES LABELS "analog;1g;ihp;geometry" TIMEOUT 600)
    add_test(NAME analog.1g.ihp_transmit.mos_layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_layout.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --case tx_steering --case tx_sink --case tx_cascode --case tx_history_cascode
        --case tx_history_steering --case tx_history_sink --case tx_missing_bank
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_mos_collectors")
    set_tests_properties(analog.1g.ihp_transmit.mos_layout PROPERTIES
      LABELS "analog;1g;ihp;geometry;transmitter" TIMEOUT 600
      FIXTURES_SETUP one_g_transmit_mos_layout)
    if(EXISTS "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}")
      add_test(NAME analog.1g.ihp_transmit.mos_extraction
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_bank_extraction.py"
          --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_mos_collectors/checks.json"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_mos_extraction")
      set_tests_properties(analog.1g.ihp_transmit.mos_extraction PROPERTIES
        LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 600
        FIXTURES_REQUIRED one_g_transmit_mos_layout
        FIXTURES_SETUP one_g_transmit_mos_extraction)
      set(ihp_transmit_ngspice "${NGSPICE_EXECUTABLE}")
      if(NOT ihp_transmit_ngspice)
        set(ihp_transmit_ngspice "${CPPHDL_ANALOG_REAL_ROOT}/out/veriloga-toolchain/install/bin/ngspice")
      endif()
      if(EXISTS "${ihp_transmit_ngspice}")
        add_test(NAME analog.1g.ihp_transmit.current_cell
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_cell.py"
            --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
            --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}" --ngspice "${ihp_transmit_ngspice}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell")
        set_tests_properties(analog.1g.ihp_transmit.current_cell PROPERTIES
          LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 1200
          FIXTURES_SETUP one_g_transmit_current_cell)
        if(TARGET analog_1g_ihp_transmit_physical_export)
          add_test(NAME analog.1g.ihp_transmit.storage
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_storage.py"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
              --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_storage")
          set_tests_properties(analog.1g.ihp_transmit.storage PROPERTIES
            LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 1800
            FIXTURES_SETUP one_g_transmit_storage)
          add_test(NAME analog.1g.ihp_transmit.current_binding
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_current_binding.py"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
              --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell/checks.json")
          set_tests_properties(analog.1g.ihp_transmit.current_binding PROPERTIES
            LABELS "analog;1g;ihp;physical;transmitter" TIMEOUT 60
            FIXTURES_REQUIRED one_g_transmit_current_cell)
          add_test(NAME analog.1g.ihp_transmit.charge_ownership
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_charge_realization.py"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
              --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell/checks.json")
          set_tests_properties(analog.1g.ihp_transmit.charge_ownership PROPERTIES
            LABELS "analog;1g;ihp;physical;transmitter" TIMEOUT 60
            FIXTURES_REQUIRED one_g_transmit_current_cell)
          add_test(NAME analog.1g.ihp_transmit.charge_storage
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_charge_storage.py"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
              --current-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell/checks.json"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
              --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_charge_storage")
          set_tests_properties(analog.1g.ihp_transmit.charge_storage PROPERTIES
            LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 1800
            FIXTURES_REQUIRED one_g_transmit_current_cell
            FIXTURES_SETUP one_g_transmit_charge_storage)
          add_test(NAME analog.1g.ihp_transmit.charge_switching
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_charge_switching.py"
              --current-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell/checks.json"
              --storage-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_charge_storage/checks.json"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_transmit_ngspice}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_charge_switching")
          set_tests_properties(analog.1g.ihp_transmit.charge_switching PROPERTIES
            LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 172800
            FIXTURES_SETUP one_g_transmit_charge_switching
            FIXTURES_REQUIRED "one_g_transmit_current_cell;one_g_transmit_charge_storage")
        endif()
        add_test(NAME analog.1g.ihp_transmit.current_cell_switching
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_cell_switching.py"
            --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell/checks.json"
            --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_transmit_ngspice}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_current_cell_switching")
        set_tests_properties(analog.1g.ihp_transmit.current_cell_switching PROPERTIES
          LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 3600
          FIXTURES_REQUIRED one_g_transmit_current_cell)
        add_test(NAME analog.1g.ihp_transmit.pullup
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_pullup.py"
            --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
            --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}" --ngspice "${ihp_transmit_ngspice}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_pullup")
        set_tests_properties(analog.1g.ihp_transmit.pullup PROPERTIES
          LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 600
          FIXTURES_SETUP one_g_transmit_pullup)
        add_test(NAME analog.1g.ihp_transmit.mos_dc
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_bank_dc.py"
            --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_mos_extraction/checks.json"
            --pdk "${CPPHDL_IHP_PDK_ROOT}"
            --ngspice "${ihp_transmit_ngspice}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_mos_dc")
        set_tests_properties(analog.1g.ihp_transmit.mos_dc PROPERTIES
          LABELS "analog;1g;ihp;physical;psp;transmitter" TIMEOUT 600
          FIXTURES_REQUIRED one_g_transmit_mos_extraction)
      endif()
    endif()
  endif()
  add_test(NAME analog.1g.ihp_filter.physical_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/physical_module.py")
  set_tests_properties(analog.1g.ihp_filter.physical_guards PROPERTIES LABELS "analog;1g;hardware;ihp" TIMEOUT 30)
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
    add_test(NAME analog.1g.ihp_receiver.routing_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receiver_routing.py"
        --openroad "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
    set_tests_properties(analog.1g.ihp_receiver.routing_guards PROPERTIES
      LABELS "analog;1g;hardware;ihp;receiver" TIMEOUT 90)
  endif()
  add_test(NAME analog.1g.ihp_adc_lane.physical_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/adc_lane_physical.py")
  set_tests_properties(analog.1g.ihp_adc_lane.physical_guards PROPERTIES LABELS "analog;1g;hardware;ihp;receiver" TIMEOUT 30)
  add_custom_target(one-g-receive-filter-gds
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/physical_module.py"
      --module ReceiveFilter --mapped-work "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/filter_datapath"
      --work-root "${CMAKE_CURRENT_SOURCE_DIR}/results/receive_filter_runs"
      --pdk "${CPPHDL_IHP_PDK_ROOT}"
    WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
  add_custom_target(one-g-timing-gds
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/physical_module.py"
      --module ReceiveTimingLoop --mapped-work "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/timing"
      --work-root "${CMAKE_CURRENT_SOURCE_DIR}/results/timing_runs" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
  foreach(block normalizer pam5_driver adc_receive)
    if(block STREQUAL "normalizer")
      set(physical_top ReceiveSampleNormalizer)
    elseif(block STREQUAL "adc_receive")
      set(physical_top ADCReceiveController)
    else()
      set(physical_top PAM5DriverControl)
    endif()
    add_custom_target(one-g-${block}-gds
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/physical_module.py"
        --module "${physical_top}" --mapped-work "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/${block}"
        --work-root "${CMAKE_CURRENT_SOURCE_DIR}/results/${block}_runs" --pdk "${CPPHDL_IHP_PDK_ROOT}"
      WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
  endforeach()
  option(CPPHDL_1G_PHYSICAL_TESTS "Run full standalone receive DSP/timing GDS regressions" OFF)
  if(CPPHDL_1G_PHYSICAL_TESTS AND ONE_G_VERILATOR AND
     EXISTS "${CPPHDL_1G_CONVERTER}")
    add_test(NAME analog.1g.ihp_reset.physical
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/phy_reset_physical.py"
        --proof "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/phy_reset_control/checks.json"
        --work-root "${CMAKE_CURRENT_BINARY_DIR}/tests/results/phy_reset_physical"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
    set_tests_properties(analog.1g.ihp_reset.physical PROPERTIES
      LABELS "analog;1g;ihp;physical;clock;reset" TIMEOUT 7200
      FIXTURES_REQUIRED one_g_phy_reset_control_cells)
  endif()
  if(CPPHDL_1G_PHYSICAL_TESTS)
    if(TEST analog.1g.processing_clock_control.cells)
      set_tests_properties(analog.1g.processing_clock_control.cells PROPERTIES
        FIXTURES_SETUP one_g_processing_clock_control_cells)
      add_test(NAME analog.1g.ihp_processing_clock.physical
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_physical.py"
          --native-proof "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/processing_clock_control/checks.json"
          --cell-proof "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/processing_clock_control_cells/checks.json"
          --work-root "${CMAKE_CURRENT_BINARY_DIR}/tests/physical/processing_clock_control"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
      set_tests_properties(analog.1g.ihp_processing_clock.physical PROPERTIES
        LABELS "analog;1g;ihp;clock;physical" RUN_SERIAL TRUE TIMEOUT 14400
        FIXTURES_REQUIRED one_g_processing_clock_control_cells)
    endif()
    foreach(block filter timing normalizer pam5_driver adc_receive)
      set(physical_timeout 14400)
      if(block STREQUAL "filter")
        set(physical_timeout 86400)
        set(physical_top ReceiveFilter)
        set(physical_case filter_datapath)
        set(physical_fixture one_g_filter_datapath_mapped)
      elseif(block STREQUAL "timing")
        set(physical_top ReceiveTimingLoop)
        set(physical_case timing)
        set(physical_fixture one_g_timing_mapped)
      elseif(block STREQUAL "normalizer")
        set(physical_top ReceiveSampleNormalizer)
        set(physical_case normalizer)
        set(physical_fixture one_g_normalizer_mapped)
      elseif(block STREQUAL "adc_receive")
        set(physical_top ADCReceiveController)
        set(physical_case adc_receive)
        set(physical_fixture one_g_adc_receive_mapped)
      else()
        set(physical_top PAM5DriverControl)
        set(physical_case pam5_driver)
        set(physical_fixture one_g_pam5_driver_mapped)
      endif()
      add_test(NAME analog.1g.ihp_${block}.physical
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/physical_module.py"
          --module "${physical_top}" --mapped-work "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/${physical_case}"
          --work-root "${CMAKE_CURRENT_BINARY_DIR}/tests/physical/${block}" --pdk "${CPPHDL_IHP_PDK_ROOT}")
      set_tests_properties(analog.1g.ihp_${block}.physical PROPERTIES
        LABELS "analog;1g;ihp;physical" RUN_SERIAL TRUE TIMEOUT "${physical_timeout}" FIXTURES_REQUIRED "${physical_fixture}")
    endforeach()
  endif()
endif()

if(TARGET analog_1g_ihp_charge_balanced_receive_array)
  set(charge_array_source "${CMAKE_CURRENT_SOURCE_DIR}/tests/charge_balanced_receive_array.cpp")
  set(charge_array_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/charge_balanced_receive_array")
  add_test(NAME analog.1g.ihp_charge_balanced_array.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_charge_balanced_receive_array>"
    --dummy-p-width-um 30 --charge-defined-nodes)
  add_test(NAME analog.1g.ihp_charge_balanced_conversion.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_conversion_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_charge_balanced_receive_conversion>" --dummy-p-width-um 30)
  set_tests_properties(analog.1g.ihp_charge_balanced_array.bindings analog.1g.ihp_charge_balanced_conversion.bindings
    PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 120)
  set(charge_array_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND TARGET cpphdl-veriloga
      AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND charge_array_flows veriloga)
  endif()
  foreach(flow IN LISTS charge_array_flows)
    add_test(NAME analog.1g.ihp_charge_balanced_array.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_ihp_charge_balanced_receive_array>" --fixture-source "${charge_array_source}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${charge_array_work}/${flow}"
      --driver "$<TARGET_FILE:cpphdl-veriloga>" --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.ihp_charge_balanced_array.${flow} PROPERTIES
      LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 21600 ENVIRONMENT "OMP_THREAD_LIMIT=1"
      RESOURCE_LOCK one_g_adc_array_memory FIXTURES_SETUP "one_g_charge_array_${flow}")
  endforeach()
  if("veriloga" IN_LIST charge_array_flows)
    add_test(NAME analog.1g.ihp_charge_balanced_array.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array.py" --flow compare --work "${charge_array_work}")
    set_tests_properties(analog.1g.ihp_charge_balanced_array.compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 300 FIXTURES_REQUIRED "one_g_charge_array_cpp;one_g_charge_array_veriloga")
  endif()
  if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103_nqs.osdi")
    foreach(profile process ss_hot ff_cold)
      set(charge_corner mos_tt)
      set(charge_temperature 27)
      if(profile STREQUAL "ss_hot")
        set(charge_corner mos_ss)
        set(charge_temperature 125)
      elseif(profile STREQUAL "ff_cold")
        set(charge_corner mos_ff)
        set(charge_temperature -40)
      endif()
      add_test(NAME analog.1g.ihp_charge_balanced_array.${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_process.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_charge_balanced_receive_array>" --fixture-source "${charge_array_source}"
        --cxx "${CMAKE_CXX_COMPILER}" --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
        --corner "${charge_corner}" --temperature "${charge_temperature}" --dummy-p-width-um 30 --charge-defined-nodes
        --work "${charge_array_work}/${profile}")
      set_tests_properties(analog.1g.ihp_charge_balanced_array.${profile} PROPERTIES
        LABELS "analog;1g;ihp;frontend;psp" TIMEOUT 18000 ENVIRONMENT "OMP_THREAD_LIMIT=1"
        RESOURCE_LOCK one_g_adc_array_memory)
    endforeach()
  endif()
endif()

if(TARGET analog_1g_cascoded_core_transmit_driver AND NGSPICE_EXECUTABLE
    AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103_nqs.osdi")
  add_test(NAME analog.1g.ihp_cascoded_core_transmit.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cascoded_driver_process.py"
    --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>"
    --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json")
  set_tests_properties(analog.1g.ihp_cascoded_core_transmit.bindings PROPERTIES
    LABELS "analog;1g;ihp;transmitter" TIMEOUT 120 FIXTURES_REQUIRED one_g_transmit_controls)
  foreach(profile nominal ss_hot ff_cold)
    set(cascoded_corner mos_tt)
    set(cascoded_temperature 27)
    set(cascoded_trim 44)
    if(profile STREQUAL "ss_hot")
      set(cascoded_corner mos_ss)
      set(cascoded_temperature 125)
      set(cascoded_trim 54)
    elseif(profile STREQUAL "ff_cold")
      set(cascoded_corner mos_ff)
      set(cascoded_temperature -40)
      set(cascoded_trim 34)
    endif()
    add_test(NAME analog.1g.ihp_cascoded_core_transmit.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/cascoded_transmit_process.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
      --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/cascoded_core_transmit_driver.cpp"
      --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
      --ngspice "${NGSPICE_EXECUTABLE}" --corner "${cascoded_corner}" --temperature "${cascoded_temperature}"
      --trim-code "${cascoded_trim}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/cascoded_core_transmit_${profile}")
    set_tests_properties(analog.1g.ihp_cascoded_core_transmit.${profile} PROPERTIES
      LABELS "analog;1g;ihp;transmitter;psp" TIMEOUT 7200 FIXTURES_REQUIRED one_g_transmit_controls)
    add_test(NAME analog.1g.ihp_transmit_interaction.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_interaction.py" --profile cascoded
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
      --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/cascoded_core_transmit_driver.cpp"
      --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
      --corner "${cascoded_corner}" --temperature "${cascoded_temperature}" --trim-code "${cascoded_trim}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_interaction_${profile}")
    set_tests_properties(analog.1g.ihp_transmit_interaction.${profile} PROPERTIES
      LABELS "analog;1g;ihp;transmitter;psp" TIMEOUT 1800 FIXTURES_REQUIRED one_g_transmit_controls)
  endforeach()
endif()
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103_nqs.osdi"
    AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  if(TARGET analog_1g_ihp_receive_process_conversion)
    add_test(NAME analog.1g.ihp_receive_conversion.xspice_controller COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/xspice_controller.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_process_conversion>" --ngspice "${NGSPICE_EXECUTABLE}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/xspice_controller_ports")
    set_tests_properties(analog.1g.ihp_receive_conversion.xspice_controller PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 600)
    add_test(NAME analog.1g.ihp_receive_conversion.evidence_io COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/sim_adc_controller_io.py" --ngspice "${NGSPICE_EXECUTABLE}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/sim_adc_controller_io")
    set_tests_properties(analog.1g.ihp_receive_conversion.evidence_io PROPERTIES
      LABELS "analog;1g;ihp;frontend" TIMEOUT 180)
    foreach(count 1 4 16)
      if(count EQUAL 16)
        set(bursts 2)
      else()
        set(bursts 1)
      endif()
      add_test(NAME analog.1g.ihp_receive_conversion_${count}.psp COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_conversion_process.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_receive_process_conversion>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --slices "${count}" --bursts "${bursts}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_process_conversion_psp_${count}")
      set_tests_properties(analog.1g.ihp_receive_conversion_${count}.psp PROPERTIES
        LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 10800)
    endforeach()
  endif()
  foreach(receiver_variant wideband precise_wideband parallel balanced)
    if(receiver_variant STREQUAL "parallel" OR receiver_variant STREQUAL "balanced")
      set(input_drivers 2)
    else()
      set(input_drivers 1)
    endif()
  if(TARGET analog_1g_ihp_receive_${receiver_variant}_conversion)
    foreach(count 1 4 16)
      if(count EQUAL 16)
        set(bursts 2)
      else()
        set(bursts 1)
      endif()
      add_test(NAME analog.1g.ihp_receive_${receiver_variant}_conversion_${count}.psp COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_conversion_process.py"
        --backend xspice --input-drivers "${input_drivers}"
        --bench "$<TARGET_FILE:analog_1g_ihp_receive_${receiver_variant}_conversion>"
        --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_${receiver_variant}_conversion.cpp"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --slices "${count}" --bursts "${bursts}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}_conversion_psp_${count}")
      set_tests_properties(analog.1g.ihp_receive_${receiver_variant}_conversion_${count}.psp PROPERTIES
        LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 10800)
    endforeach()
  endif()
  if(TARGET analog_1g_ihp_receive_${receiver_variant} AND NOT input_drivers EQUAL 2)
    add_test(NAME analog.1g.ihp_receive_${receiver_variant}.nominal COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_frontend_process.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_${receiver_variant}>"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_${receiver_variant}.cpp"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}_nominal")
    set_tests_properties(analog.1g.ihp_receive_${receiver_variant}.nominal PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 7200 ENVIRONMENT "OMP_THREAD_LIMIT=1"
      FIXTURES_SETUP one_g_ihp_receive_${receiver_variant}_nominal)
    add_test(NAME analog.1g.ihp_receive_${receiver_variant}.process_temperature COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_frontend_process.py" --process-temperature
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_${receiver_variant}>"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_${receiver_variant}.cpp"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
      --nominal-report "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}_nominal/checks.json"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_${receiver_variant}_process_temperature")
    set_tests_properties(analog.1g.ihp_receive_${receiver_variant}.process_temperature PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 21600 ENVIRONMENT "OMP_THREAD_LIMIT=1"
      FIXTURES_REQUIRED one_g_ihp_receive_${receiver_variant}_nominal)
  endif()
  endforeach()
  add_test(NAME analog.1g.ihp_amplifier.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/amplifier.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/amplifier_pvt")
  set_tests_properties(analog.1g.ihp_amplifier.pvt PROPERTIES LABELS "analog;1g;ihp;veriloga" TIMEOUT 180)
  add_test(NAME analog.1g.ihp_level_shifter.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/level_shifter.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/level_shifter_sized_pvt")
  set_tests_properties(analog.1g.ihp_level_shifter.pvt PROPERTIES LABELS "analog;1g;ihp;veriloga" TIMEOUT 1800)
  add_test(NAME analog.1g.ihp_level_down.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/level_down.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --bench "$<TARGET_FILE:analog_1g_level_down>" --cxx "${CMAKE_CXX_COMPILER}"
    --ngspice "${NGSPICE_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/level_down_pvt")
  set_tests_properties(analog.1g.ihp_level_down.pvt PROPERTIES LABELS "analog;1g;ihp;interface;veriloga" TIMEOUT 1800)
  add_test(NAME analog.1g.ihp_source_follower.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_follower.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --bench "$<TARGET_FILE:analog_1g_source_follower>" --cxx "${CMAKE_CXX_COMPILER}"
    --ngspice "${NGSPICE_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/source_follower_pvt")
  set_tests_properties(analog.1g.ihp_source_follower.pvt PROPERTIES LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 3600)
  if(TARGET analog_1g_ihp_receive_process)
    add_test(NAME analog.1g.ihp_receive_frontend.nominal COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_frontend_process.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_process>"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_process.cpp"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_frontend_process_nominal")
    set_tests_properties(analog.1g.ihp_receive_frontend.nominal PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 7200 FIXTURES_SETUP one_g_ihp_receive_frontend_nominal)
    add_test(NAME analog.1g.ihp_receive_frontend.process_temperature COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_frontend_process.py" --process-temperature
      --nominal-report "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_frontend_process_nominal/checks.json"
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_process>"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_process.cpp"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_frontend_process_process_temperature")
    set_tests_properties(analog.1g.ihp_receive_frontend.process_temperature PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 21600
      FIXTURES_REQUIRED one_g_ihp_receive_frontend_nominal)
  endif()
  add_test(NAME analog.1g.ihp_clock_level_shifter.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/level_shifter.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --ngspice "${NGSPICE_EXECUTABLE}" --clock-mhz 250 --cascoded --clock-interface
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/fingered_clock_level_shifter_200ff_250_pvt")
  set_tests_properties(analog.1g.ihp_clock_level_shifter.pvt PROPERTIES LABELS "analog;1g;ihp;clock;veriloga" TIMEOUT 7200)
  add_test(NAME analog.1g.ihp_direct_level_shifter_250.rejected COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/level_shifter.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --ngspice "${NGSPICE_EXECUTABLE}" --clock-mhz 250 --reject-direct
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/direct_level_shifter_250_rejected")
  set_tests_properties(analog.1g.ihp_direct_level_shifter_250.rejected PROPERTIES LABELS "analog;1g;ihp;clock;veriloga" TIMEOUT 240)
  add_test(NAME analog.1g.ihp_emphasized_driver.nominal COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/emphasized_driver.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --bench "$<TARGET_FILE:analog_1g_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
    --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/emphasized_driver_600pf_nominal")
  set_tests_properties(analog.1g.ihp_emphasized_driver.nominal PROPERTIES LABELS "analog;1g;ihp;transmitter;veriloga"
    TIMEOUT 1200)
  if(TARGET analog_1g_core_transmit_driver)
    add_test(NAME analog.1g.ihp_core_transmit.nominal COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_transmit_process.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
      --bench "$<TARGET_FILE:analog_1g_core_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
      --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
      --ngspice "${NGSPICE_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/core_transmit_nominal")
    set_tests_properties(analog.1g.ihp_core_transmit.nominal PROPERTIES
      LABELS "analog;1g;ihp;transmitter;veriloga" TIMEOUT 7200 FIXTURES_REQUIRED one_g_transmit_controls)
  endif()
  if(TARGET analog_1g_programmable_core_transmit_driver)
    foreach(profile nominal ss_hot ff_cold)
      set(transmit_corner mos_tt)
      set(transmit_temperature 27)
      set(transmit_trim 24)
      if(profile STREQUAL "ss_hot")
        set(transmit_corner mos_ss)
        set(transmit_temperature 125)
        set(transmit_trim 38)
      elseif(profile STREQUAL "ff_cold")
        set(transmit_corner mos_ff)
        set(transmit_temperature -40)
        set(transmit_trim 11)
      endif()
      add_test(NAME analog.1g.ihp_programmable_core_transmit.${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_transmit_process_states.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
        --bench "$<TARGET_FILE:analog_1g_programmable_core_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
        --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/programmable_core_transmit_driver.cpp"
        --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
        --ngspice "${NGSPICE_EXECUTABLE}" --corner "${transmit_corner}" --temperature "${transmit_temperature}"
        --trim-code "${transmit_trim}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_core_transmit_${profile}")
      set_tests_properties(analog.1g.ihp_programmable_core_transmit.${profile} PROPERTIES
        LABELS "analog;1g;ihp;transmitter;veriloga" TIMEOUT 7200 FIXTURES_REQUIRED one_g_transmit_controls)
    endforeach()
  endif()
  add_test(NAME analog.1g.ihp_comparator.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/comparator.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/comparator_pvt")
  set_tests_properties(analog.1g.ihp_comparator.pvt PROPERTIES LABELS "analog;1g;ihp;veriloga"
    TIMEOUT 5400 RESOURCE_LOCK one_g_heavy_hardware)
  add_test(NAME analog.1g.ihp_track_hold.pvt COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/track_hold.py" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/track_hold_compensated_pvt")
  set_tests_properties(analog.1g.ihp_track_hold.pvt PROPERTIES LABELS "analog;1g;ihp;veriloga" TIMEOUT 900)
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

add_executable(analog_1g_ihp_programmable_receive_gain tests/programmable_receive_gain.cpp)
target_link_libraries(analog_1g_ihp_programmable_receive_gain PRIVATE cpphdl::analog)
add_dependencies(check-1g analog_1g_ihp_programmable_receive_gain)
add_dependencies(check-analog analog_1g_ihp_programmable_receive_gain)
add_dependencies(check-veriloga analog_1g_ihp_programmable_receive_gain)
set(receive_gain_flows cpp)
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  list(APPEND receive_gain_flows veriloga)
endif()
foreach(flow IN LISTS receive_gain_flows)
  add_test(NAME analog.1g.ihp_receive_gain.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_gain.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_gain>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_receive_gain/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.ihp_receive_gain.${flow} PROPERTIES
    LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 3600 FIXTURES_SETUP one_g_ihp_receive_gain_${flow})
endforeach()
if("veriloga" IN_LIST receive_gain_flows)
  add_test(NAME analog.1g.ihp_receive_gain.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_gain.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_receive_gain")
  set_tests_properties(analog.1g.ihp_receive_gain.compare PROPERTIES
    LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 60
    FIXTURES_REQUIRED "one_g_ihp_receive_gain_cpp;one_g_ihp_receive_gain_veriloga")
endif()
add_test(NAME analog.1g.ihp_receive_gain.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_gain_bindings.py"
  --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_gain>")
set_tests_properties(analog.1g.ihp_receive_gain.bindings PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 60)
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  add_test(NAME analog.1g.ihp_receive_gain.process COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_gain_process.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_gain>" --cxx "${CMAKE_CXX_COMPILER}"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_receive_gain_process")
  set_tests_properties(analog.1g.ihp_receive_gain.process PROPERTIES
    LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 7200 ENVIRONMENT "OMP_THREAD_LIMIT=1")
endif()

if(TARGET Eigen3::Eigen)
  add_executable(analog_1g_ihp_programmable_receive_frontend tests/programmable_receive_frontend.cpp)
  target_link_libraries(analog_1g_ihp_programmable_receive_frontend PRIVATE cpphdl::analog Eigen3::Eigen)
  add_dependencies(check-1g analog_1g_ihp_programmable_receive_frontend)
  add_dependencies(check-analog analog_1g_ihp_programmable_receive_frontend)
  add_dependencies(check-veriloga analog_1g_ihp_programmable_receive_frontend)
endif()

add_test(NAME analog.1g.ihp_programmable_receive.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_frontend_contract.py")
set_tests_properties(analog.1g.ihp_programmable_receive.contract PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
if(TARGET analog_1g_ihp_programmable_receive_frontend)
  set(programmable_receive_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_receive_frontend")
  set(programmable_receive_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND programmable_receive_flows veriloga)
  endif()
  foreach(flow IN LISTS programmable_receive_flows)
    add_test(NAME analog.1g.ihp_programmable_receive.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_frontend.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_frontend>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${programmable_receive_work}/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.ihp_programmable_receive.${flow} PROPERTIES
      LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 21600 RESOURCE_LOCK one_g_adc_array_memory
      ENVIRONMENT "OMP_THREAD_LIMIT=1"
      FIXTURES_SETUP "one_g_ihp_programmable_receive_${flow}")
  endforeach()
  if("veriloga" IN_LIST programmable_receive_flows)
    add_test(NAME analog.1g.ihp_programmable_receive.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_frontend.py" --flow compare
      --work "${programmable_receive_work}")
    set_tests_properties(analog.1g.ihp_programmable_receive.compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_ihp_programmable_receive_cpp;one_g_ihp_programmable_receive_veriloga")
  endif()
endif()

if(TARGET analog_1g_ihp_programmable_receive_frontend)
  add_test(NAME analog.1g.ihp_programmable_receive.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_frontend_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_frontend>")
  set_tests_properties(analog.1g.ihp_programmable_receive.bindings PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 60)
  if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
    add_test(NAME analog.1g.ihp_programmable_receive.process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_frontend_process.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_programmable_receive_frontend>" --cxx "${CMAKE_CXX_COMPILER}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/programmable_frontend_process")
    set_tests_properties(analog.1g.ihp_programmable_receive.process PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 14400 RESOURCE_LOCK one_g_adc_array_memory)
  endif()
endif()

if(TARGET Eigen3::Eigen)
  add_executable(analog_1g_ihp_wideband_receive_frontend tests/wideband_programmable_receive_frontend.cpp)
  target_link_libraries(analog_1g_ihp_wideband_receive_frontend PRIVATE cpphdl::analog Eigen3::Eigen)
  foreach(check check-1g check-analog check-veriloga)
    add_dependencies(${check} analog_1g_ihp_wideband_receive_frontend)
  endforeach()
  set(wideband_receive_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/wideband_receive_frontend")
  set(wideband_receive_source "${CMAKE_CURRENT_SOURCE_DIR}/tests/wideband_programmable_receive_frontend.cpp")
  set(wideband_receive_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND wideband_receive_flows veriloga)
  endif()
  foreach(flow IN LISTS wideband_receive_flows)
    add_test(NAME analog.1g.ihp_wideband_receive.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_frontend.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_frontend>" --fixture-source "${wideband_receive_source}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${wideband_receive_work}/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.ihp_wideband_receive.${flow} PROPERTIES
      LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 21600 RESOURCE_LOCK one_g_adc_array_memory
      ENVIRONMENT "OMP_THREAD_LIMIT=1" FIXTURES_SETUP "one_g_ihp_wideband_receive_${flow}")
  endforeach()
  if("veriloga" IN_LIST wideband_receive_flows)
    add_test(NAME analog.1g.ihp_wideband_receive.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_frontend.py" --flow compare --work "${wideband_receive_work}")
    set_tests_properties(analog.1g.ihp_wideband_receive.compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_ihp_wideband_receive_cpp;one_g_ihp_wideband_receive_veriloga")
  endif()
  add_test(NAME analog.1g.ihp_wideband_receive.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_frontend_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_frontend>")
  set_tests_properties(analog.1g.ihp_wideband_receive.bindings PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 60)
  if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
    foreach(profile tt ss_hot ff_cold)
      set(corner mos_tt)
      set(temperature 27)
      if(profile STREQUAL "ss_hot")
        set(corner mos_ss)
        set(temperature 125)
      elseif(profile STREQUAL "ff_cold")
        set(corner mos_ff)
        set(temperature -40)
      endif()
      add_test(NAME analog.1g.ihp_wideband_receive.process_${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_frontend_process.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_frontend>" --fixture-source "${wideband_receive_source}"
        --cxx "${CMAKE_CXX_COMPILER}" --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
        --work "${wideband_receive_work}/process_${profile}" --corner "${corner}" --temperature "${temperature}"
        --warmup-step 1e-9 --initialization bias)
      set_tests_properties(analog.1g.ihp_wideband_receive.process_${profile} PROPERTIES
        LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 14400
        RESOURCE_LOCK one_g_adc_array_memory ENVIRONMENT "OMP_THREAD_LIMIT=1")
    endforeach()
  endif()
endif()

if(TARGET Eigen3::Eigen)
  add_executable(analog_1g_ihp_wideband_receive_array tests/wideband_receive_array.cpp)
  target_link_libraries(analog_1g_ihp_wideband_receive_array PRIVATE cpphdl::analog Eigen3::Eigen)
  foreach(check check-1g check-analog check-veriloga)
    add_dependencies(${check} analog_1g_ihp_wideband_receive_array)
  endforeach()
  set(wideband_array_source "${CMAKE_CURRENT_SOURCE_DIR}/tests/wideband_receive_array.cpp")
  set(wideband_array_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/wideband_receive_array")
  foreach(flow IN LISTS wideband_receive_flows)
    add_test(NAME analog.1g.ihp_wideband_array.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_array>" --fixture-source "${wideband_array_source}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${wideband_array_work}/${flow}"
      --driver "$<TARGET_FILE:cpphdl-veriloga>" --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.ihp_wideband_array.${flow} PROPERTIES
      LABELS "analog;1g;ihp;frontend;${flow}" TIMEOUT 21600 ENVIRONMENT "OMP_THREAD_LIMIT=1"
      RESOURCE_LOCK one_g_adc_array_memory FIXTURES_SETUP "one_g_ihp_wideband_array_${flow}")
  endforeach()
  if("veriloga" IN_LIST wideband_receive_flows)
    add_test(NAME analog.1g.ihp_wideband_array.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_array.py" --flow compare --work "${wideband_array_work}")
    set_tests_properties(analog.1g.ihp_wideband_array.compare PROPERTIES LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 300
      FIXTURES_REQUIRED "one_g_ihp_wideband_array_cpp;one_g_ihp_wideband_array_veriloga")
  endif()
  add_test(NAME analog.1g.ihp_wideband_array.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_array>")
  add_test(NAME analog.1g.ihp_wideband_conversion.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_conversion_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_conversion>")
  set_tests_properties(analog.1g.ihp_wideband_array.bindings analog.1g.ihp_wideband_conversion.bindings
    PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 120)
  if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
    foreach(profile process process_refined process_ss_hot)
      set(warmup_step 1e-9)
      set(corner mos_tt)
      set(temperature 27)
      if(profile STREQUAL "process_refined")
        set(warmup_step 5e-10)
      elseif(profile STREQUAL "process_ss_hot")
        set(corner mos_ss)
        set(temperature 125)
      endif()
      add_test(NAME analog.1g.ihp_wideband_array.${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_process.py"
        --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_array>" --fixture-source "${wideband_array_source}"
        --cxx "${CMAKE_CXX_COMPILER}" --ngspice "${NGSPICE_EXECUTABLE}" --pdk "${CPPHDL_IHP_PDK_ROOT}"
        --work "${wideband_array_work}/${profile}" --corner "${corner}" --temperature "${temperature}"
        --warmup-step "${warmup_step}" --initialization bias)
      set_tests_properties(analog.1g.ihp_wideband_array.${profile} PROPERTIES
        LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 18000 ENVIRONMENT "OMP_THREAD_LIMIT=1"
        RESOURCE_LOCK one_g_adc_array_memory FIXTURES_SETUP "one_g_ihp_wideband_array_${profile}")
    endforeach()
    add_test(NAME analog.1g.ihp_wideband_array.process_compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_array_process.py" --compare
      "${wideband_array_work}/process" "${wideband_array_work}/process_refined"
      --output "${wideband_array_work}/process_comparison.json")
    set_tests_properties(analog.1g.ihp_wideband_array.process_compare PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 300
      FIXTURES_REQUIRED "one_g_ihp_wideband_array_process;one_g_ihp_wideband_array_process_refined")
    if(conversion_cosim_available)
      foreach(profile ss_hot ff_cold)
        set(corner mos_ss)
        set(temperature 125)
        if(profile STREQUAL "ff_cold")
          set(corner mos_ff)
          set(temperature -40)
        endif()
        add_test(NAME analog.1g.ihp_wideband_conversion_1_gain8.psp_${profile} COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_conversion.py" --flow psp
          --bench "$<TARGET_FILE:analog_1g_ihp_wideband_receive_conversion>"
          --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/tests/wideband_receive_conversion.cpp"
          --cxx "${CMAKE_CXX_COMPILER}" --ngspice "${NGSPICE_EXECUTABLE}" --pdk "${CPPHDL_IHP_PDK_ROOT}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/wideband_conversion_1_gain8_${profile}"
          --slices 1 --gain 8 --storage-pf 1.5 --bursts 1 --corner "${corner}" --temperature "${temperature}")
        set_tests_properties(analog.1g.ihp_wideband_conversion_1_gain8.psp_${profile} PROPERTIES
          LABELS "analog;1g;ihp;converter;veriloga" TIMEOUT 7200 ENVIRONMENT "OMP_THREAD_LIMIT=1"
          RESOURCE_LOCK one_g_adc_array_memory)
      endforeach()
    endif()
  endif()
endif()

if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_complete_qualification.guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/complete_qualification.py")
  set_tests_properties(analog.1g.ihp_complete_qualification.guards PROPERTIES
    LABELS "analog;1g;ihp;physical" TIMEOUT 60)
endif()

add_test(NAME analog.1g.ihp_transmit_interaction.checker COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_interaction_contract.py")
set_tests_properties(analog.1g.ihp_transmit_interaction.checker PROPERTIES
  LABELS "analog;1g;ihp;transmitter" TIMEOUT 30)
if(TARGET analog_1g_programmable_core_transmit_driver AND NGSPICE_EXECUTABLE
    AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103_nqs.osdi")
  add_test(NAME analog.1g.ihp_transmit_interaction.reject_original COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_interaction.py" --profile original --expect-failure
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
    --bench "$<TARGET_FILE:analog_1g_programmable_core_transmit_driver>"
    --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/programmable_core_transmit_driver.cpp"
    --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json" --trim-code 24
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_interaction_reject_original")
  set_tests_properties(analog.1g.ihp_transmit_interaction.reject_original PROPERTIES
    LABELS "analog;1g;ihp;transmitter;psp" TIMEOUT 1800 FIXTURES_REQUIRED one_g_transmit_controls)
endif()

if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOSCAP.lib")
  add_test(NAME analog.1g.ihp_moscap_bias_storage.process COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/moscap_bias_storage.py"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/moscap_bias_storage")
  set_tests_properties(analog.1g.ihp_moscap_bias_storage.process PROPERTIES
    LABELS "analog;1g;ihp;capacitor;psp" TIMEOUT 300)
  if(TARGET analog_1g_cascoded_core_transmit_driver)
    add_test(NAME analog.1g.ihp_moscap_transmit.nominal COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/moscap_transmit_process.py"
      --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/cascoded_core_transmit_driver.cpp"
      --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --trim-code 44
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/moscap_core_transmit_nominal")
    set_tests_properties(analog.1g.ihp_moscap_transmit.nominal PROPERTIES
      LABELS "analog;1g;ihp;transmitter;psp" TIMEOUT 7200 FIXTURES_REQUIRED one_g_transmit_controls)
  endif()
endif()
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_moscap_bias_storage.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/moscap_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/moscap_layout")
  set_tests_properties(analog.1g.ihp_moscap_bias_storage.layout PROPERTIES
    LABELS "analog;1g;ihp;area;geometry" TIMEOUT 1800
    FIXTURES_SETUP one_g_moscap_storage_layout)
endif()

foreach(storage_guard moscap_extraction passive_network_reduction)
  add_test(NAME analog.1g.ihp_storage.${storage_guard}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/${storage_guard}.py")
  set_tests_properties(analog.1g.ihp_storage.${storage_guard} PROPERTIES
    LABELS "analog;1g;ihp;extraction" TIMEOUT 120)
endforeach()

set(ihp_clock_cell_ngspice "${NGSPICE_EXECUTABLE}")
add_test(NAME analog.1g.ihp_extraction.named_nodes
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/named_node_extraction.py")
set_tests_properties(analog.1g.ihp_extraction.named_nodes PROPERTIES
  LABELS "analog;1g;ihp;extraction" TIMEOUT 120)
add_test(NAME analog.1g.ihp_reference_clock.realized_evidence
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_realized_evidence.py")
set_tests_properties(analog.1g.ihp_reference_clock.realized_evidence PROPERTIES
  LABELS "analog;1g;ihp;clock;extraction" TIMEOUT 120)
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" AND EXISTS "${CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE}")
  add_test(NAME analog.1g.ihp_reference_clock_cells.macro_access
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/clock_macro_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --magic "${CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/clock_macro_access")
  set_tests_properties(analog.1g.ihp_reference_clock_cells.macro_access PROPERTIES
    LABELS "analog;1g;ihp;clock;geometry;extraction" TIMEOUT 1800)
  if(TARGET analog_1g_ihp_reference_clock_layout_export)
    add_test(NAME analog.1g.ihp_reference_clock.realized_layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_realized_layout.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_reference_clock_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_realized_layout")
    set_tests_properties(analog.1g.ihp_reference_clock.realized_layout PROPERTIES
      LABELS "analog;1g;ihp;clock;geometry" TIMEOUT 86400
      FIXTURES_SETUP one_g_reference_clock_realized_layout)
  endif()
  add_test(NAME analog.1g.ihp_reference_clock_cells.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_cell_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --magic "${CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_cell_layout")
  set_tests_properties(analog.1g.ihp_reference_clock_cells.layout PROPERTIES
    LABELS "analog;1g;ihp;clock;geometry;extraction" TIMEOUT 1800
    FIXTURES_SETUP one_g_clock_cell_layout)
  add_test(NAME analog.1g.ihp_reference_clock_divider_cells.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_cell_layout.py" --module divider
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --magic "${CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_divider_layout")
  set_tests_properties(analog.1g.ihp_reference_clock_divider_cells.layout PROPERTIES
    LABELS "analog;1g;ihp;clock;geometry;extraction" TIMEOUT 1800
    FIXTURES_SETUP one_g_clock_divider_layout)
endif()
if(NOT ihp_clock_cell_ngspice)
  set(ihp_clock_cell_ngspice "${CPPHDL_ANALOG_REAL_ROOT}/out/veriloga-toolchain/install/bin/ngspice")
endif()
if(TEST analog.1g.ihp_reference_clock.realized_layout AND EXISTS "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
    AND EXISTS "${ihp_clock_cell_ngspice}")
  add_test(NAME analog.1g.ihp_reference_clock.realized_extracted
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_realized_extracted.py"
      --layout-proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_realized_layout/checks.json"
      --exporter "$<TARGET_FILE:analog_1g_ihp_reference_clock_layout_export>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_clock_cell_ngspice}"
      --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_realized_extracted")
  set_tests_properties(analog.1g.ihp_reference_clock.realized_extracted PROPERTIES
    LABELS "analog;1g;ihp;clock;extraction;psp" TIMEOUT 21600
    FIXTURES_REQUIRED one_g_reference_clock_realized_layout)
endif()
if(EXISTS "${ihp_clock_cell_ngspice}" AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell/spice/sg13g2_stdcell.spice")
  foreach(clock_cell_corner mos_tt mos_ss mos_ff)
    add_test(NAME analog.1g.ihp_reference_clock_cells.${clock_cell_corner}
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_cells.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_clock_cell_ngspice}"
        --corner "${clock_cell_corner}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_cells_${clock_cell_corner}")
    set_tests_properties(analog.1g.ihp_reference_clock_cells.${clock_cell_corner} PROPERTIES
      LABELS "analog;1g;ihp;clock;psp" TIMEOUT 10800
      FIXTURES_SETUP "one_g_clock_cells_${clock_cell_corner}")
    if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" AND EXISTS "${CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE}")
      add_test(NAME analog.1g.ihp_reference_clock_cells.extracted_${clock_cell_corner}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_cells_extracted.py"
          --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_cell_layout/checks.json"
          --schematic "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_cells_${clock_cell_corner}/checks.json"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_clock_cell_ngspice}"
          --corner "${clock_cell_corner}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_cells_extracted_${clock_cell_corner}")
      set_tests_properties(analog.1g.ihp_reference_clock_cells.extracted_${clock_cell_corner} PROPERTIES
        LABELS "analog;1g;ihp;clock;physical;extraction" TIMEOUT 14400
        FIXTURES_REQUIRED "one_g_clock_cell_layout;one_g_clock_cells_${clock_cell_corner}")
    endif()
    add_test(NAME analog.1g.ihp_reference_clock_divider_cells.${clock_cell_corner}
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_divider_cells.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_clock_cell_ngspice}"
        --corner "${clock_cell_corner}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_divider_cells_${clock_cell_corner}")
    set_tests_properties(analog.1g.ihp_reference_clock_divider_cells.${clock_cell_corner} PROPERTIES
      LABELS "analog;1g;ihp;clock;psp" TIMEOUT 1800
      FIXTURES_SETUP "one_g_clock_divider_${clock_cell_corner}")
    if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" AND EXISTS "${CPPHDL_IHP_CLOCK_CELL_MAGIC_EXECUTABLE}")
      add_test(NAME analog.1g.ihp_reference_clock_divider_cells.extracted_${clock_cell_corner}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_cells_extracted.py"
          --module divider
          --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_divider_layout/checks.json"
          --schematic "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_divider_cells_${clock_cell_corner}/checks.json"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_clock_cell_ngspice}"
          --corner "${clock_cell_corner}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/reference_clock_divider_extracted_${clock_cell_corner}")
      set_tests_properties(analog.1g.ihp_reference_clock_divider_cells.extracted_${clock_cell_corner} PROPERTIES
        LABELS "analog;1g;ihp;clock;physical;extraction" TIMEOUT 2400
        FIXTURES_REQUIRED "one_g_clock_divider_layout;one_g_clock_divider_${clock_cell_corner}")
    endif()
  endforeach()
endif()

if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  foreach(storage_corner mos_tt mos_ss mos_ff)
    add_test(NAME analog.1g.ihp_mos_gate_storage.${storage_corner} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/moscap_bias_storage.py" --profile hv_cascode --corner "${storage_corner}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_cascode_storage_${storage_corner}")
    set_tests_properties(analog.1g.ihp_mos_gate_storage.${storage_corner} PROPERTIES
      LABELS "analog;1g;ihp;capacitor;psp" TIMEOUT 300)
  endforeach()
  if(TARGET analog_1g_cascoded_core_transmit_driver)
    foreach(profile nominal ss_hot ff_cold)
      set(storage_corner mos_tt)
      set(storage_temperature 27)
      set(storage_trim 44)
      if(profile STREQUAL "ss_hot")
        set(storage_corner mos_ss)
        set(storage_temperature 125)
        set(storage_trim 54)
      elseif(profile STREQUAL "ff_cold")
        set(storage_corner mos_ff)
        set(storage_temperature -40)
        set(storage_trim 34)
      endif()
      add_test(NAME analog.1g.ihp_mos_storage_transmit.${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_storage_transmit_process.py"
        --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
        --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/cascoded_core_transmit_driver.cpp"
        --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}"
        --corner "${storage_corner}" --temperature "${storage_temperature}" --trim-code "${storage_trim}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_storage_core_transmit_${profile}")
      set_tests_properties(analog.1g.ihp_mos_storage_transmit.${profile} PROPERTIES
        LABELS "analog;1g;ihp;transmitter;psp" TIMEOUT 7200 FIXTURES_REQUIRED one_g_transmit_controls)
      add_test(NAME analog.1g.ihp_mos_storage_interaction.${profile} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_interaction.py" --profile mos_storage
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>"
        --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/../tests/cascoded_core_transmit_driver.cpp"
        --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
        --corner "${storage_corner}" --temperature "${storage_temperature}" --trim-code "${storage_trim}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_storage_interaction_${profile}")
      set_tests_properties(analog.1g.ihp_mos_storage_interaction.${profile} PROPERTIES
        LABELS "analog;1g;ihp;transmitter;psp" TIMEOUT 1800 FIXTURES_REQUIRED one_g_transmit_controls)
    endforeach()
  endif()
endif()
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_mos_gate_storage.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_gate_storage_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_gate_storage_layout")
  set_tests_properties(analog.1g.ihp_mos_gate_storage.layout PROPERTIES
    LABELS "analog;1g;ihp;area;geometry" TIMEOUT 1800
    FIXTURES_SETUP one_g_hv_storage_layout)
  add_test(NAME analog.1g.ihp_mos_gate_storage.adapter
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_gate_storage_adapter.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_gate_storage_adapter")
  set_tests_properties(analog.1g.ihp_mos_gate_storage.adapter PROPERTIES
    LABELS "analog;1g;ihp;area;geometry" TIMEOUT 1800
    FIXTURES_SETUP one_g_hv_storage_adapter)
  if(TARGET analog_1g_ihp_transmit_translation_bias_export)
    add_test(NAME analog.1g.ihp_transmit.translation_bias_mos_layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bias_mos.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translation_bias_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias_mos_layout")
    set_tests_properties(analog.1g.ihp_transmit.translation_bias_mos_layout PROPERTIES
      LABELS "analog;1g;ihp;transmitter;area;geometry" TIMEOUT 1800
      FIXTURES_SETUP one_g_transmit_translation_bias_mos_layout)
  endif()
  set(ihp_storage_ngspice "${NGSPICE_EXECUTABLE}")
  if(NOT ihp_storage_ngspice)
    set(ihp_storage_ngspice "${CPPHDL_ANALOG_REAL_ROOT}/out/veriloga-toolchain/install/bin/ngspice")
  endif()
  if(EXISTS "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}" AND EXISTS "${ihp_storage_ngspice}")
    if(TEST analog.1g.ihp_transmit.translation_bias_mos_layout AND EXISTS "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}")
      add_test(NAME analog.1g.ihp_transmit.translation_bias_mos_extracted
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bias_mos_extracted.py"
          --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias_mos_layout/checks.json"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
          --ngspice "${ihp_storage_ngspice}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias_mos_extracted")
      set_tests_properties(analog.1g.ihp_transmit.translation_bias_mos_extracted PROPERTIES
        LABELS "analog;1g;ihp;transmitter;extraction;psp" TIMEOUT 28800
        FIXTURES_REQUIRED one_g_transmit_translation_bias_mos_layout
        FIXTURES_SETUP one_g_transmit_translation_bias_mos_extracted)
      add_test(NAME analog.1g.ihp_transmit.hierarchical_junction_geometry
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_hierarchy_junction_extraction.py"
          --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias_mos_layout/checks.json"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_hierarchical_junction_geometry")
      set_tests_properties(analog.1g.ihp_transmit.hierarchical_junction_geometry PROPERTIES
        LABELS "analog;1g;ihp;transmitter;geometry;extraction" TIMEOUT 172800
        FIXTURES_REQUIRED one_g_transmit_translation_bias_mos_layout)
      if(TARGET analog_1g_ihp_transmit_translation_bank_export)
        foreach(corner IN ITEMS mos_tt mos_ss mos_ff)
          if(corner STREQUAL "mos_ss")
            set(replica_temperature 125)
          elseif(corner STREQUAL "mos_ff")
            set(replica_temperature -40)
          else()
            set(replica_temperature 27)
          endif()
          add_test(NAME analog.1g.ihp_transmit.translation_bank_mos_storage.${corner}
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bank_mos.py"
              --translator "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_psp_layout/checks.json"
              --replica "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias_mos_extracted/checks.json"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translation_bank_export>"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
              --corner "${corner}" --temperature "${replica_temperature}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bank_mos_storage_${corner}")
          set_tests_properties(analog.1g.ihp_transmit.translation_bank_mos_storage.${corner} PROPERTIES
            LABELS "analog;1g;ihp;transmitter;extraction;psp" TIMEOUT 100000
            FIXTURES_REQUIRED "one_g_transmit_translator_psp_layout;one_g_transmit_translation_bias_mos_extracted")
        endforeach()
      endif()
    endif()
    if(TARGET analog_1g_ihp_transmit_translation_bias_export AND EXISTS "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}")
      add_test(NAME analog.1g.ihp_transmit.translation_bias_guards
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bias_guards.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translation_bias_export>")
      set_tests_properties(analog.1g.ihp_transmit.translation_bias_guards PROPERTIES
        LABELS "analog;1g;ihp;transmitter" TIMEOUT 60)
      add_test(NAME analog.1g.ihp_transmit.translation_bias
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bias.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translation_bias_export>"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
          --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}" --ngspice "${ihp_storage_ngspice}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias")
      set_tests_properties(analog.1g.ihp_transmit.translation_bias PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical;extraction;psp" TIMEOUT 3600
        FIXTURES_SETUP one_g_transmit_translation_bias)
    endif()
    if(TARGET analog_1g_ihp_transmit_translator_export)
      add_test(NAME analog.1g.ihp_transmit.translator_guards
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator_guards.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translator_export>")
      set_tests_properties(analog.1g.ihp_transmit.translator_guards PROPERTIES
        LABELS "analog;1g;ihp;transmitter" TIMEOUT 60)
      add_test(NAME analog.1g.ihp_transmit.translator_layout
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator.py"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
          --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translator_export>"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_layout")
      set_tests_properties(analog.1g.ihp_transmit.translator_layout PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical;extraction" TIMEOUT 1800
        FIXTURES_SETUP one_g_transmit_translator_layout)
      add_test(NAME analog.1g.ihp_transmit.translator_extracted
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator_extracted.py"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
          --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_layout/checks.json"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_extracted")
      set_tests_properties(analog.1g.ihp_transmit.translator_extracted PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical;extraction" TIMEOUT 5400
        FIXTURES_REQUIRED one_g_transmit_translator_layout)
      add_test(NAME analog.1g.ihp_transmit.translator_psp_layout
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator.py"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
          --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}" --storage psp-charge
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translator_export>"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_psp_layout")
      set_tests_properties(analog.1g.ihp_transmit.translator_psp_layout PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical;extraction" TIMEOUT 1800
        FIXTURES_SETUP one_g_transmit_translator_psp_layout)
      if(TEST analog.1g.ihp_transmit.translation_bias)
        add_test(NAME analog.1g.ihp_transmit.translator_replica_guards
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator_replica_guards.py")
        set_tests_properties(analog.1g.ihp_transmit.translator_replica_guards PROPERTIES
          LABELS "analog;1g;ihp;transmitter" TIMEOUT 120)
        add_test(NAME analog.1g.ihp_transmit.translation_bank_guards
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bank_guards.py"
            --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translation_bank_export>"
            --ngspice "${ihp_storage_ngspice}")
        set_tests_properties(analog.1g.ihp_transmit.translation_bank_guards PROPERTIES
          LABELS "analog;1g;ihp;transmitter" TIMEOUT 180)
      endif()
      foreach(translator_corner mos_tt mos_ss mos_ff)
        set(translator_temperature 27)
        if(translator_corner STREQUAL "mos_ss")
          set(translator_temperature 125)
        elseif(translator_corner STREQUAL "mos_ff")
          set(translator_temperature -40)
        endif()
        add_test(NAME analog.1g.ihp_transmit.translator_psp_${translator_corner}
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator_extracted.py"
            --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
            --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_psp_layout/checks.json"
            --corner "${translator_corner}" --temperature "${translator_temperature}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_psp_${translator_corner}")
        set_tests_properties(analog.1g.ihp_transmit.translator_psp_${translator_corner} PROPERTIES
          LABELS "analog;1g;ihp;transmitter;physical;extraction;psp" TIMEOUT 5400
          FIXTURES_REQUIRED one_g_transmit_translator_psp_layout)
        if(TEST analog.1g.ihp_transmit.translation_bias)
          add_test(NAME analog.1g.ihp_transmit.translator_replica_${translator_corner}
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translator_replica.py"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
              --translator "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_psp_layout/checks.json"
              --replica "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias/checks.json"
              --corner "${translator_corner}" --temperature "${translator_temperature}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_replica_${translator_corner}")
          set_tests_properties(analog.1g.ihp_transmit.translator_replica_${translator_corner} PROPERTIES
            LABELS "analog;1g;ihp;transmitter;physical;extraction;psp" TIMEOUT 14400
            FIXTURES_REQUIRED "one_g_transmit_translator_psp_layout;one_g_transmit_translation_bias")
          add_test(NAME analog.1g.ihp_transmit.translation_bank_${translator_corner}
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_translation_bank.py"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_translation_bank_export>"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
              --translator "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translator_psp_layout/checks.json"
              --replica "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bias/checks.json"
              --corner "${translator_corner}" --temperature "${translator_temperature}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_translation_bank_${translator_corner}")
          set_tests_properties(analog.1g.ihp_transmit.translation_bank_${translator_corner} PROPERTIES
            LABELS "analog;1g;ihp;transmitter;physical;extraction;psp" TIMEOUT 86400
            FIXTURES_REQUIRED "one_g_transmit_translator_psp_layout;one_g_transmit_translation_bias")
        endif()
      endforeach()
    endif()
    add_test(NAME analog.1g.ihp_transmit.bias
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_bias.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}" --ngspice "${ihp_storage_ngspice}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_bias")
    set_tests_properties(analog.1g.ihp_transmit.bias PROPERTIES
      LABELS "analog;1g;ihp;transmitter;physical;extraction" TIMEOUT 1800
      FIXTURES_SETUP one_g_transmit_bias)
    if(TARGET analog_1g_ihp_transmit_physical_export)
      add_test(NAME analog.1g.ihp_transmit.bias_binding
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_bias_binding.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
          --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_bias/checks.json")
      set_tests_properties(analog.1g.ihp_transmit.bias_binding PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical" TIMEOUT 60
        FIXTURES_REQUIRED one_g_transmit_bias)
    endif()
    add_test(NAME analog.1g.ihp_transmit.bias_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_bias_guards.py")
    set_tests_properties(analog.1g.ihp_transmit.bias_guards PROPERTIES
      LABELS "analog;1g;ihp;transmitter" TIMEOUT 60)
    add_test(NAME analog.1g.ihp_mos_gate_storage.extracted_charge
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_gate_storage_extraction.py"
        --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_gate_storage_layout/checks.json"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
        --ngspice "${ihp_storage_ngspice}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_gate_storage_extraction")
    set_tests_properties(analog.1g.ihp_mos_gate_storage.extracted_charge PROPERTIES
      LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 1800
      FIXTURES_REQUIRED one_g_hv_storage_layout FIXTURES_SETUP one_g_hv_storage_extraction)
    add_test(NAME analog.1g.ihp_moscap_bias_storage.extracted_charge
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_gate_storage_extraction.py" --profile dedicated
        --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/moscap_layout/checks.json"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_SIDEWALL_MAGIC_EXECUTABLE}"
        --ngspice "${ihp_storage_ngspice}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/moscap_storage_extraction")
    set_tests_properties(analog.1g.ihp_moscap_bias_storage.extracted_charge PROPERTIES
      LABELS "analog;1g;ihp;physical;extraction;transmitter" TIMEOUT 1800
      FIXTURES_REQUIRED one_g_moscap_storage_layout FIXTURES_SETUP one_g_moscap_storage_extraction)
    if(TARGET analog_1g_ihp_transmit_physical_export AND
       TEST analog.1g.ihp_transmit.storage AND TEST analog.1g.ihp_transmit.translator_psp_layout)
      add_test(NAME analog.1g.ihp_transmit.connected_guards
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_connected_guards.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
          --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json")
      set_tests_properties(analog.1g.ihp_transmit.connected_guards PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical" TIMEOUT 60
        FIXTURES_REQUIRED one_g_transmit_controls)
      add_test(NAME analog.1g.ihp_transmit.connected_startup
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_connected_startup.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
          --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
          --proofs "${CMAKE_CURRENT_BINARY_DIR}/tests/results"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_connected_startup")
      set_tests_properties(analog.1g.ihp_transmit.connected_startup PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical;psp" TIMEOUT 172800
        FIXTURES_SETUP one_g_transmit_connected_startup
        FIXTURES_REQUIRED "one_g_transmit_controls;one_g_transmit_translator_psp_layout;one_g_transmit_translation_bias;one_g_transmit_bias;one_g_transmit_current_cell;one_g_transmit_pullup;one_g_transmit_storage;one_g_hv_storage_extraction;one_g_moscap_storage_extraction")
      add_test(NAME analog.1g.ihp_transmit.connected_mos_tt
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_connected.py"
          --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
          --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
          --proofs "${CMAKE_CURRENT_BINARY_DIR}/tests/results"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
          --corner mos_tt --temperature 27 --trim-code 44
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_connected_mos_tt")
      set_tests_properties(analog.1g.ihp_transmit.connected_mos_tt PROPERTIES
        LABELS "analog;1g;ihp;transmitter;physical;extraction;psp" TIMEOUT 172800
        FIXTURES_REQUIRED "one_g_transmit_connected_startup;one_g_transmit_controls;one_g_transmit_translator_psp_layout;one_g_transmit_translation_bias;one_g_transmit_bias;one_g_transmit_current_cell;one_g_transmit_pullup;one_g_transmit_storage;one_g_hv_storage_extraction;one_g_moscap_storage_extraction")
      if(TEST analog.1g.ihp_transmit.charge_switching)
        get_property(charge_connected_fixtures TEST analog.1g.ihp_transmit.connected_startup PROPERTY FIXTURES_REQUIRED)
        list(APPEND charge_connected_fixtures one_g_transmit_charge_storage one_g_transmit_charge_switching)
        foreach(phase IN ITEMS startup mos_tt)
          set(charge_phase_args)
          set(charge_phase_fixtures ${charge_connected_fixtures})
          if(phase STREQUAL "startup")
            list(APPEND charge_phase_args --startup)
          else()
            list(APPEND charge_phase_fixtures one_g_transmit_charge_connected_startup)
          endif()
          add_test(NAME analog.1g.ihp_transmit.charge_connected_${phase}
            COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/transmit_connected_charge.py"
              --exporter "$<TARGET_FILE:analog_1g_ihp_transmit_physical_export>"
              --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json"
              --proofs "${CMAKE_CURRENT_BINARY_DIR}/tests/results"
              --charge-storage "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_charge_storage/checks.json"
              --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/transmit_charge_connected_${phase}"
              ${charge_phase_args})
          set_tests_properties(analog.1g.ihp_transmit.charge_connected_${phase} PROPERTIES
            LABELS "analog;1g;ihp;transmitter;physical;extraction;psp" TIMEOUT 172800
            FIXTURES_REQUIRED "${charge_phase_fixtures}")
        endforeach()
        set_tests_properties(analog.1g.ihp_transmit.charge_connected_startup PROPERTIES
          FIXTURES_SETUP one_g_transmit_charge_connected_startup)
      endif()
    endif()
  endif()
endif()

if(TARGET analog_1g_cascoded_core_transmit_driver)
  add_test(NAME analog.1g.ihp_mos_storage_transmit.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_storage_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_cascoded_core_transmit_driver>"
    --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_controls/checks.json")
  set_tests_properties(analog.1g.ihp_mos_storage_transmit.bindings PROPERTIES
    LABELS "analog;1g;ihp;transmitter" TIMEOUT 120 FIXTURES_REQUIRED one_g_transmit_controls)
endif()

add_executable(analog_1g_ihp_differential_receive_gain tests/differential_receive_gain.cpp)
target_link_libraries(analog_1g_ihp_differential_receive_gain PRIVATE cpphdl::analog)
foreach(check check-1g check-analog check-veriloga)
  add_dependencies(${check} analog_1g_ihp_differential_receive_gain)
endforeach()

set(differential_gain_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/differential_receive_gain")
set(differential_gain_flows cpp)
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE)
  list(APPEND differential_gain_flows veriloga)
endif()
foreach(flow IN LISTS differential_gain_flows)
  set(differential_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND differential_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_differential_gain.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_receive_gain.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_gain>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${differential_gain_work}" --common 1.1 1.4 ${differential_tools})
  set_tests_properties(analog.1g.ihp_differential_gain.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 1800
    FIXTURES_SETUP "one_g_differential_gain_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_differential_gain.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_receive_gain.py" --flow compare --work "${differential_gain_work}")
  set_tests_properties(analog.1g.ihp_differential_gain.compare PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 300
    FIXTURES_REQUIRED "one_g_differential_gain_cpp;one_g_differential_gain_veriloga")
endif()
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  foreach(profile nominal ss_hot ff_cold)
    set(differential_corner mos_tt)
    set(differential_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(differential_corner mos_ss)
      set(differential_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(differential_corner mos_ff)
      set(differential_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_differential_gain.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_receive_gain.py" --flow psp
      --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_gain>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${differential_gain_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${differential_corner}" --temperature "${differential_temperature}" --common 1.1 1.4)
    set_tests_properties(analog.1g.ihp_differential_gain.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 14400 RESOURCE_LOCK one_g_pga_process_memory)
  endforeach()
endif()

if(Eigen3_FOUND)
  foreach(gain 4 8)
    foreach(flow IN LISTS conversion_flows)
      set(tracking_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/charge_balanced_conversion_16_gain${gain}")
      add_test(NAME analog.1g.ihp_charge_balanced_tracking_${gain}.${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/adc_tracking_contract.py"
        --receipt "${tracking_work}/${flow}/checks.json" --output "${tracking_work}/tracking_${flow}.json")
      set_tests_properties(analog.1g.ihp_charge_balanced_tracking_${gain}.${flow} PROPERTIES
        LABELS "analog;1g;ihp;converter" TIMEOUT 30
        FIXTURES_REQUIRED "one_g_charge_balanced_conversion_16_gain${gain}_${flow}")
    endforeach()
  endforeach()
endif()

add_executable(analog_1g_ihp_receive_reference tests/receive_reference.cpp)
target_link_libraries(analog_1g_ihp_receive_reference PRIVATE cpphdl::analog)
foreach(check check-1g check-analog check-veriloga)
  if(TARGET ${check})
    add_dependencies(${check} analog_1g_ihp_receive_reference)
  endif()
endforeach()

set(receive_reference_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_reference")
foreach(flow IN LISTS differential_gain_flows)
  set(reference_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND reference_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_receive_reference.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_receive_reference>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${receive_reference_work}" ${reference_tools})
  set_tests_properties(analog.1g.ihp_receive_reference.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 1800
    FIXTURES_SETUP "one_g_receive_reference_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_receive_reference.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference.py" --flow compare --work "${receive_reference_work}")
  set_tests_properties(analog.1g.ihp_receive_reference.compare PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 300
    FIXTURES_REQUIRED "one_g_receive_reference_cpp;one_g_receive_reference_veriloga")
endif()

add_test(NAME analog.1g.ihp_receive_analog.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_analog_bindings.py"
  --single "$<TARGET_FILE:analog_1g_ihp_programmable_receive_gain>"
  --differential "$<TARGET_FILE:analog_1g_ihp_differential_receive_gain>"
  --reference "$<TARGET_FILE:analog_1g_ihp_receive_reference>")
set_tests_properties(analog.1g.ihp_receive_analog.bindings PROPERTIES LABELS "analog;1g;ihp;receiver" TIMEOUT 60)

if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  foreach(profile nominal ss_hot ff_cold)
    set(reference_corner mos_tt)
    set(reference_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(reference_corner mos_ss)
      set(reference_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(reference_corner mos_ff)
      set(reference_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_receive_reference.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference.py" --flow psp
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_reference>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${receive_reference_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${reference_corner}" --temperature "${reference_temperature}")
    set_tests_properties(analog.1g.ihp_receive_reference.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 1800)
  endforeach()
endif()

add_executable(analog_1g_ihp_differential_preamplifier tests/differential_preamplifier.cpp)
target_link_libraries(analog_1g_ihp_differential_preamplifier PRIVATE cpphdl::analog)
foreach(check check-1g check-analog check-veriloga)
  if(TARGET ${check})
    add_dependencies(${check} analog_1g_ihp_differential_preamplifier)
  endif()
endforeach()

set(differential_preamplifier_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/differential_preamplifier")
foreach(flow IN LISTS differential_gain_flows)
  set(preamplifier_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND preamplifier_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_differential_preamplifier.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_preamplifier.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_differential_preamplifier>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${differential_preamplifier_work}" ${preamplifier_tools})
  set_tests_properties(analog.1g.ihp_differential_preamplifier.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 1800
    FIXTURES_SETUP "one_g_differential_preamplifier_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_differential_preamplifier.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_preamplifier.py" --flow compare --work "${differential_preamplifier_work}")
  set_tests_properties(analog.1g.ihp_differential_preamplifier.compare PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 300
    FIXTURES_REQUIRED "one_g_differential_preamplifier_cpp;one_g_differential_preamplifier_veriloga")
endif()

add_test(NAME analog.1g.ihp_differential_preamplifier.bindings COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_preamplifier_bindings.py"
  --bench "$<TARGET_FILE:analog_1g_ihp_differential_preamplifier>")
set_tests_properties(analog.1g.ihp_differential_preamplifier.bindings PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 60)
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  foreach(profile nominal ss_hot ff_cold)
    set(preamplifier_corner mos_tt)
    set(preamplifier_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(preamplifier_corner mos_ss)
      set(preamplifier_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(preamplifier_corner mos_ff)
      set(preamplifier_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_differential_preamplifier.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_preamplifier.py" --flow psp
      --bench "$<TARGET_FILE:analog_1g_ihp_differential_preamplifier>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${differential_preamplifier_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${preamplifier_corner}" --temperature "${preamplifier_temperature}")
    set_tests_properties(analog.1g.ihp_differential_preamplifier.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 7200 RESOURCE_LOCK one_g_preamplifier_process_memory)
  endforeach()
endif()

if(Eigen3_FOUND)
  add_executable(analog_1g_ihp_differential_receive_conversion tests/differential_receive_conversion.cpp)
  target_link_libraries(analog_1g_ihp_differential_receive_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  foreach(check check-1g check-analog check-veriloga)
    if(TARGET ${check})
      add_dependencies(${check} analog_1g_ihp_differential_receive_conversion)
    endif()
  endforeach()
endif()

add_executable(analog_1g_ihp_adc_range_reference tests/receive_reference.cpp)
target_compile_definitions(analog_1g_ihp_adc_range_reference PRIVATE CPPHDL_ADC_RANGE_REFERENCE)
target_link_libraries(analog_1g_ihp_adc_range_reference PRIVATE cpphdl::analog)
foreach(check check-1g check-analog check-veriloga)
  if(TARGET ${check})
    add_dependencies(${check} analog_1g_ihp_adc_range_reference)
  endif()
endforeach()

add_test(NAME analog.1g.ihp_receive_reference.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference_contract.py")
set_tests_properties(analog.1g.ihp_receive_reference.contract PROPERTIES LABELS "analog;1g;ihp;receiver" TIMEOUT 60)

set(adc_reference_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/adc_range_reference_receiver")
foreach(flow IN LISTS differential_gain_flows)
  set(adc_reference_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND adc_reference_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_adc_range_reference.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference.py" --flow "${flow}"
    --profile adc_range --load-profile receiver --bench "$<TARGET_FILE:analog_1g_ihp_adc_range_reference>"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${adc_reference_work}" ${adc_reference_tools})
  set_tests_properties(analog.1g.ihp_adc_range_reference.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 1800 FIXTURES_SETUP "one_g_adc_range_reference_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_adc_range_reference.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference.py" --flow compare --work "${adc_reference_work}")
  set_tests_properties(analog.1g.ihp_adc_range_reference.compare PROPERTIES LABELS "analog;1g;ihp;receiver" TIMEOUT 300
    FIXTURES_REQUIRED "one_g_adc_range_reference_cpp;one_g_adc_range_reference_veriloga")
endif()
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  foreach(profile nominal ss_hot ff_cold)
    set(receiver_corner mos_tt)
    set(receiver_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(receiver_corner mos_ss)
      set(receiver_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(receiver_corner mos_ff)
      set(receiver_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_adc_range_reference.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_reference.py" --flow psp --profile adc_range --load-profile receiver
      --bench "$<TARGET_FILE:analog_1g_ihp_adc_range_reference>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${adc_reference_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${receiver_corner}" --temperature "${receiver_temperature}")
    set_tests_properties(analog.1g.ihp_adc_range_reference.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 1800 RESOURCE_LOCK one_g_reference_process_memory)
    add_test(NAME analog.1g.ihp_mdi_input_envelope.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/mdi_input_envelope.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_differential_preamplifier>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mdi_input_envelope_${profile}"
      --ngspice "${NGSPICE_EXECUTABLE}" --pdk "${CPPHDL_IHP_PDK_ROOT}"
      --corner "${receiver_corner}" --temperature "${receiver_temperature}")
    set_tests_properties(analog.1g.ihp_mdi_input_envelope.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 7200 RESOURCE_LOCK one_g_preamplifier_process_memory)
  endforeach()
endif()

if(TARGET analog_1g_ihp_differential_receive_conversion)
  set(cold_conversion_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE)
    list(APPEND cold_conversion_flows veriloga)
  endif()
  if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
    list(APPEND cold_conversion_flows psp)
  endif()
  foreach(slices 1 4 13)
    set(cold_bursts 1)
    if(slices EQUAL 13)
      set(cold_bursts 2)
    endif()
    foreach(gain 4 8)
      set(cold_name "analog.1g.ihp_differential_conversion_${slices}_gain${gain}")
      set(cold_fixture "one_g_differential_conversion_${slices}_gain${gain}")
      set(cold_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/balanced_sampler_${slices}_gain${gain}")
      foreach(flow IN LISTS cold_conversion_flows)
        set(cold_tools)
        if(NOT flow STREQUAL "cpp")
          list(APPEND cold_tools --ngspice "${NGSPICE_EXECUTABLE}")
        endif()
        if(flow STREQUAL "veriloga")
          list(APPEND cold_tools --driver "$<TARGET_FILE:cpphdl-veriloga>" --openvaf "${OPENVAF_EXECUTABLE}")
        elseif(flow STREQUAL "psp")
          list(APPEND cold_tools --pdk "${CPPHDL_IHP_PDK_ROOT}")
        endif()
        add_test(NAME "${cold_name}.${flow}" COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_receive_conversion.py" --flow "${flow}"
          --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_conversion>" --cxx "${CMAKE_CXX_COMPILER}"
          --work "${cold_work}" --slices "${slices}" --gain "${gain}" --storage-pf 1.5 --bursts "${cold_bursts}"
          --simulation-timeout 18000 ${cold_tools})
        set_tests_properties("${cold_name}.${flow}" PROPERTIES
          LABELS "analog;1g;ihp;receiver;closed;${flow}" TIMEOUT 20000
          FIXTURES_SETUP "${cold_fixture}_${flow}" RESOURCE_LOCK one_g_closed_adc_memory)
      endforeach()
      if("veriloga" IN_LIST cold_conversion_flows)
        add_test(NAME "${cold_name}.compare" COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_conversion_compare.py" --work "${cold_work}")
        set_tests_properties("${cold_name}.compare" PROPERTIES LABELS "analog;1g;ihp;receiver;closed" TIMEOUT 300
          FIXTURES_REQUIRED "${cold_fixture}_cpp;${cold_fixture}_veriloga")
      endif()
    endforeach()
  endforeach()
endif()

if(TARGET analog_1g_ihp_differential_receive_conversion)
  add_test(NAME analog.1g.ihp_differential_conversion.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_conversion_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_conversion>")
  set_tests_properties(analog.1g.ihp_differential_conversion.bindings PROPERTIES LABELS "analog;1g;ihp;receiver;closed" TIMEOUT 60)
endif()
if(NGSPICE_EXECUTABLE)
  add_test(NAME analog.1g.ihp_differential_conversion.evidence_io COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/sim_adc_controller_io.py" --compact --slices 13
    --ngspice "${NGSPICE_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/controller_io_compact")
  set_tests_properties(analog.1g.ihp_differential_conversion.evidence_io PROPERTIES LABELS "analog;1g;ihp;receiver;closed" TIMEOUT 180)
endif()
if(TARGET analog_1g_adc_bank AND EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
  add_test(NAME analog.1g.ihp_adc_bank.physical COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_bank_physical.py"
    --proof "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/adc_bank_mapped/checks.json"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/compact_adc_bank_physical")
  set_tests_properties(analog.1g.ihp_adc_bank.physical PROPERTIES LABELS "analog;1g;ihp;receiver;physical;area"
    TIMEOUT 18000 RESOURCE_LOCK one_g_heavy_hardware FIXTURES_REQUIRED one_g_adc_bank_mapped)
endif()
if(TEST analog.1g.adc_receive_lane.hardware AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
  add_test(NAME analog.1g.ihp_adc_lane.physical COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_lane_physical.py"
    --proof "${CMAKE_CURRENT_BINARY_DIR}/../tests/hardware/adc_receive_lane/checks.json"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/adc_receive_lane_physical")
  set_tests_properties(analog.1g.ihp_adc_lane.physical PROPERTIES LABELS "analog;1g;ihp;receiver;physical;area"
    TIMEOUT 18000 RESOURCE_LOCK one_g_heavy_hardware FIXTURES_REQUIRED one_g_adc_lane_mapped)
endif()

if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  add_test(NAME analog.1g.ihp_differential_gain.high_common_stress COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_gain_stress.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_gain>" --cxx "${CMAKE_CXX_COMPILER}"
    --ngspice "${NGSPICE_EXECUTABLE}" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/differential_gain_high_common_stress")
  set_tests_properties(analog.1g.ihp_differential_gain.high_common_stress PROPERTIES
    LABELS "analog;1g;ihp;receiver;psp;characterization" TIMEOUT 1800)
endif()

add_test(NAME analog.1g.ihp_differential_conversion.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_conversion_contract.py")
set_tests_properties(analog.1g.ihp_differential_conversion.contract PROPERTIES LABELS "analog;1g;ihp;receiver;closed" TIMEOUT 60)

add_executable(analog_1g_ihp_receive_common_reference tests/receive_common_reference.cpp)
target_link_libraries(analog_1g_ihp_receive_common_reference PRIVATE cpphdl::analog)
add_executable(analog_1g_ihp_lead_receive_common_reference tests/lead_receive_common_reference.cpp)
target_link_libraries(analog_1g_ihp_lead_receive_common_reference PRIVATE cpphdl::analog)
add_executable(analog_1g_ihp_receive_common_layout_export tests/receive_common_layout_export.cpp)
target_link_libraries(analog_1g_ihp_receive_common_layout_export PRIVATE cpphdl::analog)
if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_receive_common_reference.layout_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_layout_guards.py"
      --exporter "$<TARGET_FILE:analog_1g_ihp_receive_common_layout_export>"
      --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}")
  set_tests_properties(analog.1g.ihp_receive_common_reference.layout_guards PROPERTIES
    LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60)
  add_test(NAME analog.1g.ihp_receive_common_reference.extracted_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_extracted_guards.py"
      --exporter "$<TARGET_FILE:analog_1g_ihp_receive_common_layout_export>"
      --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}")
  set_tests_properties(analog.1g.ihp_receive_common_reference.extracted_guards PROPERTIES
    LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60)
  if(EXISTS "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}")
    add_test(NAME analog.1g.ihp_receive_common_reference.layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_layout.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_receive_common_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_layout")
    set_tests_properties(analog.1g.ihp_receive_common_reference.layout PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 3600
      FIXTURES_SETUP one_g_receive_common_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.current_route_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_current_routes.py"
        --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_layout/checks.json")
    set_tests_properties(analog.1g.ihp_receive_common_reference.current_route_guards PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60
      FIXTURES_REQUIRED one_g_receive_common_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.sense_route_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sense_routes.py"
        --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_layout/checks.json")
    set_tests_properties(analog.1g.ihp_receive_common_reference.sense_route_guards PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60
      FIXTURES_REQUIRED one_g_receive_common_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.sense_layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sense_layout.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_receive_common_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_sense_layout")
    set_tests_properties(analog.1g.ihp_receive_common_reference.sense_layout PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 3600
      FIXTURES_SETUP one_g_receive_common_sense_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.upper_current_route_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_upper_current_routes.py"
        --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_layout/checks.json")
    set_tests_properties(analog.1g.ihp_receive_common_reference.upper_current_route_guards PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60
      FIXTURES_REQUIRED one_g_receive_common_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.upper_current_layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_upper_current_layout.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_receive_common_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_layout")
    set_tests_properties(analog.1g.ihp_receive_common_reference.upper_current_layout PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 3600
      FIXTURES_SETUP one_g_receive_common_upper_current_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.storage_route_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_storage_routes.py"
        --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_layout/checks.json")
    set_tests_properties(analog.1g.ihp_receive_common_reference.storage_route_guards PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 120
      FIXTURES_REQUIRED one_g_receive_common_upper_current_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.storage_layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_storage_layout.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_receive_common_layout_export>"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_storage_layout")
    set_tests_properties(analog.1g.ihp_receive_common_reference.storage_layout PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 3600
      FIXTURES_SETUP one_g_receive_common_storage_layout)
    add_test(NAME analog.1g.ihp_receive_common_reference.upper_current_dc_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_upper_current_dc_guards.py")
    set_tests_properties(analog.1g.ihp_receive_common_reference.upper_current_dc_guards PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60)
    if(EXISTS "${ihp_storage_ngspice}")
      add_test(NAME analog.1g.ihp_receive_common_reference.upper_current_emission
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_upper_current_extracted.py"
          --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_layout/checks.json"
          --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
          --corner mos_tt --temperature 27 --cxx "${CMAKE_CXX_COMPILER}" --emit-only
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_emission")
      set_tests_properties(analog.1g.ihp_receive_common_reference.upper_current_emission PROPERTIES
        LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 600
        FIXTURES_REQUIRED one_g_receive_common_upper_current_layout
        FIXTURES_SETUP one_g_receive_common_upper_current_emission)
      add_test(NAME analog.1g.ihp_receive_common_reference.upper_current_dc_screen
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_upper_current_dc.py"
          --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_layout/checks.json"
          --emitted "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_emission/checks.json"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_dc")
      set_tests_properties(analog.1g.ihp_receive_common_reference.upper_current_dc_screen PROPERTIES
        LABELS "analog;1g;ihp;receiver;physical;extraction;psp" TIMEOUT 86400
        FIXTURES_REQUIRED one_g_receive_common_upper_current_emission)
      foreach(receive_corner mos_tt mos_ss mos_ff)
        set(receive_temperature 27)
        if(receive_corner STREQUAL "mos_ss")
          set(receive_temperature 125)
        elseif(receive_corner STREQUAL "mos_ff")
          set(receive_temperature -40)
        endif()
        add_test(NAME analog.1g.ihp_receive_common_reference.extracted_${receive_corner}
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_extracted.py"
            --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_layout/checks.json"
            --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>"
            --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
            --corner "${receive_corner}" --temperature "${receive_temperature}"
            --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_extracted_${receive_corner}")
        set_tests_properties(analog.1g.ihp_receive_common_reference.extracted_${receive_corner} PROPERTIES
          LABELS "analog;1g;ihp;receiver;physical;extraction;psp" TIMEOUT 86400
          FIXTURES_REQUIRED one_g_receive_common_layout)
        add_test(NAME analog.1g.ihp_receive_common_reference.upper_current_extracted_${receive_corner}
          COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_upper_current_extracted.py"
            --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_layout/checks.json"
            --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>"
            --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
            --corner "${receive_corner}" --temperature "${receive_temperature}"
            --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_common_upper_current_extracted_${receive_corner}")
        set_tests_properties(analog.1g.ihp_receive_common_reference.upper_current_extracted_${receive_corner} PROPERTIES
          LABELS "analog;1g;ihp;receiver;physical;extraction;psp" TIMEOUT 86400
          FIXTURES_REQUIRED one_g_receive_common_upper_current_layout)
      endforeach()
    endif()
  endif()
endif()
add_test(NAME analog.1g.ihp_receive_common_reference.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_contract.py")
set_tests_properties(analog.1g.ihp_receive_common_reference.contract PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 120)
add_test(NAME analog.1g.ihp_receive_common_reference.stimulus COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_stimulus_test.py")
set_tests_properties(analog.1g.ihp_receive_common_reference.stimulus PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive_common_reference.target_contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_common_target_test.py")
set_tests_properties(analog.1g.ihp_receive_common_reference.target_contract PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 120)
foreach(check check-1g check-analog check-veriloga)
  if(TARGET ${check})
    add_dependencies(${check} analog_1g_ihp_receive_common_reference)
    add_dependencies(${check} analog_1g_ihp_lead_receive_common_reference)
    add_dependencies(${check} analog_1g_ihp_receive_common_layout_export)
  endif()
endforeach()

set(lead_reference_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/lead_receive_common_reference_regression")
foreach(flow IN LISTS differential_gain_flows)
  set(lead_reference_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND lead_reference_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_lead_receive_common_reference.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/lead_receive_common_reference.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${lead_reference_work}" ${lead_reference_tools})
  set_tests_properties(analog.1g.ihp_lead_receive_common_reference.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 2400 RESOURCE_LOCK one_g_closed_adc_memory
    FIXTURES_SETUP "one_g_lead_reference_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_lead_receive_common_reference.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/lead_receive_common_reference.py" --flow compare --work "${lead_reference_work}")
  set_tests_properties(analog.1g.ihp_lead_receive_common_reference.compare PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 600 RESOURCE_LOCK one_g_closed_adc_memory
    FIXTURES_REQUIRED "one_g_lead_reference_cpp;one_g_lead_reference_veriloga")
endif()
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  add_test(NAME analog.1g.ihp_lead_receive_common_reference.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/lead_receive_common_process_test.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>" --pdk "${CPPHDL_IHP_PDK_ROOT}")
  set_tests_properties(analog.1g.ihp_lead_receive_common_reference.bindings PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 60)
  foreach(profile nominal ss_hot ff_cold)
    set(lead_reference_corner mos_tt)
    set(lead_reference_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(lead_reference_corner mos_ss)
      set(lead_reference_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(lead_reference_corner mos_ff)
      set(lead_reference_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_lead_receive_common_reference.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/lead_receive_common_reference.py" --flow psp
      --bench "$<TARGET_FILE:analog_1g_ihp_lead_receive_common_reference>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${lead_reference_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${lead_reference_corner}" --temperature "${lead_reference_temperature}")
    set_tests_properties(analog.1g.ihp_lead_receive_common_reference.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 2400 RESOURCE_LOCK one_g_closed_adc_memory)
  endforeach()
endif()

add_executable(analog_1g_ihp_core_receive_dac tests/core_receive_dac.cpp)
target_link_libraries(analog_1g_ihp_core_receive_dac PRIVATE cpphdl::analog)
add_executable(analog_1g_ihp_core_receive_decision tests/core_receive_decision.cpp)
target_link_libraries(analog_1g_ihp_core_receive_decision PRIVATE cpphdl::analog)
if(TARGET Eigen3::Eigen)
  target_link_libraries(analog_1g_ihp_core_receive_decision PRIVATE Eigen3::Eigen)
endif()
add_test(NAME analog.1g.ihp_core_receive_decision.timing_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_decision_timing_test.py")
set_tests_properties(analog.1g.ihp_core_receive_decision.timing_guards PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 30)
add_test(NAME analog.1g.ihp_core_receive_decision.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_decision_contract.py")
set_tests_properties(analog.1g.ihp_core_receive_decision.contract PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 30)
set(core_decision_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/core_receive_decision")
foreach(flow IN LISTS differential_gain_flows)
  set(core_decision_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND core_decision_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_core_receive_decision.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_decision.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_core_receive_decision>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${core_decision_work}" ${core_decision_tools})
  set_tests_properties(analog.1g.ihp_core_receive_decision.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 18600 RESOURCE_LOCK one_g_closed_adc_memory
    FIXTURES_SETUP "one_g_core_decision_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_core_receive_decision.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_decision.py" --flow compare --work "${core_decision_work}")
  set_tests_properties(analog.1g.ihp_core_receive_decision.compare PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 1200 RESOURCE_LOCK one_g_closed_adc_memory
    FIXTURES_REQUIRED "one_g_core_decision_cpp;one_g_core_decision_veriloga")
endif()
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  add_test(NAME analog.1g.ihp_core_receive_decision.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_decision_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_core_receive_decision>" --pdk "${CPPHDL_IHP_PDK_ROOT}")
  set_tests_properties(analog.1g.ihp_core_receive_decision.bindings PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 60)
  foreach(profile nominal ss_hot ff_cold)
    set(core_decision_corner mos_tt)
    set(core_decision_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(core_decision_corner mos_ss)
      set(core_decision_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(core_decision_corner mos_ff)
      set(core_decision_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_core_receive_decision.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_decision.py" --flow psp
      --bench "$<TARGET_FILE:analog_1g_ihp_core_receive_decision>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${core_decision_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${core_decision_corner}" --temperature "${core_decision_temperature}")
    set_tests_properties(analog.1g.ihp_core_receive_decision.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 18600 RESOURCE_LOCK one_g_closed_adc_memory)
  endforeach()
endif()
add_test(NAME analog.1g.ihp_core_receive_dac.contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_dac_contract.py")
set_tests_properties(analog.1g.ihp_core_receive_dac.contract PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 30)
add_test(NAME analog.1g.ihp_core_receive_dac.comparison_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_dac_compare_test.py")
set_tests_properties(analog.1g.ihp_core_receive_dac.comparison_guards PROPERTIES
  LABELS "analog;1g;ihp;receiver" TIMEOUT 30)
foreach(check check-1g check-analog check-veriloga)
  if(TARGET ${check})
    add_dependencies(${check} analog_1g_ihp_core_receive_dac analog_1g_ihp_core_receive_decision)
  endif()
endforeach()
set(core_dac_work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/core_receive_dac")
foreach(flow IN LISTS differential_gain_flows)
  set(core_dac_tools)
  if(flow STREQUAL "veriloga")
    list(APPEND core_dac_tools --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  endif()
  add_test(NAME analog.1g.ihp_core_receive_dac.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_dac.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_ihp_core_receive_dac>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${core_dac_work}" ${core_dac_tools})
  set_tests_properties(analog.1g.ihp_core_receive_dac.${flow} PROPERTIES
    LABELS "analog;1g;ihp;receiver;${flow}" TIMEOUT 18600 RESOURCE_LOCK one_g_closed_adc_memory
    FIXTURES_SETUP "one_g_core_dac_${flow}")
endforeach()
if("veriloga" IN_LIST differential_gain_flows)
  add_test(NAME analog.1g.ihp_core_receive_dac.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_dac_compare.py" --work "${core_dac_work}")
  set_tests_properties(analog.1g.ihp_core_receive_dac.compare PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 600 RESOURCE_LOCK one_g_closed_adc_memory
    FIXTURES_REQUIRED "one_g_core_dac_cpp;one_g_core_dac_veriloga")
endif()
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/models/cornerMOShv.lib")
  add_test(NAME analog.1g.ihp_core_receive_dac.bindings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_dac_bindings.py"
    --bench "$<TARGET_FILE:analog_1g_ihp_core_receive_dac>" --pdk "${CPPHDL_IHP_PDK_ROOT}")
  set_tests_properties(analog.1g.ihp_core_receive_dac.bindings PROPERTIES
    LABELS "analog;1g;ihp;receiver" TIMEOUT 60)
  foreach(profile nominal ss_hot ff_cold)
    set(core_dac_corner mos_tt)
    set(core_dac_temperature 27)
    if(profile STREQUAL "ss_hot")
      set(core_dac_corner mos_ss)
      set(core_dac_temperature 125)
    elseif(profile STREQUAL "ff_cold")
      set(core_dac_corner mos_ff)
      set(core_dac_temperature -40)
    endif()
    add_test(NAME analog.1g.ihp_core_receive_dac.${profile} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_receive_dac.py" --flow psp
      --bench "$<TARGET_FILE:analog_1g_ihp_core_receive_dac>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${core_dac_work}_${profile}" --ngspice "${NGSPICE_EXECUTABLE}"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --corner "${core_dac_corner}" --temperature "${core_dac_temperature}")
    set_tests_properties(analog.1g.ihp_core_receive_dac.${profile} PROPERTIES
      LABELS "analog;1g;ihp;receiver;psp" TIMEOUT 18600 RESOURCE_LOCK one_g_closed_adc_memory)
  endforeach()
endif()

if(EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_reference_clock.supply_pin_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_supply_ports.py")
  set_tests_properties(analog.1g.ihp_reference_clock.supply_pin_guards PROPERTIES
    LABELS "analog;1g;ihp;clock;physical" TIMEOUT 60)
endif()

add_test(NAME analog.1g.ihp_reference_clock.buffer_placement_guards
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_clock_buffer_stable_placement.py")
set_tests_properties(analog.1g.ihp_reference_clock.buffer_placement_guards PROPERTIES
  LABELS "analog;1g;ihp;clock;physical" TIMEOUT 60)

if(TARGET analog_1g_ihp_receive_sampler_export AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python")
  add_test(NAME analog.1g.ihp_receive.sampler_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_guards.py"
      --exporter "$<TARGET_FILE:analog_1g_ihp_receive_sampler_export>")
  set_tests_properties(analog.1g.ihp_receive.sampler_guards PROPERTIES
    LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60)
  add_test(NAME analog.1g.ihp_receive.sampler_geometry
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_layout.py"
      --exporter "$<TARGET_FILE:analog_1g_ihp_receive_sampler_export>"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_layout")
  set_tests_properties(analog.1g.ihp_receive.sampler_geometry PROPERTIES
    LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 172800
    FIXTURES_SETUP one_g_receive_sampler_layout)
  add_test(NAME analog.1g.ihp_receive.sampler_check_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_checks.py")
  set_tests_properties(analog.1g.ihp_receive.sampler_check_guards PROPERTIES
    LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60)
  if(TARGET analog_1g_ihp_differential_receive_conversion)
    add_test(NAME analog.1g.ihp_receive.sampler_integration_guards
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_integration.py"
        --exporter "$<TARGET_FILE:analog_1g_ihp_receive_sampler_export>"
        --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_conversion>")
    set_tests_properties(analog.1g.ihp_receive.sampler_integration_guards PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical" TIMEOUT 60)
  endif()
  if(EXISTS "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}" AND EXISTS "${ihp_storage_ngspice}")
    add_test(NAME analog.1g.ihp_receive.sampler_junction_geometry
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_junction_extraction.py"
        --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_layout/checks.json"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_junction_geometry")
    set_tests_properties(analog.1g.ihp_receive.sampler_junction_geometry PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 172800
      FIXTURES_REQUIRED one_g_receive_sampler_layout)
    add_test(NAME analog.1g.ihp_receive.sampler_junction_extracted
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_junction_extracted.py"
        --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_layout/checks.json"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
        --ngspice "${ihp_storage_ngspice}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_junction_extracted")
    set_tests_properties(analog.1g.ihp_receive.sampler_junction_extracted PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 172800
      FIXTURES_REQUIRED one_g_receive_sampler_layout)
    add_test(NAME analog.1g.ihp_receive.sampler_extracted
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_extracted.py"
        --proof "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_layout/checks.json"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --magic "${CPPHDL_IHP_PLL_MAGIC_EXECUTABLE}"
        --ngspice "${ihp_storage_ngspice}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_extracted")
    set_tests_properties(analog.1g.ihp_receive.sampler_extracted PROPERTIES
      LABELS "analog;1g;ihp;receiver;physical;extraction" TIMEOUT 172800
      FIXTURES_REQUIRED one_g_receive_sampler_layout FIXTURES_SETUP one_g_receive_sampler_extracted)
    if(TARGET analog_1g_ihp_differential_receive_conversion)
      add_test(NAME analog.1g.ihp_receive.sampler_conversion
        COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_sampler_conversion.py"
          --layout "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_layout/checks.json"
          --extraction "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_extracted/checks.json"
          --bench "$<TARGET_FILE:analog_1g_ihp_differential_receive_conversion>"
          --pdk "${CPPHDL_IHP_PDK_ROOT}" --ngspice "${ihp_storage_ngspice}"
          --cxx "${CMAKE_CXX_COMPILER}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/receive_sampler_conversion")
      set_tests_properties(analog.1g.ihp_receive.sampler_conversion PROPERTIES
        LABELS "analog;1g;ihp;receiver;physical;closed" TIMEOUT 172800
        FIXTURES_REQUIRED one_g_receive_sampler_extracted)
    endif()
  endif()
endif()
