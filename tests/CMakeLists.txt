cmake_minimum_required(VERSION 3.15)
project(tests LANGUAGES CXX)
add_test(NAME optimizer_generated_helper COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/GeneratedHelperTest.sh"
    "$<TARGET_FILE:cpphdl>" "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer" "${CMAKE_CXX_COMPILER}")
set_tests_properties(optimizer_generated_helper PROPERTIES TIMEOUT 120)

add_test(NAME optimizer_template_helper COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/TemplateHelperTest.sh"
    "$<TARGET_FILE:cpphdl>" "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer" "${CMAKE_CXX_COMPILER}")
add_test(NAME optimizer_indexed_proof COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/IndexedProofTest.sh"
    "$<TARGET_FILE:cpphdl>" "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/optimizer" "${CMAKE_CXX_COMPILER}")
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    foreach(flow cpp reject)
        add_test(NAME cast_${flow} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/code/CastChecks.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/casts" --flow ${flow})
        set_tests_properties(cast_${flow} PROPERTIES TIMEOUT 240)
    endforeach()
    if(CPPHDL_BUILD_HDLCPP)
        add_test(NAME native_graph_pipeline COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/graph/NativeGraphChecks.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --hdlcpp "$<TARGET_FILE:hdlcpp>"
            --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/native-graph")
        set_tests_properties(native_graph_pipeline PROPERTIES TIMEOUT 300
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        add_test(NAME cpp_graph_pipeline COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/graph/CppGraphChecks.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --hdlcpp "$<TARGET_FILE:hdlcpp>"
            --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/cpp-graph")
        set_tests_properties(cpp_graph_pipeline PROPERTIES TIMEOUT 300
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    endif()
    add_test(NAME netlist_driver COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/netlist/test_driver.py")
    set_tests_properties(netlist_driver PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    find_program(CPPHDL_NETLIST_VERILATOR verilator)
    if(CPPHDL_NETLIST_VERILATOR)
        add_test(NAME cast_parameter_widths COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/code/CastChecks.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/casts" --flow widths
            --verilator "${CPPHDL_NETLIST_VERILATOR}")
        set_tests_properties(cast_parameter_widths PROPERTIES TIMEOUT 240)
        add_test(NAME cast_verilator COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/code/CastChecks.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --cxx "${CMAKE_CXX_COMPILER}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/casts" --flow verilator
            --verilator "${CPPHDL_NETLIST_VERILATOR}")
        set_tests_properties(cast_verilator PROPERTIES TIMEOUT 240)
        add_test(NAME netlist_converter COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/netlist/ConverterChecks.py"
            --cpphdl "$<TARGET_FILE:cpphdl>" --source-root "${CMAKE_SOURCE_DIR}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/netlist-converter"
            --verilator "${CPPHDL_NETLIST_VERILATOR}")
    endif()
    find_program(CPPHDL_NETLIST_YOSYS yosys)
    find_program(CPPHDL_NETLIST_SV2V sv2v)
    find_path(CPPHDL_CXXRTL_RUNTIME cxxrtl/cxxrtl.h
        PATHS /usr/share/yosys/include/backends/cxxrtl/runtime
              /usr/local/share/yosys/include/backends/cxxrtl/runtime)
    if(CPPHDL_NETLIST_YOSYS AND CPPHDL_NETLIST_SV2V AND CPPHDL_CXXRTL_RUNTIME)
        if(CPPHDL_BUILD_HDLCPP)
            add_test(NAME word_pipeline COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/netlist/WordChecks.py"
                --cpphdl "$<TARGET_FILE:cpphdl>" --hdlcpp "$<TARGET_FILE:hdlcpp>"
                --sv2v "${CPPHDL_NETLIST_SV2V}" --yosys "${CPPHDL_NETLIST_YOSYS}"
                --runtime "${CPPHDL_CXXRTL_RUNTIME}" --cxx "${CMAKE_CXX_COMPILER}"
                --work "${CMAKE_CURRENT_BINARY_DIR}/word-pipeline")
            set_tests_properties(word_pipeline PROPERTIES TIMEOUT 300
                ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        endif()
        add_test(NAME netlist_word_graph COMMAND "${CMAKE_COMMAND}"
            "-DPYTHON=${Python3_EXECUTABLE}" "-DCPPHDL=$<TARGET_FILE:cpphdl>"
            "-DSV2V=${CPPHDL_NETLIST_SV2V}" "-DYOSYS=${CPPHDL_NETLIST_YOSYS}"
            "-DCXXRTL_RUNTIME=${CPPHDL_CXXRTL_RUNTIME}" "-DCXX=${CMAKE_CXX_COMPILER}"
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/netlist"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/netlist/Smoke.cmake")
        set_tests_properties(netlist_word_graph PROPERTIES TIMEOUT 180)
    endif()
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Unsupported closures must fail conversion, not emit malformed RTL.
foreach(lambda_case reference value captureless immediate explicit reference_alias)
    add_test(NAME code_unsupported_lambda_${lambda_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DCXX=${CMAKE_CXX_COMPILER}
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DCASE=${lambda_case}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/unsupported_lambda
            -P ${CMAKE_CURRENT_SOURCE_DIR}/code/CheckUnsupportedLambda.cmake)
    set_tests_properties(code_unsupported_lambda_${lambda_case} PROPERTIES TIMEOUT 60)
endforeach()

# These tests select concrete template types/widths in their native testbench.
# Parse that testbench too, so conversion sees the same implicit instances.
set(template_hierarchy_tests
    code/TypeTemplateModule.cpp
    templates/TemplateArrayDimension.cpp
    templates/TemplateBaseAliasPackage.cpp
    templates/TemplateTypeWidth.cpp
    templates/TemplateInheritedBaseArray.cpp)

list(FILTER TEST_SOURCES EXCLUDE REGEX "/[.#][^/]*\\.cpp$")
list(FILTER TEST_SOURCES EXCLUDE REGEX "~$")
# Verilator can leave specialization work directories below a test source
# directory. They contain generated .cpp files, not independent CppHDL tests.
list(FILTER TEST_SOURCES EXCLUDE REGEX "/obj_dir/")
# This source is an integration driver for two optimizer-generated trees.
# Its custom target is declared below instead of using the converter loop.
list(FILTER TEST_SOURCES EXCLUDE REGEX "/combs/OptimizeL1\\.cpp$")
list(FILTER TEST_SOURCES EXCLUDE REGEX "/combs/OptimizeMath\\.cpp$")
list(FILTER TEST_SOURCES EXCLUDE REGEX "/combs/OptimizeThreads\\.cpp$")

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

    # Public header APIs must remain usable by C++17 projects even though converter
    # regressions normally compile in the newest supported language mode.
    if(rel_path STREQUAL "datatypes/ConstexprHelpers.cpp" OR
       rel_path STREQUAL "datatypes/PackedWriteback.cpp" OR
       rel_path STREQUAL "datatypes/ArrayUnpacked.cpp" OR
       rel_path STREQUAL "datatypes/PointerPorts.cpp" OR
       rel_path STREQUAL "datatypes/NoStdFormatCpp17.cpp")
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
    if(rel_path STREQUAL "cdc/TwoClocksCdc.cpp" OR
       rel_path STREQUAL "cdc/LifecycleDelegation.cpp")
        set(cpphdl_clock_args
            "--primary_clock fast_clk 100000000 --secondary_clock slow_clk 40000000")
    endif()
    if(rel_path STREQUAL "reset/AsyncReset.cpp")
        set(cpphdl_clock_args
            "--primary_clock fast_clk 100000000 --secondary_clock slow_clk 40000000")
    endif()
    if(rel_path MATCHES "^json/")
        set(cpphdl_json_args "--json-output generated/${name}")
    endif()
    # Include guarded definitions/instances in tests that require them.
    if(rel_path STREQUAL "json/JsonNoSynthesisFlag.cpp" OR rel_path IN_LIST template_hierarchy_tests)
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

    if(rel_path IN_LIST template_hierarchy_tests)
        set(template_top ${name})
        if(rel_path STREQUAL "code/TypeTemplateModule.cpp")
            set(template_top TypeTemplateModuleParent)
        endif()
        add_test(NAME ${target_name}
            COMMAND ${CMAKE_COMMAND}
                -DCPPHDL=$<TARGET_FILE:cpphdl>
                -DSOURCE=${src}
                -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
                -DTEST_EXE=$<TARGET_FILE:${target_name}>
                -DTOP=${template_top}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/template_hierarchy/${target_name}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/templates/TemplateHierarchy.cmake)
    else()
        add_test(
            NAME ${target_name}
            COMMAND "$<TARGET_FILE:${target_name}>"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        )
    endif()
    set_tests_properties(${target_name} PROPERTIES
        TIMEOUT 600
    )
    # Several converter regressions deliberately launch Verilator from the
    # generated C++ test executable.  CMake finds LLVM and Verilator in the
    # repository-local Conda prefix, but CTest does not implicitly activate
    # that environment.  Give subprocesses the same tools used by the build so
    # a plain `ctest` is reproducible without requiring the caller to modify
    # PATH or run `conda activate` first.
    if(CPPHDL_LOCAL_CONDA_PREFIX AND
       EXISTS "${CPPHDL_LOCAL_CONDA_PREFIX}/bin/verilator")
        set_tests_properties(${target_name} PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${CPPHDL_LOCAL_CONDA_PREFIX}/bin;CONDA_PREFIX=set:${CPPHDL_LOCAL_CONDA_PREFIX}"
        )
    endif()

endforeach()

# Exercise the byte-oriented fallback even on little-endian development hosts.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(datatypes_PackedWritebackPortable datatypes/PackedWriteback.cpp)
    set_target_properties(datatypes_PackedWritebackPortable PROPERTIES CXX_STANDARD 17)
    target_include_directories(datatypes_PackedWritebackPortable PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_options(datatypes_PackedWritebackPortable PRIVATE -U__BYTE_ORDER__)
    cpphdl_link_stdcxxexp_if_available(datatypes_PackedWritebackPortable)
    add_test(NAME datatypes_PackedWritebackPortable COMMAND datatypes_PackedWritebackPortable)
endif()

find_program(CPPHDL_TEMPLATE_VERILATOR NAMES verilator HINTS "${CPPHDL_LOCAL_CONDA_PREFIX}/bin")
find_program(CPPHDL_REPLACEMENT_MAKE NAMES gmake make)
add_test(NAME code_NativeLambdaHarness
    COMMAND ${CMAKE_COMMAND}
        -DCPPHDL=$<TARGET_FILE:cpphdl>
        -DCXX=${CMAKE_CXX_COMPILER}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/native_lambda_harness
        -DVERILATOR=${CPPHDL_TEMPLATE_VERILATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/code/NativeLambdaHarness.cmake)
set_tests_properties(code_NativeLambdaHarness PROPERTIES TIMEOUT 60)

add_test(NAME templates_DependentBitVectorAliases
    COMMAND ${CMAKE_COMMAND}
        -DCPPHDL=$<TARGET_FILE:cpphdl>
        -DCXX=${CMAKE_CXX_COMPILER}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/dependent_bit_vector_aliases
        -DVERILATOR=${CPPHDL_TEMPLATE_VERILATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/templates/DependentBitVectorAliases.cmake)
set_tests_properties(templates_DependentBitVectorAliases PROPERTIES TIMEOUT 120)
if(CPPHDL_LOCAL_CONDA_PREFIX)
    set_tests_properties(templates_DependentBitVectorAliases PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CPPHDL_LOCAL_CONDA_PREFIX}/bin;CONDA_PREFIX=set:${CPPHDL_LOCAL_CONDA_PREFIX}")
endif()

foreach(replacement_case standalone concrete partial expressions named parent)
    add_test(NAME format_replacement_defaults_${replacement_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DCASE=${replacement_case}
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_replacement_defaults
            -DVERILATOR=${CPPHDL_TEMPLATE_VERILATOR}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DMAKE=${CPPHDL_REPLACEMENT_MAKE}
            -DCONDA_PREFIX=${CPPHDL_LOCAL_CONDA_PREFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/format/ReplacementDefaults.cmake)
    set_tests_properties(format_replacement_defaults_${replacement_case} PROPERTIES TIMEOUT 120)
endforeach()

foreach(standalone_case numeric numeric_instantiated fp_sqrt)
    set(fp_sqrt_test_arg)
    if(standalone_case STREQUAL "fp_sqrt" AND TARGET math_FpSqrt AND CPPHDL_TEMPLATE_VERILATOR)
        set(fp_sqrt_test_arg -DFP_SQRT_TEST=$<TARGET_FILE:math_FpSqrt>)
    endif()
    add_test(NAME templates_standalone_${standalone_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DCASE=${standalone_case}
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_standalone
            -DVERILATOR=${CPPHDL_TEMPLATE_VERILATOR}
            ${fp_sqrt_test_arg}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/templates/StandaloneNumeric.cmake)
    set_tests_properties(templates_standalone_${standalone_case} PROPERTIES TIMEOUT 300)
    if(CPPHDL_LOCAL_CONDA_PREFIX)
        set_tests_properties(templates_standalone_${standalone_case} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CPPHDL_LOCAL_CONDA_PREFIX}/bin;CONDA_PREFIX=set:${CPPHDL_LOCAL_CONDA_PREFIX}")
    endif()
endforeach()

foreach(cli_case help forwarding)
    add_test(NAME cli_${cli_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DCASE=${cli_case}
            -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cli
            -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_cli
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/CommandLine.cmake)
    set_tests_properties(cli_${cli_case} PROPERTIES TIMEOUT 60)
endforeach()

foreach(lifecycle_case valid register memory child_work child_strobe work_method strobe_method wrong_phase dead_helper inheritance hidden_base named missing_named missing_legacy empty missing_effectful deferred_template deferred_struct empty_delegated missing_effectful_delegated derived composed_base assign_ports constexpr_ports tuple_pipeline)
    add_test(NAME lint_lifecycle_${lifecycle_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DCASE=${lifecycle_case}
            -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/lint
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_lifecycle
            -P ${CMAKE_CURRENT_SOURCE_DIR}/lint/CheckMissedCalls.cmake)
    set_tests_properties(lint_lifecycle_${lifecycle_case} PROPERTIES TIMEOUT 60)
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

foreach(async_reset_failure_case invalid_signature missing_negative_process wrong_owner)
    add_test(NAME reset_async_${async_reset_failure_case}
        COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/reset/InvalidAsyncReset.h
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_async_reset_failure
            -DCASE=${async_reset_failure_case}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/reset/ExpectFailure.cmake)
endforeach()

foreach(unsupported_std_formatting_case format print)
    add_test(NAME datatypes_std_${unsupported_std_formatting_case}_unavailable_cxx17
        COMMAND ${CMAKE_COMMAND}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/datatypes/UnsupportedStdFormatting.h
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -DCASE=${unsupported_std_formatting_case}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/datatypes/ExpectUnsupportedStdFormatting.cmake)
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

# A discarded generate branch may contain deliberately invalid widths. The
# comb optimizer must retain the _assign() constexpr guard instead of eagerly
# scheduling that branch, so exercise generation, compilation, and execution.
add_test(
    NAME optimizer_constexpr_binding
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ConstexprBindingTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_constexpr_binding PROPERTIES TIMEOUT 120)

# A lazy comb cycle retains storage at the edge reached recursively from the
# first _work demand. Verify static scheduling preserves that demand order
# instead of selecting a different edge through graph node creation order.
add_test(
    NAME optimizer_lazy_cycle_order
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/LazyCycleOrderTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_lazy_cycle_order PROPERTIES TIMEOUT 120)

# Dynamic ordinary comb methods repeat, while _LAZY_COMB and ports cache.
# Exercise all three through conditionally retained graph dependencies.
# The optimized model must preserve their distinct same-clock behavior.
add_test(
    NAME optimizer_dynamic_comb_repeat
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/DynamicCombRepeatTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_dynamic_comb_repeat PROPERTIES TIMEOUT 120)

add_test(
    NAME optimizer_replay_context
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ReplayContextTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_replay_context PROPERTIES TIMEOUT 120)

add_test(
    NAME optimizer_native_bit_store
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/NativeBitStoreTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_native_bit_store PROPERTIES TIMEOUT 120)

add_test(
    NAME optimizer_specialized_comb
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/SpecializedCombTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_specialized_comb PROPERTIES TIMEOUT 120)

# Preserve converter field dependencies while sharing actual common producers.
add_test(
    NAME optimizer_projected_comb
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ProjectedCombTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_projected_comb PROPERTIES TIMEOUT 120)

add_test(
    NAME optimizer_projected_handshake
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ProjectedHandshakeTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_projected_handshake PROPERTIES TIMEOUT 120)

# A helper called from _work may mutate module storage by non-const reference.
# Readers of that storage must remain lazy until their original demand point
# instead of being evaluated before the side-effecting work call.
add_test(
    NAME optimizer_work_mutation
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/WorkMutationTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_work_mutation PROPERTIES TIMEOUT 120)

# A large work conditional can cross the generated translation-unit limit.
# Keep continuation clauses attached to their owning statement so chunking
# cannot emit a standalone `else` and both runtime branches remain intact.
add_test(
    NAME optimizer_work_split
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/WorkSplitTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_work_split PROPERTIES TIMEOUT 120)

# Calls through a dynamically indexed child-module array cannot be replaced
# by one statically elaborated graph edge. Keep their consumers on demand so
# the original function_ref chain is entered at the source demand point.
add_test(
    NAME optimizer_module_array_call
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/ModuleArrayCallTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_module_array_call PROPERTIES TIMEOUT 120)

# Very large concrete classes may remain ordinary CppHDL subtrees while the
# surrounding hierarchy is flattened. Preserve their lifecycle calls, and
# never move structural _assign() setup into generated runtime functions.
add_test(
    NAME optimizer_opaque_subtree
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/OpaqueSubtreeTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_opaque_subtree PROPERTIES TIMEOUT 120)

# A hierarchy can be entirely combinational while generated lifecycle methods
# still recurse through children. An empty flattened _work body is valid when
# all concrete child bodies were collected and are themselves empty.
add_test(
    NAME optimizer_pure_comb_root
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/PureCombTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_pure_comb_root PROPERTIES TIMEOUT 120)

# Concrete specialization aliases are the stable public names emitted by the
# hdlcpp optimizer. Accept one as the comb root while collecting behavior from
# its underlying class-template specialization.
add_test(
    NAME optimizer_alias_root
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/AliasRootTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_alias_root PROPERTIES TIMEOUT 120)

# Structural non-type template arguments can have no valid semantic source
# spelling. Deduce them from the concrete module specialization when moving a
# dependent comb body out of class scope instead of serializing the value.
add_test(
    NAME optimizer_structural_nttp
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/optimizer/StructuralNttpTest.sh"
            "$<TARGET_FILE:cpphdl>"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/optimizer"
            "${CPPHDL_STDCXXEXP}"
)
set_tests_properties(optimizer_structural_nttp PROPERTIES TIMEOUT 120)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeL1.cpp"
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

set(CPPHDL_OPTIMIZE_THREADS_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_threads")
set(CPPHDL_OPTIMIZE_THREADS_MODEL
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeThreads.h")
set(CPPHDL_OPTIMIZE_THREADS_GENERATED
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs.h"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_internal.h"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_0_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_1_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_2_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_3_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_work_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_strobe_0.cpp")
add_custom_command(
    OUTPUT ${CPPHDL_OPTIMIZE_THREADS_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPHDL_OPTIMIZE_THREADS_DIR}"
    COMMAND "$<TARGET_FILE:cpphdl>"
            --optimize-combs-l1 OptimizeThreads
            --optimize-threads 4
            --generated-dir "${CPPHDL_OPTIMIZE_THREADS_DIR}"
            "${CPPHDL_OPTIMIZE_THREADS_MODEL}"
            -I"${CMAKE_SOURCE_DIR}/include"
    DEPENDS cpphdl "${CPPHDL_OPTIMIZE_THREADS_MODEL}"
    VERBATIM)

add_executable(combs_optimize_threads
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeThreads.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_0_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_1_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_2_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_thread_0_3_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_work_0.cpp"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}/OptimizeThreads_optimized_combs_strobe_0.cpp")
target_include_directories(combs_optimize_threads PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/combs"
    "${CPPHDL_OPTIMIZE_THREADS_DIR}")
target_link_libraries(combs_optimize_threads PRIVATE Threads::Threads)
cpphdl_link_stdcxxexp_if_available(combs_optimize_threads)
target_compile_options(combs_optimize_threads PRIVATE "-fno-strict-aliasing")
target_compile_definitions(combs_optimize_threads PRIVATE
    CPPHDL_OPTIMIZE_THREADS_DIR="${CPPHDL_OPTIMIZE_THREADS_DIR}")
set_target_properties(combs_optimize_threads PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
add_test(NAME combs_optimize_threads
    COMMAND "$<TARGET_FILE:combs_optimize_threads>")
add_test(NAME combs_optimize_threads_cli
    COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DSOURCE=${CPPHDL_OPTIMIZE_THREADS_MODEL}
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_threads_cli
            -P ${CMAKE_CURRENT_SOURCE_DIR}/combs/ExpectOptimizeThreadsFailure.cmake)
add_test(NAME combs_optimize_threads_imbalance
    COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeThreadsImbalanced.h
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_threads_imbalance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/combs/CheckOptimizeThreadsImbalance.cmake)

set(CPPHDL_OPTIMIZE_MATH_MODEL
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeMath.h")
set(CPPHDL_OPTIMIZE_MATH_BASELINE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_math_baseline")
set(CPPHDL_OPTIMIZE_MATH_COMBS_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_math_combs")
set(CPPHDL_OPTIMIZE_MATH_L1_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_combs_optimize_math_l1")

function(cpphdl_generate_optimize_math_fixture directory output_var mode)
    set(generated
        "${directory}/OptimizeMath_optimized_combs.cpp"
        "${directory}/OptimizeMath_optimized_combs.h"
        "${directory}/OptimizeMath_optimized_combs_internal.h"
        "${directory}/OptimizeMath_optimized_combs_0.cpp"
        "${directory}/OptimizeMath_optimized_combs_work_0.cpp"
        "${directory}/OptimizeMath_optimized_combs_strobe_0.cpp")
    add_custom_command(
        OUTPUT ${generated}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
        COMMAND "$<TARGET_FILE:cpphdl>" "${mode}" OptimizeMath ${ARGN}
                --generated-dir "${directory}"
                "${CPPHDL_OPTIMIZE_MATH_MODEL}"
                -I"${CMAKE_SOURCE_DIR}/include"
        DEPENDS cpphdl "${CPPHDL_OPTIMIZE_MATH_MODEL}"
        VERBATIM)
    set(${output_var} ${generated} PARENT_SCOPE)
endfunction()

cpphdl_generate_optimize_math_fixture(
    "${CPPHDL_OPTIMIZE_MATH_BASELINE_DIR}"
    CPPHDL_OPTIMIZE_MATH_BASELINE_GENERATED
    --optimize-combs-l1)
cpphdl_generate_optimize_math_fixture(
    "${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}"
    CPPHDL_OPTIMIZE_MATH_COMBS_GENERATED
    --optimize-combs --optimize-math)
cpphdl_generate_optimize_math_fixture(
    "${CPPHDL_OPTIMIZE_MATH_L1_DIR}"
    CPPHDL_OPTIMIZE_MATH_L1_GENERATED
    --optimize-combs-l1 --optimize-math)

add_custom_target(combs_optimize_math_generated
    DEPENDS ${CPPHDL_OPTIMIZE_MATH_BASELINE_GENERATED}
            ${CPPHDL_OPTIMIZE_MATH_COMBS_GENERATED}
            ${CPPHDL_OPTIMIZE_MATH_L1_GENERATED})
add_executable(combs_optimize_math
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeMath.cpp"
    "${CPPHDL_OPTIMIZE_MATH_L1_DIR}/OptimizeMath_optimized_combs.cpp"
    "${CPPHDL_OPTIMIZE_MATH_L1_DIR}/OptimizeMath_optimized_combs_0.cpp"
    "${CPPHDL_OPTIMIZE_MATH_L1_DIR}/OptimizeMath_optimized_combs_work_0.cpp"
    "${CPPHDL_OPTIMIZE_MATH_L1_DIR}/OptimizeMath_optimized_combs_strobe_0.cpp")
add_dependencies(combs_optimize_math combs_optimize_math_generated)
target_include_directories(combs_optimize_math PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/combs"
    "${CPPHDL_OPTIMIZE_MATH_L1_DIR}")
cpphdl_link_stdcxxexp_if_available(combs_optimize_math)
cpphdl_add_host_opt_flags(combs_optimize_math)
target_compile_options(combs_optimize_math PRIVATE "-fno-strict-aliasing")
target_compile_definitions(combs_optimize_math PRIVATE
    CPPHDL_OPTIMIZE_MATH_BASELINE_DIR="${CPPHDL_OPTIMIZE_MATH_BASELINE_DIR}"
    CPPHDL_OPTIMIZE_MATH_COMBS_DIR="${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}"
    CPPHDL_OPTIMIZE_MATH_L1_DIR="${CPPHDL_OPTIMIZE_MATH_L1_DIR}")
set_target_properties(combs_optimize_math PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
add_test(NAME combs_optimize_math COMMAND "$<TARGET_FILE:combs_optimize_math>")

add_executable(combs_optimize_math_combs
    "${CMAKE_CURRENT_SOURCE_DIR}/combs/OptimizeMath.cpp"
    "${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}/OptimizeMath_optimized_combs.cpp"
    "${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}/OptimizeMath_optimized_combs_0.cpp"
    "${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}/OptimizeMath_optimized_combs_work_0.cpp"
    "${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}/OptimizeMath_optimized_combs_strobe_0.cpp")
add_dependencies(combs_optimize_math_combs combs_optimize_math_generated)
target_include_directories(combs_optimize_math_combs PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/combs"
    "${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}")
cpphdl_link_stdcxxexp_if_available(combs_optimize_math_combs)
cpphdl_add_host_opt_flags(combs_optimize_math_combs)
target_compile_options(combs_optimize_math_combs PRIVATE "-fno-strict-aliasing")
target_compile_definitions(combs_optimize_math_combs PRIVATE
    CPPHDL_OPTIMIZE_MATH_BASELINE_DIR="${CPPHDL_OPTIMIZE_MATH_BASELINE_DIR}"
    CPPHDL_OPTIMIZE_MATH_COMBS_DIR="${CPPHDL_OPTIMIZE_MATH_COMBS_DIR}"
    CPPHDL_OPTIMIZE_MATH_L1_DIR="${CPPHDL_OPTIMIZE_MATH_L1_DIR}")
set_target_properties(combs_optimize_math_combs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
add_test(NAME combs_optimize_math_combs
    COMMAND "$<TARGET_FILE:combs_optimize_math_combs>")

add_test(NAME combs_optimize_math_requires_combs
    COMMAND ${CMAKE_COMMAND}
            -DCPPHDL=$<TARGET_FILE:cpphdl>
            -DSOURCE=${CPPHDL_OPTIMIZE_MATH_MODEL}
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/combs/ExpectOptimizeMathFailure.cmake)
