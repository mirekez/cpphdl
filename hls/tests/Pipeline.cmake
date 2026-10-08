include("${TOOLCHAIN}")
if(NOT SOURCE)
    set(SOURCE Pipeline.cpp)
endif()
set(LATENCY ${STAGES})
file(REMOVE_RECURSE "${WORK}/generated" "${WORK}/obj_dir")
file(REMOVE "${WORK}/graph.cc")
file(MAKE_DIRECTORY "${WORK}")
# Scheduler selection comes from the C++ wrapper.
execute_process(COMMAND "${CPPHDL}" --generated-dir "${WORK}/generated"
    "${ROOT}/hls/tests/${SOURCE}" -- "-I${ROOT}/include" "-DPIPELINE_STAGES=${STAGES}" ${HLS_PARSE_FLAGS}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/conversion.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Pipeline conversion failed:\n${output}\n${error}")
endif()
file(GLOB sources "${WORK}/generated/*.sv")
# Packages must precede modules that import them, regardless of filename order.
file(GLOB packages "${WORK}/generated/*_pkg.sv")
list(REMOVE_ITEM sources ${packages})
list(PREPEND sources ${packages})
if(SYNTH)
    if(NOT PERIOD_NS)
        set(PERIOD_NS 3.205128205)
    endif()
    file(REMOVE_RECURSE "${WORK}/synth")
    execute_process(COMMAND "${CPPHDL}" --synth --top PipelineTop --module PipelineTop
        --output "${WORK}/synth" --cxx "${CXX}" --retiming fit_pipeline_retiming
        --clock-period-ns "${PERIOD_NS}" "${ROOT}/hls/tests/${SOURCE}" -- "-I${ROOT}/include"
        "-DPIPELINE_STAGES=${STAGES}" ${HLS_PARSE_FLAGS}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Automatic pipeline synthesis failed:\n${output}\n${error}")
    endif()
    file(READ "${WORK}/synth/timing.json" timing)
    string(JSON LATENCY GET "${timing}" streaming_regions 0 latency)
    string(JSON interval GET "${timing}" streaming_regions 0 initiation_interval)
    if(NOT interval EQUAL 1 OR NOT LATENCY GREATER STAGES)
        message(FATAL_ERROR "Expected a retimed II=1 pipeline: ${timing}")
    endif()
    set(sources "${WORK}/synth/gates.v")
elseif(GRAPH)
    execute_process(COMMAND "${CPPHDL}" --lower-synthesis-graph "${ROOT}/hls/tests/${SOURCE}"
        "${WORK}/graph.cc" PipelineTop -- -std=c++17 -DSYNTHESIS "-I${ROOT}/include"
        "-DPIPELINE_STAGES=${STAGES}" ${HLS_PARSE_FLAGS}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${WORK}/graph.log" "${output}\n${error}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Pipeline graph export failed:\n${output}\n${error}")
    endif()
    execute_process(COMMAND "${CXX}" ${HLS_CXX_FLAGS} -O1 -DCPPHDL_GRAPH_NO_MAIN -DCPPHDL_GRAPH_CORE_ONLY
        "-I${ROOT}/include" "${WORK}/graph.cc" "${ROOT}/hls/tests/PipelineGraph.cpp"
        "${ROOT}/synth/Verilog.cpp" "${ROOT}/synth/retiming.cpp" "${ROOT}/synth/timing.cpp" "${ROOT}/synth/Mapping.cpp"
        ${HLS_LINK_FLAGS} -o "${WORK}/graph-check"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Pipeline graph checker failed to build:\n${output}\n${error}")
    endif()
    execute_process(COMMAND "${WORK}/graph-check" "${WORK}/pipeline.v"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Pipeline graph checks failed:\n${output}\n${error}")
    endif()
    file(WRITE "${WORK}/retiming.log" "${output}\n${error}")
    if(NOT output MATCHES "LATENCY=([0-9]+)")
        message(FATAL_ERROR "Retimer did not report pipeline latency")
    endif()
    set(LATENCY ${CMAKE_MATCH_1})
    set(sources "${WORK}/pipeline.v")
endif()
file(GLOB worker "${WORK}/generated/*ClockedPipeline*.sv")
list(LENGTH worker count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Pipeline wrapper was not instantiated")
endif()
file(READ "${worker}" rtl)
if(NOT rtl MATCHES "pipelined_logic: II=1, STAGES=${STAGES}" OR rtl MATCHES "booting|heap_next")
    message(FATAL_ERROR "Delayed FSM was used instead of the pipeline")
endif()
string(JOIN " " cflags ${HLS_CXX_FLAGS} "-I${ROOT}/include" "-DPIPELINE_STAGES=${STAGES}" "-DPIPELINE_LATENCY=${LATENCY}" -DVERILATOR)
string(JOIN " " ldflags ${HLS_LINK_FLAGS})
execute_process(COMMAND "${VERILATOR}" --cc --exe --build -j 2 --top-module PipelineTop
    --prefix VPipelineTop --Mdir "${WORK}/obj_dir" -Wno-fatal
    -CFLAGS "${cflags}" -LDFLAGS "${ldflags}" ${sources} "${ROOT}/hls/tests/${SOURCE}"
    -MAKEFLAGS "CXX=${CXX} LINK=${CXX}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/build.log" "${output}\n${error}")
if(NOT result EQUAL 0 OR error MATCHES "%Warning-(LATCH|MULTIDRIVEN)")
    message(FATAL_ERROR "Pipeline RTL build failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${WORK}/obj_dir/VPipelineTop"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${WORK}/simulation.log" "${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Pipeline simulation failed:\n${output}\n${error}")
endif()
message(STATUS "${output}")
