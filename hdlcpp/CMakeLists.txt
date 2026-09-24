cmake_minimum_required(VERSION 3.26...3.30)

project(hdlcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/slang/CMakeLists.txt")
    set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
    set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
    set(SLANG_USE_THREADS OFF CACHE BOOL "" FORCE)
    set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
    add_subdirectory(slang)
else()
    message(FATAL_ERROR
        "Missing hdlcpp/slang. Clone it with: "
        "git clone https://github.com/MikePopoloski/slang.git hdlcpp/slang")
endif()

add_executable(hdlcpp hdlcpp.cpp)
target_compile_definitions(hdlcpp PRIVATE HDLCPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(hdlcpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(hdlcpp PRIVATE slang::slang)

add_executable(test_combs tests/test_combs.cpp)
target_include_directories(test_combs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME hdlcpp_test_combs COMMAND test_combs)
add_test(NAME hdlcpp_indexed_comb_proof COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/indexed_comb_proof.sh"
    "$<TARGET_FILE:hdlcpp>" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CXX_COMPILER}")
add_test(NAME hdlcpp_blocking_array_update COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/blocking_array_update.sh"
    "$<TARGET_FILE:hdlcpp>" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CXX_COMPILER}")
add_test(NAME hdlcpp_packed_struct_array_expr COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/packed_struct_array_expr.sh"
    "$<TARGET_FILE:hdlcpp>" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CXX_COMPILER}")
add_test(NAME hdlcpp_native_packed_layout COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_packed_layout.sh"
    "$<TARGET_FILE:hdlcpp>" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CXX_COMPILER}")
find_program(HDLCPP_WIDTHS_VERILATOR verilator)
set(HDLCPP_WIDTHS_REFERENCE_ARGS)
if(HDLCPP_WIDTHS_VERILATOR)
    list(APPEND HDLCPP_WIDTHS_REFERENCE_ARGS "${HDLCPP_WIDTHS_VERILATOR}")
endif()
add_test(NAME hdlcpp_constant_widths COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/constant_widths.sh"
    "$<TARGET_FILE:hdlcpp>" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CXX_COMPILER}"
    ${HDLCPP_WIDTHS_REFERENCE_ARGS})
add_test(NAME hdlcpp_expression_helpers COMMAND bash
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/expression_helpers.sh"
    "$<TARGET_FILE:hdlcpp>" "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CXX_COMPILER}"
    ${HDLCPP_WIDTHS_REFERENCE_ARGS})

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME hdlcpp_xperm_cpp COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/hdlcpp/XpermChecks.py"
        --hdlcpp "$<TARGET_FILE:hdlcpp>" --cxx "${CMAKE_CXX_COMPILER}"
        --work "${CMAKE_CURRENT_BINARY_DIR}/xperm")
    set_tests_properties(hdlcpp_xperm_cpp PROPERTIES TIMEOUT 240)
    if(HDLCPP_WIDTHS_VERILATOR)
        add_test(NAME hdlcpp_xperm_verilator COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tests/hdlcpp/XpermChecks.py"
            --hdlcpp "$<TARGET_FILE:hdlcpp>" --cxx "${CMAKE_CXX_COMPILER}"
            --verilator "${HDLCPP_WIDTHS_VERILATOR}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/xperm")
        set_tests_properties(hdlcpp_xperm_verilator PROPERTIES TIMEOUT 240)
    endif()
endif()

add_executable(test_modules tests/test_modules.cpp)
target_include_directories(test_modules PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME hdlcpp_test_modules COMMAND test_modules)

add_executable(test_structs tests/test_structs.cpp)
target_include_directories(test_structs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME hdlcpp_test_structs COMMAND test_structs)
