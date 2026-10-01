find_program(HLS_VERILATOR NAMES verilator HINTS "${CMAKE_SOURCE_DIR}/.conda/bin")
find_program(HLS_MAKE NAMES gmake make REQUIRED)
add_custom_target(hls_tests DEPENDS cpphdl)
include(Libcxx.cmake)
add_test(NAME hls_libcxx_runtime COMMAND "${CMAKE_CURRENT_BINARY_DIR}/libcxx-check")
add_test(NAME code_libcxx_headers COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/libcxx-headers"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/LibcxxHeaders.cmake")

function(add_clocked_test container)
    set(name hls_clocked_${container})
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
    add_hls_native(${name} Clocked${container}.cpp HLS_SHARED_MEMORY=${shared_memory} HLS_BLOCK_RAM=${block_ram})
    add_test(NAME ${name}_native COMMAND "${HLS_NATIVE_BINARY}")
    if(HLS_VERILATOR)
        add_test(NAME ${name}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Clocked${container}.cpp"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-rtl"
            "-DSHARED_MEMORY=${shared_memory}"
            "-DBLOCK_RAM=${block_ram}"
            "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
            "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
            "-DCXX=${CMAKE_CXX_COMPILER}" -P "${CMAKE_SOURCE_DIR}/hls/tests/Clocked.cmake")
        set_tests_properties(${name}_verilator PROPERTIES TIMEOUT 240)
    endif()
endfunction()
add_clocked_test(Recursion)
add_clocked_test(Recursion TRUE)
add_clocked_test(Recursion TRUE TRUE)
add_clocked_test(Reuse)
add_clocked_test(Reuse TRUE)
add_clocked_test(Reuse TRUE TRUE)
add_clocked_test(Sharing)
add_clocked_test(Sharing TRUE)
add_clocked_test(Sharing TRUE TRUE)
add_clocked_test(MemoryEffects)
add_clocked_test(MemoryEffects TRUE)
add_clocked_test(MemoryEffects TRUE TRUE)
add_clocked_test(Bindings)
add_clocked_test(Address)
add_clocked_test(Address TRUE)
add_clocked_test(Address TRUE TRUE)
add_clocked_test(Aggregates)
add_clocked_test(Overrides TRUE)
foreach(case RANGE 0 8)
    add_test(NAME hls_overrides_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/overrides-reject-${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/OverridesReject.cmake")
    set_tests_properties(hls_overrides_reject_${case} PROPERTIES TIMEOUT 60 LABELS "hls;expected-rejection")
endforeach()
foreach(case RANGE 0 22)
    add_test(NAME hls_clocked_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/clocked-reject-${case}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ClockedReject.cmake")
    set_tests_properties(hls_clocked_reject_${case} PROPERTIES TIMEOUT 60 LABELS "hls;expected-rejection")
endforeach()

add_subdirectory(std)
add_subdirectory(../examples/map "${CMAKE_BINARY_DIR}/hls/examples/map")

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
    add_test(NAME hls_recursion_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/reject-${case}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Reject.cmake")
endforeach()
