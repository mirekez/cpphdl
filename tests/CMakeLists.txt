cmake_minimum_required(VERSION 3.15)
project(tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(FILTER TEST_SOURCES EXCLUDE REGEX "/[.#][^/]*\\.cpp$")
list(FILTER TEST_SOURCES EXCLUDE REGEX "~$")
# Verilator can leave specialization work directories below a test source
# directory. They contain generated .cpp files, not independent CppHDL tests.
list(FILTER TEST_SOURCES EXCLUDE REGEX "/obj_dir/")
# This source is an integration driver for two optimizer-generated trees.
# Its custom target is declared below instead of using the converter loop.
list(FILTER TEST_SOURCES EXCLUDE REGEX "/combs/optimize_l1\\.cpp$")

foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    file(RELATIVE_PATH rel_path ${CMAKE_CURRENT_SOURCE_DIR} ${src})
    string(REPLACE "/" "_" target_name ${rel_path})
    string(REPLACE ".cpp" "" target_name ${target_name})

    add_executable(${target_name} ${src})

    include(CheckCXXCompilerFlag)
    foreach(flag -std=c++26 -std=c++2c)
        check_cxx_compiler_flag("${flag}" HAS_FLAG)
        if(HAS_FLAG)
            target_compile_options(${target_name} PRIVATE "${flag}")
            message(STATUS "Using ${flag}")
            break()
        endif()
    endforeach()

    # Public constexpr helpers must remain usable by C++17 projects even though
    # converter regressions normally compile in the newest supported language mode.
    if(rel_path STREQUAL "datatypes/ConstexprHelpers.cpp")
        target_compile_options(${target_name} PRIVATE "-std=c++17")
    endif()

    cpphdl_link_stdcxxexp_if_available(${target_name})

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests
    )

    include_directories("../include")

    set(cpphdl_json_args "")
    set(cpphdl_clock_args "")
    if(rel_path STREQUAL "cdc/TwoClocksCdc.cpp")
        set(cpphdl_clock_args
            "--primary_clock fast_clk 100000000 --secondary_clock slow_clk 40000000")
    endif()
    if(rel_path MATCHES "^json/")
        set(cpphdl_json_args "--json-output generated/${name}")
    endif()
    # This regression defines its module only outside SYNTHESIS guards.
    # It must exercise cpphdl's new opt-out rather than the default preprocessing mode.
    # Append the frontend option only for that targeted JSON extraction test.
    if(rel_path STREQUAL "json/JsonNoSynthesisFlag.cpp")
        string(APPEND cpphdl_json_args " --no-synthesis-flag")
    endif()

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${cpphdl_json_args} ${cpphdl_clock_args} ${CMAKE_CURRENT_SOURCE_DIR}/${rel_path} -I${CMAKE_SOURCE_DIR}/include ${CPPHDL_TOOLCHAIN_ARGS}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    cpphdl_add_host_opt_flags(${target_name})
    target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")

    cpphdl_add_converter_dependency(${target_name})

    add_test(
        NAME ${target_name}
        COMMAND "$<TARGET_FILE:${target_name}>"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
    set_tests_properties(${target_name} PROPERTIES
        TIMEOUT 600
    )

endforeach()

foreach(cdc_failure_case secondary_without_primary secondary_above_primary missing_methods invalid_reset_signature)
    if(cdc_failure_case STREQUAL "invalid_reset_signature")
        set(cdc_failure_source ${CMAKE_CURRENT_SOURCE_DIR}/cdc/InvalidResetSignature.h)
    else()
        set(cdc_failure_source ${CMAKE_CURRENT_SOURCE_DIR}/cdc/MissingClockMethods.h)
    endif()
    add_test(NAME cdc_${cdc_failure_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DSOURCE=${cdc_failure_source}
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_cdc_missing_methods
            -DCASE=${cdc_failure_case}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cdc/ExpectFailure.cmake)
endforeach()

set(CPPHDL_NBA_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_optimizer_nonblocking_memory")
set(CPPHDL_NBA_GENERATED
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs.cpp"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs.h"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_internal.h"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_work_0.cpp"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_memory_0.cpp"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_strobe_0.cpp")
add_custom_command(
    OUTPUT ${CPPHDL_NBA_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPHDL_NBA_TEST_DIR}"
    COMMAND "$<TARGET_FILE:cpphdl>"
            --optimize-combs NonblockingMemory
            --generated-dir "${CPPHDL_NBA_TEST_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/NonblockingMemory.h"
            -I"${CMAKE_SOURCE_DIR}/include"
    DEPENDS cpphdl "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/NonblockingMemory.h"
    VERBATIM)

add_executable(optimizer_nonblocking_memory
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/NonblockingMemoryMain.cc"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs.cpp"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_work_0.cpp"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_memory_0.cpp"
    "${CPPHDL_NBA_TEST_DIR}/NonblockingMemory_optimized_combs_strobe_0.cpp")
target_include_directories(optimizer_nonblocking_memory PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
    "${CPPHDL_NBA_TEST_DIR}")
set_target_properties(optimizer_nonblocking_memory PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
add_test(NAME optimizer_nonblocking_memory
         COMMAND "$<TARGET_FILE:optimizer_nonblocking_memory>")

set(CPPHDL_PROCEDURAL_TEST_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_optimizer_procedural_comb")
set(CPPHDL_PROCEDURAL_GENERATED
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs.cpp"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs.h"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_internal.h"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_0.cpp"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_work_0.cpp"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_strobe_0.cpp")
add_custom_command(
    OUTPUT ${CPPHDL_PROCEDURAL_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPHDL_PROCEDURAL_TEST_DIR}"
    COMMAND "$<TARGET_FILE:cpphdl>"
            --optimize-combs-l1 ProceduralComb
            --generated-dir "${CPPHDL_PROCEDURAL_TEST_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ProceduralComb.h"
            -I"${CMAKE_SOURCE_DIR}/include"
    DEPENDS cpphdl "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ProceduralComb.h"
    VERBATIM)

add_executable(optimizer_procedural_comb
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ProceduralCombMain.cc"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs.cpp"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_0.cpp"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_work_0.cpp"
    "${CPPHDL_PROCEDURAL_TEST_DIR}/ProceduralComb_optimized_combs_strobe_0.cpp")
target_include_directories(optimizer_procedural_comb PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
    "${CPPHDL_PROCEDURAL_TEST_DIR}")
set_target_properties(optimizer_procedural_comb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
add_test(NAME optimizer_procedural_comb
         COMMAND "$<TARGET_FILE:optimizer_procedural_comb>")

set(CPPHDL_OPTIMIZE_L1_NORMAL_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_l1_normal")
set(CPPHDL_OPTIMIZE_L1_L1_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_l1_l1")
set(CPPHDL_OPTIMIZE_L1_MODEL
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeL1.h")

function(cpphdl_generate_optimize_l1_fixture mode directory output_var)
    set(generated
        "${directory}/OptimizeL1_optimized_combs.cpp"
        "${directory}/OptimizeL1_optimized_combs.h"
        "${directory}/OptimizeL1_optimized_combs_internal.h"
        "${directory}/OptimizeL1_optimized_combs_0.cpp"
        "${directory}/OptimizeL1_optimized_combs_work_0.cpp"
        "${directory}/OptimizeL1_optimized_combs_strobe_0.cpp")
    add_custom_command(
        OUTPUT ${generated}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
        COMMAND "$<TARGET_FILE:cpphdl>"
                "${mode}" OptimizeL1
                --generated-dir "${directory}"
                "${CPPHDL_OPTIMIZE_L1_MODEL}"
                -I"${CMAKE_SOURCE_DIR}/include"
        DEPENDS cpphdl "${CPPHDL_OPTIMIZE_L1_MODEL}"
        VERBATIM)
    set(${output_var} ${generated} PARENT_SCOPE)
endfunction()

cpphdl_generate_optimize_l1_fixture(
    --optimize-combs "${CPPHDL_OPTIMIZE_L1_NORMAL_DIR}"
    CPPHDL_OPTIMIZE_L1_NORMAL_GENERATED)
cpphdl_generate_optimize_l1_fixture(
    --optimize-combs-l1 "${CPPHDL_OPTIMIZE_L1_L1_DIR}"
    CPPHDL_OPTIMIZE_L1_L1_GENERATED)

add_custom_target(combs_optimize_l1_generated
    DEPENDS ${CPPHDL_OPTIMIZE_L1_NORMAL_GENERATED}
            ${CPPHDL_OPTIMIZE_L1_L1_GENERATED})
add_executable(combs_optimize_l1
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/optimize_l1.cpp"
    "${CPPHDL_OPTIMIZE_L1_L1_DIR}/OptimizeL1_optimized_combs.cpp"
    "${CPPHDL_OPTIMIZE_L1_L1_DIR}/OptimizeL1_optimized_combs_0.cpp"
    "${CPPHDL_OPTIMIZE_L1_L1_DIR}/OptimizeL1_optimized_combs_work_0.cpp"
    "${CPPHDL_OPTIMIZE_L1_L1_DIR}/OptimizeL1_optimized_combs_strobe_0.cpp")
add_dependencies(combs_optimize_l1 combs_optimize_l1_generated)
target_include_directories(combs_optimize_l1 PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/combs"
    "${CPPHDL_OPTIMIZE_L1_L1_DIR}")
cpphdl_link_stdcxxexp_if_available(combs_optimize_l1)
cpphdl_add_host_opt_flags(combs_optimize_l1)
target_compile_options(combs_optimize_l1 PRIVATE "-fno-strict-aliasing")
target_compile_definitions(combs_optimize_l1 PRIVATE
    CPPHDL_OPTIMIZE_L1_NORMAL_DIR="${CPPHDL_OPTIMIZE_L1_NORMAL_DIR}"
    CPPHDL_OPTIMIZE_L1_L1_DIR="${CPPHDL_OPTIMIZE_L1_L1_DIR}")
set_target_properties(combs_optimize_l1 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
add_test(NAME combs_optimize_l1 COMMAND "$<TARGET_FILE:combs_optimize_l1>")
