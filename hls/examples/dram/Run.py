"""Exercise variable-latency pointer reads and a retimed calculation pipeline."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
for name in ("cpphdl", "cxx", "verilator", "work"):
    parser.add_argument("--" + name, required=True)
parser.add_argument("--flow", choices=("rtl", "gates"), required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
source = Path(__file__).with_name("DramStream.cpp")
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
atomic = Path(subprocess.check_output([args.cxx, "-print-file-name=libatomic.so"], text=True).strip()).parent
runtime = Path(subprocess.check_output([args.cxx, "-print-file-name=libstdc++.so"], text=True).strip()).resolve().parent
env = dict(os.environ, LD_LIBRARY_PATH=str(runtime) + ":" + os.environ.get("LD_LIBRARY_PATH", ""))


def run(command, label):
    log = work / (label + ".log")
    with log.open("w") as output:
        result = subprocess.run(list(map(str, command)), env=env, stdout=output,
                                stderr=subprocess.STDOUT, timeout=900)
    if result.returncode:
        raise RuntimeError(f"{label} failed: {log}\n{log.read_text()[-8000:]}")


rtl = work / "generated"
if rtl.exists():
    shutil.rmtree(rtl)
if args.flow == "gates":
    run([args.cpphdl, "--synth", "--top", "DramStream", "--module", "DramStream",
         "--retiming", "fit_pipeline_retiming", "--clock-period-ns", "5",
         "--retime-module", "cpphdl_synth_top.calculate", "--output", rtl, "--cxx", args.cxx, source], "synthesis")
    manifest = json.loads((rtl / "manifest.json").read_text())
    assert manifest["status"] == "complete"
    timing = manifest["timing"]
    assert timing["rules"][0]["target_met"] and timing["rules"][0]["added_latency"] > 0
    assert len(timing["streaming_regions"]) == 1
    assert timing["streaming_regions"][0]["initiation_interval"] == 1
    assert not timing["rules"][0]["feedback_scheduled"], "memory handshake must not be retimed"
    sources = [rtl / "gates.v"]
else:
    run([args.cpphdl, "--generated-dir", rtl, source], "conversion")
    sources = sorted(rtl.glob("*_pkg.sv")) + sorted(path for path in rtl.glob("*.sv") if not path.name.endswith("_pkg.sv"))
obj = work / "obj"
if obj.exists():
    shutil.rmtree(obj)
run([args.verilator, "--cc", "--exe", "--build", "-j", "2", "-Wno-fatal",
     "--top-module", "DramStream", "--Mdir", obj,
     "-MAKEFLAGS", f"CXX={args.cxx} LINK={args.cxx} AR=ar LDFLAGS=-L{atomic}",
     "-CFLAGS", f"-std=c++17 -O1 -DVERILATOR -I{root / 'include'}", *sources, source], "verilator-build")
log = (work / "verilator-build.log").read_text()
assert not any("Warning-" + name in log for name in ("LATCH", "MULTIDRIVEN", "UNOPTFLAT"))
run([obj / "VDramStream"], "simulation")
print((work / "simulation.log").read_text())
