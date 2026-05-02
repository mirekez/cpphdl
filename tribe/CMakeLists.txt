cmake_minimum_required(VERSION 3.15)
project(tribe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

include(CTest)

add_executable(tribe main.cpp)

#target_compile_options(tribe PRIVATE "-O2")

include(CheckCXXCompilerFlag)
foreach(flag -std=c++26 -std=c++2c)
    check_cxx_compiler_flag("${flag}" HAS_FLAG)
    if(HAS_FLAG)
        target_compile_options(tribe PRIVATE "${flag}")
        message(STATUS "Using ${flag}")
        break()
    endif()
endforeach()

set_target_properties(tribe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tribe
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tribe
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tribe
)

include_directories(tribe PRIVATE "../include" "common" "spec")

# verilator tests
add_custom_command(
    TARGET tribe
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp -I ../../include -I ${CMAKE_CURRENT_SOURCE_DIR}/common -I ${CMAKE_CURRENT_SOURCE_DIR}/spec"
    COMMAND ${CMAKE_COMMAND} -E env bash -c "cp ${CMAKE_CURRENT_SOURCE_DIR}/code/rv32i.bin ${CMAKE_BINARY_DIR}/tribe/"
    COMMAND ${CMAKE_COMMAND} -E env bash -c "cp ${CMAKE_CURRENT_SOURCE_DIR}/code/rv32i.log ${CMAKE_BINARY_DIR}/tribe/"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tribe
)

target_compile_options(tribe PRIVATE "-mavx2")
target_compile_options(tribe PRIVATE "-fno-strict-aliasing")
#target_compile_options(tribe PRIVATE "-DCPPHDL_STATIC")

add_dependencies(tribe cpphdl)

target_link_libraries(tribe PRIVATE
  stdc++exp
)

if(BUILD_TESTING)
    set(RISCV_OPCODES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/riscv-opcodes" CACHE PATH "Path to a local riscv/riscv-opcodes checkout")
    if(NOT RISCV_OPCODES_DIR)
        set(RISCV_OPCODES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/riscv-opcodes")
    endif()

    add_executable(tribe_spec_decode_tests
        tests/spec_decode_tests.cpp
    )

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(TRIBE_RISCV_OPCODE_CASES_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/riscv_opcode_cases.generated.h"
    )
    add_custom_command(
        OUTPUT "${TRIBE_RISCV_OPCODE_CASES_HEADER}"
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/ensure_riscv_opcodes.py"
                "${RISCV_OPCODES_DIR}"
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/generate_riscv_opcode_cases.py"
                "${RISCV_OPCODES_DIR}"
                "${TRIBE_RISCV_OPCODE_CASES_HEADER}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/ensure_riscv_opcodes.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/generate_riscv_opcode_cases.py"
        COMMENT "Generating tribe spec decode opcode cases from riscv-opcodes"
        VERBATIM
    )
    target_sources(tribe_spec_decode_tests PRIVATE "${TRIBE_RISCV_OPCODE_CASES_HEADER}")
    target_include_directories(tribe_spec_decode_tests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )

    target_include_directories(tribe_spec_decode_tests PRIVATE
        "../include"
        "spec"
    )

    target_compile_options(tribe_spec_decode_tests PRIVATE
        "-fno-strict-aliasing"
    )

    add_test(
        NAME tribe_spec_decode_tests
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/run_spec_decode_tests.py"
                "${RISCV_OPCODES_DIR}"
                "$<TARGET_FILE:tribe_spec_decode_tests>"
    )
endif()
