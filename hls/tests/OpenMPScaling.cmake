include("${TOOLCHAIN}")
file(MAKE_DIRECTORY "${WORK}")
set(delays)
foreach(lanes 4 8 16)
    set(out "${WORK}/lanes-${lanes}")
    file(REMOVE_RECURSE "${out}")
    execute_process(COMMAND "${CPPHDL}" --synth --top PipelineTop --module PipelineTop
        --output "${out}" --cxx "${CXX}" --retiming fit_pipeline_retiming --clock-period-ns 2.0
        "${ROOT}/hls/examples/openmp/Tree.cpp" -- "-I${ROOT}/include" -fopenmp
        -DPIPELINE_STAGES=1 "-DOPENMP_LANES=${lanes}" ${HLS_PARSE_FLAGS}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${out}.log" "${output}\n${error}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "OpenMP tree synthesis failed:\n${output}\n${error}")
    endif()
    file(READ "${out}/timing.json" timing)
    string(JSON before GET "${timing}" rules 0 before_ns)
    string(JSON interval GET "${timing}" streaming_regions 0 initiation_interval)
    string(JSON met GET "${timing}" rules 0 target_met)
    if(NOT interval EQUAL 1 OR NOT met)
        message(FATAL_ERROR "Parallel reduction lost its streaming/timing contract: ${timing}")
    endif()
    list(APPEND delays "${before}")
endforeach()
# Doubling the lane count may add one 32-bit adder level, not N new levels.
list(GET delays 0 four)
list(GET delays 1 eight)
list(GET delays 2 sixteen)
execute_process(COMMAND "${PYTHON}" -c
    "import sys; a,b,c=map(float,sys.argv[1:]); assert 0 < b-a <= 2.0 and 0 < c-b <= 2.0, (a,b,c); print('Balanced reduction delays (4/8/16 lanes):', a,b,c,'ns')"
    "${four}" "${eight}" "${sixteen}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Reduction became a serial dependency chain:\n${output}\n${error}")
endif()
message(STATUS "${output}")
