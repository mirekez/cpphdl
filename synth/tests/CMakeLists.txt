find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(CPPHDL_SYNTH_YOSYS yosys)
find_program(CPPHDL_SYNTH_VERILATOR verilator)

add_executable(synth_graph_checks GraphChecks.cpp ../Verilog.cpp)
target_include_directories(synth_graph_checks PRIVATE .. ../../include)
target_compile_definitions(synth_graph_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_graph_checks PRIVATE cxx_std_17)
add_test(NAME synth_graph_checks COMMAND synth_graph_checks "${CMAKE_CURRENT_BINARY_DIR}/checked.v")
add_executable(synth_retiming_checks retiming/Checks.cpp ../timing.cpp ../retiming.cpp)
target_include_directories(synth_retiming_checks PRIVATE .. ../../include)
target_compile_definitions(synth_retiming_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_retiming_checks PRIVATE cxx_std_17)
add_test(NAME synth_retiming_checks COMMAND synth_retiming_checks)

if(CPPHDL_SYNTH_YOSYS AND CPPHDL_SYNTH_VERILATOR)
  foreach(example logic_retiming ram_retiming keep_box)
    add_test(NAME synth_${example} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/retiming/Run.py"
        --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
        --yosys "${CPPHDL_SYNTH_YOSYS}" --verilator "${CPPHDL_SYNTH_VERILATOR}"
        --case ${example} --work "${CMAKE_CURRENT_BINARY_DIR}/${example}")
    set_tests_properties(synth_${example} PROPERTIES TIMEOUT 900)
  endforeach()
  foreach(example math pipeline main_and_secondary two_main_clocks memory async_reset memory_async_reset)
    add_test(NAME synth_${example} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Run.py"
        --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
        --yosys "${CPPHDL_SYNTH_YOSYS}" --verilator "${CPPHDL_SYNTH_VERILATOR}"
        --case ${example} --work "${CMAKE_CURRENT_BINARY_DIR}/${example}")
    set_tests_properties(synth_${example} PROPERTIES TIMEOUT 900)
  endforeach()
else()
    message(STATUS "synth_math requires Yosys and Verilator; gate-level regression not registered")
endif()
