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
    add_hls_native(${name} Clocked${container}.cpp)
    add_test(NAME ${name}_native COMMAND "${HLS_NATIVE_BINARY}")
    if(HLS_VERILATOR)
        add_test(NAME ${name}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Clocked${container}.cpp"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-rtl"
            "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
            "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
            "-DCXX=${CMAKE_CXX_COMPILER}" -P "${CMAKE_SOURCE_DIR}/hls/tests/Clocked.cmake")
        set_tests_properties(${name}_verilator PROPERTIES TIMEOUT 240)
    endif()
endfunction()
add_clocked_test(Recursion)
add_clocked_test(Reuse)
add_clocked_test(Bindings)
foreach(case RANGE 0 11)
    add_test(NAME hls_clocked_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/clocked-reject-${case}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ClockedReject.cmake")
    set_tests_properties(hls_clocked_reject_${case} PROPERTIES TIMEOUT 60 LABELS "hls;expected-rejection")
endforeach()

add_subdirectory(std)

add_executable(hls_shared_blocks SharedBlocks_test.cpp ../SharedBlocks.cpp ../Combinational.cpp)
set_target_properties(hls_shared_blocks PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_dependencies(hls_tests hls_shared_blocks)
add_test(NAME hls_shared_blocks COMMAND hls_shared_blocks)
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
