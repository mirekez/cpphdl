if(CASE STREQUAL "format")
    set(case_define TEST_STD_FORMAT)
elseif(CASE STREQUAL "print")
    set(case_define TEST_STD_PRINT)
else()
    message(FATAL_ERROR "unknown unsupported formatting case: ${CASE}")
endif()

execute_process(
    COMMAND "${CXX}" -std=c++17 "-I${INCLUDE_DIR}" -D${case_define}=1
        -x c++ -fsyntax-only "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
    message(FATAL_ERROR
        "std::${CASE} unexpectedly compiled in C++17; a silent formatting fallback may be active")
endif()

set(diagnostics "${output}\n${error}")
if(NOT diagnostics MATCHES "${CASE}")
    message(FATAL_ERROR
        "std::${CASE} failed for an unrelated reason:\n${diagnostics}")
endif()
