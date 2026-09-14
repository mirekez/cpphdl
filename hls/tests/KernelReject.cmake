file(MAKE_DIRECTORY "${WORK}")
# A failed regeneration must not leave last run's executable-looking RTL.
file(WRITE "${WORK}/ScheduledKernel.sv" "stale output")
set(reasons "external implementation missing" "indirect call" "recursive kernel"
    "only scalar integers" "dynamic or oversized stack allocation" "atomic/volatile" "cpphdl_hls_state_align")
list(GET reasons ${CASE} reason)
execute_process(COMMAND "${CPPHDL}" --hls --hls-kernel rejected_kernel --generated-dir "${WORK}"
    "${ROOT}/hls/tests/KernelReject.cpp" -- "-DREJECT_KIND=${CASE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR NOT error MATCHES "HLS kernel error: ${reason}" OR EXISTS "${WORK}/ScheduledKernel.sv")
    message(FATAL_ERROR "Expected '${reason}' rejection with no stale SV, got ${result}:\n${output}\n${error}")
endif()
