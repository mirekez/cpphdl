import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ("hdlcpp", "cpphdl", "cxx", "verilator"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="dynamic-struct-index-", dir=args.work) as directory:
        work = Path(directory)

        def run(command):
            result = subprocess.run([str(item) for item in command], cwd=work,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = "".join(path.read_text()[-3000:] for path in work.rglob("*.log"))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end="", flush=True)

        rtl = fixtures / "DynamicStructIndex.sv"
        runner = fixtures / "DynamicStructIndexRun.cc"
        flags = ["-std=c++23", "-O1", "-I" + str(fixtures.parents[1] / "include"), "-I" + str(work)]
        run([args.hdlcpp, rtl])
        run([args.cxx, *flags, "-fsanitize=address,undefined", runner, "-o", work / "ordinary"])
        run([work / "ordinary"])
        output = work / "verilator"
        run([args.verilator, "--cc", "--exe", "--build", "-j", "1", "-Wno-fatal",
             "--top-module", "DynamicStructIndex", "--Mdir", output,
             "-CFLAGS", " ".join(flags + ["-DINDEX_RTL"]), "-MAKEFLAGS", "CXX=" + args.cxx, rtl, runner])
        run([output / "VDynamicStructIndex"])
        seed = work / "seed.cc"
        seed.write_text("#include \"generated/DynamicStructIndex.h\"\nextern DynamicStructIndex cpphdl_top;\n")
        for chunk in (0, 2):
            output = work / ("graph-" + str(chunk))
            run([args.cpphdl, "--native-graph", "--top", "cpphdl_top", "--chunk-size=" + str(chunk),
                 *(["--split-chunks=4"] if chunk else []), "--cxx", args.cxx,
                 "--frontend-flag=-I" + str(work), "--output", output, "--runner", runner, seed,
                 "--", *flags, "-DINDEX_GRAPH", "-fsanitize=address,undefined"])
            run([output / "run"])


if __name__ == "__main__":
    main()
