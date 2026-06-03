cmake_minimum_required(VERSION 3.15)
project(tribe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

include(CTest)

set(TRIBE_RAM_BYTES_CONFIG "458752" CACHE STRING "Tribe RAM size in bytes")
set(TRIBE_IO_REGION_SIZE_CONFIG "4194304" CACHE STRING "Tribe IO region size in bytes")

set(TRIBE_BUILD_BAREMETAL_SCRIPT "${CMAKE_BINARY_DIR}/tribe_build_baremetal.sh")
file(WRITE "${TRIBE_BUILD_BAREMETAL_SCRIPT}" [=[
#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$1"
OUT_DIR="$2"

find_tool()
{
    local name="$1"

    if [[ -n "${RISCV_HOME:-}" && -x "${RISCV_HOME}/bin/${name}" ]]; then
        printf '%s\n' "${RISCV_HOME}/bin/${name}"
        return 0
    fi
    if command -v "${name}" >/dev/null 2>&1; then
        command -v "${name}"
        return 0
    fi
    if [[ -n "${RISCV:-}" && -x "${RISCV}/bin/${name}" ]]; then
        printf '%s\n' "${RISCV}/bin/${name}"
        return 0
    fi
    if [[ -x "${HOME}/riscv/bin/${name}" ]]; then
        printf '%s\n' "${HOME}/riscv/bin/${name}"
        return 0
    fi
    return 1
}

GCC="$(find_tool riscv32-unknown-elf-gcc)" || {
    echo "missing RISC-V compiler: riscv32-unknown-elf-gcc" >&2
    exit 1
}

mkdir -p "${OUT_DIR}"

"${GCC}" -march=rv32im_zicsr -mabi=ilp32 \
    -O2 -g -ffreestanding -fno-builtin -msmall-data-limit=0 -mno-relax \
    -nostdlib -nostartfiles -Wl,-Ttext=0 \
    -I "${SRC_DIR}/code" \
    "${SRC_DIR}/code/rv32i.c" \
    -o "${OUT_DIR}/rv32i.bin"

"${GCC}" -march=rv32im_zicsr -mabi=ilp32 \
    -nostdlib -nostartfiles -Wl,-Ttext=0 \
    "${SRC_DIR}/code/uart.S" \
    -o "${OUT_DIR}/uart.elf"

cp "${SRC_DIR}/code/rv32i.c" "${OUT_DIR}/"
cp "${SRC_DIR}/code/rv32i.log" "${OUT_DIR}/"
cp "${SRC_DIR}/code/uart.S" "${OUT_DIR}/"
cp "${SRC_DIR}/code/uart.log" "${OUT_DIR}/"
]=])
file(CHMOD "${TRIBE_BUILD_BAREMETAL_SCRIPT}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

function(add_tribe_target target_name l2_axi_width output_subdir)
    add_executable(${target_name} main.cpp)

    target_compile_definitions(${target_name} PRIVATE
        L2_AXI_WIDTH=${l2_axi_width}
        TRIBE_RAM_BYTES_CONFIG=${TRIBE_RAM_BYTES_CONFIG}
        TRIBE_IO_REGION_SIZE_CONFIG=${TRIBE_IO_REGION_SIZE_CONFIG}
    )

    target_include_directories(${target_name} PRIVATE
        "../include"
        "common"
        "spec"
        "cache"
        "devices"
        "../examples/axi"
    )

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${output_subdir}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/${output_subdir}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/${output_subdir}
    )

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory generated
        COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp -DL2_AXI_WIDTH=${l2_axi_width} -DTRIBE_RAM_BYTES_CONFIG=${TRIBE_RAM_BYTES_CONFIG} -DTRIBE_IO_REGION_SIZE_CONFIG=${TRIBE_IO_REGION_SIZE_CONFIG} -I ../../include -I ${CMAKE_CURRENT_SOURCE_DIR}/common -I ${CMAKE_CURRENT_SOURCE_DIR}/spec -I ${CMAKE_CURRENT_SOURCE_DIR}/devices"
        COMMAND "${TRIBE_BUILD_BAREMETAL_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/${output_subdir}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${output_subdir}
    )

    target_compile_options(${target_name} PRIVATE "-mavx2")
    target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")
    add_dependencies(${target_name} cpphdl)
    target_link_libraries(${target_name} PRIVATE stdc++exp)
endfunction()

add_tribe_target(tribe256 256 tribe256)
add_tribe_target(tribe128 128 tribe128)
add_tribe_target(tribe64 64 tribe64)

function(add_soc_target target_name l2_axi_width output_subdir)
    add_executable(${target_name} SoC/System.cpp)

    target_compile_definitions(${target_name} PRIVATE
        L2_AXI_WIDTH=${l2_axi_width}
        TRIBE_RAM_BYTES_CONFIG=${TRIBE_RAM_BYTES_CONFIG}
        TRIBE_IO_REGION_SIZE_CONFIG=${TRIBE_IO_REGION_SIZE_CONFIG}
    )

    target_include_directories(${target_name} PRIVATE
        "../include"
        "."
        "common"
        "spec"
        "cache"
        "devices"
        "../examples/axi"
    )

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${output_subdir}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/${output_subdir}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/${output_subdir}
    )

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory generated
        COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/SoC/System.cpp -DL2_AXI_WIDTH=${l2_axi_width} -DTRIBE_RAM_BYTES_CONFIG=${TRIBE_RAM_BYTES_CONFIG} -DTRIBE_IO_REGION_SIZE_CONFIG=${TRIBE_IO_REGION_SIZE_CONFIG} -I ../../include -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${CMAKE_CURRENT_SOURCE_DIR}/common -I ${CMAKE_CURRENT_SOURCE_DIR}/spec -I ${CMAKE_CURRENT_SOURCE_DIR}/cache -I ${CMAKE_CURRENT_SOURCE_DIR}/devices"
        COMMAND "${TRIBE_BUILD_BAREMETAL_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/${output_subdir}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${output_subdir}
    )

    target_compile_options(${target_name} PRIVATE "-mavx2")
    target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")
    add_dependencies(${target_name} cpphdl)
    target_link_libraries(${target_name} PRIVATE stdc++exp)
endfunction()

add_soc_target(SoC256 256 SoC256)
add_soc_target(SoC128 128 SoC128)
add_soc_target(SoC64 64 SoC64)

include(CheckCXXCompilerFlag)
foreach(flag -std=c++26 -std=c++2c)
    check_cxx_compiler_flag("${flag}" HAS_FLAG)
    if(HAS_FLAG)
        target_compile_options(tribe256 PRIVATE "${flag}")
        target_compile_options(tribe128 PRIVATE "${flag}")
        target_compile_options(tribe64 PRIVATE "${flag}")
        target_compile_options(SoC256 PRIVATE "${flag}")
        target_compile_options(SoC128 PRIVATE "${flag}")
        target_compile_options(SoC64 PRIVATE "${flag}")
        message(STATUS "Using ${flag}")
        break()
    endif()
endforeach()

if(BUILD_TESTING)
    add_subdirectory(tests)

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
