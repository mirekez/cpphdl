include("${TOOLCHAIN}")
set(reasons "constant integers" "shared scalar write" "cross-lane read/write dependence"
    "schedule.static" "supported directives" "4096 hardware lanes" "unsigned integers"
    "unsupported clause" "before reading" "carries state" "before reading"
    "outside the fixed array" "constant integers" "4096 hardware lanes"
    "supported defaults" "constant integers" "overflows" "fixed arrays" "pure helper"
    "cross-lane read/write dependence" "bound automatic variable"
    "arithmetic value parameters" "FP32" "recursive pure helpers"
    "outside the fixed array" "disjoint array writes" "before reading")
list(GET reasons ${CASE} reason)
file(REMOVE_RECURSE "${WORK}/generated")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/generated"
    "${ROOT}/hls/tests/OpenMPReject.cpp" -- "-I${ROOT}/include" -fopenmp
    "-DOPENMP_REJECT=${CASE}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
file(GLOB rtl "${WORK}/generated/*.sv")
if(NOT result EQUAL 1 OR NOT error MATCHES "OpenMP HLS:.*${reason}" OR rtl)
    message(FATAL_ERROR "Expected OpenMP rejection '${reason}' and no RTL; got ${result}:\n${output}\n${error}")
endif()
