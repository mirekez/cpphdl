cmake_minimum_required(VERSION 3.15)
project(Examples LANGUAGES CXX)
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

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/examples
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/examples
    )

    include_directories("../include")
endforeach()
