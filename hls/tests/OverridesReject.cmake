include("${TOOLCHAIN}")
set(reasons "signature mismatch" "duplicate HLS override" "cyclic HLS override"
    "override has no body" "requires an explicit override" "only forward goto"
    "concrete non-variadic function" "lifetime-extended nontrivial temporary" "variadic HLS override target")
list(GET reasons ${CASE} reason)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/generated"
    "${ROOT}/hls/tests/OverridesReject.cpp" -- "-I${ROOT}/include" "-DOVERRIDE_CASE=${CASE}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
file(GLOB rtl "${WORK}/generated/*.sv")
if(NOT result EQUAL 1 OR NOT error MATCHES "${reason}" OR rtl)
    message(FATAL_ERROR "Expected '${reason}' without partial RTL; got ${result}:\n${output}\n${error}")
endif()
