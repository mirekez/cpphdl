include_guard(GLOBAL)

# Both examples/ and lib/ keep their native and RTL tests in reusable headers.
# The caller's source/build directories determine discovery and output paths.
function(cpphdl_add_header_tests)
    cmake_parse_arguments(HEADER "" "LABEL" "DIRECT_RTL_SOURCES" ${ARGN})
    set(RTL_EXAMPLE_SOURCES ${HEADER_DIRECT_RTL_SOURCES})
    if(BUILD_TESTING)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )

    list(FILTER EXAMPLE_SOURCES EXCLUDE REGEX "/[.#][^/]*\\.h$")
    list(FILTER EXAMPLE_SOURCES EXCLUDE REGEX "~$")

    foreach(src ${EXAMPLE_SOURCES})
        file(STRINGS "${src}" example_entry REGEX "^int main_from_header\\(")
        if(NOT example_entry)
            continue()
        endif()
        get_filename_component(name ${src} NAME_WE)
        # prefix with relative path for uniqueness
        file(RELATIVE_PATH rel_path ${CMAKE_CURRENT_SOURCE_DIR} ${src})
        string(REPLACE "/" "_" target_name ${rel_path})
        string(REPLACE ".h" "" target_name ${target_name})

        add_executable(${target_name} "${CMAKE_SOURCE_DIR}/tests/ExampleMain.cpp")
        target_compile_definitions(${target_name} PRIVATE MAINFILE)
        target_compile_options(${target_name} PRIVATE -include "${src}")

        include(CheckCXXCompilerFlag)
        foreach(flag -std=c++26 -std=c++2c)
            check_cxx_compiler_flag("${flag}" HAS_FLAG)
            if(HAS_FLAG)
                target_compile_options(${target_name} PRIVATE "${flag}")
                message(STATUS "Using ${flag}")
                break()
            endif()
        endforeach()

        cpphdl_link_stdcxxexp_if_available(${target_name})

        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
            RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
        )

        target_include_directories(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/include")

        set(cpphdl_clock_args "")
        if(rel_path STREQUAL "cdc/Fifo2clk.h")
            set(cpphdl_clock_args
                "--primary_clock write_clk 100000000 --secondary_clock read_clk 50000000")
        elseif(rel_path STREQUAL "math/FpAdd.h")
            set(cpphdl_clock_args "--primary_clock sum_clock 100000000")
        elseif(rel_path STREQUAL "math/FpConverter.h")
            # Concrete type combinations are instantiated by this example's testbench.
            set(cpphdl_clock_args "--no-synthesis-flag")
        endif()
        if(rel_path STREQUAL "avalon/AvalonEndpoint.h"
           OR rel_path MATCHES "^axi/Axi4Mux")
            set(cpphdl_clock_args "--no-synthesis-flag")
        endif()
        set(cpphdl_output_args "")
        if(rel_path IN_LIST RTL_EXAMPLE_SOURCES)
            # Shared helper modules can have different clocks in different examples.
            set(cpphdl_output_args "--generated-dir='${CMAKE_CURRENT_BINARY_DIR}/generated/${target_name}'")
        endif()

        # verilator tests
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env bash -c "'$<TARGET_FILE:${CPPHDL_TEST_TARGET}>' ${cpphdl_clock_args} ${cpphdl_output_args} ${CMAKE_CURRENT_SOURCE_DIR}/${rel_path} -DMAINFILE -I${CMAKE_SOURCE_DIR}/include ${CPPHDL_TOOLCHAIN_ARGS}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )

        if(name STREQUAL "RiscV")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${CMAKE_SOURCE_DIR}/examples/tuple/rv32i.bin"
                    "${CMAKE_SOURCE_DIR}/examples/tuple/rv32i.log"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        endif()

        cpphdl_add_host_opt_flags(${target_name})
        target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")
        cpphdl_strip_gnu_property_after_build(${target_name})

        cpphdl_add_converter_dependency(${target_name})

        if(BUILD_TESTING AND rel_path IN_LIST RTL_EXAMPLE_SOURCES)
            target_compile_options(${target_name} PRIVATE -ffp-contract=off)
            add_test(NAME ${target_name}_native COMMAND $<TARGET_FILE:${target_name}>)
            set_tests_properties(${target_name}_native PROPERTIES TIMEOUT 300 LABELS "rtl;${HEADER_LABEL}")
            find_program(CPPHDL_EXAMPLE_VERILATOR NAMES verilator
                HINTS "${CPPHDL_LOCAL_CONDA_PREFIX}/bin")
            if(Python3_Interpreter_FOUND AND CPPHDL_EXAMPLE_VERILATOR)
                set(example_top "${name}Example")
                if(name STREQUAL "Fp32Mac")
                    set(example_top Fp32Mac)
                elseif(name STREQUAL "MatrixTransfer")
                    set(example_top cpphdl_examples_MatrixTransfer)
                endif()
                add_test(NAME ${target_name}_verilator COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/examples/RunRtl.py"
                    --cpphdl "$<TARGET_FILE:${CPPHDL_TEST_TARGET}>" --cxx "${CPPHDL_TEST_CXX}"
                    --verilator "${CPPHDL_EXAMPLE_VERILATOR}" --source "${src}"
                    --top "${example_top}" --work "${CMAKE_CURRENT_BINARY_DIR}/rtl-tests")
                set_tests_properties(${target_name}_verilator PROPERTIES TIMEOUT 600 LABELS "rtl;${HEADER_LABEL}")
            endif()
        endif()

        if(BUILD_TESTING AND NOT rel_path IN_LIST RTL_EXAMPLE_SOURCES AND NOT rel_path STREQUAL "math/Transposer.h")
            add_test(NAME ${target_name}_native COMMAND $<TARGET_FILE:${target_name}> --noveril)
            find_program(CPPHDL_MATH_VERILATOR NAMES verilator
                HINTS "${CPPHDL_LOCAL_CONDA_PREFIX}/bin")
            set(math_tests ${target_name}_native)
            if(CPPHDL_MATH_VERILATOR)
                if(Python3_Interpreter_FOUND)
                    add_test(NAME ${target_name}_verilator COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_SOURCE_DIR}/examples/RunRtl.py"
                        --cpphdl "$<TARGET_FILE:${CPPHDL_TEST_TARGET}>" --cxx "${CPPHDL_TEST_CXX}"
                        --verilator "${CPPHDL_MATH_VERILATOR}" --source "${src}"
                        --top "${name}" --work "${CMAKE_CURRENT_BINARY_DIR}/rtl-tests"
                        --self-test "$<TARGET_FILE:${target_name}>")
                else()
                    add_test(NAME ${target_name}_verilator COMMAND $<TARGET_FILE:${target_name}>)
                endif()
                list(APPEND math_tests ${target_name}_verilator)
                if(CPPHDL_LOCAL_CONDA_PREFIX)
                    set_tests_properties(${target_name}_verilator PROPERTIES
                        ENVIRONMENT_MODIFICATION
                            "PATH=path_list_prepend:${CPPHDL_LOCAL_CONDA_PREFIX}/bin;CONDA_PREFIX=set:${CPPHDL_LOCAL_CONDA_PREFIX}")
                endif()
            endif()
            set_tests_properties(${math_tests} PROPERTIES
                TIMEOUT 600 LABELS "${HEADER_LABEL};rtl")
            foreach(test IN LISTS math_tests)
                set(test_work "${CMAKE_CURRENT_BINARY_DIR}/ctest/${test}")
                file(MAKE_DIRECTORY "${test_work}")
                file(CREATE_LINK "${CMAKE_CURRENT_BINARY_DIR}/generated"
                    "${test_work}/generated" SYMBOLIC)
                if(name STREQUAL "RiscV")
                    configure_file("${CMAKE_SOURCE_DIR}/examples/tuple/rv32i.bin" "${test_work}/rv32i.bin" COPYONLY)
                    configure_file("${CMAKE_SOURCE_DIR}/examples/tuple/rv32i.log" "${test_work}/rv32i.log" COPYONLY)
                endif()
                set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY "${test_work}")
            endforeach()
        endif()

        if(rel_path STREQUAL "math/Transposer.h" AND BUILD_TESTING)
            add_test(NAME math_Transposer_cpp
                COMMAND $<TARGET_FILE:${target_name}> --noveril)
            add_test(NAME math_Transposer_verilator
                COMMAND $<TARGET_FILE:${target_name}> --verilator-only)
            find_program(CPPHDL_TRANSPOSER_VERILATOR NAMES verilator
                HINTS "${CPPHDL_LOCAL_CONDA_PREFIX}/bin")
            if(CPPHDL_TRANSPOSER_VERILATOR)
                add_test(NAME math_Transposer_mlab_rdw
                    COMMAND ${CMAKE_COMMAND}
                        -DCPPHDL=$<TARGET_FILE:${CPPHDL_TEST_TARGET}>
                        -DTEST_EXE=$<TARGET_FILE:${target_name}>
                        -DVERILATOR=${CPPHDL_TRANSPOSER_VERILATOR}
                        -DCXX=${CPPHDL_TEST_CXX}
                        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
                        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/transposer_mlab_rdw
                        -P ${CMAKE_SOURCE_DIR}/tests/math/TransposerMlabRdw.cmake)
                set_tests_properties(math_Transposer_mlab_rdw PROPERTIES TIMEOUT 300)
                if(CPPHDL_LOCAL_CONDA_PREFIX)
                    set_tests_properties(math_Transposer_mlab_rdw PROPERTIES
                        ENVIRONMENT_MODIFICATION
                            "PATH=path_list_prepend:${CPPHDL_LOCAL_CONDA_PREFIX}/bin;CONDA_PREFIX=set:${CPPHDL_LOCAL_CONDA_PREFIX}")
                endif()
            endif()
            set_tests_properties(math_Transposer_cpp math_Transposer_verilator PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                TIMEOUT 300)
            if(CPPHDL_LOCAL_CONDA_PREFIX)
                set_tests_properties(math_Transposer_verilator PROPERTIES
                    ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:${CPPHDL_LOCAL_CONDA_PREFIX}/bin;CONDA_PREFIX=set:${CPPHDL_LOCAL_CONDA_PREFIX}")
            endif()
        endif()

    endforeach()

endfunction()
