if(CASE STREQUAL "secondary_without_primary")
    set(args --secondary_clock slow_clk 40000000)
    set(expected "--secondary_clock requires --primary_clock")
elseif(CASE STREQUAL "secondary_above_primary")
    set(args --primary_clock fast_clk 40000000
        --secondary_clock slow_clk 100000000)
    set(expected "must have the highest frequency")
elseif(CASE STREQUAL "missing_methods")
    set(args --generated-dir "${GENERATED_DIR}"
        --primary_clock fast_clk 100000000
        --secondary_clock slow_clk 40000000)
    set(expected "must define _work_slow_clk\\(bool\\) and _strobe_slow_clk\\(\\)")
elseif(CASE STREQUAL "invalid_reset_signature")
    set(args --generated-dir "${GENERATED_DIR}"
        --primary_clock fast_clk 100000000
        --secondary_clock slow_clk 40000000)
    set(expected "must define _work_slow_clk with signature void _work_slow_clk\\(bool reset\\)")
else()
    message(FATAL_ERROR "unknown CDC failure case: ${CASE}")
endif()

execute_process(
    COMMAND "${CPPHDL}" ${args} "${SOURCE}" "-I${INCLUDE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
set(diagnostic "${output}${error}")

if(result EQUAL 0)
    message(FATAL_ERROR "CppHDL unexpectedly accepted ${CASE}")
endif()
if(NOT diagnostic MATCHES "${expected}")
    message(FATAL_ERROR
        "CppHDL failed without the expected ${CASE} diagnostic:\n${diagnostic}")
endif()
