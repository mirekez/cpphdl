include("${TOOLCHAIN}")
set(reasons "II=1" "II=1" "memory/escaping pointers" "recursive clocked call"
    "nonautomatic local declaration" "memory/escaping pointers" "global" "STAGES must be in 1..64"
    "floating-point" "missing instantiated body" "STAGES must be in 1..64"
    "function timing/keep-box constraints" "function timing/keep-box constraints"
    "Argument/Result must match" "Argument/Result must match" "same unsigned integer width"
    "external pointer access requires ClockedMemory" "external pointer access requires ClockedMemory")
list(GET reasons ${CASE} reason)
file(REMOVE_RECURSE "${WORK}/generated")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/generated"
    "${ROOT}/hls/tests/PipelineReject.cpp" -- "-I${ROOT}/include" "-DPIPELINE_REJECT=${CASE}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
file(GLOB rtl "${WORK}/generated/*.sv")
if(NOT result EQUAL 1 OR NOT error MATCHES "${reason}" OR rtl)
    message(FATAL_ERROR "Expected pipeline rejection '${reason}' and no RTL; got ${result}:\n${output}\n${error}")
endif()
