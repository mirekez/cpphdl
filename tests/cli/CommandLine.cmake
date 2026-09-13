if(CASE STREQUAL "help")
    foreach(flag --help -h)
        execute_process(COMMAND "${CPPHDL}" "${flag}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT status EQUAL 0 OR NOT error STREQUAL "")
            message(FATAL_ERROR "${flag} failed (${status}): ${error}")
        endif()
        foreach(required "CppHDL - convert C++ RTL" "Before --:" "After  --:"
                --generated-dir --json-output --no-synthesis-flag --debug
                --primary_clock --secondary_clock --optimize-combs
                --optimize-combs-l1 --optimize-math --optimize-threads
                --optimize-combs-collect --optimize-combs-load
                "-I<directory>" "-DNAME[=value]" "Examples")
            string(FIND "${output}" "${required}" position)
            if(position LESS 0)
                message(FATAL_ERROR "${flag} is missing '${required}'")
            endif()
        endforeach()
        if(output MATCHES "OVERVIEW: clang|OpenCL|dxc compatibility")
            message(FATAL_ERROR "${flag} unexpectedly printed compiler help")
        endif()
    endforeach()

    # Help may follow tool options and a source; it must not parse that source
    # or create the requested output directory.
    execute_process(COMMAND "${CPPHDL}" --generated-dir "${GENERATED_DIR}/help"
            does-not-exist.cpp --help
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR EXISTS "${GENERATED_DIR}/help")
        message(FATAL_ERROR "Help attempted conversion: ${status}: ${error}")
    endif()

    # An explicit separator makes --help a Clang argument, not a tool option.
    execute_process(COMMAND "${CPPHDL}" "${TEST_DIR}/CommandLineInput.h" -- --help
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if("${output}${error}" MATCHES "CppHDL - convert C[+][+] RTL")
        message(FATAL_ERROR "CppHDL consumed compiler-side --help")
    endif()
elseif(CASE STREQUAL "forwarding")
    foreach(mode explicit legacy)
        set(separator)
        if(mode STREQUAL "explicit")
            set(separator --)
            set(output_options "--generated-dir=${GENERATED_DIR}/${mode}"
                "--json-output=${GENERATED_DIR}/${mode}/design")
        else()
            set(output_options --generated-dir "${GENERATED_DIR}/${mode}"
                --json-output "${GENERATED_DIR}/${mode}/design")
        endif()
        file(REMOVE_RECURSE "${GENERATED_DIR}/${mode}")
        execute_process(COMMAND "${CPPHDL}" ${output_options}
                "${TEST_DIR}/CommandLineInput.h" ${separator}
                "-I${TEST_DIR}/support" -DCPPHDL_CLI_VALUE=7
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${mode} forwarding failed: ${output}\n${error}")
        endif()
        foreach(artifact CommandLineInput.sv design.json)
            if(NOT EXISTS "${GENERATED_DIR}/${mode}/${artifact}")
                message(FATAL_ERROR "${mode}: missing ${artifact}")
            endif()
        endforeach()
    endforeach()
else()
    message(FATAL_ERROR "Unknown CLI test case: ${CASE}")
endif()
