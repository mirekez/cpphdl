cmake_minimum_required(VERSION 3.22...3.30)

#good take for windows while not supported yet
#if(WIN32)
#set (CMAKE_LINKER "ld64.lld.exe")
#set (CMAKE_C_COMPILER "clang")
#set (CMAKE_CXX_COMPILER "clang++")
#set (CMAKE_GENERATOR "Unix Makefiles")
#set (CMAKE_MAKE_PROGRAM make)
#set (CMAKE_C_COMPILER_FORCED "TRUE")
#set (CMAKE_CXX_COMPILER_FORCED "TRUE")
#endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

set(CPPHDL_USE_LOCAL_CONDA ON CACHE BOOL "Use the repository .conda toolchain prefix when it exists")
set(CPPHDL_FETCH_LLVM OFF CACHE BOOL "Fetch and build LLVM+Clang when no complete local/system Clang tooling install is available")
set(CPPHDL_FETCH_LLVM_TAG "llvmorg-21.1.3" CACHE STRING "LLVM tag used by CPPHDL_FETCH_LLVM")
set(CPPHDL_HOST_OPT_FLAGS "" CACHE STRING "Extra host CPU optimization flags for examples/tests, for example -mavx2")

if(CPPHDL_USE_LOCAL_CONDA AND NOT CPPHDL_FETCH_LLVM AND NOT DEFINED CMAKE_C_COMPILER AND EXISTS "${CMAKE_SOURCE_DIR}/.conda/bin/clang")
  set(CMAKE_C_COMPILER "${CMAKE_SOURCE_DIR}/.conda/bin/clang" CACHE FILEPATH "C compiler")
endif()
if(CPPHDL_USE_LOCAL_CONDA AND NOT CPPHDL_FETCH_LLVM AND NOT DEFINED CMAKE_CXX_COMPILER AND EXISTS "${CMAKE_SOURCE_DIR}/.conda/bin/clang++")
  set(CMAKE_CXX_COMPILER "${CMAKE_SOURCE_DIR}/.conda/bin/clang++" CACHE FILEPATH "C++ compiler")
endif()
if(CPPHDL_FETCH_LLVM)
  foreach(CPPHDL_COMPILER_VAR CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
    if(DEFINED ${CPPHDL_COMPILER_VAR}
        AND "${${CPPHDL_COMPILER_VAR}}" MATCHES "^${CMAKE_SOURCE_DIR}/\\.conda/")
      message(FATAL_ERROR
        "CPPHDL_FETCH_LLVM is enabled, but ${CPPHDL_COMPILER_VAR} is cached as "
        "${${CPPHDL_COMPILER_VAR}}. Remove the build directory or configure with "
        "CC=/usr/bin/cc CXX=/usr/bin/c++ so fetched LLVM tools can run on this CPU.")
    endif()
  endforeach()
endif()

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
  LANGUAGES C CXX)

