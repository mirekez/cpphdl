import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    for name in ("hdlcpp", "cpphdl", "cxx", "verilator"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="narrow-arithmetic-", dir=args.work) as directory:
        work = Path(directory)
        tools = TestToolchain(args.cxx, args.verilator, work)

        def run(command):
            result = subprocess.run(tools.command(command), cwd=work, env=tools.env,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = "".join(path.read_text()[-3000:] for path in work.rglob("*.log"))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end="", flush=True)

        rtl = fixtures / "NarrowArithmeticCast.sv"
        runner = fixtures / "NarrowArithmeticCastRun.cc"
        run([args.hdlcpp, rtl])
        for width in (2, 3, 5):
            flags = ["-std=c++23", "-O1", "-I" + str(fixtures.parents[1] / "include"),
                     "-I" + str(work), "-DNARROW_WIDTH=" + str(width)]
            output = work / ("verilator-" + str(width))
            run([args.verilator, "--cc", "--exe", "--build", "-j", "1", "-Wno-fatal",
                 "--top-module", "NarrowArithmeticCast", "-GWidth=" + str(width), "--Mdir", output,
                 "-CFLAGS", " ".join(flags + ["-DNARROW_RTL"]),
                 "-MAKEFLAGS", "CXX=" + args.cxx, rtl, runner])
            run([output / "VNarrowArithmeticCast"])
            run([args.cxx, *flags, "-fsanitize=address,undefined", runner, "-o", work / "ordinary"])
            run([work / "ordinary"])
            seed = work / "seed.cc"
            seed.write_text('#include "generated/NarrowArithmeticCast.h"\n'
                            f'extern NarrowArithmeticCast<{width}> cpphdl_top;\n')
            for chunk in (0, 2):
                output = work / ("graph-" + str(width) + "-" + str(chunk))
                run([args.cpphdl, "--native-graph", "--top", "cpphdl_top",
                     "--chunk-size=" + str(chunk), *(["--split-chunks=4"] if chunk else []),
                     "--cxx", args.cxx, "--frontend-flag=-I" + str(work), "--output", output,
                     "--runner", runner, seed, "--", *flags, "-DNARROW_GRAPH",
                     "-fsanitize=address,undefined"])
                run([output / "run"])

        deep = work / "NarrowArithmeticCast.sv"
        expression = "(" * 24 + "index_i + other_i" + " + 32'd0)" * 24
        deep.write_text(rtl.read_text().replace("(index_i + other_i)", expression))
        run([args.hdlcpp, deep])
        generated = work / "generated/NarrowArithmeticCast.h"
        assert generated.stat().st_size < 1024 * 1024, "dependent width expansion is unbounded"
        flags = ["-std=c++23", "-O1", "-I" + str(fixtures.parents[1] / "include"),
                 "-I" + str(work), "-DNARROW_WIDTH=2"]
        run([args.cxx, *flags, "-fsanitize=address,undefined", runner, "-o", work / "deep"])
        run([work / "deep"])


if __name__ == "__main__":
    main()
