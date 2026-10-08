add_hls_native(hls_hft hft.cpp)
add_test(NAME hls_hft_native COMMAND "${HLS_NATIVE_BINARY}")
if(HLS_VERILATOR)
    foreach(stages 4 8)
    if(stages EQUAL 4)
        set(test_name hls_hft_verilator)
    else()
        set(test_name hls_hft_pipeline8_verilator)
    endif()
    add_test(NAME ${test_name} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/hft.cpp"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rtl${stages}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}" "-DSTAGES=${stages}"
        "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Run.cmake")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
    endforeach()
endif()
