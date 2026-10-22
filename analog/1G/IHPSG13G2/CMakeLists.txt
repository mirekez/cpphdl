add_test(NAME analog.1g.ihp_receive.load_profiles COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_profiles.py")
set_tests_properties(analog.1g.ihp_receive.load_profiles PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
add_test(NAME analog.1g.ihp_transmit.spice_vectors COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/spice_differential_vectors.py")
set_tests_properties(analog.1g.ihp_transmit.spice_vectors PROPERTIES LABELS "analog;1g;ihp;transmitter" TIMEOUT 30)
add_test(NAME analog.1g.ihp_receive.dc_surface_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/receive_dc_surface.py")
set_tests_properties(analog.1g.ihp_receive.dc_surface_guards PROPERTIES LABELS "analog;1g;ihp;frontend" TIMEOUT 30)
set(CPPHDL_IHP_PDK_ROOT "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2" CACHE PATH "Prepared IHP SG13G2 checkout")
add_test(NAME analog.1g.ihp_area.evidence_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/mapped_area.py")
set_tests_properties(analog.1g.ihp_area.evidence_guards PROPERTIES LABELS "analog;1g;ihp;area" TIMEOUT 30)
add_test(NAME analog.1g.ihp_route.guide_guards COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/route_eco.py")
set_tests_properties(analog.1g.ihp_route.guide_guards PROPERTIES LABELS "analog;1g;ihp;physical" TIMEOUT 30)
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
  add_executable(analog_1g_ihp_programmable_receive_conversion tests/programmable_receive_conversion.cpp)
  target_link_libraries(analog_1g_ihp_programmable_receive_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  add_executable(analog_1g_ihp_wideband_receive_conversion tests/wideband_receive_conversion.cpp)
  target_link_libraries(analog_1g_ihp_wideband_receive_conversion PRIVATE cpphdl::analog Eigen3::Eigen)
  foreach(check_target check-1g check-analog check-veriloga)
    if(TARGET ${check_target})
      add_dependencies(${check_target} analog_1g_ihp_programmable_receive_conversion analog_1g_ihp_wideband_receive_conversion)
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
  foreach(conversion_model programmable wideband)
    set(conversion_target analog_1g_ihp_programmable_receive_conversion)
    set(conversion_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/programmable_receive_conversion.cpp")
    if(conversion_model STREQUAL "wideband")
      set(conversion_target analog_1g_ihp_wideband_receive_conversion)
      set(conversion_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/wideband_receive_conversion.cpp")
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
            if(conversion_model STREQUAL "wideband" AND count EQUAL 16)
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
  add_test(NAME analog.1g.ihp_capacitor_bank.layout
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capacitor_bank_layout.py"
      --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
      --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/capacitor_bank_layout")
  set_tests_properties(analog.1g.ihp_capacitor_bank.layout PROPERTIES LABELS "analog;1g;ihp;geometry;area" TIMEOUT 1200)
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
  if(EXISTS "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain/bin/openroad")
    add_test(NAME analog.1g.ihp_mos.layout
      COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mos_layout.py"
        --pdk "${CPPHDL_IHP_PDK_ROOT}" --tools "${CPPHDL_ANALOG_REAL_ROOT}/out/repeater-toolchain"
        --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/mos_layout")
    set_tests_properties(analog.1g.ihp_mos.layout PROPERTIES LABELS "analog;1g;ihp;geometry" TIMEOUT 600)
  endif()
  add_test(NAME analog.1g.ihp_filter.physical_guards
    COMMAND "${CPPHDL_IHP_PDK_ROOT}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/tests/physical_module.py")
  set_tests_properties(analog.1g.ihp_filter.physical_guards PROPERTIES LABELS "analog;1g;hardware;ihp" TIMEOUT 30)
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
  if(CPPHDL_1G_PHYSICAL_TESTS)
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
if(NGSPICE_EXECUTABLE AND EXISTS "${CPPHDL_IHP_PDK_ROOT}/ihp-sg13g2/libs.tech/ngspice/osdi/psp103_nqs.osdi"
    AND NOT CPPHDL_ANALOG_VERILOGA_TESTS STREQUAL "OFF")
  if(TARGET analog_1g_ihp_receive_process_conversion)
    add_test(NAME analog.1g.ihp_receive_conversion.xspice_controller COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/xspice_controller.py"
      --bench "$<TARGET_FILE:analog_1g_ihp_receive_process_conversion>" --ngspice "${NGSPICE_EXECUTABLE}"
      --cxx "${CMAKE_CXX_COMPILER}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/xspice_controller_ports")
    set_tests_properties(analog.1g.ihp_receive_conversion.xspice_controller PROPERTIES
      LABELS "analog;1g;ihp;frontend;veriloga" TIMEOUT 600)
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
      --fixture "${CMAKE_CURRENT_BINARY_DIR}/../tests/results/transmit_driver_600pf/cpp/checks.json"
      --ngspice "${NGSPICE_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/tests/results/core_transmit_nominal")
    set_tests_properties(analog.1g.ihp_core_transmit.nominal PROPERTIES
      LABELS "analog;1g;ihp;transmitter;veriloga" TIMEOUT 7200 FIXTURES_REQUIRED one_g_transmit_driver_cpp)
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
