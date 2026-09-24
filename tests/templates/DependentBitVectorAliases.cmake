string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(work "${WORK}/${run_id}")
file(MAKE_DIRECTORY "${work}")
set(source "${SOURCE_ROOT}/tests/templates/DependentBitVectorAliases.h")

execute_process(COMMAND "${CXX}" -std=c++23 "-I${SOURCE_ROOT}/include"
    -DDEPENDENT_ALIAS_NATIVE -x c++ "${source}" -o "${work}/native"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native alias checks failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${work}/native" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native alias test failed: ${result}")
endif()

# Do not expose the native test's instantiations to the converter.
execute_process(COMMAND "${CPPHDL}" "--generated-dir=${work}/generated" "${source}"
    -- -std=c++23 "-I${SOURCE_ROOT}/include"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Dependent alias conversion failed:\n${output}\n${error}")
endif()
file(READ "${work}/generated/DependentBitVectorAliases.sv" rtl)
foreach(alias data_t strb_t chained_t)
    set(parameter AXI_DATA_WIDTH)
    if(alias STREQUAL "strb_t")
        set(parameter AXI_STRB_WIDTH)
    endif()
    # Integer promotions can wrap widths in SV size/sign casts. Preserve the
    # symbolic dependency; the elaborated $bits checks below verify its value.
    if(NOT rtl MATCHES "typedef logic\\[[^\n;]*${parameter}[^\n;]*-1:0\\] ${alias};")
        message(FATAL_ERROR "Missing symbolic alias '${alias}' using ${parameter}:\n${rtl}")
    endif()
endforeach()

if(VERILATOR)
    # Add only a verification block to a private copy, leaving all generated
    # type/parameter declarations intact. Local typedefs need lexical scope:
    # Verilator does not resolve them in bind parameter expressions.
    file(READ "${SOURCE_ROOT}/tests/templates/DependentBitVectorAliasesChecks.svh" checks)
    string(REPLACE "endmodule" "${checks}\nendmodule" checked_rtl "${rtl}")
    file(WRITE "${work}/DependentBitVectorAliases.sv" "${checked_rtl}")
    execute_process(COMMAND "${VERILATOR}" --binary --timing -j 1 -Wno-fatal
        --top-module DependentBitVectorAliasesTb --Mdir "${work}/obj_dir"
        "${work}/generated/Predef_pkg.sv"
        "${work}/DependentBitVectorAliases.sv"
        "${SOURCE_ROOT}/tests/templates/DependentBitVectorAliasesTb.sv"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Verilator alias checks failed to build:\n${output}\n${error}")
    endif()
    execute_process(COMMAND "${work}/obj_dir/VDependentBitVectorAliasesTb"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Verilator alias width checks failed:\n${output}\n${error}")
    endif()
else()
    message(STATUS "Verilator unavailable; checked native types and symbolic RTL only")
endif()
