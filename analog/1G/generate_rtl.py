#!/usr/bin/env python3
"""Export or check the 1G digital catalog through the root C++RTL converter."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def merge_export(source, output, namespace, canonical):
    """Keep each converter invocation's clock-specialized hierarchy intact."""
    files = {p.stem: p for p in source.glob('*.sv') if p.name != 'Predef_pkg.sv'}
    if not files:
        raise ValueError('converter produced no modules for ' + namespace)
    token = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(files, key=len, reverse=True))) + r')\b')
    comments = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
    used = set()
    for name, path in files.items():
        text = comments.sub('', path.read_text())
        declaration = re.search(r'\bmodule\s+' + re.escape(name) + r'\b', text)
        if declaration is None:
            raise ValueError('module/file name mismatch: ' + str(path))
        body = text[:declaration.start()] + text[declaration.end():]
        used.update(token.findall(body))
    roots = set(files) - used
    if not roots:
        raise ValueError('export has no hierarchy root: ' + namespace)
    names = {}
    for name in files:
        if name in roots and name not in canonical:
            names[name] = name
            canonical.add(name)
        else:
            names[name] = namespace + '__' + name
    package = source / 'Predef_pkg.sv'
    target_package = output / package.name
    if target_package.exists() and target_package.read_bytes() != package.read_bytes():
        raise ValueError('converter invocations produced incompatible runtime packages')
    shutil.copyfile(package, target_package)
    for name, path in files.items():
        target = output / (names[name] + '.sv')
        if target.exists():
            raise ValueError('duplicate catalog module: ' + target.name)
        target.write_text(token.sub(lambda match: names[match[0]], path.read_text()))
    # Relative file names are usable from the generated directory. Each list
    # includes its own complete hierarchy, without other clock specializations.
    (output / (namespace + '.f')).write_text(
        '// Tops: ' + ', '.join(sorted(names[name] for name in roots)) + '\n' +
        'Predef_pkg.sv\n' + ''.join(names[name] + '.sv\n' for name in sorted(files)))
    return {name + '.sv' for name in files} | {'Predef_pkg.sv'}


