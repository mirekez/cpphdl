execute_process(
    COMMAND "${CPPHDL}" --optimize-math "${SOURCE}" -I"${INCLUDE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
    message(FATAL_ERROR "--optimize-math unexpectedly succeeded without a comb optimizer")
endif()

set(message "${output}${error}")
if(NOT message MATCHES
   "--optimize-math requires --optimize-combs or --optimize-combs-l1")
    message(FATAL_ERROR "missing optimize-math mode diagnostic: ${message}")
endif()
