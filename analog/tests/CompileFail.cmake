file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${CXX}" -std=c++20
  "-I${PACKAGE_ROOT}/include" "-I${PACKAGE_ROOT}/analog/include" "-I${PACKAGE_ROOT}/analog"
  -c "${SOURCE}" -o "${WORK}/invalid.o"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
  message(FATAL_ERROR "Invalid analog program unexpectedly compiled: ${SOURCE}")
endif()
get_filename_component(fixture "${SOURCE}" NAME_WE)
if(fixture STREQUAL "wrong_units")
  set(expected "no matching|no viable|cannot convert")
elseif(fixture STREQUAL "wide_bridge")
  set(expected "analog bridge widths are 1..64")
else()
  set(expected "Analog ICs require per-instance ports")
endif()
if(NOT error MATCHES "${expected}")
  message(FATAL_ERROR "Compilation failed for an unexpected reason:\n${output}\n${error}")
endif()
