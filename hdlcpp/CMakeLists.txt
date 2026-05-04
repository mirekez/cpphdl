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
target_include_directories(hdlcpp PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(hdlcpp PRIVATE slang::slang)
