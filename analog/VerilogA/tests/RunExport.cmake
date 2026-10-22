file(MAKE_DIRECTORY "${WORK}")
set(output "${WORK}/module with spaces.va")
file(REMOVE "${output}")
set(mode)
if(ROOT_DRIVER)
  list(APPEND mode --verilog-a)
endif()
execute_process(COMMAND "${CONVERTER}" ${mode} "${SOURCE}"
  --top=export_test::Inverter --ports=Vdd,Vss,A,Y --configure export_test::configure
  --module=ConfiguredInverter --output "${output}"
  RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error TIMEOUT 60)
if(NOT "${result}" STREQUAL "0" OR NOT EXISTS "${output}")
  message(FATAL_ERROR "Native IC conversion failed (${result})\n${out}\n${error}")
endif()
file(READ "${output}" verilog)
if(NOT verilog MATCHES "module ConfiguredInverter" OR NOT verilog MATCHES "0.002" OR NOT verilog MATCHES "cpphdl_channel")
  message(FATAL_ERROR "Native configuration, interface or channel model lost in export")
endif()
# A digital/non-IC top must fail in the real compiler; a missing output cannot be mistaken for success.
set(invalid "${WORK}/invalid.va")
file(REMOVE "${invalid}")
execute_process(COMMAND "${CONVERTER}" ${mode} "${SOURCE}"
  --top export_test::NotIC --ports Vdd --output "${invalid}"
  RESULT_VARIABLE bad_result OUTPUT_QUIET ERROR_VARIABLE diagnostic TIMEOUT 60)
if("${bad_result}" STREQUAL "0" OR EXISTS "${invalid}" OR NOT diagnostic MATCHES "top must derive from analog::IC")
  message(FATAL_ERROR "Non-IC rejection did not produce its intended compiler diagnostic: ${diagnostic}")
endif()
