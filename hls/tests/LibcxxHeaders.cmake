include("${TOOLCHAIN}")
foreach(mode explicit selected)
    set(flags ${HLS_PARSE_FLAGS})
    if(mode STREQUAL "selected")
        set(flags -stdlib=libc++)
    endif()
    file(REMOVE_RECURSE "${WORK}/${mode}")
    execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/${mode}"
        "${ROOT}/tests/code/LibcxxHeaders.h" -- "-I${ROOT}/include" ${flags}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/${mode}/LibcxxHeaders.sv")
        message(FATAL_ERROR "libc++ header selection (${mode}) failed:\n${output}\n${error}")
    endif()
endforeach()
