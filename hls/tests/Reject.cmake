include("${TOOLCHAIN}")
file(REMOVE_RECURSE "${WORK}")
execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}"
    "${ROOT}/hls/tests/RecursionReject.h" -- "-I${ROOT}/include" "-DHLS_BAD_CASE=${CASE}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(CASE EQUAL 5)
    if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/RecursionReject.sv")
        message(FATAL_ERROR "Automatic bounded recursion failed:\n${output}\n${error}")
    endif()
    file(READ "${WORK}/RecursionReject.sv" rtl)
    if(NOT rtl MATCHES "recurse_hls_9")
        message(FATAL_ERROR "Default recursion bound was not applied")
    endif()
    return()
endif()
set(expected
    "requires a nonrecursive recurse_limit method"
    "MAX_RECURSION must be between 1 and 64"
    "must have the same signature"
    "must not call recursive methods"
    "generated method name collision")
list(GET expected ${CASE} diagnostic)
string(FIND "${error}" "${diagnostic}" found)
if(NOT result EQUAL 1 OR found EQUAL -1)
    message(FATAL_ERROR "Missing bounded-recursion diagnostic: ${diagnostic}\n${output}\n${error}")
endif()
if(EXISTS "${WORK}/RecursionReject.sv")
    message(FATAL_ERROR "Rejected recursion must not generate RTL")
endif()
