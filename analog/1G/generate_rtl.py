#!/usr/bin/env python3
"""Export or check the 1G digital catalog through the root C++RTL converter."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def generate(converter, source, output):
    modules = ("pcs.cpp", "pcs_transmit.cpp", "pcs_receive.cpp", "pcs_branch_metrics_pipeline.cpp", "pcs_synchronize.cpp",
               "pcs_framer.cpp", "pcs_receiver.cpp", "receive_alignment.cpp", "receive_filter.cpp",
               "receive_filter_pipeline.cpp", "receive_filter_datapath.cpp", "receive_timing.cpp", "receive_normalizer.cpp", "receive_calibration.cpp", "pam5_driver_control.cpp", "adc_sar_control.cpp",
               "flp_transmit.cpp", "flp_receive.cpp", "master_slave.cpp", "negotiation.cpp", "autonegotiation.cpp", "pma_control.cpp", "pma_endpoint.cpp", "pma_aligned.cpp", "adc_interleave.cpp", "adc_receive.cpp", "pair_acquisition.cpp", "pma_test_pattern.cpp", "pma_transmit_lane.cpp", "receive_gain_control.cpp", "receive_range_calibration.cpp", "receive_acquisition.cpp")
    for name in modules:
        clocks = (["--primary_clock", "ref_clk", "125000000", "--secondary_clock", "rx_clk", "125000000"]
                  if name == "receive_alignment.cpp" else ["--primary_clock", "clk", "250000000"]
                  if name in ("adc_sar_control.cpp", "adc_interleave.cpp", "adc_receive.cpp", "receive_normalizer.cpp", "receive_gain_control.cpp", "receive_range_calibration.cpp", "receive_acquisition.cpp") else ["--primary_clock", "clk", "125000000"]
                  if name in ("autonegotiation.cpp", "pma_control.cpp", "pma_endpoint.cpp", "pma_aligned.cpp", "pair_acquisition.cpp", "receive_calibration.cpp", "pma_test_pattern.cpp", "pma_transmit_lane.cpp", "receive_gain_control.cpp", "receive_range_calibration.cpp", "receive_acquisition.cpp") else [])
        subprocess.run([str(converter), *clocks, "--generated-dir", str(output), str(source / name),
                        "--", "-I" + str(source.parents[1] / "include")], check=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    destination = (args.output or source / "generated").resolve()
    converter = args.converter.resolve()
    with tempfile.TemporaryDirectory(prefix="cpphdl-1g-rtl-") as directory:
        temporary = Path(directory)
        generate(converter, source, temporary)
        generated = {p.name for p in temporary.glob("*.sv")}
        expected = {p.name for p in destination.glob("*.sv")}
        if args.check:
            if generated != expected:
                raise ValueError("generated digital catalog differs; run generate-1g-rtl")
            for name in generated:
                if (temporary / name).read_bytes() != (destination / name).read_bytes():
                    raise ValueError("stale digital export " + name + "; run generate-1g-rtl")
        else:
            if expected - generated:
                raise ValueError("destination contains models outside the 1G catalog: " + str(sorted(expected - generated)))
            destination.mkdir(parents=True, exist_ok=True)
            for name in generated:
                shutil.copyfile(temporary / name, destination / name)
        print(f"{'Checked' if args.check else 'Exported'} {len(generated)} 1G RTL/package files")


if __name__ == "__main__":
    main()
