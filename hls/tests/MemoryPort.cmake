include("${TOOLCHAIN}")
if(NOT SOURCE)
    set(SOURCE "${ROOT}/hls/tests/MemoryPort.cpp")
endif()
if(NOT BENCH)
    set(BENCH "${ROOT}/hls/tests/MemoryPort.sv")
endif()
if(NOT TOP)
    set(TOP MemoryPortChecks)
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/generated")
if(BLOCK_RAM)
    list(APPEND HLS_PARSE_FLAGS -DHLS_BLOCK_RAM=1)
    set(bench_definitions -DHLS_BLOCK_RAM=1)
endif()
execute_process(COMMAND "${CPPHDL}" --hls --generated-dir "${WORK}/generated"
    "${SOURCE}" -- "-I${ROOT}/include" -fno-exceptions ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Conversion failed:\n${output}\n${error}")
endif()
file(GLOB modules "${WORK}/generated/cpphdl_hls_*.sv")
list(GET modules 0 module)
get_filename_component(module_name "${module}" NAME_WE)
file(READ "${module}" source)
if(NOT source MATCHES "localparam int MEM_BYTES = ([0-9]+);")
    message(FATAL_ERROR "Missing bounded storage extent")
endif()
set(bytes "${CMAKE_MATCH_1}")
list(JOIN HLS_CXX_FLAGS " " cxx_flags)
list(JOIN HLS_LINK_FLAGS " " link_flags)
execute_process(COMMAND "${CXX}" -print-file-name=libatomic.so OUTPUT_VARIABLE atomic OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(atomic_dir "${atomic}" DIRECTORY)
execute_process(COMMAND "${VERILATOR}" --binary --timing --assert -Wno-fatal
    --top-module "${TOP}" --Mdir "${WORK}/obj_dir" -j 2
    -CFLAGS "${cxx_flags} -std=c++20" -LDFLAGS "${link_flags} -L${atomic_dir}"
    -MAKEFLAGS "CXX=${CXX} LINK=${CXX}"
    "-DMEMORY_DUT=${module_name}" "-DMEMORY_BYTES=${bytes}"
    ${bench_definitions}
    "${module}" "${BENCH}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/build.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Build failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${WORK}/obj_dir/V${TOP}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Memory port regression failed:\n${output}\n${error}")
endif()
message(STATUS "${output}")
