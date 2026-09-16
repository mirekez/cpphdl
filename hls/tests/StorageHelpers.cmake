# Exercise the actual generated helper functions with a separate byte buffer.
# Invalid-address cases cannot safely be expressed as native C++ dereferences.
string(REGEX MATCH "localparam int MEM_BYTES = ([0-9]+);" memory_size "${rtl}")
if(NOT memory_size)
    message(FATAL_ERROR "Missing generated storage size")
endif()
file(REMOVE_RECURSE "${WORK}/helpers_obj")
execute_process(COMMAND "${VERILATOR}" --binary --top-module StorageHelpers
    --Mdir "${WORK}/helpers_obj" -Wno-fatal ${verilator_options}
    "-DHLS_STORAGE_BYTES=${CMAKE_MATCH_1}"
    -CFLAGS "${cxx_flags}" -LDFLAGS "${link_flags} -L${atomic_dir}"
    -MAKEFLAGS "CXX=${CXX} LINK=${CXX}" -j 2
    ${generated} "${CMAKE_CURRENT_LIST_DIR}/StorageHelpers.sv"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/helpers_build.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Storage helper build failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${WORK}/helpers_obj/VStorageHelpers"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/helpers_simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Storage helper simulation failed:\n${output}\n${error}")
endif()
message(STATUS "${output}")