add_compile_definitions(CPPHDL_CMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER}")
add_compile_definitions(CPPHDL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

option(CPPHDL_BUILD_HDLCPP "Build the hdlcpp SystemVerilog-to-CppHDL converter" OFF)
option(CPPHDL_BUILD_EXAMPLES "Build CppHDL examples" ON)
option(CPPHDL_BUILD_TESTS "Build CppHDL standalone tests" ON)
option(CPPHDL_BUILD_TRIBE "Build Tribe CPU models and tests" OFF)

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
]=])
file(CHMOD "${CPPHDL_TEST_ALL_SCRIPT}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

add_definitions(-DSTATIC_BUILD)

find_library(CPPHDL_STDCXXEXP stdc++exp)
if(NOT CPPHDL_STDCXXEXP)
  file(GLOB CPPHDL_STDCXXEXP_CANDIDATES "${CMAKE_SOURCE_DIR}/.conda/lib/gcc/*/*/libstdc++exp.a")
  if(CPPHDL_STDCXXEXP_CANDIDATES)
    list(GET CPPHDL_STDCXXEXP_CANDIDATES 0 CPPHDL_STDCXXEXP)
  endif()
endif()

include(CheckCXXSourceCompiles)
set(CPPHDL_STACKTRACE_TEST_SOURCE [=[
#include <stacktrace>
int main()
{
    auto trace = std::stacktrace::current();
    return trace.size() == 0 ? 0 : 0;
}
]=])
check_cxx_source_compiles("${CPPHDL_STACKTRACE_TEST_SOURCE}" CPPHDL_STACKTRACE_LINKS_WITH_DEFAULT_LIBS)
if(NOT CPPHDL_STACKTRACE_LINKS_WITH_DEFAULT_LIBS AND CPPHDL_STDCXXEXP)
  set(CMAKE_REQUIRED_LIBRARIES_SAVE "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_LIBRARIES "${CPPHDL_STDCXXEXP}")
  check_cxx_source_compiles("${CPPHDL_STACKTRACE_TEST_SOURCE}" CPPHDL_STACKTRACE_LINKS_WITH_STDCXXEXP)
  set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES_SAVE}")
endif()
if(CPPHDL_STACKTRACE_LINKS_WITH_DEFAULT_LIBS OR CPPHDL_STACKTRACE_LINKS_WITH_STDCXXEXP)
  message(STATUS "CppHDL stacktrace support enabled")
  add_compile_definitions(CPPHDL_HAVE_STACKTRACE=1)
else()
  message(STATUS "CppHDL stacktrace support disabled: <stacktrace> did not link")
endif()

function(cpphdl_link_stdcxxexp_if_available target)
  if(CPPHDL_STDCXXEXP)
    target_link_libraries(${target} PRIVATE ${CPPHDL_STDCXXEXP})
  endif()
endfunction()

function(cpphdl_add_host_opt_flags target)
  if(CPPHDL_HOST_OPT_FLAGS)
    separate_arguments(CPPHDL_HOST_OPT_FLAGS_LIST NATIVE_COMMAND "${CPPHDL_HOST_OPT_FLAGS}")
    target_compile_options(${target} PRIVATE ${CPPHDL_HOST_OPT_FLAGS_LIST})
  endif()
endfunction()

set(CPPHDL_DISCOVERED_INCLUDE_DIRS "")
execute_process(
  COMMAND "${CMAKE_CXX_COMPILER}" -E -x c++ - -v
  INPUT_FILE /dev/null
  OUTPUT_VARIABLE CPPHDL_CXX_INCLUDE_PROBE_OUT
  ERROR_VARIABLE CPPHDL_CXX_INCLUDE_PROBE_ERR
  RESULT_VARIABLE CPPHDL_CXX_INCLUDE_PROBE_RESULT
)
set(CPPHDL_CXX_INCLUDE_PROBE_TEXT "${CPPHDL_CXX_INCLUDE_PROBE_OUT}\n${CPPHDL_CXX_INCLUDE_PROBE_ERR}")
if(CPPHDL_CXX_INCLUDE_PROBE_RESULT EQUAL 0)
  string(REPLACE "\r\n" "\n" CPPHDL_CXX_INCLUDE_PROBE_TEXT "${CPPHDL_CXX_INCLUDE_PROBE_TEXT}")
  string(REPLACE "\r" "\n" CPPHDL_CXX_INCLUDE_PROBE_TEXT "${CPPHDL_CXX_INCLUDE_PROBE_TEXT}")
  string(REPLACE "\n" ";" CPPHDL_CXX_INCLUDE_PROBE_LINES "${CPPHDL_CXX_INCLUDE_PROBE_TEXT}")
  set(CPPHDL_IN_ANGLE_INCLUDE_LIST OFF)
  foreach(CPPHDL_INCLUDE_LINE IN LISTS CPPHDL_CXX_INCLUDE_PROBE_LINES)
    string(STRIP "${CPPHDL_INCLUDE_LINE}" CPPHDL_INCLUDE_LINE_STRIPPED)
    if(CPPHDL_INCLUDE_LINE_STRIPPED STREQUAL "#include <...> search starts here:")
      set(CPPHDL_IN_ANGLE_INCLUDE_LIST ON)
      continue()
    endif()
    if(CPPHDL_INCLUDE_LINE_STRIPPED STREQUAL "End of search list.")
      set(CPPHDL_IN_ANGLE_INCLUDE_LIST OFF)
      continue()
    endif()
    if(CPPHDL_IN_ANGLE_INCLUDE_LIST AND EXISTS "${CPPHDL_INCLUDE_LINE_STRIPPED}")
      list(APPEND CPPHDL_DISCOVERED_INCLUDE_DIRS "${CPPHDL_INCLUDE_LINE_STRIPPED}")
    endif()
  endforeach()
endif()

set(CPPHDL_ALL_IMPLICIT_INCLUDE_DIRS ${CPPHDL_DISCOVERED_INCLUDE_DIRS} ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
list(REMOVE_DUPLICATES CPPHDL_ALL_IMPLICIT_INCLUDE_DIRS)

set(CPPHDL_TOOLCHAIN_ARG_LIST "")
set(CPPHDL_TOOLCHAIN_ARGS "")
foreach(CPPHDL_IMPLICIT_INCLUDE_DIR IN LISTS CPPHDL_ALL_IMPLICIT_INCLUDE_DIRS)
  if(EXISTS "${CPPHDL_IMPLICIT_INCLUDE_DIR}")
    list(APPEND CPPHDL_TOOLCHAIN_ARG_LIST "-isystem" "${CPPHDL_IMPLICIT_INCLUDE_DIR}")
    string(REPLACE "'" "'\\''" CPPHDL_ESCAPED_INCLUDE_DIR "${CPPHDL_IMPLICIT_INCLUDE_DIR}")
    string(APPEND CPPHDL_TOOLCHAIN_ARGS " -isystem '${CPPHDL_ESCAPED_INCLUDE_DIR}'")
  endif()
endforeach()
string(REPLACE ";" "|" CPPHDL_IMPLICIT_INCLUDE_DIRS_DEFINE "${CPPHDL_ALL_IMPLICIT_INCLUDE_DIRS}")
add_compile_definitions(CPPHDL_CXX_IMPLICIT_INCLUDE_DIRS="${CPPHDL_IMPLICIT_INCLUDE_DIRS_DEFINE}")

function(cpphdl_llvm_prefix_works prefix out_var)
  set(${out_var} TRUE PARENT_SCOPE)
  if(NOT EXISTS "${prefix}/lib/libLLVM.so")
    return()
  endif()

  set(test_dir "${CMAKE_BINARY_DIR}/CMakeFiles/cpphdl-llvm-prefix-check")
  file(MAKE_DIRECTORY "${test_dir}")
  file(WRITE "${test_dir}/llvm_runtime_check.cpp" [=[
#include "llvm/Support/raw_ostream.h"
int main()
{
    llvm::outs() << "";
    return 0;
}
]=])
  execute_process(
    COMMAND
      "${CMAKE_CXX_COMPILER}"
      "${test_dir}/llvm_runtime_check.cpp"
      "-I${prefix}/include"
      "-L${prefix}/lib"
      "-Wl,-rpath,${prefix}/lib"
      "-lLLVM"
      "-o" "${test_dir}/llvm_runtime_check"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
  )
  if(NOT compile_result EQUAL 0)
    message(STATUS "Skipping LLVM prefix ${prefix}: runtime probe did not link")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND "${test_dir}/llvm_runtime_check"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
  )
  if(NOT run_result EQUAL 0)
    string(STRIP "${run_output}\n${run_error}" runtime_error)
    message(STATUS "Skipping LLVM prefix ${prefix}: runtime probe failed: ${runtime_error}")
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

set(CPPHDL_LOCAL_CONDA_PREFIX "" CACHE PATH "Explicit conda prefix that contains LLVM/Clang tooling")
if(NOT CPPHDL_LOCAL_CONDA_PREFIX AND CPPHDL_USE_LOCAL_CONDA)
  set(CPPHDL_LOCAL_CONDA_PREFIX "${CMAKE_SOURCE_DIR}/.conda")
endif()
if(CPPHDL_LOCAL_CONDA_PREFIX)
  file(TO_CMAKE_PATH "${CPPHDL_LOCAL_CONDA_PREFIX}" CPPHDL_LOCAL_CONDA_PREFIX)
endif()

foreach(CPPHDL_LLVM_PREFIX
    "${CPPHDL_LOCAL_CONDA_PREFIX}"
    "${CMAKE_SOURCE_DIR}/.conda"
    "/usr/lib/llvm-20"
    "/usr/lib/llvm-19"
    "/usr/lib/llvm-18"
    "/usr/lib/llvm-17"
    "/usr/lib/llvm-16"
    "/usr/lib/llvm-15")
  if((CPPHDL_USE_LOCAL_CONDA OR NOT CPPHDL_LLVM_PREFIX STREQUAL "${CMAKE_SOURCE_DIR}/.conda")
      AND EXISTS "${CPPHDL_LLVM_PREFIX}/include/clang/Tooling/Tooling.h")
    cpphdl_llvm_prefix_works("${CPPHDL_LLVM_PREFIX}" CPPHDL_PREFIX_WORKS)
    if(CPPHDL_PREFIX_WORKS)
      list(APPEND CMAKE_PREFIX_PATH "${CPPHDL_LLVM_PREFIX}")
      list(APPEND CPPHDL_LLVM_PREFIXES "${CPPHDL_LLVM_PREFIX}")
    endif()
  endif()
endforeach()
if(CPPHDL_LLVM_PREFIXES)
  find_package(LLVM REQUIRED CONFIG PATHS ${CPPHDL_LLVM_PREFIXES} NO_DEFAULT_PATH)
  find_package(Clang REQUIRED CONFIG PATHS ${CPPHDL_LLVM_PREFIXES} NO_DEFAULT_PATH)
  include_directories(${LLVM_INCLUDE_DIRS})
  include_directories(${CLANG_INCLUDE_DIRS})
elseif(CPPHDL_FETCH_LLVM)
  set(CPPHDL_USING_FETCHED_LLVM TRUE)
  message(STATUS "CppHDL using fetched LLVM/Clang")
  include(FetchContent)
  set(LLVM_ENABLE_PROJECTS clang CACHE STRING "" FORCE)
  set(LLVM_TARGETS_TO_BUILD Native CACHE STRING "" FORCE)
  set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_TOOLS ON CACHE BOOL "" FORCE)
  set(LLVM_INCLUDE_UTILS OFF CACHE BOOL "" FORCE)
  set(LLVM_ENABLE_BINDINGS OFF CACHE BOOL "" FORCE)
  set(LLVM_ENABLE_TERMINFO OFF CACHE BOOL "" FORCE)
  set(LLVM_ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
  set(LLVM_ENABLE_ZLIB OFF CACHE BOOL "" FORCE)
  set(LLVM_ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
  set(LLVM_ENABLE_RTTI ON CACHE BOOL "" FORCE)
  set(CLANG_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(CLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
  set(CLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(cpphdl_llvm_project
    GIT_REPOSITORY https://github.com/llvm/llvm-project.git
    GIT_TAG ${CPPHDL_FETCH_LLVM_TAG}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR llvm
  )
  set(CPPHDL_SAVED_CMAKE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
  set(CPPHDL_SAVED_CMAKE_CXX_STANDARD_REQUIRED "${CMAKE_CXX_STANDARD_REQUIRED}")
  set(CPPHDL_SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  set(CPPHDL_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=x86-64 -mtune=generic")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=x86-64 -mtune=generic")
  FetchContent_MakeAvailable(cpphdl_llvm_project)
  set(CMAKE_CXX_STANDARD "${CPPHDL_SAVED_CMAKE_CXX_STANDARD}")
  set(CMAKE_CXX_STANDARD_REQUIRED "${CPPHDL_SAVED_CMAKE_CXX_STANDARD_REQUIRED}")
  set(CMAKE_C_FLAGS "${CPPHDL_SAVED_CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CPPHDL_SAVED_CMAKE_CXX_FLAGS}")
  include_directories(
    "${cpphdl_llvm_project_SOURCE_DIR}/llvm/include"
    "${cpphdl_llvm_project_BINARY_DIR}/include"
    "${cpphdl_llvm_project_SOURCE_DIR}/clang/include"
    "${cpphdl_llvm_project_BINARY_DIR}/tools/clang/include"
  )
else()
  message(FATAL_ERROR
    "No complete LLVM/Clang tooling install found. "
    "Install clang development packages, enable CPPHDL_USE_LOCAL_CONDA, "
    "or configure with -DCPPHDL_FETCH_LLVM=ON to fetch/build LLVM+Clang.")
endif()

add_executable(cpphdl cpphdl.cpp helpers.cpp Project.cpp Module.cpp Method.cpp Field.cpp Expr.cpp Optimizer.cpp Struct.cpp Enum.cpp json_output.cpp)

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
)
if(TARGET LLVM)
  target_link_libraries(cpphdl PRIVATE LLVM)
elseif(NOT CPPHDL_USING_FETCHED_LLVM AND LLVM_LIBRARIES)
  target_link_libraries(cpphdl PRIVATE ${LLVM_LIBRARIES})
endif()
foreach(dependency
    ClangDeclNodes
    ClangStmtNodes
    ClangTypeNodes
    clang-headers
    clang-tablegen-targets
    clangAST
    clangBasic
    clangTooling
    clangASTMatchers
    clangFrontend
    clangRewrite)
  if(TARGET ${dependency})
    message(STATUS "cpphdl depends on fetched Clang target ${dependency}")
    add_dependencies(cpphdl ${dependency})
  endif()
endforeach()
if(CPPHDL_USING_FETCHED_LLVM)
  if(NOT TARGET clangAST)
    message(FATAL_ERROR "Fetched Clang did not define target clangAST; cpphdl cannot safely build against generated Clang headers.")
  endif()
  message(STATUS "cpphdl depends on fetched Clang AST generated headers through clangAST")
  add_dependencies(cpphdl clangAST)
endif()
cpphdl_link_stdcxxexp_if_available(cpphdl)

function(cpphdl_add_converter_dependency target)
  # POST_BUILD conversion runs only when the owning executable links. Make a
  # rebuilt converter invalidate that link so generated RTL cannot stay stale.
  add_dependencies(${target} cpphdl)
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${CMAKE_BINARY_DIR}/cpphdl")
endfunction()

if(CPPHDL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(CPPHDL_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(CPPHDL_BUILD_TRIBE)
  add_subdirectory(tribe)
endif()
if(CPPHDL_BUILD_HDLCPP)
  add_subdirectory(hdlcpp)
endif()
