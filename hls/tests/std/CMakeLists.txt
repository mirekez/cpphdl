add_clocked_test(Array)
add_clocked_test(Vector)
add_clocked_test(Map)

foreach(container Vector List Map Multimap UnorderedMap)
    set(name hls_std_${container}_object)
    add_hls_native(${name} ${container}.cpp)
    add_test(NAME ${name}_native COMMAND "${HLS_NATIVE_BINARY}")
    add_test(NAME ${name}_unsupported_rtl COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
        "-DCONTAINER=${container}" "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-analysis"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Analyze.cmake")
    set_tests_properties(${name}_unsupported_rtl PROPERTIES LABELS "hls-analysis;expected-rejection" TIMEOUT 120)
endforeach()
add_test(NAME hls_std_missing_capacity COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    -DCONTAINER=Vector -DMISSING_CAPACITY=1 "-DTOOLCHAIN=${HLS_TOOLCHAIN}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/missing-capacity-analysis"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/Analyze.cmake")
