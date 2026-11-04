cmake_minimum_required(VERSION 3.15)
project(cpphdl_lib LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

include("${CMAKE_SOURCE_DIR}/cmake/CpphdlHeaderTests.cmake")
cpphdl_add_header_tests(LABEL lib DIRECT_RTL_SOURCES
    memory/SynchronousRam.h memory/MaskedSynchronousRam.h memory/MatrixTransfer.h
    math/Fp32Mac.h math/Fp32Bits.h math/Fp32Add.h)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME math_standalone_headers COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/math/StandaloneHeaders.py" --cxx "${CPPHDL_TEST_CXX}")
        set_tests_properties(math_standalone_headers PROPERTIES TIMEOUT 120 LABELS "lib;math;rtl")
        add_test(NAME lib_header_contract COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/HeaderExamples.py" --cxx "${CPPHDL_TEST_CXX}" --library-only)
        set_tests_properties(lib_header_contract PROPERTIES TIMEOUT 180 LABELS "lib;rtl")
    endif()
endif()

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    find_program(CPPHDL_EXAMPLE_VERILATOR NAMES verilator HINTS "${CPPHDL_LOCAL_CONDA_PREFIX}/bin")
    add_executable(math_FpConverter_options "${CMAKE_SOURCE_DIR}/tests/ExampleMain.cpp")
    target_compile_definitions(math_FpConverter_options PRIVATE MAINFILE FP_CONVERTER_OPTIONS_TEST)
    target_compile_options(math_FpConverter_options PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/math/FpConverter.h")
    target_include_directories(math_FpConverter_options PRIVATE "${CMAKE_SOURCE_DIR}/include")
    cpphdl_add_converter_dependency(math_FpConverter_options)
    cpphdl_strip_gnu_property_after_build(math_FpConverter_options)
    cpphdl_add_host_opt_flags(math_FpConverter_options)
    cpphdl_link_stdcxxexp_if_available(math_FpConverter_options)
    add_test(NAME math_FpConverter_options_native COMMAND math_FpConverter_options)
    set_tests_properties(math_FpConverter_options_native PROPERTIES TIMEOUT 60 LABELS "rtl;lib;math")
    if(Python3_Interpreter_FOUND AND CPPHDL_EXAMPLE_VERILATOR)
        add_test(NAME math_FpConverter_options_verilator COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/examples/RunRtl.py"
            --cpphdl "$<TARGET_FILE:${CPPHDL_TEST_TARGET}>" --cxx "${CPPHDL_TEST_CXX}"
            --verilator "${CPPHDL_EXAMPLE_VERILATOR}"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/math/FpConverter.h"
            --top FpConverterOptions --define FP_CONVERTER_OPTIONS_TEST
            --work "${CMAKE_CURRENT_BINARY_DIR}/rtl-tests")
        set_tests_properties(math_FpConverter_options_verilator PROPERTIES TIMEOUT 300 LABELS "rtl;lib;math")
    endif()
endif()
