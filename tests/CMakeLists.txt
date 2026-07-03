cmake_minimum_required(VERSION 3.15)
project(tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(FILTER TEST_SOURCES EXCLUDE REGEX "/[.#][^/]*\\.cpp$")
list(FILTER TEST_SOURCES EXCLUDE REGEX "~$")

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

    cpphdl_link_stdcxxexp_if_available(${target_name})

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests
    )

    include_directories("../include")

    set(cpphdl_json_args "")
    if(rel_path MATCHES "^json/")
        set(cpphdl_json_args "--json-output generated/${name}")
    endif()

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${cpphdl_json_args} ${CMAKE_CURRENT_SOURCE_DIR}/${rel_path} -I${CMAKE_SOURCE_DIR}/include ${CPPHDL_TOOLCHAIN_ARGS}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    cpphdl_add_host_opt_flags(${target_name})
    target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")

    add_dependencies(${target_name} cpphdl)

    add_test(
        NAME ${target_name}
        COMMAND "$<TARGET_FILE:${target_name}>"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
    set_tests_properties(${target_name} PROPERTIES
        TIMEOUT 600
    )

endforeach()
