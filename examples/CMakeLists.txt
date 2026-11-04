cmake_minimum_required(VERSION 3.15)
project(examples LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

include("${CMAKE_SOURCE_DIR}/cmake/CpphdlHeaderTests.cmake")
cpphdl_add_header_tests(LABEL examples DIRECT_RTL_SOURCES
    math/Fp32Exp.h math/Maximum.h)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME examples_header_contract COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/HeaderExamples.py" --cxx "${CPPHDL_TEST_CXX}")
        set_tests_properties(examples_header_contract PROPERTIES TIMEOUT 300 LABELS "examples;rtl")
    endif()
endif()
