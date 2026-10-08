set(reasons "missing instantiated body" "recursive clocked call" "nonautomatic local declaration"
    "volatile/atomic" "floating-point" "indirect call" "source-selected scheduler" "was removed" "default clk" "MAX_RECURSION must be in 0..16"
    "mutable global constant object" "volatile/atomic"
    "do not fit ADDRESS_BITS" "ADDRESS_BITS must be in 8..64" "ADDRESS_BITS must be in 8..64"
    "HEAP_BYTES must be a multiple of 16" "HEAP_BYTES must be a multiple of 16" "HEAP_BYTES must be a multiple of 16"
    "BLOCK_RAM requires SHARED_MEMORY" "do not fit ADDRESS_BITS"
    "array delete is not supported" "virtual delete is not supported" "custom delete is not supported")
include("${TOOLCHAIN}")
list(GET reasons ${CASE} reason)
set(flags)
if(CASE EQUAL 7)
    set(flags --hls-kernel old_entry)
elseif(CASE EQUAL 8)
    set(flags --primary_clock cpu 100)
endif()
file(REMOVE_RECURSE "${WORK}/generated")
execute_process(COMMAND "${CPPHDL}" ${flags} --generated-dir "${WORK}/generated"
    "${CMAKE_CURRENT_LIST_DIR}/DelayedReject.cpp" -- "-I${ROOT}/include" "-DHLS_REJECT=${CASE}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
file(GLOB rtl "${WORK}/generated/*.sv")
if(CASE EQUAL 6)
    if(NOT result EQUAL 0 OR NOT rtl)
        message(FATAL_ERROR "Automatic ClockedDelayer scheduling failed:\n${output}\n${error}")
    endif()
    return()
endif()
if(NOT result EQUAL 1 OR NOT error MATCHES "${reason}" OR rtl)
    message(FATAL_ERROR "Expected '${reason}' rejection, no partial RTL; got ${result}:\n${output}\n${error}")
endif()
