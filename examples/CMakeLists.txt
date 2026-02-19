cmake_minimum_required(VERSION 3.15)
project(examples LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)

file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(src ${EXAMPLE_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    # prefix with relative path for uniqueness
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

#    target_compile_options(${target_name} PRIVATE "-O2")

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/examples
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/examples
    )

    include_directories("../include")

    # verilator tests
    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env bash -c "../cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/${rel_path} -I../../include"
        COMMAND ${CMAKE_COMMAND} -E env bash -c "cp ${CMAKE_CURRENT_SOURCE_DIR}/tuple/rv32i.bin ${CMAKE_BINARY_DIR}/examples/"
        COMMAND ${CMAKE_COMMAND} -E env bash -c "cp ${CMAKE_CURRENT_SOURCE_DIR}/tuple/rv32i.log ${CMAKE_BINARY_DIR}/examples/"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )

    target_compile_options(${target_name} PRIVATE "-mavx2")
    target_compile_options(${target_name} PRIVATE "-fno-strict-aliasing")

    add_dependencies(${target_name} cpphdl)

endforeach()
