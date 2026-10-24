#!/usr/bin/env python3
"""Reject changes to the reviewed shared-reference physical binding."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(ANALOG / '1G/IHPSG13G2'))
from lead_receive_common_process import process_deck
from mos_geometry import size_mos_instances


def run(args):
    with tempfile.TemporaryDirectory(prefix='lead-reference-binding-') as temporary:
        work = Path(temporary)
        subprocess.run([str(args.bench.resolve()), str(work), 'export'], check=True)
        model, deck = (work / 'design.va').read_text(), (work / 'bench.cir').read_text()
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
        mapped, inventory = process_deck(model, deck, models)
        sized, geometry = size_mos_instances(mapped)
        assert (inventory['mos_count'], inventory['resistor_count'], inventory['capacitor_count']) == (11, 66, 37)
        sink = next(item for item in inventory['mos'] if item['source'] == 'shunt_voltage_buffer.hpp')
        assert sink['width_um'] == 150 and sink['length_um'] == .45
        assert re.search(r'^Xphysical' + sink['id'] + r' .* sg13_hv_nmos w=150u l=.45u ng=15$', sized, re.M)
        observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
        lead = re.search(r'(?m)^.*// device \d+,.*lead_shunt_voltage_buffer.hpp:\d+\n([^\n]+)', model).group(1)
        sink_line = re.search(r'(?m)^.*// device ' + sink['id'] + r',[^\n]+\n([^\n]+)', model).group(1)
        cap_lines = [line for line in deck.splitlines() if line.startswith('Cdevice')]
        lead_cap = next(line for line in cap_lines if float(line.split()[3]) == 2e-12)
        output_cap = next(line for line in cap_lines if float(line.split()[3]) == 80e-12)
        passed = []

        def reject(name, changed_model, changed_deck, reason):
            try:
                process_deck(changed_model, changed_deck, models)
            except ValueError as error:
                if reason not in str(error):
                    raise AssertionError(name + ': unexpected rejection: ' + str(error)) from error
                passed.append(name)
            else:
                raise AssertionError(name + ': invalid physical binding accepted')

        reject('missing lead resistor', model.replace(lead, ''), deck, 'resistor inventory')
        reject('changed lead resistance', model.replace(lead, lead.replace('/ 1000.0;', '/ 500.0;')), deck, 'resistor inventory')
        reject('missing lead capacitor', model, deck.replace(lead_cap + '\n', ''), 'storage or cold charge')
        fields = lead_cap.split(); fields[3] = '1e-12'
        reject('changed lead capacitance', model, deck.replace(lead_cap, ' '.join(fields)), 'storage or cold charge')
        wrong_sink = sink_line.replace('0.029999999999999999', '0.02')
        assert wrong_sink != sink_line
        reject('wrong sink strength', model.replace(sink_line, wrong_sink), deck, 'transistor profile')
        reject('nonzero cold output charge', model, deck.replace(output_cap, output_cap.replace('IC=0.0', 'IC=0.01')), 'storage or cold charge')
        fields = lead_cap.split(); fields[1:3] = ['0' if n == observations['0'] else n for n in fields[1:3]]
        reject('lead capacitor wrong return', model, deck.replace(lead_cap, ' '.join(fields)), 'compensation capacitor connection')
        bad_lead = re.sub(r'\b' + observations['3'] + r'\b', observations['2'], lead)
        reject('lead resistor wrong drive', model.replace(lead, bad_lead), deck, 'resistor at DRIVE')
        fields = output_cap.split(); fields[1:3] = [observations['2'] if n == observations['0'] else n for n in fields[1:3]]
        reject('output storage wrong node', model, deck.replace(output_cap, ' '.join(fields)), 'output storage connection')
        bad_sink = re.sub(r'\b' + observations['0'] + r'\b', observations['2'], sink_line)
        reject('sink drain wrong node', model.replace(sink_line, bad_sink), deck, 'feedback, bias or sink connections')
        target_line = next(line for line in model.splitlines() if '/ 12150.0;' in line)
        reject('wrong target divider ratio', model.replace(target_line, target_line.replace('12150.0', '12000.0')), deck, 'resistor inventory')
        wrong_target = re.sub(r'\b' + observations['2'] + r'\b', observations['0'], target_line)
        reject('wrong target divider wiring', model.replace(target_line, wrong_target), deck, 'target divider topology')
        return dict(status='pass', rejected_mutations=passed, positive_inventory=inventory,
                    test_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                    bench_sha256=hashlib.sha256(args.bench.read_bytes()).hexdigest(),
                    geometry=geometry, model_sha256=hashlib.sha256(model.encode()).hexdigest(),
                    mapper_sha256=hashlib.sha256((ANALOG / '1G/IHPSG13G2/lead_receive_common_process.py').read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = run(args)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS reviewed11-MOS reference binding and', len(result['rejected_mutations']), 'negative mutations')
