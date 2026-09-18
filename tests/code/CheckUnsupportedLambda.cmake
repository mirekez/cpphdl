string(TOUPPER "${CASE}" case_define)
set(source "${SOURCE_ROOT}/tests/code/UnsupportedLambda.h")
# Fresh output for every run: stale files must not hide partial RTL emission.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(work "${WORK}/${CASE}-${run_id}")
file(MAKE_DIRECTORY "${work}")

execute_process(
    COMMAND "${CXX}" -std=c++23 "-I${SOURCE_ROOT}/include"
        -DLAMBDA_${case_define} -DLAMBDA_NATIVE -x c++ "${source}"
        -o "${work}/native"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native lambda compilation failed:\n${output}${error}")
endif()
execute_process(COMMAND "${work}/native" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native lambda did not produce result = 6: ${result}")
endif()

execute_process(
    COMMAND "${CPPHDL}" "--generated-dir=${work}/generated" "${source}"
        -- -std=c++23 "-I${SOURCE_ROOT}/include" -DLAMBDA_${case_define}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
set(diagnostics "${output}${error}")
if(result EQUAL 0)
    message(FATAL_ERROR "Unsupported lambda conversion succeeded:\n${diagnostics}")
endif()
if(NOT diagnostics MATCHES "UnsupportedLambda.h:[0-9]+:[0-9]+: error: cpphdl: local lambda objects and lambda calls are not supported in RTL conversion")
    message(FATAL_ERROR "Missing source-located lambda diagnostic:\n${diagnostics}")
endif()
file(GLOB_RECURSE generated_sv "${work}/generated/*.sv")
if(generated_sv)
    message(FATAL_ERROR "Failed conversion still emitted RTL: ${generated_sv}")
endif()
