file(MAKE_DIRECTORY "${WORK}")
foreach(source tests/graph/ClockedBoundary.cc hls/examples/net/RetimingProbe.cpp)
    file(REMOVE "${WORK}/graph.cc")
    execute_process(COMMAND "${CPPHDL}" --lower-cpp-graph "${ROOT}/${source}"
        "${WORK}/graph.cc" cpphdl_top -- -std=c++17 "-I${ROOT}/include"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT "${output}\n${error}" MATCHES "HLS Clocked requires the scheduled HLS graph")
        message(FATAL_ERROR "Must reject the transaction reference, not compile it as cycle-accurate hardware: ${output}\n${error}")
    endif()
    if(EXISTS "${WORK}/graph.cc")
        message(FATAL_ERROR "Rejected Clocked lowering left a graph artifact")
    endif()
endforeach()
