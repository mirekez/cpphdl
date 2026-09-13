# Convert into an empty tree on every run. Old generated RTL must not make a
# regression pass when preprocessing hides its concrete template instances.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/generated")
execute_process(COMMAND "${CPPHDL}" --no-synthesis-flag
        "--generated-dir=${WORK}/generated" "${SOURCE}"
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Template hierarchy conversion failed:\n${output}\n${error}")
endif()

if(TOP STREQUAL "TypeTemplateModule")
    set(TOP TypeTemplateModuleParent)
endif()
set(rtl_path "${WORK}/generated/${TOP}.sv")
if(NOT EXISTS "${rtl_path}")
    message(FATAL_ERROR "Converter did not generate ${rtl_path}:\n${output}\n${error}")
endif()
file(READ "${rtl_path}" rtl)
if(rtl MATCHES "unknown:|RecoveryExpr")
    message(FATAL_ERROR "Unconverted expression in ${rtl_path}:\n${rtl}")
endif()

# The executable runs its native checks and (where provided) its Verilator
# checks against this fresh tree. Absolute includes allow isolated work dirs.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "CPPHDL_VERILATOR_CFLAGS=$ENV{CPPHDL_VERILATOR_CFLAGS} -I${SOURCE_ROOT}/include"
        "${TEST_EXE}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
message("${output}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Template hierarchy regression failed:\n${error}")
endif()
