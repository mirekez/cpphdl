add_delayed_test(Array)
add_delayed_test(Vector)
add_delayed_test(Map)
# The register-backed recursive map emits a much larger model than the shared
# memory variants; allow its host C++ compilation to finish on smaller machines.
if(HLS_VERILATOR)
    set_tests_properties(hls_delayed_Map_verilator PROPERTIES TIMEOUT 900 LABELS "slow")
endif()
add_delayed_test(MapSmall)
add_delayed_test(Array TRUE)
add_delayed_test(Vector TRUE)
add_delayed_test(Map TRUE)
add_delayed_test(MapSmall TRUE)
add_delayed_test(List TRUE)
add_delayed_test(Multimap TRUE)
add_delayed_test(UnorderedMap TRUE)
foreach(container Array Vector MapSmall Map List Multimap UnorderedMap)
    add_delayed_test(${container} TRUE TRUE)
endforeach()
add_hls_native(hls_std_override_helpers OverrideHelpers.cpp)
add_test(NAME hls_std_override_helpers COMMAND "${HLS_NATIVE_BINARY}")

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
