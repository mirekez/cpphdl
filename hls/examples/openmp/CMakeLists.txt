find_program(OPENMP_GXX NAMES g++)
foreach(example ParallelFor Reduction Collapse Private Boundaries Tree StateLanes)
    set(flows native)
    if(HLS_VERILATOR)
        list(APPEND flows rtl gates)
    endif()
    foreach(flow IN LISTS flows)
        add_test(NAME hls_openmp_${example}_${flow} COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DVERILATOR=${HLS_VERILATOR}"
            "-DEXAMPLE=${example}" "-DFLOW=${flow}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${example}-${flow}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Run.cmake")
        set_tests_properties(hls_openmp_${example}_${flow} PROPERTIES TIMEOUT 300 LABELS "hls;openmp")
    endforeach()
    if(OPENMP_GXX)
        add_test(NAME hls_openmp_${example}_gcc COMMAND "${CMAKE_COMMAND}"
            "-DROOT=${CMAKE_SOURCE_DIR}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
            "-DNATIVE_CXX=${OPENMP_GXX}" "-DEXAMPLE=${example}" -DFLOW=native
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${example}-gcc"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Run.cmake")
        set_tests_properties(hls_openmp_${example}_gcc PROPERTIES TIMEOUT 120 LABELS "hls;openmp")
    endif()
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME hls_openmp_reduction_scaling COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DPYTHON=${Python3_EXECUTABLE}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/scaling"
    -P "${CMAKE_SOURCE_DIR}/hls/tests/OpenMPScaling.cmake")
set_tests_properties(hls_openmp_reduction_scaling PROPERTIES TIMEOUT 300 LABELS "hls;openmp")
