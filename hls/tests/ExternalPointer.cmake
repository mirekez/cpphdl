include("${TOOLCHAIN}")
file(REMOVE_RECURSE "${WORK}/generated")
file(MAKE_DIRECTORY "${WORK}")
set(source "${ROOT}/hls/tests/ExternalPointer.cpp")
if(SYNTH)
    execute_process(COMMAND "${CPPHDL}" --synth --top ExternalPointerTop --module ExternalPointerTop
        --output "${WORK}/generated" --cxx "${CXX}" "${source}" -- "-I${ROOT}/include" ${HLS_PARSE_FLAGS}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(modules "${WORK}/generated/gates.v")
else()
    execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/generated" "${source}"
        -- "-I${ROOT}/include" ${HLS_PARSE_FLAGS}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(GLOB modules "${WORK}/generated/*.sv")
    list(REMOVE_ITEM modules "${WORK}/generated/Predef_pkg.sv")
    list(PREPEND modules "${WORK}/generated/Predef_pkg.sv")
endif()
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External pointer conversion failed: ${output}\n${error}")
endif()
list(JOIN HLS_CXX_FLAGS " " cxx_flags)
list(JOIN HLS_LINK_FLAGS " " link_flags)
execute_process(COMMAND "${CXX}" -print-file-name=libatomic.so OUTPUT_VARIABLE atomic OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(atomic_dir "${atomic}" DIRECTORY)
execute_process(COMMAND "${VERILATOR}" --binary --timing --assert -Wno-fatal
    --top-module ExternalPointerChecks --Mdir "${WORK}/obj" -j 2
    -CFLAGS "${cxx_flags} -std=c++20" -LDFLAGS "${link_flags} -L${atomic_dir}"
    -MAKEFLAGS "CXX=${CXX} LINK=${CXX}" -DMEMORY_DUT=ExternalPointerTop
    ${modules} "${ROOT}/hls/tests/ExternalPointer.sv"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/build.log" "${output}\n${error}")
if(NOT result EQUAL 0 OR error MATCHES "Warning-(LATCH|MULTIDRIVEN|UNOPTFLAT)")
    message(FATAL_ERROR "External pointer build failed: ${WORK}/build.log\n${error}")
endif()
execute_process(COMMAND "${WORK}/obj/VExternalPointerChecks"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
file(WRITE "${WORK}/simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External pointer simulation failed: ${output}\n${error}")
endif()
message(STATUS "${output}")
