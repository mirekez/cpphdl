cmake_minimum_required(VERSION 3.15)
project(tribe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)

add_executable(tribe main.cpp)

#target_compile_options(tribe PRIVATE "-O2")

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

