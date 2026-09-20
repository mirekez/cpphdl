# Reuse the example's native/RTL matrix oracle, but replace only the private
# generated RAM read with adversarial read-during-write behavior. Attributes
# alone have no simulation effect, so ordinary Verilator tests cannot check
# whether relaxing collision semantics is safe.
foreach(required CPPHDL TEST_EXE VERILATOR CXX SOURCE_ROOT WORK)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(work "${WORK}/${run_id}")
file(MAKE_DIRECTORY "${work}")
execute_process(COMMAND "${CPPHDL}"
    "${SOURCE_ROOT}/examples/math/Transposer.cpp"
    "--generated-dir=${work}/generated" "-I${SOURCE_ROOT}/include"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Transposer conversion failed:\n${output}${error}")
endif()

set(lane "${work}/generated/TransposerLane.sv")
foreach(vendor quartus vivado)
    set(defines)
    if(vendor STREQUAL "quartus")
        set(defines +define+ALTERA_RESERVED_QIS)
    endif()
    execute_process(COMMAND "${VERILATOR}" -E -P ${defines} "${lane}"
        RESULT_VARIABLE result OUTPUT_VARIABLE rtl ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot preprocess ${vendor} lane:\n${error}")
    endif()
    if(vendor STREQUAL "quartus")
        set(expected "(* ramstyle = \"MLAB, no_rw_check\" *)")
        set(forbidden "ram_style")
        set(quartus_rtl "${rtl}")
    else()
        set(expected "(* ram_style = \"distributed\" *)")
        set(forbidden "no_rw_check")
    endif()
    string(FIND "${rtl}" "${expected}" found)
    if(found LESS 0 OR rtl MATCHES "${forbidden}")
        message(FATAL_ERROR "Incorrect ${vendor} RAM attributes:\n${rtl}")
    endif()
endforeach()

set(read "assign read_data_out = storage[read_address_in];")
string(FIND "${quartus_rtl}" "${read}" found)
if(found LESS 0)
    message(FATAL_ERROR "Lane read changed; update the collision model")
endif()
file(READ "${SOURCE_ROOT}/tests/math/TransposerMlabRdw.svh" poison)
string(REPLACE "${read}" "${poison}" quartus_rtl "${quartus_rtl}")
file(WRITE "${lane}" "${quartus_rtl}")

# The runner picks up generated/ from its working directory and exercises all
# five configurations, including SIZE=32, stalls, restarts and delayed resets.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "CPPHDL_VERILATOR=${VERILATOR}" "CPPHDL_VERILATOR_CXX=${CXX}"
    "${TEST_EXE}" --verilator-only
    WORKING_DIRECTORY "${work}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${work}/simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Collision-poisoned Transposer failed (${work}/simulation.log):\n${output}${error}")
endif()
# Non-vacuity: each lane must actually encounter the poisoned read. Match the
# SIZE token including its terminator so SIZE=3 cannot match SIZE=32.
foreach(size 2 3 4 8 32)
    string(REGEX MATCHALL "MLAB_RDW_POISON_EXERCISED SIZE=${size};" hits "${output}")
    list(LENGTH hits count)
    if(NOT count EQUAL size)
        message(FATAL_ERROR "SIZE=${size}: expected ${size} poisoned lanes, got ${count}; see ${work}/simulation.log")
    endif()
endforeach()
message(STATUS "Quartus/Vivado attributes and collision-poisoned Transposer passed; ${work}/simulation.log")
