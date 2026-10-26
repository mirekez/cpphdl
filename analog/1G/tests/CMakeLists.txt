if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "1G independent checks require Python3")
endif()
find_program(ONE_G_VERILATOR verilator)
foreach(contract programmable_transmit_contract transmit_state_contract_test transmit_reuse_contract comparison_revision_test)
  add_test(NAME analog.1g.${contract} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${contract}.py")
  set_tests_properties(analog.1g.${contract} PROPERTIES LABELS "analog;1g;transmitter" TIMEOUT 30)
endforeach()
function(add_one_g_analog_interface interface)
  set(interface_flows cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    list(APPEND interface_flows veriloga)
  endif()
  foreach(flow IN LISTS interface_flows)
    add_test(NAME analog.1g.${interface}.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${interface}.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_${interface}>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/${interface}/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.${interface}.${flow} PROPERTIES
      LABELS "analog;1g;interface;${flow}" TIMEOUT 3600 FIXTURES_SETUP "one_g_${interface}_${flow}")
  endforeach()
  if("veriloga" IN_LIST interface_flows)
    add_test(NAME analog.1g.${interface}.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${interface}.py" --flow compare
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/${interface}")
    set_tests_properties(analog.1g.${interface}.compare PROPERTIES
      LABELS "analog;1g;interface;veriloga" TIMEOUT 120
      FIXTURES_REQUIRED "one_g_${interface}_cpp;one_g_${interface}_veriloga")
  endif()
  add_dependencies(check-analog analog_1g_${interface})
  add_dependencies(check-veriloga analog_1g_${interface})
endfunction()
add_one_g_analog_interface(level_down)
add_one_g_analog_interface(source_follower)
if(TARGET analog_1g_receive_frontend)
  add_one_g_analog_interface(receive_frontend)
  add_test(NAME analog.1g.receive_frontend.contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_frontend_contract.py")
  set_tests_properties(analog.1g.receive_frontend.contract PROPERTIES LABELS "analog;1g;frontend" TIMEOUT 30)
endif()
if(TARGET analog_1g_receive_conversion)
  add_test(NAME analog.1g.receive_conversion.contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_conversion_contract.py")
  set_tests_properties(analog.1g.receive_conversion.contract PROPERTIES LABELS "analog;1g;frontend" TIMEOUT 30)
  foreach(receive_case 1 4 16_repeat)
    set(receive_slices "${receive_case}")
    set(receive_bursts 1)
    if(receive_case STREQUAL "16_repeat")
      set(receive_slices 16)
      set(receive_bursts 2)
    endif()
    set(receive_work "${CMAKE_CURRENT_BINARY_DIR}/results/receive_conversion_${receive_case}")
    set(receive_flows cpp)
    if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
      list(APPEND receive_flows veriloga)
    endif()
    foreach(flow IN LISTS receive_flows)
      add_test(NAME analog.1g.receive_conversion_${receive_case}.${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/receive_conversion.py" --flow "${flow}"
        --bench "$<TARGET_FILE:analog_1g_receive_conversion>" --cxx "${CMAKE_CXX_COMPILER}"
        --slices "${receive_slices}" --bursts "${receive_bursts}" --work "${receive_work}/${flow}"
        --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
      set_tests_properties(analog.1g.receive_conversion_${receive_case}.${flow} PROPERTIES
        LABELS "analog;1g;adc;frontend;${flow}" TIMEOUT 21600 RESOURCE_LOCK one_g_adc_array_memory
        FIXTURES_SETUP "one_g_receive_conversion_${receive_case}_${flow}")
    endforeach()
    if("veriloga" IN_LIST receive_flows)
      add_test(NAME analog.1g.receive_conversion_${receive_case}.compare COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/receive_conversion.py" --flow compare
        --slices "${receive_slices}" --bursts "${receive_bursts}" --work "${receive_work}")
      set_tests_properties(analog.1g.receive_conversion_${receive_case}.compare PROPERTIES
        LABELS "analog;1g;adc;frontend;veriloga" TIMEOUT 120
        FIXTURES_REQUIRED "one_g_receive_conversion_${receive_case}_cpp;one_g_receive_conversion_${receive_case}_veriloga")
    endif()
  endforeach()
  add_dependencies(check-analog analog_1g_receive_conversion)
  add_dependencies(check-veriloga analog_1g_receive_conversion)
endif()
set(reference_clock_flows cpp)
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  list(APPEND reference_clock_flows veriloga)
endif()
foreach(flow IN LISTS reference_clock_flows)
  add_test(NAME analog.1g.reference_clock.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/reference_clock.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_reference_clock>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/reference_clock/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.reference_clock.${flow} PROPERTIES
    LABELS "analog;1g;clock;${flow}" TIMEOUT 7200
    FIXTURES_SETUP "one_g_reference_clock_${flow}")
endforeach()
if("veriloga" IN_LIST reference_clock_flows)
  add_test(NAME analog.1g.reference_clock.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/reference_clock.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/reference_clock")
  set_tests_properties(analog.1g.reference_clock.compare PROPERTIES
    LABELS "analog;1g;clock;veriloga" TIMEOUT 180
    FIXTURES_REQUIRED "one_g_reference_clock_cpp;one_g_reference_clock_veriloga")
endif()
add_dependencies(check-analog analog_1g_reference_clock)
add_dependencies(check-veriloga analog_1g_reference_clock)
add_test(NAME analog.1g.pair_training.generated COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/generate_pair_training.py" --check)
set_tests_properties(analog.1g.pair_training.generated PROPERTIES LABELS "analog;1g;hardware" TIMEOUT 30)
add_test(NAME analog.1g.hardware_runner COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/hardware_runner.py")
set_tests_properties(analog.1g.hardware_runner PROPERTIES LABELS "analog;1g;hardware" TIMEOUT 30)
if(one_g_converter_dependency OR EXISTS "${CPPHDL_1G_CONVERTER}")
  add_test(NAME analog.1g.rtl_catalog COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/rtl_catalog.py")
  set_tests_properties(analog.1g.rtl_catalog PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 30)
  set(catalog_lint_args)
  if(ONE_G_VERILATOR)
    list(APPEND catalog_lint_args --verilator "${ONE_G_VERILATOR}")
  endif()
  add_test(NAME analog.1g.generated_rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../generate_rtl.py" --converter "${one_g_converter_command}"
    --output "${CPPHDL_ANALOG_GENERATED_ROOT}/1G/generated" --check ${catalog_lint_args})
  set_tests_properties(analog.1g.generated_rtl PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 1800)
endif()
add_executable(analog_1g_receive_sample_recovery receive_sample_recovery.cpp)
target_link_libraries(analog_1g_receive_sample_recovery PRIVATE cpphdl::runtime)
add_test(NAME analog.1g.receive_sample_recovery.native COMMAND analog_1g_receive_sample_recovery none)
set_tests_properties(analog.1g.receive_sample_recovery.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 180)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.receive_sample_recovery.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_sample_recovery.py"
    --native "$<TARGET_FILE:analog_1g_receive_sample_recovery>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_sample_recovery")
  set_tests_properties(analog.1g.receive_sample_recovery.rtl PROPERTIES
    LABELS "analog;1g;hardware;rtl" TIMEOUT 3600)
endif()
add_dependencies(check-analog analog_1g_receive_sample_recovery)
foreach(dsp_case receive_dsp receive_dsp_pcs)
  add_executable(analog_1g_${dsp_case} ${dsp_case}.cpp)
  target_link_libraries(analog_1g_${dsp_case} PRIVATE cpphdl::runtime)
  add_dependencies(check-analog analog_1g_${dsp_case})
  set(dsp_vectors "${CMAKE_CURRENT_BINARY_DIR}/${dsp_case}_vectors")
  add_test(NAME analog.1g.${dsp_case}.reference COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${dsp_case}_reference.py" "${dsp_vectors}")
  set_tests_properties(analog.1g.${dsp_case}.reference PROPERTIES
    LABELS "analog;1g;hardware" TIMEOUT 120 FIXTURES_SETUP one_g_${dsp_case}_vectors)
  add_test(NAME analog.1g.${dsp_case}.native COMMAND analog_1g_${dsp_case}
    "${dsp_vectors}" "${CMAKE_CURRENT_BINARY_DIR}/${dsp_case}_native.csv")
  set_tests_properties(analog.1g.${dsp_case}.native PROPERTIES
    LABELS "analog;1g;hardware;cpp" TIMEOUT 300 FIXTURES_REQUIRED one_g_${dsp_case}_vectors)
  if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
    set(dsp_args)
    if(dsp_case STREQUAL "receive_dsp_pcs")
      list(APPEND dsp_args --pcs)
    endif()
    add_test(NAME analog.1g.${dsp_case}.rtl COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/receive_dsp.py" ${dsp_args}
      --native "$<TARGET_FILE:analog_1g_${dsp_case}>"
      --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${dsp_case}")
    set_tests_properties(analog.1g.${dsp_case}.rtl PROPERTIES
      LABELS "analog;1g;hardware;rtl" TIMEOUT 7200 RESOURCE_LOCK one_g_receive_dsp_memory
      FIXTURES_SETUP one_g_${dsp_case}_rtl)
  endif()
endforeach()
foreach(reset_case IN ITEMS phy_reset_release phy_reset_control)
  set(reset_args)
  if(reset_case STREQUAL "phy_reset_control")
    list(APPEND reset_args --control)
  endif()
  add_executable(analog_1g_${reset_case} ${reset_case}.cpp)
  target_link_libraries(analog_1g_${reset_case} PRIVATE cpphdl::runtime)
  add_dependencies(check-analog analog_1g_${reset_case})
  if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR
      AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys")
    add_test(NAME analog.1g.${reset_case}.cells COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/phy_reset_release.py" ${reset_args}
      --native "$<TARGET_FILE:analog_1g_${reset_case}>" --converter "${CPPHDL_1G_CONVERTER}"
      --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
      --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
      --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${reset_case}")
    set_tests_properties(analog.1g.${reset_case}.cells PROPERTIES
      LABELS "analog;1g;hardware;rtl;ihp;clock" TIMEOUT 1200)
  endif()
endforeach()

add_executable(analog_1g_processing_clock_control processing_clock_control.cpp)
target_link_libraries(analog_1g_processing_clock_control PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_processing_clock_control)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.processing_clock_control.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/processing_clock_control.py"
    --native "$<TARGET_FILE:analog_1g_processing_clock_control>" --converter "${CPPHDL_1G_CONVERTER}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/processing_clock_control")
  set_tests_properties(analog.1g.processing_clock_control.rtl PROPERTIES
    LABELS "analog;1g;hardware;rtl;clock" TIMEOUT 1800 RESOURCE_LOCK one_g_eda_memory
    FIXTURES_SETUP one_g_processing_clock_control_rtl)
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad"
      AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
      AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_slow_1p08V_125C.lib")
    add_test(NAME analog.1g.processing_clock_control.cells COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../IHPSG13G2/tests/processing_clock_control_cells.py"
      --proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/processing_clock_control/checks.json"
      --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/processing_clock_control_cells"
      --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
      --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
    set_tests_properties(analog.1g.processing_clock_control.cells PROPERTIES
      LABELS "analog;1g;hardware;ihp;clock;timing" TIMEOUT 1800
      FIXTURES_REQUIRED one_g_processing_clock_control_rtl)
  endif()
endif()

foreach(sampled_case receive_sample_context sampled_receive_phy adc_receive_capture adc_receive_phy reference_clock_monitor clocked_receive_phy asynchronous_receive_phy autonomous_receive_pcs autonomous_receive_phy autonomous_adc_receive_phy qualified_adc_receive_phy)
  add_executable(analog_1g_${sampled_case} ${sampled_case}.cpp)
  target_link_libraries(analog_1g_${sampled_case} PRIVATE cpphdl::runtime)
  add_dependencies(check-analog analog_1g_${sampled_case})
  if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
    set(sampled_mode context)
    if(sampled_case STREQUAL "sampled_receive_phy")
      set(sampled_mode phy)
    elseif(sampled_case STREQUAL "adc_receive_capture")
      set(sampled_mode capture)
    elseif(sampled_case STREQUAL "adc_receive_phy")
      set(sampled_mode adc_phy)
    elseif(sampled_case STREQUAL "reference_clock_monitor")
      set(sampled_mode clock)
    elseif(sampled_case STREQUAL "clocked_receive_phy")
      set(sampled_mode clocked_phy)
    elseif(sampled_case STREQUAL "asynchronous_receive_phy")
      set(sampled_mode async_phy)
    elseif(sampled_case STREQUAL "autonomous_receive_pcs")
      set(sampled_mode autonomous_pcs)
    elseif(sampled_case STREQUAL "qualified_adc_receive_phy")
      set(sampled_mode qualified_adc_phy)
    elseif(sampled_case STREQUAL "autonomous_adc_receive_phy")
      set(sampled_mode autonomous_adc_phy)
    elseif(sampled_case STREQUAL "autonomous_receive_phy")
      set(sampled_mode autonomous_phy)
    endif()
    set(sampled_extra_args)
    if(sampled_case STREQUAL "qualified_adc_receive_phy")
      list(APPEND sampled_extra_args --memory-mib 1024)
    endif()
    add_test(NAME analog.1g.${sampled_case}.rtl COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/sampled_receive_phy.py" --case "${sampled_mode}" ${sampled_extra_args}
      --native "$<TARGET_FILE:analog_1g_${sampled_case}>"
      --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${sampled_case}")
    set_tests_properties(analog.1g.${sampled_case}.rtl PROPERTIES
      LABELS "analog;1g;hardware;rtl;clock" TIMEOUT 7200 RESOURCE_LOCK one_g_receive_dsp_memory)
  endif()
endforeach()
add_test(NAME analog.1g.receive_sample_recovery.bounds COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/receive_sample_recovery_bounds.py")
set_tests_properties(analog.1g.receive_sample_recovery.bounds PROPERTIES
  LABELS "analog;1g;hardware" TIMEOUT 30)
add_executable(analog_1g_receive_sample_pcs receive_sample_pcs.cpp)
target_link_libraries(analog_1g_receive_sample_pcs PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_sample_pcs)
add_test(NAME analog.1g.receive_sample_pcs.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/receive_sample_pcs_reference.py"
  "${CMAKE_CURRENT_BINARY_DIR}/receive_sample_pcs_fixtures")
set_tests_properties(analog.1g.receive_sample_pcs.reference PROPERTIES
  LABELS "analog;1g;hardware" TIMEOUT 60 FIXTURES_SETUP one_g_sample_pcs_vectors)
add_test(NAME analog.1g.receive_sample_pcs.native COMMAND analog_1g_receive_sample_pcs
  "${CMAKE_CURRENT_BINARY_DIR}/receive_sample_pcs_fixtures"
  "${CMAKE_CURRENT_BINARY_DIR}/receive_sample_pcs_native.csv")
set_tests_properties(analog.1g.receive_sample_pcs.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 300 FIXTURES_REQUIRED one_g_sample_pcs_vectors)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.receive_sample_pcs.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_sample_pcs.py"
    --native "$<TARGET_FILE:analog_1g_receive_sample_pcs>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_sample_pcs")
  set_tests_properties(analog.1g.receive_sample_pcs.rtl PROPERTIES
    LABELS "analog;1g;hardware;rtl" TIMEOUT 7200)
endif()
add_executable(analog_1g_receive_quality receive_quality.cpp)
target_link_libraries(analog_1g_receive_quality PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_quality)
add_test(NAME analog.1g.receive_quality.native COMMAND analog_1g_receive_quality
  "${CMAKE_CURRENT_BINARY_DIR}/receive_quality_native.csv")
set_tests_properties(analog.1g.receive_quality.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 60)
add_executable(analog_1g_receive_timing_confidence receive_timing_confidence.cpp)
target_link_libraries(analog_1g_receive_timing_confidence PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_timing_confidence)
add_test(NAME analog.1g.receive_timing_confidence.native COMMAND analog_1g_receive_timing_confidence
  "${CMAKE_CURRENT_BINARY_DIR}/receive_timing_confidence_native.csv")
set_tests_properties(analog.1g.receive_timing_confidence.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 120)
add_executable(analog_1g_receive_symbol_crossing receive_symbol_crossing.cpp)
target_link_libraries(analog_1g_receive_symbol_crossing PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_symbol_crossing)
add_test(NAME analog.1g.receive_symbol_crossing.native COMMAND analog_1g_receive_symbol_crossing
  "${CMAKE_CURRENT_BINARY_DIR}/receive_symbol_crossing_native.csv")
set_tests_properties(analog.1g.receive_symbol_crossing.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 120)
add_executable(analog_1g_receive_dsp_crossing receive_dsp_crossing.cpp)
target_link_libraries(analog_1g_receive_dsp_crossing PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_dsp_crossing)
add_executable(analog_1g_receive_transmit_crossing receive_transmit_crossing.cpp)
target_link_libraries(analog_1g_receive_transmit_crossing PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_transmit_crossing)
foreach(pause_kind RANGE 0 3)
  add_executable(analog_1g_ethernet_pause_${pause_kind} ethernet_pause.cpp)
  target_compile_definitions(analog_1g_ethernet_pause_${pause_kind} PRIVATE PAUSE_KIND=${pause_kind})
  target_link_libraries(analog_1g_ethernet_pause_${pause_kind} PRIVATE cpphdl::runtime)
  add_dependencies(check-analog analog_1g_ethernet_pause_${pause_kind})
endforeach()
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.ethernet_pause.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ethernet_pause.py"
    --native "$<TARGET_FILE:analog_1g_ethernet_pause_0>" "$<TARGET_FILE:analog_1g_ethernet_pause_1>" "$<TARGET_FILE:analog_1g_ethernet_pause_2>" "$<TARGET_FILE:analog_1g_ethernet_pause_3>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/ethernet_pause")
  set_tests_properties(analog.1g.ethernet_pause.rtl PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 2400)
endif()
add_executable(analog_1g_synchronous_frame_fifo synchronous_frame_fifo.cpp)
target_link_libraries(analog_1g_synchronous_frame_fifo PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_synchronous_frame_fifo)
add_executable(analog_1g_flow_controlled_repeater flow_controlled_repeater.cpp)
target_link_libraries(analog_1g_flow_controlled_repeater PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_flow_controlled_repeater)
add_executable(analog_1g_repeater_forwarding repeater_forwarding.cpp)
target_link_libraries(analog_1g_repeater_forwarding PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_repeater_forwarding)
add_executable(analog_1g_autonomous_receive_filter autonomous_receive_filter.cpp)
target_link_libraries(analog_1g_autonomous_receive_filter PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_autonomous_receive_filter)
add_executable(analog_1g_receive_sample_pcs_qualified receive_sample_pcs_qualified.cpp)
target_link_libraries(analog_1g_receive_sample_pcs_qualified PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_sample_pcs_qualified)
add_test(NAME analog.1g.receive_sample_pcs_qualified.native COMMAND analog_1g_receive_sample_pcs_qualified
  "${CMAKE_CURRENT_BINARY_DIR}/receive_sample_pcs_fixtures"
  "${CMAKE_CURRENT_BINARY_DIR}/receive_sample_pcs_qualified_native.csv")
set_tests_properties(analog.1g.receive_sample_pcs_qualified.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 300 FIXTURES_REQUIRED one_g_sample_pcs_vectors)
add_executable(analog_1g_receive_earliest_peer receive_earliest_peer.cpp)
target_link_libraries(analog_1g_receive_earliest_peer PRIVATE cpphdl::runtime)
add_dependencies(check-analog analog_1g_receive_earliest_peer)
add_test(NAME analog.1g.receive_earliest_peer.native COMMAND analog_1g_receive_earliest_peer
  -1500 0.105 "${CMAKE_CURRENT_BINARY_DIR}/receive_earliest_peer_native.csv" 2 1514)
set_tests_properties(analog.1g.receive_earliest_peer.native PROPERTIES
  LABELS "analog;1g;hardware;cpp" TIMEOUT 120)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  foreach(quality_case receive_quality receive_timing_confidence receive_symbol_crossing receive_dsp_crossing receive_transmit_crossing repeater_forwarding flow_controlled_repeater synchronous_frame_fifo autonomous_receive_filter receive_sample_pcs_qualified receive_earliest_peer)
    set(forwarding_options "")
    if(quality_case STREQUAL "repeater_forwarding" OR quality_case STREQUAL "receive_transmit_crossing")
      set(forwarding_options --memory-mib 1536)
    endif()
    add_test(NAME analog.1g.${quality_case}.rtl COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${quality_case}.py"
      --native "$<TARGET_FILE:analog_1g_${quality_case}>"
      --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${quality_case}" ${forwarding_options})
    set_tests_properties(analog.1g.${quality_case}.rtl PROPERTIES
      LABELS "analog;1g;hardware;rtl" TIMEOUT 7200)
  endforeach()
endif()
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.pause_negotiation.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/pause_negotiation.py" --native "$<TARGET_FILE:analog_1g_negotiation>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/pause_negotiation")
  set_tests_properties(analog.1g.pause_negotiation.rtl PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 900)
  add_test(NAME analog.1g.pause_link.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/pause_negotiation.py" --link --native "$<TARGET_FILE:analog_1g_autoneg_link>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/pause_link")
  set_tests_properties(analog.1g.pause_link.rtl PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 3000)
endif()
if(TARGET analog_1g_adc_array)
  foreach(array_case 1 4 16 16_repeat)
    set(slices "${array_case}")
    set(bursts 1)
    if(array_case STREQUAL "16_repeat")
      set(slices 16)
      set(bursts 2)
    endif()
    set(array_work "${CMAKE_CURRENT_BINARY_DIR}/results/adc_array_${array_case}")
    set(array_flows cpp)
    if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
      list(APPEND array_flows veriloga)
    endif()
    set(array_fixtures "")
    foreach(flow IN LISTS array_flows)
      add_test(NAME analog.1g.adc_array_${array_case}.${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/adc_array.py"
        --bench "$<TARGET_FILE:analog_1g_adc_array>" --work "${array_work}/${flow}"
        --flow "${flow}" --slices "${slices}" --bursts "${bursts}" --cxx "${CMAKE_CXX_COMPILER}"
        --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
      set_tests_properties(analog.1g.adc_array_${array_case}.${flow} PROPERTIES
        LABELS "analog;1g;adc;${flow}" TIMEOUT 21600
        RESOURCE_LOCK one_g_adc_array_memory
        FIXTURES_SETUP "one_g_adc_array_${array_case}_${flow}")
      list(APPEND array_fixtures "one_g_adc_array_${array_case}_${flow}")
    endforeach()
    if("veriloga" IN_LIST array_flows)
      add_test(NAME analog.1g.adc_array_${array_case}.compare COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/adc_array.py" --work "${array_work}" --flow compare --slices "${slices}" --bursts "${bursts}")
      set_tests_properties(analog.1g.adc_array_${array_case}.compare PROPERTIES
        LABELS "analog;1g;adc;veriloga" TIMEOUT 120 FIXTURES_REQUIRED "${array_fixtures}")
    endif()
  endforeach()
  add_dependencies(check-analog analog_1g_adc_array)
  add_dependencies(check-veriloga analog_1g_adc_array)
endif()
set(one_g_hardware_args "")
if(TARGET analog_1g_adc_core)
  foreach(core_case 1 4 16_repeat)
    set(core_slices "${core_case}")
    set(core_bursts 1)
    if(core_case STREQUAL "16_repeat")
      set(core_slices 16)
      set(core_bursts 2)
    endif()
    set(core_work "${CMAKE_CURRENT_BINARY_DIR}/results/adc_core_${core_case}")
    set(core_flows cpp)
    if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
      list(APPEND core_flows veriloga)
    endif()
    foreach(flow IN LISTS core_flows)
      add_test(NAME analog.1g.adc_core_${core_case}.${flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/adc_core.py" --flow "${flow}"
        --bench "$<TARGET_FILE:analog_1g_adc_core>" --cxx "${CMAKE_CXX_COMPILER}"
        --slices "${core_slices}" --bursts "${core_bursts}" --work "${core_work}/${flow}"
        --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
      set_tests_properties(analog.1g.adc_core_${core_case}.${flow} PROPERTIES
        LABELS "analog;1g;adc;interface;${flow}" TIMEOUT 21600 RESOURCE_LOCK one_g_adc_array_memory
        FIXTURES_SETUP "one_g_adc_core_${core_case}_${flow}")
    endforeach()
    if("veriloga" IN_LIST core_flows)
      add_test(NAME analog.1g.adc_core_${core_case}.compare COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/adc_core.py" --flow compare --slices "${core_slices}"
        --bursts "${core_bursts}" --work "${core_work}")
      set_tests_properties(analog.1g.adc_core_${core_case}.compare PROPERTIES
        LABELS "analog;1g;adc;interface;veriloga" TIMEOUT 120
        FIXTURES_REQUIRED "one_g_adc_core_${core_case}_cpp;one_g_adc_core_${core_case}_veriloga")
    endif()
  endforeach()
  add_dependencies(check-analog analog_1g_adc_core)
  add_dependencies(check-veriloga analog_1g_adc_core)
endif()
if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
  list(APPEND one_g_hardware_args --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
    --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
endif()
if(one_g_hardware_args AND EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  foreach(dsp_case receive_dsp receive_dsp_pcs)
    set(dsp_args)
    set(dsp_mapping receive_dsp_mapping.py)
    if(dsp_case STREQUAL "receive_dsp_pcs")
      list(APPEND dsp_args --pcs)
      set(dsp_mapping receive_hierarchy_mapping.py)
    endif()
    add_test(NAME analog.1g.${dsp_case}.mapped COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${dsp_mapping}" ${dsp_args} ${one_g_hardware_args}
      --functional "${CMAKE_CURRENT_BINARY_DIR}/hardware/${dsp_case}/checks.json"
      --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${dsp_case}_mapped")
    set_tests_properties(analog.1g.${dsp_case}.mapped PROPERTIES
      LABELS "analog;1g;hardware;ihp" TIMEOUT 7200 RESOURCE_LOCK one_g_receive_dsp_memory
      FIXTURES_REQUIRED one_g_${dsp_case}_rtl FIXTURES_SETUP one_g_${dsp_case}_mapped)
    if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
      add_test(NAME analog.1g.${dsp_case}.timing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/receive_dsp_timing.py" ${one_g_hardware_args}
        --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${dsp_case}_mapped")
      set_tests_properties(analog.1g.${dsp_case}.timing PROPERTIES
        LABELS "analog;1g;hardware;timing" TIMEOUT 360 RESOURCE_LOCK one_g_receive_dsp_memory
        FIXTURES_REQUIRED one_g_${dsp_case}_mapped)
    endif()
  endforeach()
endif()
add_test(NAME analog.1g.transmit.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/transmit_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/transmit-vectors.txt")
set_tests_properties(analog.1g.transmit.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_transmit_vectors TIMEOUT 60)
add_test(NAME analog.1g.filter.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/filter_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/filter-vectors.txt")
set_tests_properties(analog.1g.filter.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_filter_vectors TIMEOUT 60)
add_test(NAME analog.1g.filter_pipeline.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/filter_pipeline_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/filter-pipeline-vectors.txt")
set_tests_properties(analog.1g.filter_pipeline.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_filter_pipeline_vectors TIMEOUT 60)
add_test(NAME analog.1g.timing.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/timing_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/timing-vectors.txt")
set_tests_properties(analog.1g.timing.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_timing_vectors TIMEOUT 30)
add_test(NAME analog.1g.synchronize.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/synchronize_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/synchronize-vectors.txt")
set_tests_properties(analog.1g.synchronize.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_synchronize_vectors TIMEOUT 60)
add_test(NAME analog.1g.receiver.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/receiver_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/receiver-vectors.txt")
set_tests_properties(analog.1g.receiver.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_receiver_vectors TIMEOUT 60)
add_test(NAME analog.1g.receiver_faults.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/receiver_faults_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/receiver-fault-vectors.txt")
set_tests_properties(analog.1g.receiver_faults.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_receiver_fault_vectors TIMEOUT 60)
add_test(NAME analog.1g.receiver_faults.native COMMAND analog_1g_receiver --faults "${CMAKE_CURRENT_BINARY_DIR}/receiver-fault-vectors.txt")
set_tests_properties(analog.1g.receiver_faults.native PROPERTIES LABELS "analog;1g;hardware;cpp"
  FIXTURES_REQUIRED one_g_receiver_fault_vectors TIMEOUT 30)
add_test(NAME analog.1g.receiver_enable.native COMMAND analog_1g_receiver --enable "${CMAKE_CURRENT_BINARY_DIR}/receiver-vectors.txt")
set_tests_properties(analog.1g.receiver_enable.native PROPERTIES LABELS "analog;1g;hardware;cpp"
  FIXTURES_REQUIRED one_g_receiver_vectors TIMEOUT 60)
add_test(NAME analog.1g.framer.reference COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/framer_reference.py" "${CMAKE_CURRENT_BINARY_DIR}/framer-vectors.txt")
set_tests_properties(analog.1g.framer.reference PROPERTIES LABELS "analog;1g;hardware"
  FIXTURES_SETUP one_g_framer_vectors TIMEOUT 60)
foreach(block alignment pcs scrambler transmit_bits transmitter branch_metrics branch_metrics_pipeline trellis synchronize receiver framer filter filter_pipeline filter_datapath timing adc_sar flp_tx flp_rx master_slave negotiation autoneg_link pma_control pma_endpoint pma_aligned adc_interleave adc_receive pair_acquisition normalizer receive_calibration pam5_driver)
  if(block STREQUAL "filter_pipeline")
    add_executable(analog_1g_${block} filter.cpp)
    target_compile_definitions(analog_1g_${block} PRIVATE PIPELINED_FILTER)
  elseif(block STREQUAL "pma_aligned")
    add_executable(analog_1g_${block} pma_endpoint.cpp)
    target_compile_definitions(analog_1g_${block} PRIVATE ALIGNED_PMA)
  elseif(block STREQUAL "branch_metrics_pipeline")
    add_executable(analog_1g_${block} branch_metrics.cpp)
    target_compile_definitions(analog_1g_${block} PRIVATE PIPELINED_METRICS)
  else()
    add_executable(analog_1g_${block} "${block}.cpp")
  endif()
  target_link_libraries(analog_1g_${block} PRIVATE cpphdl::runtime)
  if(block STREQUAL "pcs" OR block MATCHES "^branch_metrics" OR block STREQUAL "trellis")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_SOURCE_DIR}")
  elseif(block MATCHES "^transmit")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/transmit-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_transmit_vectors)
  elseif(block STREQUAL "filter")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/filter-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_filter_vectors)
  elseif(block MATCHES "^(filter_pipeline|filter_datapath)$")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/filter-pipeline-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_filter_pipeline_vectors)
  elseif(block STREQUAL "timing")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/timing-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_timing_vectors)
  elseif(block STREQUAL "synchronize")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/synchronize-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_synchronize_vectors)
  elseif(block STREQUAL "receiver")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/receiver-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_receiver_vectors)
  elseif(block STREQUAL "framer")
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block} "${CMAKE_CURRENT_BINARY_DIR}/framer-vectors.txt")
    set_tests_properties(analog.1g.${block}.native PROPERTIES FIXTURES_REQUIRED one_g_framer_vectors)
  else()
    add_test(NAME analog.1g.${block}.native COMMAND analog_1g_${block})
  endif()
  set_tests_properties(analog.1g.${block}.native PROPERTIES LABELS "analog;1g;hardware;cpp" TIMEOUT 30)
  if(block STREQUAL "autoneg_link")
    set_tests_properties(analog.1g.${block}.native PROPERTIES TIMEOUT 600)
  endif()
  if(ONE_G_VERILATOR AND EXISTS "${CPPHDL_1G_CONVERTER}")
    set(hardware_case "${block}")
    if(block MATCHES "^(pma_control|pma_endpoint|pma_aligned|autoneg_link|adc_interleave|adc_receive|pair_acquisition|normalizer|receive_calibration|pam5_driver)$")
      set(hardware_runner composed_hardware.py)
      if(block STREQUAL "autoneg_link")
        set(hardware_case autoneg_link_complete)
      endif()
    else()
      set(hardware_runner hardware.py)
    endif()
    set(block_hardware_args ${one_g_hardware_args})
    if(block STREQUAL "receiver")
      set(block_hardware_args)
    endif()
    add_test(NAME analog.1g.${block}.rtl COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${hardware_runner}" --case "${block}" --converter "${CPPHDL_1G_CONVERTER}"
      --native "$<TARGET_FILE:analog_1g_${block}>"
      --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
      ${block_hardware_args}
      --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${hardware_case}")
    set_tests_properties(analog.1g.${block}.rtl PROPERTIES LABELS "analog;1g;hardware;rtl" TIMEOUT 300)
    if(block MATCHES "^(pma_control|pma_endpoint|pma_aligned|adc_interleave|adc_receive|pair_acquisition|normalizer|receive_calibration|pam5_driver)$")
      set_tests_properties(analog.1g.${block}.rtl PROPERTIES TIMEOUT 21600 RESOURCE_LOCK one_g_heavy_hardware)
    endif()
    if(block MATCHES "^(trellis|receiver|filter|filter_pipeline|filter_datapath|branch_metrics_pipeline)$")
      set_tests_properties(analog.1g.${block}.rtl PROPERTIES TIMEOUT 1200 RESOURCE_LOCK one_g_heavy_hardware)
    endif()
    if(block MATCHES "^filter")
      set_tests_properties(analog.1g.${block}.rtl PROPERTIES TIMEOUT 2400 RESOURCE_LOCK one_g_heavy_hardware)
    endif()
    if(block STREQUAL "autoneg_link")
      set_tests_properties(analog.1g.${block}.rtl PROPERTIES TIMEOUT 21600 RESOURCE_LOCK one_g_heavy_hardware)
    endif()
    if(block STREQUAL "receiver" AND one_g_hardware_args)
      set_tests_properties(analog.1g.receiver.rtl PROPERTIES FIXTURES_SETUP one_g_receiver_rtl)
      add_test(NAME analog.1g.receiver.mapped COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/receiver_mapped.py" ${one_g_hardware_args}
        --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/receiver")
      set_tests_properties(analog.1g.receiver.mapped PROPERTIES
        LABELS "analog;1g;hardware;mapped" TIMEOUT 7200 RESOURCE_LOCK one_g_heavy_hardware
        FIXTURES_REQUIRED one_g_receiver_rtl FIXTURES_SETUP one_g_receiver_mapped)
    endif()
    if(block MATCHES "^(trellis|branch_metrics_pipeline|adc_sar|filter_datapath|receiver|flp_tx|flp_rx|master_slave|negotiation|autoneg_link|pma_control|pma_endpoint|pma_aligned|adc_interleave|adc_receive|pair_acquisition|normalizer|receive_calibration|pam5_driver)$" AND one_g_hardware_args
        AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
      if(block STREQUAL "trellis")
        set(cell_module PCSTrellisDecoder)
        set(cell_period 8)
      elseif(block STREQUAL "branch_metrics_pipeline")
        set(cell_module PipelinedPCSBranchMetrics)
        set(cell_period 8)
      elseif(block STREQUAL "filter_datapath")
        set(cell_module ReceiveFilter)
        set(cell_period 8)
      elseif(block STREQUAL "flp_tx")
        set(cell_module FLPTransmitter)
        set(cell_period 8)
      elseif(block STREQUAL "flp_rx")
        set(cell_module FLPReceiver)
        set(cell_period 8)
      elseif(block STREQUAL "master_slave")
        set(cell_module MasterSlaveResolver)
        set(cell_period 8)
      elseif(block STREQUAL "negotiation")
        set(cell_module Autonegotiator)
        set(cell_period 8)
      elseif(block STREQUAL "autoneg_link")
        set(cell_module Autonegotiation)
        set(cell_period 8)
      elseif(block STREQUAL "pma_control")
        set(cell_module PMAControl)
        set(cell_period 8)
      elseif(block STREQUAL "pma_endpoint")
        set(cell_module PMAEndpoint)
        set(cell_period 8)
      elseif(block STREQUAL "pma_aligned")
        set(cell_module AlignedPMAEndpoint)
        set(cell_period 8)
      elseif(block STREQUAL "adc_interleave")
        set(cell_module InterleavedSARController)
        set(cell_period 4)
      elseif(block STREQUAL "adc_receive")
        set(cell_module ADCReceiveController)
        set(cell_period 4)
      elseif(block STREQUAL "normalizer")
        set(cell_module ReceiveSampleNormalizer)
        set(cell_period 4)
      elseif(block STREQUAL "receive_calibration")
        set(cell_module ReceiveGainCalibration)
        set(cell_period 8)
      elseif(block STREQUAL "pam5_driver")
        set(cell_module PAM5DriverControl)
        set(cell_period 8)
      elseif(block STREQUAL "pair_acquisition")
        set(cell_module ReceivePairAcquirer)
        set(cell_period 8)
      elseif(block STREQUAL "receiver")
        set(cell_module PCSReceiver)
        # The autonomous receive path crosses from the ADC into its separately
        # qualified processing clock before DSP/PCS. Verify its upper bound.
        set(cell_period 7.352941176)
      else()
        set(cell_module ADCSARController)
        set(cell_period 4)
      endif()
      if(NOT block STREQUAL "receiver")
        set_tests_properties(analog.1g.${block}.rtl PROPERTIES FIXTURES_SETUP one_g_${block}_mapped)
      endif()
      add_test(NAME analog.1g.${block}.cells COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cell_timing.py" --module "${cell_module}" --period "${cell_period}"
        --repair --require-functional --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${hardware_case}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
      set_tests_properties(analog.1g.${block}.cells PROPERTIES LABELS "analog;1g;hardware;timing"
        TIMEOUT 180 FIXTURES_REQUIRED one_g_${block}_mapped)
    endif()
    if(block STREQUAL "filter_pipeline" AND one_g_hardware_args
        AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
      set_tests_properties(analog.1g.${block}.rtl PROPERTIES FIXTURES_SETUP one_g_pipeline_mapped)
      add_test(NAME analog.1g.filter_pipeline.timing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cell_timing.py" --module PipelinedReceiveFilter --repair --require-functional
        --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/filter_pipeline"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
      set_tests_properties(analog.1g.filter_pipeline.timing PROPERTIES LABELS "analog;1g;hardware;timing"
        TIMEOUT 180 FIXTURES_REQUIRED one_g_pipeline_mapped)
    endif()
    if(block STREQUAL "timing" AND one_g_hardware_args
        AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
      set_tests_properties(analog.1g.timing.rtl PROPERTIES FIXTURES_SETUP one_g_timing_mapped)
      add_test(NAME analog.1g.timing.cells COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cell_timing.py" --module ReceiveTimingLoop --repair --require-functional
        --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/timing"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
      set_tests_properties(analog.1g.timing.cells PROPERTIES LABELS "analog;1g;hardware;timing"
        TIMEOUT 180 FIXTURES_REQUIRED one_g_timing_mapped)
    endif()
  endif()
endforeach()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.flp_frontend.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/flp_frontend.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_flp_frontend>" --cxx "${CMAKE_CXX_COMPILER}"
    --decoder "$<TARGET_FILE:analog_1g_flp_frontend_receiver>"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/flp_frontend/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.flp_frontend.${flow} PROPERTIES LABELS "analog;1g;${flow}"
    TIMEOUT 600 FIXTURES_SETUP one_g_flp_frontend_${flow})
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.flp_frontend.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/flp_frontend.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/flp_frontend")
  set_tests_properties(analog.1g.flp_frontend.compare PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 30
    FIXTURES_REQUIRED "one_g_flp_frontend_cpp;one_g_flp_frontend_veriloga")
endif()
foreach(profile pam center)
if(profile STREQUAL "pam")
  set(adc_case adc_cosim)
else()
  set(adc_case adc_zero)
endif()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.${adc_case}.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_cosim.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_adc_analog>" --cxx "${CMAKE_CXX_COMPILER}"
    --profile "${profile}" --work "${CMAKE_CURRENT_BINARY_DIR}/results/${adc_case}/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.${adc_case}.${flow} PROPERTIES
    LABELS "analog;1g;${flow}" TIMEOUT 7200 FIXTURES_SETUP one_g_${adc_case}_${flow}
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1")
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.${adc_case}.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_cosim.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/${adc_case}/cpp"
    --other "${CMAKE_CURRENT_BINARY_DIR}/results/${adc_case}/veriloga")
  set_tests_properties(analog.1g.${adc_case}.compare PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 30
    FIXTURES_REQUIRED "one_g_${adc_case}_cpp;one_g_${adc_case}_veriloga")
endif()
endforeach()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.clock_oscillator.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/clock_oscillator.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_clock_oscillator>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/clock_oscillator/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.clock_oscillator.${flow} PROPERTIES
    LABELS "analog;1g;${flow}" TIMEOUT 3600 FIXTURES_SETUP one_g_clock_oscillator_${flow}
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1")
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.clock_oscillator.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/clock_oscillator.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/clock_oscillator/cpp"
    --other "${CMAKE_CURRENT_BINARY_DIR}/results/clock_oscillator/veriloga")
  set_tests_properties(analog.1g.clock_oscillator.compare PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 30
    FIXTURES_REQUIRED "one_g_clock_oscillator_cpp;one_g_clock_oscillator_veriloga")
endif()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.level_shifter.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/level_shifter.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_level_shifter>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/level_shifter/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.level_shifter.${flow} PROPERTIES
    LABELS "analog;1g;${flow}" TIMEOUT 900 FIXTURES_SETUP one_g_level_shifter_${flow}
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1")
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.level_shifter.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/level_shifter.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/level_shifter/cpp"
    --other "${CMAKE_CURRENT_BINARY_DIR}/results/level_shifter/veriloga")
  set_tests_properties(analog.1g.level_shifter.compare PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 30
    FIXTURES_REQUIRED "one_g_level_shifter_cpp;one_g_level_shifter_veriloga")
endif()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.level_shifter_250.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/level_shifter.py" --flow "${flow}" --clock-mhz 250
    --bench "$<TARGET_FILE:analog_1g_level_shifter>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/level_shifter_250/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.level_shifter_250.${flow} PROPERTIES
    LABELS "analog;1g;clock;${flow}" TIMEOUT 1800 FIXTURES_SETUP one_g_level_shifter_250_${flow})
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.level_shifter_250.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/level_shifter.py" --flow compare --clock-mhz 250
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/level_shifter_250/cpp"
    --other "${CMAKE_CURRENT_BINARY_DIR}/results/level_shifter_250/veriloga")
  set_tests_properties(analog.1g.level_shifter_250.compare PROPERTIES LABELS "analog;1g;clock;veriloga"
    TIMEOUT 120 FIXTURES_REQUIRED "one_g_level_shifter_250_cpp;one_g_level_shifter_250_veriloga")
endif()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.clock_level_shifter_250.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/level_shifter.py" --flow "${flow}" --clock-mhz 250 --cascoded
    --bench "$<TARGET_FILE:analog_1g_level_shifter>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/clock_level_shifter_250/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.clock_level_shifter_250.${flow} PROPERTIES
    LABELS "analog;1g;clock;${flow}" TIMEOUT 7200 FIXTURES_SETUP one_g_clock_level_shifter_250_${flow})
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.clock_level_shifter_250.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/level_shifter.py" --flow compare --clock-mhz 250 --cascoded
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/clock_level_shifter_250/cpp"
    --other "${CMAKE_CURRENT_BINARY_DIR}/results/clock_level_shifter_250/veriloga")
  set_tests_properties(analog.1g.clock_level_shifter_250.compare PROPERTIES LABELS "analog;1g;clock;veriloga"
    TIMEOUT 120 FIXTURES_REQUIRED "one_g_clock_level_shifter_250_cpp;one_g_clock_level_shifter_250_veriloga")
endif()
set(one_g_cases dac dac_loaded r2r_dac r2r_dac_loaded driver driver_loaded mos_driver biased_driver trimmed_driver strongarm strongarm_loaded
  mos_track_hold mos_track_hold_loaded
  passive_hybrid passive_hybrid_loaded mos_amplifier mos_amplifier_loaded memory pulse_shaper slicer
  hybrid hybrid_bad channel channel_loaded channel_reverse ctle_low ctle_nyquist
  dfe dfe_bypass transmitter receiver pair_link link link_loaded link_noisy link_uncancelled link_pcs
  pair_link_refined link_refined strongarm_refined strongarm_loaded_refined mos_track_hold_refined mos_track_hold_loaded_refined)
foreach(circuit IN LISTS one_g_cases)
  string(REGEX REPLACE "_refined$" "" bench_case "${circuit}")
  if(circuit MATCHES "_refined$")
    set(refinement 2)
  else()
    set(refinement 1)
  endif()
  set(work "${CMAKE_CURRENT_BINARY_DIR}/results/${circuit}")
  add_test(NAME analog.1g.${circuit}.cpp COMMAND "${CMAKE_COMMAND}"
    "-DBENCH=$<TARGET_FILE:analog_1g_bench>" "-DDSP=$<TARGET_FILE:analog_1g_process>" "-DPCS=$<TARGET_FILE:analog_1g_pcs_waveform>"
    "-DFIXED_DSP=$<TARGET_FILE:analog_1g_process_fixed>"
    "-DCASE=${bench_case}" "-DREFINEMENT=${refinement}" "-DFLOW=cpp" "-DWORK=${work}/cpp" "-DPYTHON=${Python3_EXECUTABLE}"
    "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/verify.py" -P "${CMAKE_CURRENT_SOURCE_DIR}/RunFlow.cmake")
  set_tests_properties(analog.1g.${circuit}.cpp PROPERTIES LABELS "analog;1g;cpp" TIMEOUT 900
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1" FIXTURES_SETUP one_g_${circuit}_cpp)
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.1g.${circuit}.veriloga COMMAND "${CMAKE_COMMAND}"
      "-DBENCH=$<TARGET_FILE:analog_1g_bench>" "-DDSP=$<TARGET_FILE:analog_1g_process>" "-DPCS=$<TARGET_FILE:analog_1g_pcs_waveform>"
      "-DFIXED_DSP=$<TARGET_FILE:analog_1g_process_fixed>"
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
  foreach(circuit pair_link link strongarm strongarm_loaded mos_track_hold mos_track_hold_loaded)
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
  "-DCATALOG=${CPPHDL_ANALOG_GENERATED_ROOT}/1G"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/Generated.cmake")
set_tests_properties(analog.1g.generated PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 60)
if(OPENVAF_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.generated_compile COMMAND "${CMAKE_COMMAND}"
    "-DGENERATOR=$<TARGET_FILE:analog_1g_generate>" "-DROOT=${CPPHDL_ANALOG_PACKAGE_ROOT}"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generated-compile"
    "-DCATALOG=${CPPHDL_ANALOG_GENERATED_ROOT}/1G"
    "-DDRIVER=$<TARGET_FILE:cpphdl-veriloga>"
    "-DOPENVAF=${OPENVAF_EXECUTABLE}" "-DPYTHON=${Python3_EXECUTABLE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Generated.cmake")
  set_tests_properties(analog.1g.generated_compile PROPERTIES LABELS "analog;1g;veriloga" TIMEOUT 3600 RUN_SERIAL TRUE)
endif()
add_custom_target(check-1g
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -L 1g --output-on-failure -j 2
  DEPENDS analog_1g_bench analog_1g_generate analog_1g_process analog_1g_process_fixed analog_1g_algorithms analog_1g_pcs_waveform
    analog_1g_alignment analog_1g_pcs analog_1g_scrambler analog_1g_transmit_bits analog_1g_transmitter
    analog_1g_branch_metrics analog_1g_branch_metrics_pipeline analog_1g_trellis analog_1g_synchronize analog_1g_receiver analog_1g_framer
    analog_1g_receive_dsp analog_1g_receive_dsp_pcs analog_1g_receive_sample_context analog_1g_sampled_receive_phy analog_1g_adc_receive_capture analog_1g_adc_receive_phy analog_1g_reference_clock_monitor analog_1g_clocked_receive_phy analog_1g_asynchronous_receive_phy analog_1g_autonomous_receive_pcs analog_1g_autonomous_receive_phy analog_1g_autonomous_adc_receive_phy analog_1g_processing_clock_control
    analog_1g_filter analog_1g_filter_pipeline analog_1g_filter_datapath analog_1g_timing analog_1g_adc_sar
    analog_1g_adc_analog analog_1g_clock_oscillator analog_1g_reference_clock analog_1g_level_shifter analog_1g_flp_frontend analog_1g_flp_frontend_receiver
    analog_1g_flp_tx analog_1g_flp_rx analog_1g_master_slave analog_1g_negotiation analog_1g_autoneg_link
    analog_1g_pma_control analog_1g_pma_endpoint analog_1g_pma_aligned analog_1g_adc_interleave analog_1g_adc_receive analog_1g_pair_acquisition analog_1g_normalizer analog_1g_receive_calibration analog_1g_pam5_driver USES_TERMINAL)
add_dependencies(check-1g
  analog_1g_phy_reset_release analog_1g_phy_reset_control
  analog_1g_receive_sample_recovery analog_1g_receive_sample_pcs
  analog_1g_receive_quality analog_1g_receive_timing_confidence
  analog_1g_receive_symbol_crossing analog_1g_receive_dsp_crossing analog_1g_receive_transmit_crossing analog_1g_autonomous_receive_filter analog_1g_receive_sample_pcs_qualified
  analog_1g_receive_earliest_peer analog_1g_repeater_forwarding analog_1g_flow_controlled_repeater analog_1g_synchronous_frame_fifo
  analog_1g_ethernet_pause_0 analog_1g_ethernet_pause_1 analog_1g_ethernet_pause_2 analog_1g_ethernet_pause_3)
if(TARGET analog_1g_adc_array)
  add_dependencies(check-1g analog_1g_adc_array)
endif()
if(TARGET analog_1g_adc_core)
  add_dependencies(check-1g analog_1g_adc_core)
endif()
add_dependencies(check-1g analog_1g_level_down analog_1g_source_follower)
if(TARGET analog_1g_receive_frontend)
  add_dependencies(check-1g analog_1g_receive_frontend analog_1g_receive_conversion)
endif()
foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.transmit_driver.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/transmit_driver.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_transmit_driver>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/transmit_driver_600pf/${flow}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
  set_tests_properties(analog.1g.transmit_driver.${flow} PROPERTIES
    LABELS "analog;1g;transmitter;${flow}" TIMEOUT 7200 FIXTURES_SETUP one_g_transmit_driver_${flow})
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.transmit_driver.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/transmit_driver.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/transmit_driver_600pf")
  set_tests_properties(analog.1g.transmit_driver.compare PROPERTIES LABELS "analog;1g;transmitter;veriloga"
    TIMEOUT 180 FIXTURES_REQUIRED "one_g_transmit_driver_cpp;one_g_transmit_driver_veriloga")
endif()
add_custom_target(check-1g-transmit-driver
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 2
    -R "^analog[.]1g[.]transmit_driver[.]"
  DEPENDS analog_1g_transmit_driver USES_TERMINAL VERBATIM)
add_test(NAME analog.1g.transmit_driver.checker_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/transmit_driver_guards.py"
  "${CMAKE_CURRENT_BINARY_DIR}/results/transmit_driver_600pf/cpp")
set_tests_properties(analog.1g.transmit_driver.checker_guards PROPERTIES
  LABELS "analog;1g;transmitter" TIMEOUT 180 FIXTURES_REQUIRED one_g_transmit_driver_cpp)
add_dependencies(check-1g analog_1g_transmit_driver)
add_custom_target(check-1g-sample-and-driver-control
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 1
    -R "^analog[.]1g[.](normalizer|pam5_driver)[.]"
  DEPENDS analog_1g_normalizer analog_1g_pam5_driver USES_TERMINAL VERBATIM)
add_custom_target(check-1g-clock-interface
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 2
    -R "^analog[.]1g[.](clock_level_shifter_250[.]|ihp_clock_level_shifter[.]|ihp_direct_level_shifter_250[.])"
  DEPENDS analog_1g_level_shifter USES_TERMINAL VERBATIM)
if(TARGET analog_1g_reference_clock_domain)
  foreach(flow cpp veriloga)
    if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
      continue()
    endif()
    add_test(NAME analog.1g.reference_clock_domain.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/reference_clock_domain.py" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_reference_clock_domain>" --cxx "${CMAKE_CXX_COMPILER}"
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/reference_clock_domain/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.reference_clock_domain.${flow} PROPERTIES
      LABELS "analog;1g;clock;${flow}" TIMEOUT 7200 FIXTURES_SETUP one_g_reference_clock_domain_${flow})
  endforeach()
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.1g.reference_clock_domain.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/reference_clock_domain.py" --flow compare
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/reference_clock_domain")
    set_tests_properties(analog.1g.reference_clock_domain.compare PROPERTIES LABELS "analog;1g;clock;veriloga"
      TIMEOUT 180 FIXTURES_REQUIRED "one_g_reference_clock_domain_cpp;one_g_reference_clock_domain_veriloga")
  endif()
  add_custom_target(check-1g-reference-clock-domain
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 2
      -R "^analog[.]1g[.]reference_clock_domain[.]"
    DEPENDS analog_1g_reference_clock_domain USES_TERMINAL VERBATIM)
  add_dependencies(check-1g analog_1g_reference_clock_domain)
endif()
add_custom_target(check-1g-reference-clock
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 2
    -R "^analog[.]1g[.]reference_clock[.]"
  DEPENDS analog_1g_reference_clock USES_TERMINAL VERBATIM)
add_test(NAME analog.1g.transmit_controls.native COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/transmit_control_fixture.py"
  --bench "$<TARGET_FILE:analog_1g_transmit_controls>" --cxx "${CMAKE_CXX_COMPILER}"
  --work "${CMAKE_CURRENT_BINARY_DIR}/results/transmit_controls")
set_tests_properties(analog.1g.transmit_controls.native PROPERTIES
  LABELS "analog;1g;transmitter;hardware;cpp" TIMEOUT 60 FIXTURES_SETUP one_g_transmit_controls)
foreach(driver_model core_transmit_driver programmable_core_transmit_driver cascoded_core_transmit_driver)
  if(NOT TARGET analog_1g_${driver_model})
    continue()
  endif()
  set(driver_trim_args "")
  set(driver_checker core_transmit_driver.py)
  if(driver_model MATCHES "^(programmable|cascoded)_core_transmit_driver$")
    list(APPEND driver_trim_args --trim-code 26)
    set(driver_checker core_transmit_states.py)
  endif()
  set(core_transmit_fixtures "")
  foreach(flow cpp veriloga)
    if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
      continue()
    endif()
    add_test(NAME analog.1g.${driver_model}.${flow} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${driver_checker}" --flow "${flow}"
      --bench "$<TARGET_FILE:analog_1g_${driver_model}>" --cxx "${CMAKE_CXX_COMPILER}"
      --fixture-source "${CMAKE_CURRENT_SOURCE_DIR}/${driver_model}.cpp" ${driver_trim_args}
      --fixture "${CMAKE_CURRENT_BINARY_DIR}/results/transmit_controls/checks.json"
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/${driver_model}/${flow}"
      --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}")
    set_tests_properties(analog.1g.${driver_model}.${flow} PROPERTIES
      LABELS "analog;1g;transmitter;${flow}" TIMEOUT 18000
      FIXTURES_REQUIRED one_g_transmit_controls FIXTURES_SETUP one_g_${driver_model}_${flow})
    list(APPEND core_transmit_fixtures one_g_${driver_model}_${flow})
  endforeach()
  if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
    add_test(NAME analog.1g.${driver_model}.compare COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${driver_checker}" --flow compare
      --work "${CMAKE_CURRENT_BINARY_DIR}/results/${driver_model}")
    set_tests_properties(analog.1g.${driver_model}.compare PROPERTIES
      LABELS "analog;1g;transmitter;veriloga" TIMEOUT 600 FIXTURES_REQUIRED "${core_transmit_fixtures}")
  endif()
  string(REPLACE "_" "-" driver_check_name "${driver_model}")
  add_custom_target(check-1g-${driver_check_name}
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -j 2
      -R "^analog[.]1g[.]${driver_model}[.]"
    DEPENDS analog_1g_${driver_model} analog_1g_transmit_controls USES_TERMINAL VERBATIM)
  add_dependencies(check-1g analog_1g_${driver_model} analog_1g_transmit_controls)
  add_dependencies(check-analog analog_1g_${driver_model} analog_1g_transmit_controls)
  add_dependencies(check-veriloga analog_1g_${driver_model} analog_1g_transmit_controls)
endforeach()

foreach(pattern_module pma_test_pattern pma_transmit_lane receive_gain_control receive_range_calibration receive_acquisition)
  add_executable(analog_1g_${pattern_module} ${pattern_module}.cpp)
  target_link_libraries(analog_1g_${pattern_module} PRIVATE cpphdl::runtime)
  add_dependencies(check-1g analog_1g_${pattern_module})
  add_dependencies(check-analog analog_1g_${pattern_module})
  add_test(NAME analog.1g.${pattern_module}.native COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${pattern_module}.py" --flow cpp
    --bench "$<TARGET_FILE:analog_1g_${pattern_module}>" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${pattern_module}/cpp")
  set_tests_properties(analog.1g.${pattern_module}.native PROPERTIES LABELS "analog;1g;hardware;cpp" TIMEOUT 60)
  if(ONE_G_VERILATOR AND EXISTS "${CPPHDL_1G_CONVERTER}")
    foreach(pattern_flow rtl mapped)
      if(pattern_flow STREQUAL "mapped" AND (NOT EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
          OR NOT EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"))
        continue()
      endif()
      add_test(NAME analog.1g.${pattern_module}.${pattern_flow} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${pattern_module}.py" --flow "${pattern_flow}"
        --bench "$<TARGET_FILE:analog_1g_${pattern_module}>" --cxx "${CMAKE_CXX_COMPILER}"
        --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
        --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
        --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/${pattern_module}/${pattern_flow}")
      set_tests_properties(analog.1g.${pattern_module}.${pattern_flow} PROPERTIES
        LABELS "analog;1g;hardware;${pattern_flow}" TIMEOUT 3600 RESOURCE_LOCK one_g_heavy_hardware)
    endforeach()
  endif()
endforeach()

if(TEST analog.1g.receive_gain_control.mapped AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
  set_tests_properties(analog.1g.receive_gain_control.mapped PROPERTIES FIXTURES_SETUP one_g_gain_control_mapped)
  add_test(NAME analog.1g.receive_gain_control.cells COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_gain_control_cells.py"
    --mapped-proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_gain_control/mapped/checks.json"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_gain_control/cells"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
  set_tests_properties(analog.1g.receive_gain_control.cells PROPERTIES LABELS "analog;1g;hardware;timing"
    TIMEOUT 1200 RESOURCE_LOCK one_g_heavy_hardware FIXTURES_REQUIRED one_g_gain_control_mapped)
endif()

if(TEST analog.1g.receive_range_calibration.mapped AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
  set_tests_properties(analog.1g.receive_range_calibration.mapped PROPERTIES FIXTURES_SETUP one_g_range_calibration_mapped)
  add_test(NAME analog.1g.receive_range_calibration.cells COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_gain_control_cells.py" --module ReceiveRangeCalibration
    --mapped-proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_range_calibration/mapped/checks.json"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_range_calibration/cells"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
  set_tests_properties(analog.1g.receive_range_calibration.cells PROPERTIES LABELS "analog;1g;hardware;timing"
    TIMEOUT 1200 RESOURCE_LOCK one_g_heavy_hardware FIXTURES_REQUIRED one_g_range_calibration_mapped)
endif()

if(TEST analog.1g.receive_acquisition.mapped AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
  set_tests_properties(analog.1g.receive_acquisition.mapped PROPERTIES FIXTURES_SETUP one_g_acquisition_mapped)
  add_test(NAME analog.1g.receive_acquisition.cells COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/receive_gain_control_cells.py" --module ReceiveAnalogAcquisition
    --mapped-proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_acquisition/mapped/checks.json"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/receive_acquisition/cells"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
  set_tests_properties(analog.1g.receive_acquisition.cells PROPERTIES LABELS "analog;1g;hardware;timing"
    TIMEOUT 1200 RESOURCE_LOCK one_g_heavy_hardware FIXTURES_REQUIRED one_g_acquisition_mapped)
endif()

foreach(flow cpp veriloga)
  if(flow STREQUAL "veriloga" AND (NOT OPENVAF_EXECUTABLE OR NOT NGSPICE_EXECUTABLE OR CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF"))
    continue()
  endif()
  add_test(NAME analog.1g.mdi_input_network.${flow} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/mdi_input_network.py" --flow "${flow}"
    --bench "$<TARGET_FILE:analog_1g_mdi_input_network>" --cxx "${CMAKE_CXX_COMPILER}"
    --openvaf "${OPENVAF_EXECUTABLE}" --ngspice "${NGSPICE_EXECUTABLE}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/mdi_input_network/${flow}")
  set_tests_properties(analog.1g.mdi_input_network.${flow} PROPERTIES
    LABELS "analog;1g;frontend;${flow}" TIMEOUT 1200 FIXTURES_SETUP one_g_mdi_input_network_${flow})
endforeach()
if(OPENVAF_EXECUTABLE AND NGSPICE_EXECUTABLE AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  add_test(NAME analog.1g.mdi_input_network.compare COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/mdi_input_network.py" --flow compare
    --work "${CMAKE_CURRENT_BINARY_DIR}/results/mdi_input_network")
  set_tests_properties(analog.1g.mdi_input_network.compare PROPERTIES
    LABELS "analog;1g;frontend;veriloga" TIMEOUT 120
    FIXTURES_REQUIRED "one_g_mdi_input_network_cpp;one_g_mdi_input_network_veriloga")
endif()
foreach(check check-1g check-analog check-veriloga)
  add_dependencies(${check} analog_1g_mdi_input_network)
endforeach()

add_executable(analog_1g_adc_bank adc_bank.cpp)
target_link_libraries(analog_1g_adc_bank PRIVATE cpphdl::runtime)
add_test(NAME analog.1g.adc_bank.native COMMAND analog_1g_adc_bank)
set_tests_properties(analog.1g.adc_bank.native PROPERTIES LABELS "analog;1g;hardware;area" TIMEOUT 60)
add_dependencies(check-1g analog_1g_adc_bank)
if(CPPHDL_1G_CONVERTER AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.adc_bank.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_bank_rtl.py"
    --native "$<TARGET_FILE:analog_1g_adc_bank>" --converter "${CPPHDL_1G_CONVERTER}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_bank")
  set_tests_properties(analog.1g.adc_bank.rtl PROPERTIES
    LABELS "analog;1g;hardware;rtl;area" TIMEOUT 1800 RESOURCE_LOCK one_g_eda_memory)
endif()

if(TEST analog.1g.adc_bank.rtl AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
  set_tests_properties(analog.1g.adc_bank.rtl PROPERTIES FIXTURES_SETUP one_g_adc_bank_rtl)
  add_test(NAME analog.1g.adc_bank.mapped COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_bank_mapped.py"
    --proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_bank/checks.json"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_bank_mapped"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
  set_tests_properties(analog.1g.adc_bank.mapped PROPERTIES
    LABELS "analog;1g;hardware;mapped;area" TIMEOUT 3600 RESOURCE_LOCK one_g_eda_memory FIXTURES_REQUIRED one_g_adc_bank_rtl FIXTURES_SETUP one_g_adc_bank_mapped)
endif()

add_executable(analog_1g_adc_receive_bank adc_receive_bank.cpp)
target_link_libraries(analog_1g_adc_receive_bank PRIVATE cpphdl::runtime)
add_test(NAME analog.1g.adc_receive_bank.native COMMAND analog_1g_adc_receive_bank)
set_tests_properties(analog.1g.adc_receive_bank.native PROPERTIES LABELS "analog;1g;hardware;receiver" TIMEOUT 60)
add_dependencies(check-1g analog_1g_adc_receive_bank)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.adc_receive_bank.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_receive_bank_rtl.py"
    --native "$<TARGET_FILE:analog_1g_adc_receive_bank>" --converter "${CPPHDL_1G_CONVERTER}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_receive_bank")
  set_tests_properties(analog.1g.adc_receive_bank.rtl PROPERTIES
    LABELS "analog;1g;hardware;receiver;rtl" TIMEOUT 1800 RESOURCE_LOCK one_g_eda_memory)
endif()

if(TEST analog.1g.adc_receive_bank.rtl AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
  set_tests_properties(analog.1g.adc_receive_bank.rtl PROPERTIES FIXTURES_SETUP one_g_adc_receive_bank_rtl)
  add_test(NAME analog.1g.adc_receive_bank.mapped COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_receive_bank_mapped.py"
    --proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_receive_bank/checks.json"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_receive_bank_mapped"
    --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_IHP_PDK_ROOT}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}")
  set_tests_properties(analog.1g.adc_receive_bank.mapped PROPERTIES
    LABELS "analog;1g;hardware;receiver;mapped;timing" TIMEOUT 3600
    RESOURCE_LOCK one_g_eda_memory FIXTURES_REQUIRED one_g_adc_receive_bank_rtl)
endif()

add_executable(analog_1g_adc_startup adc_startup.cpp)
target_link_libraries(analog_1g_adc_startup PRIVATE cpphdl::runtime)
add_dependencies(check-1g analog_1g_adc_startup)
add_test(NAME analog.1g.adc_startup.native COMMAND analog_1g_adc_startup)
set_tests_properties(analog.1g.adc_startup.native PROPERTIES LABELS "analog;1g;hardware;receiver" TIMEOUT 60)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  set(startup_mapping)
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
      AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
    list(APPEND startup_mapping --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain" --pdk "${CPPHDL_IHP_PDK_ROOT}")
  endif()
  add_test(NAME analog.1g.adc_startup.hardware COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_startup.py" --native "$<TARGET_FILE:analog_1g_adc_startup>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_startup" ${startup_mapping})
  set_tests_properties(analog.1g.adc_startup.hardware PROPERTIES LABELS "analog;1g;hardware;receiver;rtl;mapped"
    TIMEOUT 1800 RESOURCE_LOCK one_g_eda_memory)
endif()

add_executable(analog_1g_adc_receive_lane adc_receive_lane.cpp)
target_link_libraries(analog_1g_adc_receive_lane PRIVATE cpphdl::runtime)
add_dependencies(check-1g analog_1g_adc_receive_lane)
add_test(NAME analog.1g.adc_receive_lane.native COMMAND analog_1g_adc_receive_lane)
set_tests_properties(analog.1g.adc_receive_lane.native PROPERTIES LABELS "analog;1g;hardware;receiver" TIMEOUT 60)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.adc_receive_lane.hardware COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adc_receive_lane.py" --native "$<TARGET_FILE:analog_1g_adc_receive_lane>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/adc_receive_lane" ${startup_mapping})
  set_tests_properties(analog.1g.adc_receive_lane.hardware PROPERTIES LABELS "analog;1g;hardware;receiver;rtl;mapped"
    TIMEOUT 3600 RESOURCE_LOCK one_g_eda_memory)
  if(startup_mapping)
    set_tests_properties(analog.1g.adc_receive_lane.hardware PROPERTIES FIXTURES_SETUP one_g_adc_lane_mapped)
  endif()
endif()

add_executable(analog_1g_negotiated_repeater negotiated_repeater.cpp)
target_link_libraries(analog_1g_negotiated_repeater PRIVATE cpphdl::runtime)
add_dependencies(check-1g analog_1g_negotiated_repeater)
if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR)
  add_test(NAME analog.1g.negotiated_repeater.rtl COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/negotiated_repeater.py"
    --native "$<TARGET_FILE:analog_1g_negotiated_repeater>" --converter "${CPPHDL_1G_CONVERTER}"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/negotiated_repeater")
  set_tests_properties(analog.1g.negotiated_repeater.rtl PROPERTIES
    LABELS "analog;1g;hardware;rtl;flow-control" TIMEOUT 4200)
endif()

add_executable(analog_1g_adc_receive_lane_boundaries adc_receive_lane_boundaries.cpp)
target_link_libraries(analog_1g_adc_receive_lane_boundaries PRIVATE cpphdl::runtime)
add_dependencies(check-1g analog_1g_adc_receive_lane_boundaries)
add_test(NAME analog.1g.adc_receive_lane.boundaries COMMAND analog_1g_adc_receive_lane_boundaries)
set_tests_properties(analog.1g.adc_receive_lane.boundaries PROPERTIES LABELS "analog;1g;hardware;receiver" TIMEOUT 60)

if(EXISTS "${CPPHDL_1G_CONVERTER}" AND ONE_G_VERILATOR
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    AND EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/sv2v"
    AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.ref/sg13g2_sram/gds/RM_IHPSG13_2P_1024x16_c2_bm_bist.gds")
  add_test(NAME analog.1g.ihp_flow_control.sram COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/flow_controlled_repeater.py"
    --native "$<TARGET_FILE:analog_1g_flow_controlled_repeater>"
    --converter "${CPPHDL_1G_CONVERTER}" --verilator "${ONE_G_VERILATOR}"
    --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/flow_control_sram"
    --sram-pdk "${CPPHDL_IHP_PDK_ROOT}"
    --yosys "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/yosys"
    --sv2v "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/sv2v")
  set_tests_properties(analog.1g.ihp_flow_control.sram PROPERTIES
    LABELS "analog;1g;ihp;hardware;sram" TIMEOUT 2400 FIXTURES_SETUP one_g_flow_sram)
  add_test(NAME analog.1g.ihp_flow_control.cells COMMAND "${Python3_EXECUTABLE}"
    "${CPPHDL_ANALOG_REAL_ROOT}/1G/IHPSG13G2/flow_control_synthesis.py"
    --proof "${CMAKE_CURRENT_BINARY_DIR}/hardware/flow_control_sram/checks.json"
    --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
    --verilator "${ONE_G_VERILATOR}" --cxx "${CMAKE_CXX_COMPILER}"
    --work "${CMAKE_CURRENT_BINARY_DIR}/hardware/flow_control_cells")
  set_tests_properties(analog.1g.ihp_flow_control.cells PROPERTIES
    LABELS "analog;1g;ihp;hardware;mapped" TIMEOUT 3600 FIXTURES_REQUIRED one_g_flow_sram)
endif()
