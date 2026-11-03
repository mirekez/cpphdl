"""Required process workloads, with explicit native-model coverage gaps.

This catalog describes coverage, not passing test results. Process comparisons
cannot use a square-law MOS surrogate in place of the reference PSP model.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Workload:
    name: str
    purpose: str
    reference: str
    short_window: str
    native_gap: str


WORKLOADS = (
    Workload('dc_operating_sweep', 'DC bias, current delivery and nonlinear convergence',
             '1G/IHPSG13G2/tests/mos_bank_dc.py', 'Five bias points including cutoff and operating region',
             'PSP primitive LV/HV sweeps now tested; native extracted transistor-bank hierarchy still required'),
    Workload('intrinsic_charge', 'Terminal charge and nonlinear capacitance',
             '1G/IHPSG13G2/tests/moscap_bias_storage.py', 'One charge/discharge excursion',
             'PSP intrinsic charge and voltage derivatives now tested; full MOSCAP/MOSVAR wrapper still required'),
    Workload('startup_bias', 'Cold supply startup and interacting bias loops',
             '1G/IHPSG13G2/tests/transmit_connected_startup.py', 'Supply ramp and initial bias response',
             'Equivalent UIC/initial conditions and startup integration'),
    Workload('pam5_switching', 'Loaded five-level TX transitions and shared supplies',
             '1G/IHPSG13G2/tests/transmit_connected_charge.py', 'One traversal of five levels plus a full-scale reversal',
             'Coupled process devices, switch charge and actual load'),
    Workload('sample_hold', 'Acquisition, aperture, charge injection and held value',
             '1G/IHPSG13G2/tests/receive_sampler_conversion.py', 'One slice and one complete conversion',
             'Extracted sampler plus process DAC/comparator and native control events'),
    Workload('mixed_adc_feedback', 'Digital SAR decisions feed back into the electrical solve',
             '1G/IHPSG13G2/tests/differential_receive_conversion.py', 'One conversion with every decision and commit retained',
             'Native class backend digital scheduler and PSP devices together'),
    Workload('clock_feedback', 'Oscillation, gate admission and controller interaction',
             '1G/IHPSG13G2/tests/processing_clock_extracted_delivery.py', 'Several edges; no lock/drift claim from a short startup window',
             'Coupled PSP inverter now tested; native process oscillator, actual edge location and controller coupling still required'),
    Workload('storage_state', 'SRAM and latch state, read disturbance and switching',
             '1G/IHPSG13G2/tests/sram_bitcell_sensitivity.py', 'One write/read of each state',
             'Same process devices and accepted/rejected state handling'),
    Workload('extracted_network', 'Distributed RC and extracted device hierarchy',
             '1G/IHPSG13G2/tests/transmit_bias_reference_access.py', 'Small extracted subnetwork and a bias or load transition',
             'SPICE hierarchy/parameter frontend and complete compact-model node mapping'),
    Workload('process_temperature', 'Process, supply and temperature dependence',
             '1G/IHPSG13G2/tests/receive_frontend_process.py', 'Nominal plus one slow/hot and one fast/cold short case',
             'TT/SS/FF and temperature now tested on LV/HV PSP loads; full receive frontend still required'),
    Workload('pad_interface', 'Pad loading, body/junction paths and terminal operating limits',
             'pdk/1G_repeater_IHPSG13G2/tests/pad_operating.py', 'Nominal and two terminal-envelope points',
             'Process junction/body behavior with the same pad netlist'),
)


def report():
    lines = ['IHP SG13G2 native-first simulation workload coverage',
             'Status: incomplete; this catalog is not simulation evidence.',
             'Primary: C++RTL. Independent checker: ngspice.',
             'Short profiles preserve the circuit/model; long qualification is separate.', '']
    lines += ['Executable prerequisite coverage: tests/IHPSG13G2/run.py (16 PSP process cases).',
              'Coupled circuit coverage: tests/IHPSG13G2/inverter.py (8 schematic inverter cases).',
              'Primitive coverage does not mark the larger original workloads complete.', '']
    for item in WORKLOADS:
        if not (ROOT / item.reference).is_file():
            raise ValueError('missing reference workload: ' + item.reference)
        lines += [item.name + ': ' + item.purpose, '  Existing reference: ' + item.reference,
                  '  Short profile: ' + item.short_window, '  Native gap: ' + item.native_gap, '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--require-complete', action='store_true',
                        help='fail while same-model native process coverage remains missing')
    args = parser.parse_args()
    text = report() + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end='')
    if args.require_complete:
        raise SystemExit('IHP native process workload coverage is incomplete')
