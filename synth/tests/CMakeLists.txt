find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(CPPHDL_SYNTH_VERILATOR verilator)

add_test(NAME synth_auto_scheduler COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/auto_scheduler"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/hls/AutoScheduler.cmake")

add_executable(synth_graph_checks GraphChecks.cpp ../Verilog.cpp)
target_include_directories(synth_graph_checks PRIVATE .. ../../include)
target_compile_definitions(synth_graph_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_graph_checks PRIVATE cxx_std_17)
add_test(NAME synth_graph_checks COMMAND synth_graph_checks "${CMAKE_CURRENT_BINARY_DIR}/checked.v")
add_executable(synth_retiming_checks retiming/Checks.cpp ../timing.cpp ../retiming.cpp ../Mapping.cpp)
target_include_directories(synth_retiming_checks PRIVATE .. ../../include)
target_compile_definitions(synth_retiming_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_retiming_checks PRIVATE cxx_std_17)
add_test(NAME synth_retiming_checks COMMAND synth_retiming_checks)
add_executable(synth_stream_checks retiming/StreamChecks.cpp ../timing.cpp ../retiming.cpp ../Mapping.cpp)
target_include_directories(synth_stream_checks PRIVATE .. ../../include)
target_compile_definitions(synth_stream_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_stream_checks PRIVATE cxx_std_17)
add_test(NAME synth_stream_checks COMMAND synth_stream_checks)
add_executable(synth_mapping_checks MappingChecks.cpp ../Mapping.cpp)
target_include_directories(synth_mapping_checks PRIVATE .. ../../include)
target_compile_definitions(synth_mapping_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_mapping_checks PRIVATE cxx_std_17)
add_test(NAME synth_mapping_checks COMMAND synth_mapping_checks)
add_executable(synth_wide_arithmetic WideArithmetic.cpp ../Mapping.cpp)
target_include_directories(synth_wide_arithmetic PRIVATE .. ../../include)
target_compile_definitions(synth_wide_arithmetic PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_wide_arithmetic PRIVATE cxx_std_17)
add_test(NAME synth_wide_arithmetic COMMAND synth_wide_arithmetic)
add_executable(synth_blackbox_checks BlackBoxChecks.cpp ../Verilog.cpp ../Mapping.cpp ../timing.cpp ../retiming.cpp)
target_include_directories(synth_blackbox_checks PRIVATE .. ../../include)
target_compile_definitions(synth_blackbox_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_blackbox_checks PRIVATE cxx_std_17)
add_test(NAME synth_blackbox_checks COMMAND synth_blackbox_checks "${CMAKE_CURRENT_BINARY_DIR}/blackboxes_checked.v")
add_executable(synth_schedule_checks hls/ScheduleChecks.cpp ../ScheduledGraph.cpp ../Mapping.cpp)
target_include_directories(synth_schedule_checks PRIVATE .. ../../include)
target_compile_definitions(synth_schedule_checks PRIVATE CPPHDL_GRAPH_CORE_ONLY)
target_compile_features(synth_schedule_checks PRIVATE cxx_std_17)
add_test(NAME synth_schedule_checks COMMAND synth_schedule_checks)

if(CPPHDL_SYNTH_VERILATOR)
    foreach(storage registers ram)
      set(storage_args)
      if(storage STREQUAL "ram")
        set(storage_args --ram)
      endif()
      add_test(NAME synth_hls_${storage} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/hls/Run.py"
          --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
          --verilator "${CPPHDL_SYNTH_VERILATOR}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/hls_${storage}" ${storage_args})
      set_tests_properties(synth_hls_${storage} PROPERTIES TIMEOUT 1200)
    endforeach()
    add_test(NAME synth_hls_hft COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/hls/Run.py"
        --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
        --verilator "${CPPHDL_SYNTH_VERILATOR}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/hls_hft" --hft)
    set_tests_properties(synth_hls_hft PROPERTIES TIMEOUT 2400 LABELS "hls;slow")
  foreach(example one_clock feedback_frames)
    add_test(NAME synth_${example} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/retiming/Feedback.py"
        --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
        --verilator "${CPPHDL_SYNTH_VERILATOR}"
        --case ${example} --work "${CMAKE_CURRENT_BINARY_DIR}/${example}")
    set_tests_properties(synth_${example} PROPERTIES TIMEOUT 900)
  endforeach()
  foreach(example logic_retiming ram_retiming keep_box)
    add_test(NAME synth_${example} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/retiming/Run.py"
        --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
        --verilator "${CPPHDL_SYNTH_VERILATOR}"
        --case ${example} --work "${CMAKE_CURRENT_BINARY_DIR}/${example}")
    set_tests_properties(synth_${example} PROPERTIES TIMEOUT 900)
  endforeach()
  foreach(example math pipeline blackbox interface_proxy main_and_secondary two_main_clocks memory async_reset memory_async_reset)
    add_test(NAME synth_${example} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Run.py"
        --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
        --verilator "${CPPHDL_SYNTH_VERILATOR}"
        --case ${example} --work "${CMAKE_CURRENT_BINARY_DIR}/${example}")
    set_tests_properties(synth_${example} PROPERTIES TIMEOUT 900)
  endforeach()
else()
    message(STATUS "Verilator required for gate-level simulation regressions")
endif()
