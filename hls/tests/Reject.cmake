file(REMOVE_RECURSE "${WORK}")
set(options --hls)
if(CASE EQUAL 5)
    set(options)
endif()
execute_process(COMMAND "${CPPHDL}" ${options} --generated-dir "${WORK}"
    "${ROOT}/hls/tests/RecursionReject.h" -- "-I${ROOT}/include" "-DHLS_BAD_CASE=${CASE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
set(expected
    "requires a nonrecursive recurse_limit method"
    "MAX_RECURSION must be between 1 and 64"
    "must have the same signature"
    "must not call recursive methods"
    "generated method name collision"
    "is recursive; use --hls")
list(GET expected ${CASE} diagnostic)
string(FIND "${error}" "${diagnostic}" found)
if(NOT result EQUAL 1 OR found EQUAL -1)
    message(FATAL_ERROR "Missing bounded-recursion diagnostic: ${diagnostic}\n${output}\n${error}")
endif()
if(EXISTS "${WORK}/RecursionReject.sv")
    message(FATAL_ERROR "Rejected recursion must not generate RTL")
endif()
