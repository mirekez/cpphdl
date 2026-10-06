add_hls_native(hls_dram_stream DramStream.cpp)
add_test(NAME hls_dram_stream_native COMMAND "${HLS_NATIVE_BINARY}")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(HLS_VERILATOR)
    foreach(flow rtl gates)
        add_test(NAME hls_dram_stream_${flow} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Run.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
            --verilator "${HLS_VERILATOR}" --work "${CMAKE_CURRENT_BINARY_DIR}/${flow}" --flow ${flow})
        set_tests_properties(hls_dram_stream_${flow} PROPERTIES TIMEOUT 1200 LABELS "hls;dram")
    endforeach()
endif()
