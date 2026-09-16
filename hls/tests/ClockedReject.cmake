set(reasons "missing instantiated body" "recursive clocked call" "nonautomatic local declaration"
    "volatile/atomic" "floating-point" "indirect call" "requires --hls" "was removed" "default clk" "MAX_RECURSION must be in 0..16"
    "mutable global constant object" "volatile/atomic")
include("${TOOLCHAIN}")
list(GET reasons ${CASE} reason)
set(flags --hls)
if(CASE EQUAL 6)
    set(flags)
elseif(CASE EQUAL 7)
    set(flags --hls --hls-kernel old_entry)
elseif(CASE EQUAL 8)
    set(flags --hls --primary_clock cpu 100)
endif()
file(REMOVE_RECURSE "${WORK}/generated")
execute_process(COMMAND "${CPPHDL}" ${flags} --generated-dir "${WORK}/generated"
    "${CMAKE_CURRENT_LIST_DIR}/ClockedReject.cpp" -- "-I${ROOT}/include" "-DHLS_REJECT=${CASE}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
file(GLOB rtl "${WORK}/generated/*.sv")
if(NOT result EQUAL 1 OR NOT error MATCHES "${reason}" OR rtl)
    message(FATAL_ERROR "Expected '${reason}' rejection, no partial RTL; got ${result}:\n${output}\n${error}")
endif()
