"""Check that mapped trellis reachability has no metric/comparison dependency."""
import argparse
import hashlib
import json
from pathlib import Path


def check(module):
    name = 'core.reachable' if 'core.reachable' in module['netnames'] else 'reachable'
    reachable = set(module['netnames'][name]['bits'])
    allowed = set(reachable)
    controls = {'clk', 'reset', 'start_in', 'end_in', 'advance_in', 'symbol_enable_in'}
    for name, port in module['ports'].items():
        if name in controls and port['direction'] == 'input':
            allowed.update(port['bits'])
    drivers = {}
    for name, cell in module['cells'].items():
        for pin, direction in cell['port_directions'].items():
            if direction == 'output':
                for bit in cell['connections'][pin]:
                    if isinstance(bit, int):
                        if bit in drivers:
                            raise ValueError('multiple mapped drivers')
                        drivers[bit] = (name, cell, pin)
    visited = set()
    active = set()

    def visit(bit):
        if not isinstance(bit, int) or bit in allowed or bit in visited:
            return
        if bit in active:
            raise ValueError('combinational cycle in reachability cone')
        if bit not in drivers:
            raise ValueError('reachability depends on a data input or undriven bit')
        name, cell, pin = drivers[bit]
        if 'CLK' in cell['connections'] and pin == 'Q':
            raise ValueError('reachability depends on another state register: ' + name)
        active.add(bit)
        for input_pin, direction in cell['port_directions'].items():
            if direction == 'input':
                for upstream in cell['connections'][input_pin]:
                    visit(upstream)
        active.remove(bit)
        visited.add(bit)

    states = []
    for bit in sorted(b for b in reachable if isinstance(b, int)):
        name, cell, pin = drivers[bit]
        if pin != 'Q' or not {'D', 'CLK'} <= cell['connections'].keys():
            raise ValueError('expected a mapped reachability register')
        # Reset, enable and clock logic can change state just as D can.
        # Check every input, including asynchronous library control pins.
        for input_pin, direction in cell['port_directions'].items():
            if direction == 'input':
                for upstream in cell['connections'][input_pin]:
                    visit(upstream)
        states.append(name)
    if not states:
        raise ValueError('missing reachability state')
    return dict(status='pass', reachable_registers=states, combinational_bits=len(visited),
                scope='mapped reachability depends only on its previous state and control inputs')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', default='PCSTrellisDecoder',
                        choices=('PCSTrellisDecoder', 'PreparedPCSTrellisDecoder'))
    parser.add_argument('--mapped-json', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = args.mapped_json.read_bytes()
    result = check(json.loads(raw)['modules'][args.module])
    result['mapped_json_sha256'] = hashlib.sha256(raw).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS mapped trellis reachability: no cost/metric-dependent data path')
