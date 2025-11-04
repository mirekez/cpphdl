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

SET (CMAKE_C_FLAGS_INIT                "-Wall -Wextra -std=c11")
SET (CMAKE_C_FLAGS_DEBUG_INIT          "-g")
SET (CMAKE_C_FLAGS_MINSIZEREL_INIT     "-Os -DNDEBUG")
SET (CMAKE_C_FLAGS_RELEASE_INIT        "-O3 -DNDEBUG")
SET (CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2 -g")

SET (CMAKE_CXX_FLAGS_INIT                "-Wall -Wextra -std=c++26")
SET (CMAKE_CXX_FLAGS_DEBUG_INIT          "-g")
SET (CMAKE_CXX_FLAGS_MINSIZEREL_INIT     "-Os -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELEASE_INIT        "-O3 -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-O2 -g")

set(CMAKE_CXX_FLAGS "-std=c++2c -g -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-missing-field-initializers")

project(
  cpphdl
  VERSION 1.0
  LANGUAGES CXX)

add_definitions(-DSTATIC_BUILD)

project(MyClangTool LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG PATHS .conda/lib/cmake/llvm)
find_package(Clang REQUIRED CONFIG PATHS .conda/lib/cmake/clang)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

add_executable(cpphdl cpphdl.cpp helpers.cpp Project.cpp Module.cpp Method.cpp Field.cpp Expr.cpp Comb.cpp Struct.cpp)
target_link_libraries(cpphdl
  PRIVATE
  clangTooling
  clangBasic
  clangASTMatchers
  clangFrontend
  clangRewrite
  LLVM
)

add_subdirectory(examples)
