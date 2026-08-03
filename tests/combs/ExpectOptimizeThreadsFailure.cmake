function(expect_failure expected)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "command unexpectedly succeeded: ${ARGN}")
    endif()
    set(message "${output}${error}")
    if(NOT message MATCHES "${expected}")
        message(FATAL_ERROR "missing diagnostic '${expected}': ${message}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(
    COMMAND "${CPPHDL}" --optimize-combs OptimizeThreads
            --optimize-threads=2 --generated-dir "${OUTPUT_DIR}"
            "${SOURCE}" -I"${INCLUDE_DIR}"
    RESULT_VARIABLE normal_result
    OUTPUT_VARIABLE normal_output
    ERROR_VARIABLE normal_error)
if(NOT normal_result EQUAL 0 OR
   NOT EXISTS "${OUTPUT_DIR}/OptimizeThreads_optimized_combs.cpp")
    message(FATAL_ERROR
        "--optimize-combs threaded generation failed: ${normal_output}${normal_error}")
endif()

expect_failure(
    "--optimize-threads requires --optimize-combs or --optimize-combs-l1"
    "${CPPHDL}" --optimize-threads 2 "${SOURCE}" -I"${INCLUDE_DIR}")
expect_failure(
    "--optimize-threads requires a positive integer"
    "${CPPHDL}" --optimize-combs-l1 OptimizeThreads --optimize-threads 0
    "${SOURCE}" -I"${INCLUDE_DIR}")
expect_failure(
    "--optimize-threads requires a positive integer"
    "${CPPHDL}" --optimize-combs OptimizeThreads --optimize-threads=invalid
    "${SOURCE}" -I"${INCLUDE_DIR}")
