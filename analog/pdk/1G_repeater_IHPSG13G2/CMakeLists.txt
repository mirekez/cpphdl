add_executable(analog_1g_repeater_core_test tests/core.cpp)
target_link_libraries(analog_1g_repeater_core_test PRIVATE cpphdl::runtime)
if(TARGET check-analog)
  add_dependencies(check-analog analog_1g_repeater_core_test)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
if(Python3_Interpreter_FOUND)
  add_custom_target(repeater-core-gds
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/flow.py" --target core
    WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
  add_custom_target(repeater-chip-gds
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/flow.py" --target chip
      --work "${CMAKE_CURRENT_SOURCE_DIR}/results/chip"
    WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
  set(repeater_pdk "${CPPHDL_ANALOG_REAL_ROOT}/pdk/IHPSG13G2")
  if(EXISTS "${repeater_pdk}/.cpphdl/venv/bin/python")
    add_custom_target(repeater-padframe
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/padframe.py"
      WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
    add_custom_target(repeater-padframe-filled
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/fill_padframe.py"
        --layout "${CMAKE_CURRENT_SOURCE_DIR}/results/padframe/repeater_padframe.gds"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/results/padframe/repeater_padframe_filled.gds"
        --work "${CMAKE_CURRENT_SOURCE_DIR}/results/padframe/fill"
      DEPENDS repeater-padframe
      WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
    add_custom_target(repeater-chip-verify
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/verify_physical.py"
        --top RepeaterChip --layout "${CMAKE_CURRENT_SOURCE_DIR}/results/chip/repeater_chip.gds"
        --netlist "${CMAKE_CURRENT_SOURCE_DIR}/results/chip/repeater_chip.cdl"
        --work "${CMAKE_CURRENT_BINARY_DIR}/chip-verification"
      WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
    add_custom_target(repeater-padframe-verify
      COMMAND "${repeater_pdk}/.cpphdl/venv/bin/python" "${CMAKE_CURRENT_SOURCE_DIR}/verify_physical.py"
        --top RepeaterPadframe
        --layout "${CMAKE_CURRENT_SOURCE_DIR}/results/padframe/repeater_padframe_filled.gds"
        --netlist "${CMAKE_CURRENT_SOURCE_DIR}/results/padframe/lvs.cir"
        --interface "${CMAKE_CURRENT_SOURCE_DIR}/results/padframe/interface.json"
        --work "${CMAKE_CURRENT_BINARY_DIR}/padframe-verification"
      DEPENDS repeater-padframe-filled
      WORKING_DIRECTORY "${CPPHDL_ANALOG_PACKAGE_ROOT}" USES_TERMINAL VERBATIM)
  endif()
  if(TARGET cpphdl)
    add_dependencies(repeater-core-gds cpphdl)
  endif()
endif()
