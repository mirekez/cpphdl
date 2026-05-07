add_executable(L1Cache_test
    L1Cache_test.cpp
)

add_executable(BranchPredictor_test
    BranchPredictor_test.cpp
)

add_executable(L2Cache_test
    L2Cache_test.cpp
)

set_target_properties(L1Cache_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
)

set_target_properties(BranchPredictor_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
)

set_target_properties(L2Cache_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(L1Cache_test PRIVATE
    "../../include"
    "../common"
    "../cache"
    "../../examples/axi"
)

target_include_directories(BranchPredictor_test PRIVATE
    "../../include"
    ".."
    "../spec"
)

target_include_directories(L2Cache_test PRIVATE
    "../../include"
    "../common"
    "../cache"
    "../../examples/axi"
)

target_compile_options(L1Cache_test PRIVATE
    "-mavx2"
    "-fno-strict-aliasing"
)

target_compile_options(BranchPredictor_test PRIVATE
    "-mavx2"
    "-fno-strict-aliasing"
)

target_compile_options(L2Cache_test PRIVATE
    "-mavx2"
    "-fno-strict-aliasing"
)

target_link_libraries(L1Cache_test PRIVATE
    stdc++exp
)

target_link_libraries(BranchPredictor_test PRIVATE
    stdc++exp
)

target_link_libraries(L2Cache_test PRIVATE
    stdc++exp
)

add_dependencies(L1Cache_test cpphdl)
add_dependencies(BranchPredictor_test cpphdl)
add_dependencies(L2Cache_test cpphdl)

add_custom_command(
    TARGET L1Cache_test
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory generated
    COMMAND ${CMAKE_BINARY_DIR}/cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/L1Cache_test.cpp
            -I ${CMAKE_SOURCE_DIR}/include
            -I ${CMAKE_CURRENT_SOURCE_DIR}/../common
            -I ${CMAKE_CURRENT_SOURCE_DIR}/../cache
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_command(
    TARGET BranchPredictor_test
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory generated
    COMMAND ${CMAKE_BINARY_DIR}/cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/BranchPredictor_test.cpp
            -I ${CMAKE_SOURCE_DIR}/include
            -I ${CMAKE_CURRENT_SOURCE_DIR}/..
            -I ${CMAKE_CURRENT_SOURCE_DIR}/../spec
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_command(
    TARGET L2Cache_test
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory generated
    COMMAND ${CMAKE_BINARY_DIR}/cpphdl ${CMAKE_CURRENT_SOURCE_DIR}/L2Cache_test.cpp
            -I ${CMAKE_SOURCE_DIR}/include
            -I ${CMAKE_CURRENT_SOURCE_DIR}/../common
            -I ${CMAKE_CURRENT_SOURCE_DIR}/../cache
            -I ${CMAKE_SOURCE_DIR}/examples/axi
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
    NAME L1Cache_test
    COMMAND L1Cache_test --noveril
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
    NAME BranchPredictor_test
    COMMAND BranchPredictor_test --noveril
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
    NAME L2Cache_test
    COMMAND L2Cache_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
