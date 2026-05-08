find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(TRIBE_TEST_ALL_SCRIPT "${CMAKE_BINARY_DIR}/tribe/.test_all.sh")
file(WRITE "${TRIBE_TEST_ALL_SCRIPT}" [=[
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

DIR="tests"
if [[ ! -d "$DIR" ]]; then
    echo "Missing tests directory: $ROOT/$DIR" >&2
    exit 1
fi

while IFS= read -r -d '' EXE; do
    echo "==> ${EXE}"
    EXE_DIR="$(dirname "$EXE")"
    EXE_NAME="$(basename "$EXE")"
    (cd "$EXE_DIR" && "./$EXE_NAME")
done < <(find "$DIR" -maxdepth 1 -type f -perm -111 -print0 | sort -z)
]=])
file(CHMOD "${TRIBE_TEST_ALL_SCRIPT}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

file(GLOB TRIBE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(FILTER TRIBE_TEST_SOURCES EXCLUDE REGEX "/spec_decode_tests\\.cpp$")

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
        "${CMAKE_CURRENT_SOURCE_DIR}/../spec"
        "${CMAKE_SOURCE_DIR}/examples/axi"
    )

    target_compile_options(${target_name} PRIVATE
        "-mavx2"
        "-fno-strict-aliasing"
    )

    target_link_libraries(${target_name} PRIVATE
        stdc++exp
    )

    add_dependencies(${target_name} cpphdl)

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory generated
        COMMAND ${CMAKE_BINARY_DIR}/cpphdl ${src}
                -I ${CMAKE_SOURCE_DIR}/include
                -I ${CMAKE_CURRENT_SOURCE_DIR}/..
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../common
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../cache
                -I ${CMAKE_CURRENT_SOURCE_DIR}/../spec
                -I ${CMAKE_SOURCE_DIR}/examples/axi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    set(test_args --noveril)

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

add_test(
    NAME Tribe_rv32_spike_fragments
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_spike_fragments.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_SPIKE_WORK_DIR}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_rv32_spike_fragments PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 180
    ENVIRONMENT "PATH=/home/me/riscv/bin:$ENV{PATH}"
)

add_test(
    NAME Tribe_rv32_spike_fragments_verilator
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_spike_fragments.py"
            "$<TARGET_FILE:tribe>"
            "${CMAKE_CURRENT_BINARY_DIR}/spike-fragments-verilator"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_rv32_spike_fragments_verilator PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "PATH=/home/me/riscv/bin:$ENV{PATH};TRIBE_SPIKE_FRAGMENTS_BACKEND=verilator"
)

add_test(
    NAME Tribe_riscv_tests_rv32
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_tests.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_RISCV_TESTS_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/riscv-tests-work"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_riscv_tests_rv32 PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "PATH=/home/me/riscv/bin:$ENV{PATH};RISCV=/home/me/riscv"
)

add_test(
    NAME Tribe_riscv_tests_rv32_verilator_supported
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_tests.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_RISCV_TESTS_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/riscv-tests-work"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_riscv_tests_rv32_verilator_supported PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 300
    ENVIRONMENT "PATH=/home/me/riscv/bin:$ENV{PATH};RISCV=/home/me/riscv;TRIBE_RISCV_TESTS_BACKEND=verilator;TRIBE_RISCV_TESTS_PATTERN=rv32ui-p-* rv32um-p-* rv32uc-p-rvc"
)

add_test(
    NAME Tribe_riscv_arch_test
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_arch_test.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_RISCV_ARCH_TEST_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/riscv-arch-test-work"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_riscv_arch_test PROPERTIES
    TIMEOUT 1200
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/pydeps/bin:/home/me/riscv/bin:$ENV{PATH};RISCV=/home/me/riscv;UV_PYTHON=/usr/bin/python3;MISE_DATA_DIR=${CMAKE_BINARY_DIR}/mise-data;MISE_CACHE_DIR=${CMAKE_BINARY_DIR}/mise-cache;MISE_CONFIG_DIR=${CMAKE_BINARY_DIR}/mise-config;MISE_STATE_DIR=${CMAKE_BINARY_DIR}/mise-state"
)

add_test(
    NAME Tribe_riscv_arch_test_verilator
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_arch_test.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_RISCV_ARCH_TEST_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/riscv-arch-test-work-verilator"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_riscv_arch_test_verilator PROPERTIES
    TIMEOUT 1200
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/pydeps/bin:/home/me/riscv/bin:$ENV{PATH};RISCV=/home/me/riscv;UV_PYTHON=/usr/bin/python3;MISE_DATA_DIR=${CMAKE_BINARY_DIR}/mise-data;MISE_CACHE_DIR=${CMAKE_BINARY_DIR}/mise-cache;MISE_CONFIG_DIR=${CMAKE_BINARY_DIR}/mise-config;MISE_STATE_DIR=${CMAKE_BINARY_DIR}/mise-state;TRIBE_ARCH_TEST_BACKEND=verilator"
)

add_test(
    NAME Tribe_riscv_dv
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_dv.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_RISCV_DV_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/riscv-dv-work"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_riscv_dv PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 120
    ENVIRONMENT "PATH=/usr/bin:/home/me/riscv/bin:$ENV{PATH};RISCV=/home/me/riscv;TRIBE_RISCV_DV_PYTHON=/usr/bin/python3"
)

add_test(
    NAME Tribe_riscv_dv_verilator
    COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_riscv_dv.py"
            "$<TARGET_FILE:tribe>"
            "${TRIBE_RISCV_DV_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/riscv-dv-work-verilator"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(Tribe_riscv_dv_verilator PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 120
    ENVIRONMENT "PATH=/usr/bin:/home/me/riscv/bin:$ENV{PATH};RISCV=/home/me/riscv;TRIBE_RISCV_DV_PYTHON=/usr/bin/python3;TRIBE_RISCV_DV_BACKEND=verilator"
)
