"""Require mapped selector decisions to use registered branch differences."""
import argparse
import hashlib
import json
from pathlib import Path


def check(module):
    selectors = {bit for name, net in module['netnames'].items()
                 if name == 'core.selectors' or name.startswith('core.selectors[')
                 for bit in net['bits'] if isinstance(bit, int)}
    if not selectors:
        raise ValueError('missing mapped selector registers')
    metric = set(module['ports']['metric_in']['bits'])
    difference = module['ports']['difference_in']['bits']
    drivers = {}
    for name, cell in module['cells'].items():
        for pin, direction in cell['port_directions'].items():
            if direction == 'output':
                for bit in cell['connections'][pin]:
                    if isinstance(bit, int):
                        if bit in drivers:
                            raise ValueError('multiple drivers')
                        drivers[bit] = (name, cell, pin)
    input_bits = {bit for port in module['ports'].values() if port['direction'] == 'input'
                  for bit in port['bits']}
    visited, active, used_differences = set(), set(), set()

    def visit(bit):
        if not isinstance(bit, int) or bit in visited:
            return
        if bit in metric:
            raise ValueError('selector still depends on raw branch metrics')
        if bit in difference:
            used_differences.add(bit)
        if bit in input_bits:
            return
        if bit in active:
            raise ValueError('combinational cycle')
        if bit not in drivers:
            raise ValueError('undriven selector dependency')
        name, cell, pin = drivers[bit]
        if 'CLK' in cell['connections'] and pin == 'Q':
            return
        active.add(bit)
        for pin, direction in cell['port_directions'].items():
            if direction == 'input':
                for source in cell['connections'][pin]:
                    visit(source)
        active.remove(bit)
        visited.add(bit)

    states = set()
    for bit in selectors:
        name, cell, pin = drivers[bit]
        if pin != 'Q' or not {'D', 'CLK'} <= cell['connections'].keys():
            raise ValueError('selector is not a mapped register')
        states.add(name)
        for pin, direction in cell['port_directions'].items():
            if direction == 'input':
                for source in cell['connections'][pin]:
                    visit(source)
    groups = sum(bool(used_differences.intersection(difference[i:i + 21]))
                 for i in range(0, len(difference), 21))
    if len(difference) != 504 or groups != 24:
        raise ValueError('not all 24 directed differences reach the selector logic')
    return dict(status='pass', selector_registers=len(states), directed_differences=groups,
                raw_metric_dependency=False, scope='mapped combinational selector cones')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mapped-json', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = args.mapped_json.read_bytes()
    result = check(json.loads(raw)['modules']['PreparedPCSTrellisDecoder'])
    result['mapped_json_sha256'] = hashlib.sha256(raw).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS selectors use all 24 registered differences and no raw branch metrics')
