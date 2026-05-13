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

    target_link_libraries(${target_name} PRIVATE
      stdc++exp
    )

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests
    )

    include_directories("../include")

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/${rel_path} -I../../include"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    target_compile_options(${target_name} PRIVATE "-mavx2")
    target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")

    add_dependencies(${target_name} cpphdl)

endforeach()
