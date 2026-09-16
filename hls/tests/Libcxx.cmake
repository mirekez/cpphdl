# Keep the examples' C++ library independent of the converter's own ABI.
find_program(HLS_CXX NAMES clang++ HINTS "${CMAKE_SOURCE_DIR}/.conda/bin" REQUIRED)
get_filename_component(hls_compiler_bin "${HLS_CXX}" DIRECTORY)
get_filename_component(hls_compiler_prefix "${hls_compiler_bin}" DIRECTORY)
set(HLS_LIBCXX_LIBRARY_DIR "" CACHE PATH "Optional directory containing libc++ and libc++abi")
if(NOT HLS_LIBCXX_LIBRARY_DIR AND EXISTS "${hls_compiler_prefix}/lib/libc++.so")
    set(HLS_LIBCXX_LIBRARY_DIR "${hls_compiler_prefix}/lib")
endif()
set(HLS_CXX_FLAGS -stdlib=libc++ -std=c++17 -fno-strict-aliasing)
# The converter may itself have been built with a different compiler/library.
# Supply the selected HLS compiler's header search order explicitly.
execute_process(COMMAND "${HLS_CXX}" -stdlib=libc++ -E -x c++ -v "${CMAKE_CURRENT_SOURCE_DIR}/Libcxx.cpp"
    OUTPUT_QUIET ERROR_VARIABLE header_search RESULT_VARIABLE header_result)
if(NOT header_result EQUAL 0 OR NOT header_search MATCHES "#include <\.\.\.> search starts here:\n([^#]+)End of search list\.")
    message(FATAL_ERROR "Cannot discover HLS libc++ headers:\n${header_search}")
endif()
string(REPLACE "\n" ";" header_dirs "${CMAKE_MATCH_1}")
set(HLS_PARSE_FLAGS -nostdinc++)
foreach(directory IN LISTS header_dirs)
    string(STRIP "${directory}" directory)
    if(IS_DIRECTORY "${directory}")
        list(APPEND HLS_PARSE_FLAGS -isystem "${directory}")
    endif()
endforeach()
set(HLS_LINK_FLAGS -stdlib=libc++)
if(HLS_LIBCXX_LIBRARY_DIR)
    list(APPEND HLS_LINK_FLAGS "-L${HLS_LIBCXX_LIBRARY_DIR}" "-Wl,-rpath,${HLS_LIBCXX_LIBRARY_DIR}")
endif()
execute_process(COMMAND "${HLS_CXX}" ${HLS_CXX_FLAGS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Libcxx.cpp" ${HLS_LINK_FLAGS}
    -o "${CMAKE_CURRENT_BINARY_DIR}/libcxx-check"
    RESULT_VARIABLE hls_libcxx_result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT hls_libcxx_result EQUAL 0)
    message(FATAL_ERROR "HLS tests need Clang and libc++ development headers/libraries. Set HLS_CXX and, if needed, HLS_LIBCXX_LIBRARY_DIR.\n${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_CURRENT_BINARY_DIR}/libcxx-check" RESULT_VARIABLE hls_libcxx_result)
if(NOT hls_libcxx_result EQUAL 0)
    message(FATAL_ERROR "HLS libc++ runtime check failed: ${hls_libcxx_result}")
endif()
set(HLS_TOOLCHAIN "${CMAKE_CURRENT_BINARY_DIR}/LibcxxToolchain.cmake")
file(CONFIGURE OUTPUT "${HLS_TOOLCHAIN}" CONTENT "set(CXX [==[${HLS_CXX}]==])\nset(HLS_CXX_FLAGS [==[${HLS_CXX_FLAGS}]==])\nset(HLS_PARSE_FLAGS [==[${HLS_PARSE_FLAGS}]==])\nset(HLS_LINK_FLAGS [==[${HLS_LINK_FLAGS}]==])\n" @ONLY)

function(add_hls_native name source)
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/native/${name}")
    set(definitions)
    foreach(definition IN LISTS ARGN)
        list(APPEND definitions "-D${definition}")
    endforeach()
    add_custom_command(OUTPUT "${binary}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/native"
        COMMAND "${HLS_CXX}" ${HLS_CXX_FLAGS} "-I${CMAKE_SOURCE_DIR}/include" ${definitions}
            -MMD -MF "${binary}.d" "${CMAKE_CURRENT_SOURCE_DIR}/${source}" ${HLS_LINK_FLAGS} -o "${binary}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}" "${HLS_TOOLCHAIN}"
        DEPFILE "${binary}.d" VERBATIM)
    add_custom_target(${name} ALL DEPENDS "${binary}")
    add_dependencies(hls_tests ${name})
    set(HLS_NATIVE_BINARY "${binary}" PARENT_SCOPE)
endfunction()