def generate(converter, source, output, include_root=None):
    include_root = include_root or source.parents[1] / 'include'
    modules = ("pcs.cpp", "pcs_transmit.cpp", "pcs_receive.cpp", "pcs_branch_metrics_pipeline.cpp", "pcs_synchronize.cpp",
               "pcs_framer.cpp", "pcs_receiver.cpp", "receive_sample_recovery.cpp", "receive_sample_pcs.cpp", "receive_quality.cpp", "receive_timing_confidence.cpp", "receive_symbol_crossing.cpp", "receive_dsp_crossing.cpp", "receive_sample_pcs_qualified.cpp", "receive_alignment.cpp", "receive_filter.cpp",
               "receive_filter_pipeline.cpp", "receive_filter_datapath.cpp", "receive_dsp.cpp", "receive_dsp_pcs.cpp", "receive_sample_context.cpp", "sampled_receive_phy.cpp", "adc_receive_capture.cpp", "adc_receive_phy.cpp", "reference_clock_monitor.cpp", "clocked_receive_phy.cpp", "asynchronous_receive_phy.cpp", "receive_timing.cpp", "receive_normalizer.cpp", "receive_calibration.cpp", "pam5_driver_control.cpp", "adc_sar_control.cpp",
               "flp_transmit.cpp", "flp_receive.cpp", "master_slave.cpp", "negotiation.cpp", "autonegotiation.cpp", "pma_control.cpp", "pma_endpoint.cpp", "pma_aligned.cpp", "adc_interleave.cpp", "adc_receive.cpp", "adc_interleaved_bank.cpp", "adc_receive_bank.cpp", "adc_startup.cpp", "adc_receive_lane.cpp", "pair_acquisition.cpp", "pma_test_pattern.cpp", "pma_transmit_lane.cpp", "receive_gain_control.cpp", "receive_range_calibration.cpp", "receive_acquisition.cpp")
    modules += ("ethernet_pause.cpp", "processing_clock_control.cpp", "phy_reset_release.cpp", "phy_reset_control.cpp", "receive_transmit_crossing.cpp", "synchronous_frame_fifo.cpp", "repeater_forwarding.cpp", "flow_controlled_repeater.cpp", "autonomous_receive_filter.cpp", "autonomous_receive_pcs.cpp", "autonomous_receive_phy.cpp", "autonomous_adc_receive_phy.cpp", "qualified_adc_receive_phy.cpp")
    modules += ("negotiated_repeater.cpp",)
    canonical, legacy = set(), set()
    for name in modules:
        clocks = (["--primary_clock", "ref_clk", "125000000", "--secondary_clock", "rx_clk", "125000000"]
                  if name == "receive_alignment.cpp" else ["--primary_clock", "adc_clk", "250000000", "--secondary_clock", "ref_clk", "125000000"]
                  if name in ("reference_clock_monitor.cpp", "clocked_receive_phy.cpp") else ["--primary_clock", "adc_clk", "250000000", "--secondary_clock", "pcs_clk", "142857143"]
                  if name == "receive_symbol_crossing.cpp" else ["--primary_clock", "adc_clk", "250000000", "--secondary_clock", "pcs_clk", "144000000"]
                  if name in ("receive_dsp_crossing.cpp", "asynchronous_receive_phy.cpp") else ["--primary_clock", "clk", "250000000"]
                  if name in ("pcs_receiver.cpp", "receive_dsp.cpp", "receive_dsp_pcs.cpp", "receive_sample_context.cpp", "sampled_receive_phy.cpp", "adc_receive_capture.cpp", "adc_receive_phy.cpp", "receive_sample_recovery.cpp", "receive_sample_pcs.cpp", "receive_quality.cpp", "receive_timing_confidence.cpp", "receive_sample_pcs_qualified.cpp", "adc_sar_control.cpp", "adc_interleave.cpp", "adc_receive.cpp", "adc_interleaved_bank.cpp", "adc_receive_bank.cpp", "adc_startup.cpp", "adc_receive_lane.cpp", "receive_normalizer.cpp", "receive_gain_control.cpp", "receive_range_calibration.cpp", "receive_acquisition.cpp") else ["--primary_clock", "clk", "125000000"]
                  if name in ("autonegotiation.cpp", "pma_control.cpp", "pma_endpoint.cpp", "pma_aligned.cpp", "pair_acquisition.cpp", "receive_calibration.cpp", "pma_test_pattern.cpp", "pma_transmit_lane.cpp", "receive_gain_control.cpp", "receive_range_calibration.cpp", "receive_acquisition.cpp") else [])
        if name == "processing_clock_control.cpp":
            clocks = ["--primary_clock", "pcs_clk", "800000000", "--secondary_clock", "ref_clk", "125000000"]
        if name in ("phy_reset_release.cpp", "phy_reset_control.cpp"):
            clocks = ["--primary_clock", "adc_clk", "250000000", "--secondary_clock", "pcs_clk", "136000000", "--secondary_clock", "ref_clk", "125000000"]
        if name == "qualified_adc_receive_phy.cpp":
            clocks = ["--primary_clock", "adc_clk", "250000000", "--secondary_clock", "pcs_clk", "136000000", "--secondary_clock", "ref_clk", "125000000"]
        if name in ("receive_transmit_crossing.cpp", "synchronous_frame_fifo.cpp"):
            clocks = ["--primary_clock", "rx_clk", "144000000", "--secondary_clock", "tx_clk", "125000000"]
        if name in ("repeater_forwarding.cpp", "flow_controlled_repeater.cpp"):
            clocks = ["--primary_clock", "rx_clk", "136000000", "--secondary_clock", "tx_clk", "125000000",
                      "--secondary_clock", "b_rx_clk", "136000000", "--secondary_clock", "a_tx_clk", "125000000"]
        if name == "negotiated_repeater.cpp":
            # ref_clk remains physically 125 MHz; the conservative conversion
            # budget keeps legacy negotiation processes in the primary domain.
            clocks = ["--primary_clock", "ref_clk", "136000000", "--secondary_clock", "rx_clk", "136000000",
                      "--secondary_clock", "tx_clk", "125000000", "--secondary_clock", "b_rx_clk", "136000000",
                      "--secondary_clock", "a_tx_clk", "125000000"]
        if name in ("autonomous_receive_filter.cpp", "autonomous_receive_pcs.cpp"):
            clocks = ["--primary_clock", "clk", "136000000"]
        if name in ("autonomous_receive_phy.cpp", "autonomous_adc_receive_phy.cpp"):
            clocks = ["--primary_clock", "adc_clk", "250000000", "--secondary_clock", "pcs_clk", "136000000"]
        with tempfile.TemporaryDirectory(prefix='cpphdl-clock-variant-') as directory:
            isolated = Path(directory)
            subprocess.run([str(converter), *clocks, "--generated-dir", str(isolated), str(source / name),
                            "--", "-I" + str(include_root)], check=True, timeout=60)
            legacy.update(merge_export(isolated, output, Path(name).stem, canonical))
    return legacy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verilator", type=Path,
                        help='lint every complete exported hierarchy before accepting the catalog')
    parser.add_argument('--include-root', type=Path,
                        help='use a frozen C++HDL include tree when other work is changing the shared compiler')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    destination = (args.output or source / "generated").resolve()
    converter = args.converter.resolve()
    include_root = (args.include_root or source.parents[1] / 'include').resolve()
    inputs = {converter, Path(__file__).resolve()}
    for directory in (source, include_root):
        inputs.update(p for p in directory.rglob('*') if p.suffix in ('.h', '.hpp', '.cpp'))
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    with tempfile.TemporaryDirectory(prefix="cpphdl-1g-rtl-") as directory:
        temporary = Path(directory)
        legacy = generate(converter, source, temporary, include_root)
        if args.verilator:
            for filelist in sorted(temporary.glob('*.f')):
                subprocess.run([str(args.verilator.resolve()), '--lint-only', '-Wno-fatal',
                                '-f', filelist.name], cwd=temporary, check=True, timeout=120)
        changed = [str(p) for p, h in hashes.items() if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != h]
        if changed:
            raise ValueError('converter or source changed during catalog generation: ' + ', '.join(sorted(changed)))
        generated = {p.name for p in temporary.iterdir() if p.suffix in ('.sv', '.f')}
        expected = {p.name for p in destination.iterdir() if p.suffix in ('.sv', '.f')} if destination.exists() else set()
        if args.check:
            if generated != expected:
                raise ValueError("generated digital catalog differs; run generate-1g-rtl")
            for name in generated:
                if (temporary / name).read_bytes() != (destination / name).read_bytes():
                    raise ValueError("stale digital export " + name + "; run generate-1g-rtl")
        else:
            if expected - generated - legacy:
                raise ValueError("destination contains models outside the 1G catalog: " + str(sorted(expected - generated - legacy)))
            destination.mkdir(parents=True, exist_ok=True)
            for name in generated:
                shutil.copyfile(temporary / name, destination / name)
            for name in expected - generated:
                (destination / name).unlink()
        print(f"{'Checked' if args.check else 'Exported'} {len(generated)} 1G RTL/package files")


if __name__ == "__main__":
    main()
