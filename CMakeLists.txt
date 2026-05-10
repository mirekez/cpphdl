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
SET (CMAKE_C_FLAGS_MINSIZEREL_INIT     "-Os -DNDEBUG -g")
SET (CMAKE_C_FLAGS_RELEASE_INIT        "-O2 -DNDEBUG -g")
SET (CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2")

SET (CMAKE_CXX_FLAGS_DEBUG_INIT          "-g")
SET (CMAKE_CXX_FLAGS_MINSIZEREL_INIT     "-Os -DNDEBUG -g")
SET (CMAKE_CXX_FLAGS_RELEASE_INIT        "-O2 -DNDEBUG -g")
SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-O2")

set(CMAKE_CXX_FLAGS "-fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-missing-field-initializers")
set(CMAKE_CXX_LINK_FLAGS "-static-libstdc++ -static-libgcc")

project(cpphdl
  VERSION 0.8
  LANGUAGES CXX)

option(CPPHDL_BUILD_HDLCPP "Build the hdlcpp SystemVerilog-to-CppHDL converter" OFF)

include(CTest)

set(CPPHDL_TEST_ALL_SCRIPT "${CMAKE_BINARY_DIR}/.test_all.sh")
file(WRITE "${CPPHDL_TEST_ALL_SCRIPT}" [=[
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

ARGS=()
for ARG in "$@"; do
    case "$ARG" in
        --noveril)
            ARGS+=("$ARG")
            ;;
        -h|--help)
            echo "usage: $0 [--noveril]"
            exit 0
            ;;
        *)
            echo "Unknown option: $ARG" >&2
            echo "usage: $0 [--noveril]" >&2
            exit 2
            ;;
    esac
done

for DIR in examples tests; do
    if [[ ! -d "$DIR" ]]; then
        continue
    fi

    while IFS= read -r -d '' EXE; do
        echo "==> ${EXE}"
        EXE_DIR="$(dirname "$EXE")"
        EXE_NAME="$(basename "$EXE")"
        (cd "$EXE_DIR" && "./$EXE_NAME" "${ARGS[@]}")
    done < <(find "$DIR" -maxdepth 1 -type f -perm -111 ! -name '*.elf' -print0 | sort -z)
done

if [[ -x tribe/.test_all.sh ]]; then
    echo "==> tribe/.test_all.sh"
    (cd tribe && "./.test_all.sh" "${ARGS[@]}")
fi
]=])
file(CHMOD "${CPPHDL_TEST_ALL_SCRIPT}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

add_definitions(-DSTATIC_BUILD)

find_package(LLVM REQUIRED CONFIG PATHS .conda/lib/cmake/llvm)
find_package(Clang REQUIRED CONFIG PATHS .conda/lib/cmake/clang)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

add_executable(cpphdl cpphdl.cpp helpers.cpp Project.cpp Module.cpp Method.cpp Field.cpp Expr.cpp Optimizer.cpp Struct.cpp Enum.cpp)

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
add_subdirectory(tests)
add_subdirectory(tribe)
if(CPPHDL_BUILD_HDLCPP)
  add_subdirectory(hdlcpp)
endif()
