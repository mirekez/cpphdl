# Process models are optional; digital and ordinary analog builds need no SDK.
option(CPPHDL_ANALOG_OSDI "Enable the native OSDI 0.3 compact-model adapter" OFF)
if(CPPHDL_ANALOG_OSDI)
  if(NOT UNIX)
    message(FATAL_ERROR "The OSDI loader currently requires a POSIX dlopen host")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(CPPHDL_OSDI_SDK "${CPPHDL_ANALOG_REAL_ROOT}/out/osdi-sdk" CACHE PATH
    "Directory for the pinned external OSDI header")
  add_custom_target(analog_osdi_sdk
    COMMAND "${Python3_EXECUTABLE}" "${CPPHDL_ANALOG_REAL_ROOT}/tests/IHPSG13G2/prepare_sdk.py"
      --output "${CPPHDL_OSDI_SDK}"
    BYPRODUCTS "${CPPHDL_OSDI_SDK}/osdi.h" VERBATIM)
  add_library(cpphdl_osdi INTERFACE)
  add_library(cpphdl::osdi ALIAS cpphdl_osdi)
  add_dependencies(cpphdl_osdi analog_osdi_sdk)
  target_include_directories(cpphdl_osdi INTERFACE "${CPPHDL_OSDI_SDK}")
  target_link_libraries(cpphdl_osdi INTERFACE cpphdl::analog ${CMAKE_DL_LIBS})
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # OSDI shared objects may rely on libm symbols supplied by their host.
    target_link_libraries(cpphdl_osdi INTERFACE "-Wl,--push-state,--no-as-needed" m "-Wl,--pop-state")
  endif()
endif()
