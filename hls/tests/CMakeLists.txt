find_program(HLS_VERILATOR NAMES verilator HINTS "${CMAKE_SOURCE_DIR}/.conda/bin")
find_program(HLS_MAKE NAMES gmake make REQUIRED)
find_program(HLS_YOSYS NAMES yosys)
add_custom_target(hls_tests DEPENDS cpphdl)

function(add_scheduled_test name source entry)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    set_target_properties(${name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_dependencies(hls_tests ${name})
    add_test(NAME ${name}_native COMMAND ${name})
    if(HLS_VERILATOR)
    foreach(sram RANGE 0 1)
        add_test(NAME ${name}_rtl_${sram} COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${source}" "-DENTRY=${entry}"
            "-DSRAM=${sram}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-${sram}"
            "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
            "-DYOSYS=${HLS_YOSYS}"
            "-DCXX=${CMAKE_CXX_COMPILER}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Scheduled.cmake")
        set_tests_properties(${name}_rtl_${sram} PROPERTIES TIMEOUT 180 LABELS "hls;scheduled-rtl")
    endforeach()
    endif()
endfunction()
add_scheduled_test(hls_bounded_vector BoundedVector.cpp bounded_vector)
add_scheduled_test(hls_kernel_memory KernelMemory.cpp memory_kernel)
add_scheduled_test(hls_grouped_schedule GroupedSchedule.cpp grouped_schedule)
foreach(case RANGE 0 6)
    add_test(NAME hls_kernel_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/kernel-reject-${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/KernelReject.cmake")
endforeach()
add_test(NAME hls_kernel_cli COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/KernelCli.cmake")

function(add_hls_test name source top)
    add_executable(${name} ${source})
    add_dependencies(hls_tests ${name})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    set_target_properties(${name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(${name} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    string(REPLACE ";" " " definitions "${ARGN}")
    if(HLS_VERILATOR)
        add_test(NAME ${name}_verilator COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>"
            "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${source}"
            "-DTOP=${top}" "-DDEFINITIONS=${definitions}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-rtl"
            "-DVERILATOR=${HLS_VERILATOR}" "-DMAKE=${HLS_MAKE}"
            "-DCXX=${CMAKE_CXX_COMPILER}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Run.cmake")
        set_tests_properties(${name}_verilator PROPERTIES TIMEOUT 180)
    else()
        message(WARNING "Verilator unavailable: ${name}_verilator is not registered")
    endif()
endfunction()

foreach(container Vector List Map Multimap UnorderedMap)
    foreach(heap RANGE 0 1)
        if(heap)
            set(storage heap)
        else()
            set(storage object)
        endif()
        set(name hls_std_${container}_${storage})
        add_executable(${name} ${container}.cpp)
        add_dependencies(hls_tests ${name})
        target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/include")
        set_target_properties(${name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        target_compile_options(${name} PRIVATE -fno-strict-aliasing)
        target_compile_definitions(${name} PRIVATE HLS_HEAP=${heap})
        add_test(NAME ${name}_native COMMAND ${name})
        add_test(NAME ${name}_unsupported_rtl COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DCONTAINER=${container}" "-DHEAP=${heap}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-analysis"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Analyze.cmake")
        set_tests_properties(${name}_unsupported_rtl PROPERTIES LABELS "hls-analysis;expected-rejection" TIMEOUT 120)
    endforeach()
endforeach()
add_test(NAME hls_std_missing_capacity COMMAND "${CMAKE_COMMAND}"
    "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
    -DCONTAINER=Vector -DHEAP=0 -DMISSING_CAPACITY=1
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/missing-capacity-analysis"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/Analyze.cmake")
set(CPPHDL_HLS_STDLIB_TREE_SOURCE "" CACHE FILEPATH "Optional matching libstdc++ tree.cc for real algorithm-body collection tests")
if(CPPHDL_HLS_STDLIB_TREE_SOURCE)
    foreach(container Map Multimap)
        add_test(NAME hls_std_${container}_library_source COMMAND "${CMAKE_COMMAND}"
            "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DCONTAINER=${container}" -DHEAP=1
            "-DSTDLIB_SOURCE=${CPPHDL_HLS_STDLIB_TREE_SOURCE}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${container}-library-analysis"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Analyze.cmake")
    endforeach()
endif()
add_hls_test(hls_recursion Recursion.cpp Recursion)
add_hls_test(hls_static_helper StaticHelper.cpp StaticHelper)
add_hls_test(hls_sram Sram.cpp SramTop)
foreach(case RANGE 0 5)
    add_test(NAME hls_recursion_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DCPPHDL=$<TARGET_FILE:cpphdl>" "-DROOT=${CMAKE_SOURCE_DIR}" "-DCASE=${case}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/reject-${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Reject.cmake")
endforeach()
