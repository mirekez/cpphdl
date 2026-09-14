file(MAKE_DIRECTORY "${WORK}/generated")
execute_process(COMMAND "${CPPHDL}" --hls "--hls-kernel=${ENTRY}"
    --generated-dir "${WORK}/generated" "${SOURCE}"
    -- "-I${ROOT}/include"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Kernel conversion failed:\n${output}\n${error}")
endif()
file(READ "${WORK}/generated/kernel-source.ll" ir)
if(ENTRY STREQUAL "bounded_vector" AND (NOT ir MATCHES "vector" OR NOT ir MATCHES "memmove"))
    message(FATAL_ERROR "Expected real vector methods and erase movement in LLVM input")
endif()
execute_process(COMMAND "${CXX}" -print-file-name=libatomic.so OUTPUT_VARIABLE atomic OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(atomic_dir "${atomic}" DIRECTORY)
execute_process(COMMAND "${VERILATOR}" --cc --exe --top-module ScheduledKernel
    "-GSRAM=${SRAM}" --Mdir "${WORK}/obj_dir"
    -CFLAGS "-std=c++17 -fno-strict-aliasing -I${ROOT}/include -DVERILATOR -DHLS_SRAM_TEST=${SRAM}"
    -LDFLAGS "-static-libstdc++ -static-libgcc -L${atomic_dir}"
    "${WORK}/generated/ScheduledKernel.sv" "${SOURCE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/verilator.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Verilator failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${MAKE}" -C "${WORK}/obj_dir" -f VScheduledKernel.mk -j2 "CXX=${CXX}" "LINK=${CXX}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/build.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Build failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${WORK}/obj_dir/VScheduledKernel"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Simulation failed:\n${output}\n${error}")
endif()
message(STATUS "${output}")
if(YOSYS AND EXISTS "${YOSYS}")
    execute_process(COMMAND "${YOSYS}" -Q -T -q -p
        "read_verilog -sv \"${WORK}/generated/ScheduledKernel.sv\"; hierarchy -top ScheduledKernel -chparam SRAM ${SRAM}; proc; opt; check -assert"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${WORK}/synthesis.log" "${output}\n${error}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Yosys process lowering/check failed:\n${output}\n${error}")
    endif()
endif()
