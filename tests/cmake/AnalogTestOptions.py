"""Check analog test opt-in without building simulators or requiring LLVM."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def run(command):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--cxx", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="cpphdl-analog-options-") as temporary:
        work = Path(temporary)
        source = work / "parent"
        source.mkdir()
        (source / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.20)
project(AnalogOptions LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(parent_testing "${BUILD_TESTING}")
add_subdirectory("${CPPHDL_ROOT}/analog" analog)
if(NOT "${BUILD_TESTING}" STREQUAL "${parent_testing}")
  message(FATAL_ERROR "Analog changed the parent's BUILD_TESTING")
endif()
foreach(export_target cpphdl_veriloga_driver cpphdl-veriloga generate-veriloga)
  if(NOT TARGET ${export_target})
    message(FATAL_ERROR "Analog tests disabled exporter ${export_target}")
  endif()
endforeach()
if(BUILD_TESTING)
  add_test(NAME parent_test COMMAND "${CMAKE_COMMAND}" -E true)
endif()
''')

        def configure(source_dir, build, options=()):
            output = run([args.cmake, "-S", str(source_dir), "-B", str(build),
                          f"-DCPPHDL_ROOT={root}", f"-DCMAKE_CXX_COMPILER={args.cxx}",
                          *options])
            tests = json.loads(run([args.ctest, "--test-dir", str(build),
                                    "--show-only=json-v1"]))
            return {test["name"] for test in tests["tests"]}, output

        build = work / "embedded"
        names, output = configure(source, build)
        assert names == {"parent_test"}, names
        assert "cross-simulator tests unavailable" not in output, output
        assert "CPPHDL_BUILD_ANALOG_TESTS:BOOL=OFF" in (build / "CMakeCache.txt").read_text()

        names, _ = configure(source, build, ["-DCPPHDL_BUILD_ANALOG_TESTS=ON",
                                            "-DCPPHDL_ANALOG_VERILOGA_TESTS=OFF"])
        assert {"parent_test", "analog.semantics", "analog.veriloga.cli",
                "analog.veriloga.generated"} <= names, names

        # Cached simulator/Python discovery must not leak tests back in.
        names, output = configure(source, build, ["-DCPPHDL_BUILD_ANALOG_TESTS=OFF",
                                                 "-DCPPHDL_ANALOG_VERILOGA_TESTS=AUTO",
                                                 f"-DNGSPICE_EXECUTABLE={args.cmake}"])
        assert names == {"parent_test"}, names
        assert "cross-simulator tests unavailable" not in output, output

        names, _ = configure(source, work / "global-off", ["-DBUILD_TESTING=OFF",
                                                          "-DCPPHDL_BUILD_ANALOG_TESTS=ON"])
        assert not names, names

        names, output = configure(root / "analog", work / "standalone")
        assert not names, names
        assert "cross-simulator tests unavailable" not in output, output
        names, _ = configure(root / "analog", work / "standalone",
                             ["-DCPPHDL_BUILD_ANALOG_TESTS=ON",
                              "-DCPPHDL_ANALOG_VERILOGA_TESTS=OFF"])
        assert {"analog.semantics", "analog.veriloga.cli"} <= names, names
    print("Analog test opt-in, exporter availability and parent isolation passed")


if __name__ == "__main__":
    main()
