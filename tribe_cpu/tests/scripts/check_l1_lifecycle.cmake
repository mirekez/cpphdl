foreach(required CPPHDL SOURCE_ROOT WORK)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

# Convert the real testbench, including all native cache and wide-refill
# specializations. Simulation alone does not catch nonstandard lifecycle names.
file(MAKE_DIRECTORY "${WORK}")
execute_process(
    COMMAND "${CPPHDL}" "${SOURCE_ROOT}/tribe_cpu/tests/L1Cache_test.cpp"
        --generated-dir "${WORK}/generated"
        -I "${SOURCE_ROOT}/include"
        -I "${SOURCE_ROOT}/tribe_cpu/common"
        -I "${SOURCE_ROOT}/tribe_cpu/cache"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
set(diagnostic "${output}\n${error}")
file(WRITE "${WORK}/conversion.log" "${diagnostic}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "L1Cache testbench conversion failed:\n${diagnostic}")
endif()
if(diagnostic MATCHES "MISSED CALL FOUND")
    message(FATAL_ERROR "L1Cache testbench has incomplete lifecycle delegation:\n${diagnostic}")
endif()
if(NOT output MATCHES "Generated:.*L1Cache.sv")
    message(FATAL_ERROR "L1Cache RTL was not generated:\n${diagnostic}")
endif()
