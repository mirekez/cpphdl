find_program(HLS_VERILATOR NAMES verilator HINTS "${CMAKE_SOURCE_DIR}/.conda/bin")
find_program(HLS_MAKE NAMES gmake make REQUIRED)
add_custom_target(hls_tests DEPENDS cpphdl)
include(Libcxx.cmake)
foreach(stages 1 3 7)
    add_hls_native(hls_pipeline_${stages} Pipeline.cpp PIPELINE_STAGES=${stages})
    add_test(NAME hls_pipeline_${stages}_native COMMAND "${HLS_NATIVE_BINARY}")
    if(HLS_VERILATOR)
        add_test(NAME hls_pipeline_${stages}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pipeline-${stages}" "-DSTAGES=${stages}"
            "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DVERILATOR=${HLS_VERILATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Pipeline.cmake")
        set_tests_properties(hls_pipeline_${stages}_verilator PROPERTIES TIMEOUT 180)
    endif()
endforeach()
foreach(stages 1 3 7)
    add_hls_native(hls_pipeline_feedback_${stages} PipelineFeedback.cpp PIPELINE_STAGES=${stages})
    add_test(NAME hls_pipeline_feedback_${stages}_native COMMAND "${HLS_NATIVE_BINARY}")
    if(HLS_VERILATOR)
        add_test(NAME hls_pipeline_feedback_${stages}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pipeline-feedback-${stages}" "-DSTAGES=${stages}"
            -DSOURCE=PipelineFeedback.cpp "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DVERILATOR=${HLS_VERILATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Pipeline.cmake")
        set_tests_properties(hls_pipeline_feedback_${stages}_verilator PROPERTIES TIMEOUT 180)
    endif()
endforeach()
if(HLS_VERILATOR)
    add_test(NAME hls_pipeline_synth_verilator COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pipeline-synth" -DSTAGES=3 -DSYNTH=ON
        -DSOURCE=PipelineFeedback.cpp "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DVERILATOR=${HLS_VERILATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Pipeline.cmake")
    set_tests_properties(hls_pipeline_synth_verilator PROPERTIES TIMEOUT 300)
    add_test(NAME hls_pipeline_feedback_graph_verilator COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pipeline-feedback-graph" -DSTAGES=3 -DGRAPH=ON
        -DSOURCE=PipelineFeedback.cpp "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DVERILATOR=${HLS_VERILATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Pipeline.cmake")
    set_tests_properties(hls_pipeline_feedback_graph_verilator PROPERTIES TIMEOUT 240)
    add_test(NAME hls_pipeline_graph_verilator COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pipeline-graph" -DSTAGES=3 -DGRAPH=ON
        "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DVERILATOR=${HLS_VERILATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Pipeline.cmake")
    set_tests_properties(hls_pipeline_graph_verilator PROPERTIES TIMEOUT 240)
endif()
add_test(NAME hls_libcxx_runtime COMMAND "${CMAKE_CURRENT_BINARY_DIR}/libcxx-check")
add_test(NAME code_libcxx_headers COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/libcxx-headers"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/LibcxxHeaders.cmake")

function(add_delayed_test container)
    set(name hls_delayed_${container})
    set(shared_memory 0)
    set(block_ram 0)
    if(ARGC GREATER 1 AND ARGV1)
        set(shared_memory 1)
        string(APPEND name _shared_memory)
    endif()
    if(ARGC GREATER 2 AND ARGV2)
        set(block_ram 1)
        string(APPEND name _bram)
    endif()
    add_hls_native(${name} Delayed${container}.cpp HLS_SHARED_MEMORY=${shared_memory} HLS_BLOCK_RAM=${block_ram})
    add_test(NAME ${name}_native COMMAND "${HLS_NATIVE_BINARY}")
    if(HLS_VERILATOR)
        add_test(NAME ${name}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Delayed${container}.cpp"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-rtl"
            "-DSHARED_MEMORY=${shared_memory}"
            "-DBLOCK_RAM=${block_ram}"
            "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
            "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
            "-DCXX=${CMAKE_CXX_COMPILER}" -P "${CMAKE_SOURCE_DIR}/hls/tests/Delayed.cmake")
        set_tests_properties(${name}_verilator PROPERTIES TIMEOUT 240)
    endif()
