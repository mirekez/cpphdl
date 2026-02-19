cmake_minimum_required(VERSION 3.26...3.30)

if(WIN32)
set (CMAKE_LINKER "ld64.lld.exe")
set (CMAKE_C_COMPILER "clang")
set (CMAKE_CXX_COMPILER "clang++")
set (CMAKE_GENERATOR "Unix Makefiles")
set (CMAKE_MAKE_PROGRAM make)
set (CMAKE_C_COMPILER_FORCED "TRUE")
set (CMAKE_CXX_COMPILER_FORCED "TRUE")
else()
set (CMAKE_LINKER "x86_64-conda-linux-gnu-ld")
set (CMAKE_C_COMPILER "clang")
set (CMAKE_CXX_COMPILER "clang++")
set (CMAKE_GENERATOR "Unix Makefiles")
set (CMAKE_MAKE_PROGRAM make)
set (CMAKE_C_COMPILER_FORCED "TRUE")
set (CMAKE_CXX_COMPILER_FORCED "TRUE")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

SET (CMAKE_C_FLAGS_DEBUG_INIT          "-g")
SET (CMAKE_C_FLAGS_MINSIZEREL_INIT     "-Os -DNDEBUG")
SET (CMAKE_C_FLAGS_RELEASE_INIT        "-O2 -DNDEBUG")
SET (CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2")

SET (CMAKE_CXX_FLAGS_DEBUG_INIT          "-g")
SET (CMAKE_CXX_FLAGS_MINSIZEREL_INIT     "-Os -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELEASE_INIT        "-O2 -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-O2")

set(CMAKE_CXX_FLAGS "-g -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-missing-field-initializers")
set(CMAKE_CXX_LINK_FLAGS "-static-libstdc++ -static-libgcc")

project(cpphdl
  VERSION 0.8
  LANGUAGES CXX)

add_definitions(-DSTATIC_BUILD)

find_package(LLVM REQUIRED CONFIG PATHS .conda/lib/cmake/llvm)
find_package(Clang REQUIRED CONFIG PATHS .conda/lib/cmake/clang)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

add_executable(cpphdl cpphdl.cpp helpers.cpp Project.cpp Module.cpp Method.cpp Field.cpp Expr.cpp Struct.cpp Enum.cpp)

include(CheckCXXCompilerFlag)
foreach(flag -std=c++26 -std=c++2c -std=c++23)
    check_cxx_compiler_flag("${flag}" HAS_FLAG)
    if(HAS_FLAG)
        target_compile_options(cpphdl PRIVATE "${flag}")
        message(STATUS "Using ${flag}")
        break()
    endif()
endforeach()

target_link_libraries(cpphdl PRIVATE
  clangTooling
  clangBasic
  clangASTMatchers
  clangFrontend
  clangRewrite
  LLVM
  stdc++exp
)

add_subdirectory(examples)
add_subdirectory(tribe)
