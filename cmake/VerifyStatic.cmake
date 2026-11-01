execute_process(COMMAND "${READELF}" --program-headers --dynamic "${BINARY}"
  RESULT_VARIABLE result OUTPUT_VARIABLE headers ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Cannot inspect ${BINARY}: ${error}")
endif()
if(headers MATCHES "INTERP|\\(NEEDED\\)")
  message(FATAL_ERROR "${BINARY} is not fully static:\n${headers}")
endif()
message(STATUS "Verified ${BINARY}: no ELF interpreter or shared-library dependencies")