endfunction()
add_delayed_test(Recursion)
add_delayed_test(Recursion TRUE)
add_delayed_test(Recursion TRUE TRUE)
add_delayed_test(Reuse)
add_delayed_test(Reuse TRUE)
add_delayed_test(Reuse TRUE TRUE)
add_delayed_test(Sharing)
add_delayed_test(Sharing TRUE)
add_delayed_test(Sharing TRUE TRUE)
add_delayed_test(MemoryEffects)
add_delayed_test(MemoryEffects TRUE)
add_delayed_test(MemoryEffects TRUE TRUE)
add_delayed_test(Bindings)
add_delayed_test(Address)
add_delayed_test(Address TRUE)
add_delayed_test(Address TRUE TRUE)
add_delayed_test(Aggregates)
add_delayed_test(Overrides TRUE)
foreach(case RANGE 0 8)
    add_test(NAME hls_overrides_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/overrides-reject-${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/OverridesReject.cmake")
    set_tests_properties(hls_overrides_reject_${case} PROPERTIES TIMEOUT 60 LABELS "hls;expected-rejection")
endforeach()
foreach(case RANGE 0 22)
    if(case EQUAL 6)
        add_test(NAME hls_delayed_without_flag COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" -DCASE=6
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/delayed-without-flag" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/DelayedReject.cmake")
        continue()
    endif()
    add_test(NAME hls_delayed_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/delayed-reject-${case}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/DelayedReject.cmake")
    set_tests_properties(hls_delayed_reject_${case} PROPERTIES TIMEOUT 60 LABELS "hls;expected-rejection")
endforeach()

foreach(case RANGE 0 12)
    add_test(NAME hls_pipeline_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pipeline-reject-${case}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/PipelineReject.cmake")
    set_tests_properties(hls_pipeline_reject_${case} PROPERTIES TIMEOUT 60 LABELS "hls;expected-rejection")
endforeach()

add_subdirectory(std)
add_subdirectory(../examples/map "${CMAKE_BINARY_DIR}/hls/examples/map")
add_subdirectory(../examples/net "${CMAKE_BINARY_DIR}/hls/examples/net")

add_executable(hls_shared_blocks SharedBlocks_test.cpp ../SharedBlocks.cpp ../Combinational.cpp)
set_target_properties(hls_shared_blocks PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_dependencies(hls_tests hls_shared_blocks)
add_test(NAME hls_shared_blocks COMMAND hls_shared_blocks)
add_executable(hls_memory_effects MemoryEffects_test.cpp ../MemoryEffects.cpp)
set_target_properties(hls_memory_effects PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_dependencies(hls_tests hls_memory_effects)
add_test(NAME hls_memory_effects COMMAND hls_memory_effects)
add_hls_native(hls_memory_port MemoryPort.cpp)
add_test(NAME hls_memory_port_native COMMAND "${HLS_NATIVE_BINARY}")
if(HLS_VERILATOR)
    add_test(NAME hls_override_policy_verilator COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/override-policy"
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/OverridePolicy.cpp"
        "-DBENCH=${CMAKE_CURRENT_SOURCE_DIR}/OverridePolicy.sv" -DTOP=OverridePolicyChecks
        "-DVERILATOR=${HLS_VERILATOR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/MemoryPort.cmake")
    set_tests_properties(hls_override_policy_verilator PROPERTIES TIMEOUT 120)
    add_test(NAME hls_memory_port_verilator COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/memory-port"
        "-DVERILATOR=${HLS_VERILATOR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/MemoryPort.cmake")
    set_tests_properties(hls_memory_port_verilator PROPERTIES TIMEOUT 120)
    add_test(NAME hls_memory_port_bram_verilator COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/memory-port-bram"
        -DBLOCK_RAM=ON
        "-DVERILATOR=${HLS_VERILATOR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/MemoryPort.cmake")
    set_tests_properties(hls_memory_port_bram_verilator PROPERTIES TIMEOUT 120)
endif()
function(add_hls_test name source top)
    add_hls_native(${name} ${source} ${ARGN})
    add_test(NAME ${name} COMMAND "${HLS_NATIVE_BINARY}")
    string(REPLACE ";" " " definitions "${ARGN}")
    if(HLS_VERILATOR)
        add_test(NAME ${name}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>"
            "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${source}"
            "-DTOP=${top}" "-DDEFINITIONS=${definitions}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-rtl"
            "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
            "-DCXX=${CMAKE_CXX_COMPILER}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Run.cmake")
        set_tests_properties(${name}_verilator PROPERTIES TIMEOUT 180)
    else()
        message(WARNING "Verilator unavailable: ${name}_verilator is not registered")
    endif()
endfunction()

add_hls_test(hls_recursion Recursion.cpp Recursion)
add_hls_test(hls_static_helper StaticHelper.cpp StaticHelper)
add_hls_test(hls_sram Sram.cpp SramTop)
foreach(case RANGE 0 5)
    set(test_name hls_recursion_reject_${case})
    if(case EQUAL 5)
        set(test_name hls_recursion_default_auto)
    endif()
    add_test(NAME ${test_name} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/reject-${case}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Reject.cmake")
endforeach()
