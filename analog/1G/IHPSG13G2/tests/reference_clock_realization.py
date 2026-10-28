#!/usr/bin/env python3
"""Guard whole-IC replacement boundaries using a real current C++ export."""
import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from reference_clock_realization import partition


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    subprocess.run([str(args.exporter.resolve()), str(work)], check=True, timeout=60)
    model = (work / 'reference.va').read_text()
    blocks = json.loads((work / 'realizations.json').read_text())
    devices, capacitors, ports, audit = partition(model, blocks)
    if (len(devices), len(capacitors)) != (42, 51) or ports['divider']['Q'] != ports['detector']['FEEDBACK']:
        raise ValueError('surrounding clock topology or connected feedback changed')
    # The external reference's original storage is outside detector ownership.
    if sum(value for a, b, value in capacitors if (a, b) == ('REF', 'VSS')) != 2e-15:
        raise ValueError('realization removed storage owned by the surrounding IC')
    checked = []

    def reject(name, text, metadata, reason):
        try:
            partition(text, metadata)
        except ValueError as error:
            if reason not in str(error):
                raise ValueError(name + ': rejected for an unrelated reason') from error
            checked.append(name)
        else:
            raise ValueError('accepted invalid realization: ' + name)

    bad = copy.deepcopy(blocks)
    bad['divider']['elements'][0] = bad['detector']['elements'][0]
    reject('overlapping_ownership', model, bad, 'ownership')
    bad = copy.deepcopy(blocks)
    bad['detector']['elements'].pop()
    reject('missing_owned_device', model, bad, 'ownership')
    bad = copy.deepcopy(blocks)
    bad['detector']['ports']['UP_N'] = 'unknown_node'
    reject('unknown_port', model, bad, 'absent')
    bad = copy.deepcopy(blocks)
    bad['detector']['ports']['UP_N'] = bad['detector']['ports']['UP']
    reject('aliased_outputs', model, bad, 'absent')
    owned = set(blocks['detector']['elements'])
    internal = set()
    for match in re.finditer(r'^\s*// device (\d+),[^\n]*\n[ \t]*(I\([^\n]+)', model, re.M):
        if int(match[1]) in owned:
            internal.update(re.findall(r'\bcpphdl_n\d+\b', match[2]))
    internal -= set(blocks['detector']['ports'].values())
    node = sorted(internal)[0]
    coupling = (f'    // device 999999, external coupling guard\n'
                f'    I({node}, Vss) <+ 1e-15 * ddt(V({node}, Vss)); // Bench initial voltage = 0.0 V.\n')
    altered = model.replace('    end\nendmodule\n', coupling + '    end\nendmodule\n')
    reject('private_node_coupling', altered, blocks, 'undeclared internal node')
    (work / 'checks.json').write_text(json.dumps(dict(status='pass', guards=checked, binding=audit), indent=2)+'\n')
    print('PASS actual C++ ownership export, retained surrounding storage and five boundary guards')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
