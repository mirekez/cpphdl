file(MAKE_DIRECTORY "${WORK}")
set(generated "${WORK}/model")
if(EXISTS "${generated}")
    file(REMOVE_RECURSE "${generated}")
endif()
execute_process(COMMAND "${PYTHON}" "${SOURCE_ROOT}/tools/cpphdl-netlist.py"
    --cpphdl "${CPPHDL}" --sv2v "${SV2V}" --yosys "${YOSYS}"
    --top WordGraph --output "${generated}" "${SOURCE_ROOT}/tests/netlist/WordGraph.h"
    -- "-I${SOURCE_ROOT}/include" -w
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Netlist generation failed: ${result}")
endif()
execute_process(COMMAND "${CXX}" -std=c++23 -O2
    "-I${SOURCE_ROOT}/include" "-I${generated}" "-I${CXXRTL_RUNTIME}"
    "${SOURCE_ROOT}/tests/netlist/Run.cc" "${generated}/model.cc"
    -o "${WORK}/run" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Netlist compilation failed: ${result}")
endif()
execute_process(COMMAND "${WORK}/run" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Netlist equivalence failed: ${result}")
endif()
