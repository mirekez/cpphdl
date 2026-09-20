foreach(required CPPHDL SOURCE SOURCE_ROOT TEST_EXE TOP WORK)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required argument: ${required}")
    endif()
endforeach()

# These regressions instantiate their concrete template hierarchy in code that
# is normally hidden by the converter's implicit SYNTHESIS definition.  Give
# each test a private output directory so parallel CTest runs cannot overwrite
# another specialization's generated RTL.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/generated")

execute_process(
    COMMAND "${CPPHDL}"
        --no-synthesis-flag
        --generated-dir "${WORK}/generated"
        "${SOURCE}"
        --
        "-I${SOURCE_ROOT}/include"
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    RESULT_VARIABLE convert_result
    OUTPUT_VARIABLE convert_output
    ERROR_VARIABLE convert_error)
if(NOT convert_result EQUAL 0)
    message(FATAL_ERROR
        "Template hierarchy conversion failed for ${TOP}:\n${convert_output}\n${convert_error}")
endif()

if(NOT EXISTS "${WORK}/generated/${TOP}.sv")
    message(FATAL_ERROR
        "Converter succeeded without generating ${WORK}/generated/${TOP}.sv:\n"
        "${convert_output}\n${convert_error}")
endif()

file(READ "${WORK}/generated/${TOP}.sv" rtl)
if(rtl MATCHES "unknown:|RecoveryExpr")
    message(FATAL_ERROR "Unconverted expression in ${WORK}/generated/${TOP}.sv:\n${rtl}")
endif()

get_filename_component(cpphdl_build_dir "${CPPHDL}" DIRECTORY)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "CPPHDL_BUILD_DIR=${cpphdl_build_dir}"
        "CPPHDL_VERILATOR_CFLAGS=$ENV{CPPHDL_VERILATOR_CFLAGS} -I${SOURCE_ROOT}/include"
        "${TEST_EXE}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR
        "Template hierarchy test failed for ${TOP}:\n${test_output}\n${test_error}")
endif()
