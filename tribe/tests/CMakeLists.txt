find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(TRIBE_TEST_ALL_SCRIPT "${CMAKE_BINARY_DIR}/tribe/.test_all.sh")
file(WRITE "${TRIBE_TEST_ALL_SCRIPT}" [=[
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

ARGS=()
for ARG in "$@"; do
    case "$ARG" in
        --noveril)
            ARGS+=("$ARG")
            ;;
        -h|--help)
            echo "usage: $0 [--noveril]"
            exit 0
            ;;
        *)
            echo "Unknown option: $ARG" >&2
            echo "usage: $0 [--noveril]" >&2
            exit 2
            ;;
    esac
done

DIR="tests"
if [[ ! -d "$DIR" ]]; then
    echo "Missing tests directory: $ROOT/$DIR" >&2
    exit 1
fi

BUILD_DIR="$(cd "$ROOT/.." && pwd)"
while IFS= read -r -d '' EXE; do
    TARGET_NAME="$(basename "$EXE")"
    cmake --build "$BUILD_DIR" --target "$TARGET_NAME"
done < <(find "$DIR" -maxdepth 1 -type f -perm -111 ! -name '*.elf' -print0 | sort -z)

while IFS= read -r -d '' EXE; do
    echo "==> ${EXE}"
    EXE_DIR="$(dirname "$EXE")"
    EXE_NAME="$(basename "$EXE")"
    (cd "$EXE_DIR" && "./$EXE_NAME" "${ARGS[@]}")
