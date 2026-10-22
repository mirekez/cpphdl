import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for name in ("hdlcpp", "cpphdl", "cxx", "verilator"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith(("HDLCPP_", "CPPHDL_", "CVA6_"))}
    records = []
    node_counts = {}

    def run(command, directory, label, env=environment):
        result = subprocess.run([str(item) for item in command], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=180)
        (directory / (label + ".log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            logs = "".join(path.read_text()[-3000:] for path in directory.rglob("*.log"))
            raise AssertionError((command, result.returncode, logs))
        return result.stdout

    for package_row, full_write in ((False, False), (True, False), (False, True)):
        directory = args.work / ("full" if full_write else "package" if package_row else "local")
        directory.mkdir(exist_ok=True)
        defines = ["-DPACKAGE_ROW"] if package_row else []
        if full_write:
            defines.append("-DFULL_WRITE")
        conversion_env = dict(environment)
        if defines:
            conversion_env["HDLCPP_DEFINES"] = ",".join(define[2:] for define in defines)
        run([args.hdlcpp, fixtures / "TypedefRam.sv"], directory, "convert", conversion_env)
        generated = (directory / "generated/TypedefRam.h").read_text()
        assert re.search(r"memory<.*?,.*?,Depth> mem;", generated), generated
        for bank in ("PackedBank", "ResetBank", "StructBank", "ArrayReadBank", "AsyncIndexBank"):
            register_bank = generated.split("class " + bank, 1)[1].split("\nclass ", 1)[0]
            assert "memory<" not in register_bank and "reg<" in register_bank, register_bank
        for address_bits, words in (((4, 2),) if full_write else ((4, 1), (9, 1), (4, 2))):
            flags = ["-std=c++23", "-O2", "-I" + str(fixtures.parents[1] / "include"),
                     "-I" + str(directory), "-DRAM_ADDR_BITS=" + str(address_bits),
                     "-DRAM_WORDS=" + str(words), *defines]
            label = f"{address_bits}-{words}"
            runner = fixtures / "TypedefRamRun.cc"
            ordinary = directory / ("ordinary-" + label)
            run([args.cxx, *flags, "-fsanitize=address,undefined", runner, "-o", ordinary],
                directory, "build-ordinary-" + label)
            binaries = {"ordinary": ordinary}
            seed = directory / ("seed-" + label + ".cc")
            seed.write_text('#include "generated/TypedefRam.h"\n'
                            f'extern TypedefRam<{address_bits}, {words}> cpphdl_top;\n')
            for chunk in (0, 512):
                output = directory / ("graph-" + label + "-" + str(chunk))
                run([args.cpphdl, "--native-graph", "--top", "cpphdl_top",
                     "--chunk-size=" + str(chunk), *(["--split-chunks=32"] if chunk else []),
                     "--cxx", args.cxx, "--frontend-flag=-I" + str(directory), "--output", output,
                     "--runner", runner, seed, "--", *flags, "-DRAM_GRAPH"],
                    directory, "build-graph-" + label + "-" + str(chunk))
                if chunk:
                    memories = (output / "memories.tsv").read_text().splitlines()
                    assert len(memories) == 1, memories
                    _, width, depth, name = memories[0].split("\t", 3)
                    assert (int(width), int(depth)) == (128 * words, 1 << address_bits), memories
                    assert name.endswith(".mem"), memories
                scheduled = re.search(r"(\d+) scheduled nodes", (output / "lower.log").read_text())
                assert scheduled and int(scheduled[1]) < 512, "RAM depth expanded into graph nodes"
                key = package_row, full_write, words, chunk
                count = int(scheduled[1])
                if key in node_counts:
                    assert count == node_counts[key], "RAM node count depends on depth"
                node_counts[key] = count
                binaries["native-" + str(chunk)] = output / "run"
            output = directory / ("verilator-" + label)
            run([args.verilator, "--cc", "--exe", "--build", "-j", "1", "-Wno-fatal",
                 "--top-module", "TypedefRam", "-GAddrBits=" + str(address_bits),
                 "-GWords=" + str(words), *defines, "--Mdir", output,
                 "-CFLAGS", " ".join(flags + ["-DRAM_RTL"]),
                 "-MAKEFLAGS", "CXX=" + args.cxx + " OPT_FAST=-O2",
                 fixtures / "TypedefRam.sv", runner], directory, "build-verilator-" + label)
            binaries["verilator"] = output / "VTypedefRam"
            checksums = set()
            for kind, binary in binaries.items():
                text = run([binary], directory, "run-" + kind + "-" + label)
                match = re.search(r"RAM_BENCH depth=(\d+) words=(\d+) cycles=(\d+) checked=(\d+) checksum=(\w+) seconds=([0-9.]+)", text)
                assert match, text
                checksums.add(match[5])
                record = dict(package_row=package_row, full_write=full_write, depth=int(match[1]), words=int(match[2]),
                              cycles=int(match[3]), checked=int(match[4]), checksum=match[5],
                              seconds=float(match[6]), kind=kind)
                records.append(record)
                print(json.dumps(record), flush=True)
            assert len(checksums) == 1, records

    directory = args.work / "odd-width"
    directory.mkdir(exist_ok=True)
    run([args.hdlcpp, fixtures / "TypedefRam.sv"], directory, "convert")
    flags = ["-std=c++23", "-O1", "-I" + str(fixtures.parents[1] / "include"),
             "-I" + str(directory)]
    runner = fixtures / "OddWidthRamRun.cc"
    ordinary = directory / "ordinary"
    run([args.cxx, *flags, "-fsanitize=address,undefined", runner, "-o", ordinary],
        directory, "build-ordinary")
    print(run([ordinary], directory, "run-ordinary"), end="", flush=True)
    seed = directory / "seed.cc"
    seed.write_text('#include "generated/TypedefRam.h"\nextern OddWidthRam cpphdl_top;\n')
    for chunk in (0, 512):
        output = directory / ("graph-" + str(chunk))
        run([args.cpphdl, "--native-graph", "--top", "cpphdl_top",
             "--chunk-size=" + str(chunk), *(["--split-chunks=32"] if chunk else []),
             "--cxx", args.cxx, "--frontend-flag=-I" + str(directory), "--output", output,
             "--runner", runner, seed, "--", *flags, "-DRAM_GRAPH"], directory,
            "build-graph-" + str(chunk))
        print(run([output / "run"], directory, "run-graph-" + str(chunk)), end="", flush=True)
    output = directory / "verilator"
    run([args.verilator, "--cc", "--exe", "--build", "-j", "1", "-Wno-fatal",
         "--top-module", "OddWidthRam", "--Mdir", output,
         "-CFLAGS", " ".join(flags + ["-DRAM_RTL"]), "-MAKEFLAGS", "CXX=" + args.cxx,
         fixtures / "TypedefRam.sv", runner], directory, "build-verilator")
    print(run([output / "VOddWidthRam"], directory, "run-verilator"), end="", flush=True)
    run([args.hdlcpp, fixtures / "TypedefBlockingBank.sv"], directory, "convert-blocking-bank")
    blocking_bank = (directory / "generated/TypedefBlockingBank.h").read_text()
    assert "memory<" not in blocking_bank, "blocking writes must remain immediately visible"
    (args.work / "results.json").write_text(json.dumps(records, indent=2) + "\n")


if __name__ == "__main__":
    main()
