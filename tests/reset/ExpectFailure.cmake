if(CASE STREQUAL "invalid_signature")
    set(define -DINVALID_ASYNC_RESET_SIGNATURE)
    set(expected
        "must define _reset_pos_fast_clk with signature void _reset_pos_fast_clk\\(\\)")
elseif(CASE STREQUAL "missing_negative_process")
    set(define -DINVALID_ASYNC_RESET_NEG_EDGE)
    set(expected
        "defines _reset_neg_slow_clk\\(\\) without the negative-edge process")
elseif(CASE STREQUAL "wrong_owner")
    set(define -DINVALID_ASYNC_RESET_OWNER)
    set(expected
        "register 'slow_reg' is written by InvalidAsyncReset::_reset_pos_fast_clk but is not owned by fast_clk posedge")
else()
    message(FATAL_ERROR "unknown asynchronous-reset failure case: ${CASE}")
endif()

execute_process(
    COMMAND "${CPPHDL}"
        --generated-dir "${GENERATED_DIR}"
        --primary_clock fast_clk 100000000
        --secondary_clock slow_clk 40000000
        "${SOURCE}"
        "-I${INCLUDE_DIR}"
        "${define}"
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
