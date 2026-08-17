file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(
    COMMAND "${CPPHDL}"
            --optimize-combs-l1 OptimizeThreadsImbalanced
            --optimize-threads 4
            --generated-dir "${OUTPUT_DIR}"
            "${SOURCE}"
            -I"${INCLUDE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "imbalanced optimizer generation failed:\n${output}${error}")
endif()
if(NOT output MATCHES "4 comb threads")
    message(FATAL_ERROR "explicit four-thread schedule was not generated:\n${output}")
endif()
file(GLOB thread_sources "${OUTPUT_DIR}/*_optimized_combs_thread_*.cpp")
list(LENGTH thread_sources thread_source_count)
if(thread_source_count LESS 4)
    message(FATAL_ERROR "four-thread schedule did not emit all worker sources: ${thread_sources}")
endif()
file(READ "${OUTPUT_DIR}/OptimizeThreadsImbalanced_optimized_combs.cpp" source)
if(NOT source MATCHES "optimized_runtime" OR
   NOT source MATCHES "workers_\\.emplace_back")
    message(FATAL_ERROR "four-thread schedule did not emit a threaded runtime")
endif()
