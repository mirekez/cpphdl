"""Configure, build and run analog regressions without LLVM, Verilator or RTL conversion."""
import argparse
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=root / "build")
    parser.add_argument("--cxx", help="C++ compiler for a new build directory")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    build = args.build.resolve()
    configure = ["cmake", "-S", str(root), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                 "-DBUILD_TESTING=ON", "-DCPPHDL_BUILD_ANALOG_TESTS=ON"]
    if args.cxx:
        configure.append("-DCMAKE_CXX_COMPILER=" + args.cxx)
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "check-analog", "--parallel", str(args.jobs)], check=True)


if __name__ == "__main__":
    main()