done < <(find "$DIR" -maxdepth 1 -type f -perm -111 ! -name '*.elf' -print0 | sort -z)
]=])
file(CHMOD "${TRIBE_TEST_ALL_SCRIPT}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

file(GLOB TRIBE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(APPEND TRIBE_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Linux_test.cpp"
)
list(REMOVE_DUPLICATES TRIBE_TEST_SOURCES)
list(FILTER TRIBE_TEST_SOURCES EXCLUDE REGEX "/spec_decode_tests\\.cpp$")
list(FILTER TRIBE_TEST_SOURCES EXCLUDE REGEX "/[.#][^/]*\\.cpp$")
list(FILTER TRIBE_TEST_SOURCES EXCLUDE REGEX "~$")

foreach(src ${TRIBE_TEST_SOURCES})
    get_filename_component(target_name ${src} NAME_WE)

    add_executable(${target_name} ${src})

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_include_directories(${target_name} PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../cache"
        "${CMAKE_CURRENT_SOURCE_DIR}/../devices"
        "${CMAKE_CURRENT_SOURCE_DIR}/../spec"
        "${CMAKE_SOURCE_DIR}/examples/axi"
    )

    target_compile_options(${target_name} PRIVATE
        "-mavx2"
        "-fno-strict-aliasing"
    )
    set(cpphdl_extra_defines "")
    if(target_name STREQUAL "Linux_test")
        target_compile_definitions(${target_name} PRIVATE
            L2_AXI_WIDTH=64
            TRIBE_RAM_BYTES_CONFIG=33554432
            TRIBE_IO_REGION_SIZE_CONFIG=1048576
        )
        set(cpphdl_extra_defines
            -DL2_AXI_WIDTH=64
            -DTRIBE_RAM_BYTES_CONFIG=33554432
            -DTRIBE_IO_REGION_SIZE_CONFIG=1048576
        )
        set(test_args --noveril --prepare-only)
    else()
        set(test_args --noveril)
    endif()

    target_link_libraries(${target_name} PRIVATE
        stdc++exp
    )

    add_dependencies(${target_name} cpphdl)
    if(target_name STREQUAL "CLINT_test" OR target_name STREQUAL "MMU_TLB_Test" OR target_name STREQUAL "NS16550A_test" OR target_name STREQUAL "Accelerator_test" OR target_name STREQUAL "CPU_test" OR target_name STREQUAL "Linux_test")
        add_dependencies(${target_name} tribe64)
    endif()

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory generated
        COMMAND ${CMAKE_BINARY_DIR}/cpphdl ${src}
                ${cpphdl_extra_defines}
                -I ${CMAKE_SOURCE_DIR}/include
                -I ${CMAKE_CURRENT_SOURCE_DIR}/..
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../common
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../cache
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../devices
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../spec
                -I ${CMAKE_SOURCE_DIR}/examples/axi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_test(
        NAME ${target_name}
        COMMAND ${target_name} ${test_args}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

set(TRIBE_SPIKE_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/spike-fragments")
set(TRIBE_RISCV_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/riscv-tests" CACHE PATH "Path to a local riscv-tests checkout")
set(TRIBE_RISCV_ARCH_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/riscv-arch-test" CACHE PATH "Path to a local riscv-arch-test checkout")
set(TRIBE_RISCV_DV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/riscv-dv" CACHE PATH "Path to a local riscv-dv checkout")

function(add_tribe_width_tests width target_name)
    set(prefix "Tribe${width}")
    set(work_suffix "w${width}")

    add_test(
        NAME ${prefix}_rv32_spike_fragments
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_spike_fragments.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_SPIKE_WORK_DIR}-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_rv32_spike_fragments PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 180
    )

    add_test(
        NAME ${prefix}_rv32_spike_fragments_verilator
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_spike_fragments.py"
                "$<TARGET_FILE:${target_name}>"
                "${CMAKE_CURRENT_BINARY_DIR}/spike-fragments-verilator-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_rv32_spike_fragments_verilator PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 300
        ENVIRONMENT "TRIBE_SPIKE_FRAGMENTS_BACKEND=verilator"
    )

    add_test(
        NAME ${prefix}_riscv_tests_rv32
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_tests.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_RISCV_TESTS_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/riscv-tests-work-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_riscv_tests_rv32 PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 300
        ENVIRONMENT "TRIBE_RISCV_TESTS_PATTERN=rv32ui-p-* rv32ua-p-* rv32um-p-* rv32uc-p-rvc;TRIBE_RISCV_TESTS_EXCLUDE_PATTERN=rv32ui-p-ma_data"
    )

    add_test(
        NAME ${prefix}_riscv_tests_rv32_verilator_supported
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_tests.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_RISCV_TESTS_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/riscv-tests-work-verilator-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_riscv_tests_rv32_verilator_supported PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 300
        ENVIRONMENT "TRIBE_RISCV_TESTS_BACKEND=verilator;TRIBE_RISCV_TESTS_PATTERN=rv32ui-p-* rv32ua-p-* rv32um-p-* rv32uc-p-rvc;TRIBE_RISCV_TESTS_EXCLUDE_PATTERN=rv32ui-p-ma_data"
    )

    add_test(
        NAME ${prefix}_riscv_arch_test
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_arch_test.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_RISCV_ARCH_TEST_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/riscv-arch-test-work-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_riscv_arch_test PROPERTIES
        TIMEOUT 1200
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/pydeps/bin:$ENV{PATH};UV_PYTHON=3.10;MISE_DATA_DIR=${CMAKE_BINARY_DIR}/mise-data;MISE_CACHE_DIR=${CMAKE_BINARY_DIR}/mise-cache;MISE_CONFIG_DIR=${CMAKE_BINARY_DIR}/mise-config;MISE_STATE_DIR=${CMAKE_BINARY_DIR}/mise-state;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/xdg-data;XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/xdg-cache"
    )

    add_test(
        NAME ${prefix}_riscv_arch_test_verilator
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_arch_test.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_RISCV_ARCH_TEST_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/riscv-arch-test-work-verilator-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_riscv_arch_test_verilator PROPERTIES
        TIMEOUT 1200
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/pydeps/bin:$ENV{PATH};UV_PYTHON=3.10;MISE_DATA_DIR=${CMAKE_BINARY_DIR}/mise-data;MISE_CACHE_DIR=${CMAKE_BINARY_DIR}/mise-cache;MISE_CONFIG_DIR=${CMAKE_BINARY_DIR}/mise-config;MISE_STATE_DIR=${CMAKE_BINARY_DIR}/mise-state;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/xdg-data;XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/xdg-cache;TRIBE_ARCH_TEST_BACKEND=verilator"
    )

    add_test(
        NAME ${prefix}_riscv_dv
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_dv.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_RISCV_DV_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/riscv-dv-work-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_riscv_dv PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        ENVIRONMENT "PATH=/usr/bin:$ENV{PATH};TRIBE_RISCV_DV_PYTHON=/usr/bin/python3;TRIBE_RISCV_DV_ISA=rv32imac_zicsr_zifencei"
    )

    add_test(
        NAME ${prefix}_riscv_dv_verilator
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_dv.py"
                "$<TARGET_FILE:${target_name}>"
                "${TRIBE_RISCV_DV_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/riscv-dv-work-verilator-${work_suffix}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${prefix}_riscv_dv_verilator PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        ENVIRONMENT "PATH=/usr/bin:$ENV{PATH};TRIBE_RISCV_DV_PYTHON=/usr/bin/python3;TRIBE_RISCV_DV_ISA=rv32imac_zicsr_zifencei;TRIBE_RISCV_DV_BACKEND=verilator"
    )
endfunction()

add_tribe_width_tests(256 tribe256)
add_tribe_width_tests(128 tribe128)
add_tribe_width_tests(64 tribe64)
