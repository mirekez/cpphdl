include("${TOOLCHAIN}")
if(NOT DEFINED STAGES)
    set(STAGES 4)
endif()
file(REMOVE_RECURSE "${WORK}/generated")
file(MAKE_DIRECTORY "${WORK}/generated")
execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/generated" "${SOURCE}"
    -- "-I${ROOT}/include" "-DHFT_PIPELINE_STAGES=${STAGES}" -fno-exceptions ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "HFT conversion failed: ${output}\n${error}")
endif()
file(GLOB rtl "${WORK}/generated/*.sv")
set(packages "${WORK}/generated/Predef_pkg.sv"
    "${WORK}/generated/HftQuote_pkg.sv" "${WORK}/generated/HftCollection_pkg.sv"
    "${WORK}/generated/HftFrameCheck_pkg.sv" "${WORK}/generated/HftFoldedFrame_pkg.sv"
    "${WORK}/generated/HftCandidate_pkg.sv" "${WORK}/generated/HftWordParts_pkg.sv")
# Keep the explicitly ordered data packages first, followed by helper packages
# added by conversion, before any importing modules.
file(GLOB generated_packages "${WORK}/generated/*_pkg.sv")
list(REMOVE_ITEM generated_packages ${packages})
list(APPEND packages ${generated_packages})
list(REMOVE_ITEM rtl ${packages})
list(PREPEND rtl ${packages})
file(REMOVE_RECURSE "${WORK}/obj_dir")
list(JOIN HLS_CXX_FLAGS " " cxx_flags)
list(JOIN HLS_LINK_FLAGS " " link_flags)
execute_process(COMMAND "${CXX}" -print-file-name=libatomic.so OUTPUT_VARIABLE atomic OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(atomic_dir "${atomic}" DIRECTORY)
execute_process(COMMAND "${VERILATOR}" --cc --exe --top-module Hft --prefix VHft
    --Mdir "${WORK}/obj_dir" -Wno-fatal
    -CFLAGS "${cxx_flags} -I${ROOT}/include -DVERILATOR -DHFT_PIPELINE_STAGES=${STAGES}" -LDFLAGS "${link_flags} -L${atomic_dir}"
    ${rtl} "${SOURCE}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/verilator.log" "${output}\n${error}")
if(NOT result EQUAL 0 OR error MATCHES "Warning-(MULTIDRIVEN|LATCH)")
    message(FATAL_ERROR "HFT Verilator failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${MAKE}" -C "${WORK}/obj_dir" -f VHft.mk -j2 "CXX=${CXX}" "LINK=${CXX}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/build.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "HFT build failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${WORK}/obj_dir/VHft"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "HFT simulation failed: ${output}\n${error}")
endif()
message(STATUS "${output}")
