"""Audit elaborated instances and critical nets, independently of RTL simulation."""
from collections import Counter


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def check(tree):
    modules = {module['addr']: module for module in tree['modulesp'] if module['type'] == 'MODULE'}
    roots = [m for m in modules.values() if m['origName'] == 'RepeaterDigital']
    if len(roots) != 1:
        raise ValueError('missing unique repeater root')
    counts = Counter()

    def visit(module, active):
        if module['addr'] in active:
            raise ValueError('recursive hardware hierarchy')
        counts[module['origName']] += 1
        for cell in nodes(module['stmtsp']):
            if cell.get('type') == 'CELL':
                if cell['modp'] not in modules:
                    raise ValueError('unresolved hardware instance: ' + cell['name'])
                visit(modules[cell['modp']], active | {module['addr']})

    visit(roots[0], set())
    required = dict(RepeaterPort=2, QualifiedADCReceivePHY=2, ADCReceiveLaneController=8,
                    ADCSARController=104, ReceiveSampleRecovery=2, ReceiveDSPCrossing=2,
                    AutonomousReceiveFilter=8, PCSReceiver=2, Autonegotiation=2,
                    PMAControl=2, PCSTransmitter=2, PMATransmitLane=8,
                    FlowControlledRepeater=1, SynchronousFrameFIFO=2,
                    ReferenceClockMonitor=1, ProcessingClockControl=1, ControlCrossing=4)
    for name, number in required.items():
        if counts[name] != number:
            raise ValueError(f'{name}: expected {number} instances, found {counts[name]}')

    def module(name):
        found = [m for m in modules.values() if m['origName'] == name]
        if len(found) != 1:
            raise ValueError('ambiguous module: ' + name)
        return found[0]

    def cell(parent, name):
        found = [c for c in nodes(module(parent)['stmtsp']) if c.get('type') == 'CELL' and c['name'] == name]
        if len(found) != 1:
            raise ValueError('missing unique instance: ' + parent + '.' + name)
        return found[0]

    def direct(expression, name):
        return len(expression) == 1 and expression[0].get('type') == 'VARREF' and expression[0]['name'] == name

    clocks = [('RepeaterDigital', 'port_a', 'tx_clk', 'tx_a_clk'),
              ('RepeaterDigital', 'port_b', 'tx_clk', 'tx_b_clk'),
              ('RepeaterDigital', 'processing_clock', 'pcs_clk', 'raw_clk'),
              ('RepeaterDigital', 'forwarding', 'rx_clk', 'pcs_clk'),
              ('RepeaterDigital', 'forwarding', 'b_rx_clk', 'pcs_clk'),
              ('RepeaterDigital', 'forwarding', 'a_tx_clk', 'tx_a_clk'),
              ('RepeaterDigital', 'forwarding', 'tx_clk', 'tx_b_clk'),
              ('RepeaterPort', 'transmitter', 'ref_clk', 'tx_clk'),
              ('RepeaterPort', 'tx_control', 'source_clk', 'ref_clk'),
              ('RepeaterPort', 'tx_control', 'destination_clk', 'tx_clk'),
              ('RepeaterPort', 'rx_control', 'source_clk', 'ref_clk'),
              ('RepeaterPort', 'rx_control', 'destination_clk', 'pcs_clk')]
    for parent, instance, port, wire in clocks:
        pins = [p for p in cell(parent, instance)['pinsp'] if p['name'] == port]
        if len(pins) != 1 or not direct(pins[0]['exprp'], wire):
            raise ValueError('wrong clock binding: ' + '.'.join((parent, instance, port)))

    connections = [('RepeaterDigital', 'forwarding__rx_a_in', 'port_a__receive_out'),
                   ('RepeaterDigital', 'forwarding__rx_b_in', 'port_b__receive_out'),
                   ('RepeaterDigital', 'port_a__transmit_in', 'forwarding__tx_a_out'),
                   ('RepeaterDigital', 'port_b__transmit_in', 'forwarding__tx_b_out'),
                   ('RepeaterPort', 'receive_out', 'receiver__receive_out'),
                   ('RepeaterPort', 'symbols_out', 'transmitter__symbols_out'),
                   ('AutonomousADCReceivePHY', 'receiver__samples_in', 'capture__samples_out'),
                   ('AutonomousADCReceivePHY', 'receiver__sample_tag_in', 'capture__tag_out'),
                   ('AutonomousReceivePHY', 'recovery__samples_in', 'samples_in'),
                   ('AutonomousReceivePHY', 'recovery__tag_in', 'sample_tag_in'),
                   ('AutonomousReceivePHY', 'crossing__samples_in', 'recovery__samples_out'),
                   ('AutonomousReceivePHY', 'crossing__tag_in', 'recovery__tag_out'),
                   ('AutonomousReceivePHY', 'receiver__samples_in', 'crossing__samples_out'),
                   ('ReceiveSampleRecovery', 'interpolator__samples_in', 'samples_in'),
                   ('ReceiveSampleRecovery', 'interpolator__tag_in', 'tag_in'),
                   ('ReceiveSampleRecovery', 'interpolator__period_in', 'timing__period_out')]
    for parent, destination, source in connections:
        assignments = [n for n in nodes(module(parent)['stmtsp']) if n.get('type') == 'ASSIGNW'
                       and direct(n['lhsp'], destination)]
        if len(assignments) != 1 or not direct(assignments[0]['rhsp'], source):
            raise ValueError('wrong data binding: ' + parent + '.' + destination)
    words = [n for n in module('SynchronousFrameFIFO')['stmtsp'] if n['type'] == 'VAR'
             and n['name'] in ('words0', 'words1', 'words2', 'words3')]
    if len(words) != 4:
        raise ValueError('forwarding memory banks missing')
    return dict(instances=dict(counts), checked_clock_bindings=len(clocks),
                checked_data_bindings=len(connections), logical_frame_memory_banks=8,
                physical_sram_mapping_qualified=False, full_chip=False)
