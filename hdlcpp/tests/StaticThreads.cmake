cmake_minimum_required(VERSION 3.28)

# On newer libc versions a static link can pass without -pthread. Inspect the
# actual hdlcpp target as well, so such hosts catch the older-host regression.
file(READ "${LIBRARIES}" libraries)
string(STRIP "${libraries}" libraries)
if(NOT "-pthread" IN_LIST libraries)
    message(FATAL_ERROR "hdlcpp must explicitly link -pthread in static Linux builds")
endif()
file(MAKE_DIRECTORY "${WORK}")
set(BINARY "${WORK}/static-threads")
execute_process(COMMAND "${CXX}" -std=c++17 -static -pthread
    "${CMAKE_CURRENT_LIST_DIR}/StaticThreads.cpp" -o "${BINARY}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Static pthread link failed:\n${output}\n${error}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/VerifyStatic.cmake")
execute_process(COMMAND "${BINARY}" RESULT_VARIABLE result TIMEOUT 15)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Static pthread cancellation probe failed: ${result}")
endif()
