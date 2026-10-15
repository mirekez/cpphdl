import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for name in ("cpphdl", "cxx", "verilator", "work", "flow"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    include = fixtures.parents[1] / "include"
    work = Path(args.work).resolve() / args.flow
    work.mkdir(parents=True, exist_ok=True)

    def run(command, directory, label):
        result = subprocess.run([str(item) for item in command], cwd=directory,
                                capture_output=True, text=True, timeout=120)
        (directory / (label + ".log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(f"{label}: {result.stdout}\n{result.stderr}")
        return result.stdout

    for width in (16, 32, 64):
        directory = work / str(width)
        directory.mkdir(exist_ok=True)
        flags = ["-std=c++23", "-I" + str(include), "-DALIAS_WIDTH=" + str(width)]
        runner = fixtures / "ResolvedTypeAliasesRun.cc"
        if args.flow == "cpp":
            binary = directory / "native"
            run([args.cxx, *flags, runner, "-o", binary], directory, "build")
        else:
            generated = directory / "generated"
            run([args.cpphdl, "--generated-dir=" + str(generated),
                 fixtures / "ResolvedTypeAliases.h", "--", *flags], directory, "convert")
            rtl = (generated / "ResolvedTypeAliases.sv").read_text()
            # Check the selected *type*, not just the ports (which used to
            # lower correctly even while the typedef itself was malformed).
            bits = 32 if width <= 32 else 64
            assert f"typedef logic[{bits - 1}:0] DTYPE;" in rtl, rtl
            assert "std_conditional" not in rtl, rtl
            # Ports are lowered independently of local aliases. Check the
            # typedefs in their lexical SV scope as well as the output values.
            checked = directory / "CheckedResolvedTypeAliases.sv"
            checks = f"""
    initial begin
        if ($bits(DTYPE) != {bits} || $bits(chained_t) != {bits} ||
            $bits(wide_t) != 64 || $bits(signed_t) != 64 || $bits(vector_t) != {width})
            $fatal(1, "Resolved alias width mismatch");
    end
"""
            checked.write_text(rtl.replace("endmodule", checks + "\nendmodule"))
            obj = directory / "obj"
            run([args.verilator, "--cc", "--exe", "--build", "-j", "1", "-Wno-fatal",
                 "--top-module", "ResolvedTypeAliases", "--Mdir", obj,
                 "-CFLAGS", " ".join([*flags, "-DALIAS_RTL"]),
                 "-MAKEFLAGS", "CXX=" + args.cxx + " LINK=" + args.cxx,
                 generated / "Predef_pkg.sv", checked, runner],
                directory, "build")
            binary = obj / "VResolvedTypeAliases"
        print(run([binary], directory, "run"), end="", flush=True)


if __name__ == "__main__":
    main()
