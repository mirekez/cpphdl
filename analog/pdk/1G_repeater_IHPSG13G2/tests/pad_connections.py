#!/usr/bin/env python3
"""Check actual pad CDL/core binding and reject broken physical boundaries."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pad_connections import check, core_bindings, join


def run(padframe, cdl, electrical):
    wrapper, abstract, report = join(padframe, cdl, electrical)
    rejected = []

    def negative(name, mutate):
        fixture = dict(pads=copy.deepcopy(padframe), cdl=cdl, electrical=electrical,
                       bindings=core_bindings())
        mutate(fixture)
        try:
            check(fixture['pads'], fixture['cdl'], fixture['electrical'], fixture['bindings'])
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError('accepted ' + name)

    def binding(name, port, net):
        negative(name, lambda d: d['bindings'].__setitem__(port, net))

    binding('TX on protected node', 'P_0', 'mdi_a_0_p_core')
    binding('RX protection bypassed', 'SENSE_P_0', 'mdi_a_0_p')
    binding('RX lane swapped', 'SENSE_N_6', 'mdi_b_1_n_core')
    binding('RX polarity swapped', 'SENSE_N_6', 'mdi_b_2_p_core')
    binding('logic on high-voltage supply', 'VLOGIC', 'iovdd')
    binding('reference clock bypasses input pad', 'ref_clk', 'ref_clock')
    binding('status bypasses output pad', 'active_out', 'active')
    negative('missing shared ground', lambda d: d['pads'].__setitem__('common_ground', False))
    negative('disconnected pad CDL', lambda d: d.__setitem__('cdl', d['cdl'].replace(
        'Xmdi_a_0_p mdi_a_0_p mdi_a_0_p_core ', 'Xmdi_a_0_p mdi_a_0_p floating ', 1)))
    negative('shorted pad terminals', lambda d: next(p for p in d['pads']['placements']
        if p['name'] == 'mdi_a_0_p')['nets'].__setitem__('padres', 'mdi_a_0_p'))
    negative('wrong pad master', lambda d: next(p for p in d['pads']['placements']
        if p['name'] == 'mdi_b_3_n').__setitem__('cell', 'sg13g2_IOPadIn'))
    negative('missing core sense terminal', lambda d: d.__setitem__('electrical',
        d['electrical'].replace('SENSE_P_0', 'missing_sense')))
    negative('wrong core receive direction', lambda d: d.__setitem__('electrical',
        d['electrical'].replace('input wire SENSE_P_0', 'output wire SENSE_P_0')))
    # No unresolved clock or control is silently tied off to make a closed top.
    assert {'tx_a_clk', 'tx_b_clk', 'local_history_in', 'power_good_in'} <= set(report['unresolved_core_inputs'])
    assert '.VLOGIC(vdd)' in wrapper and '.VDD(iovdd)' in wrapper
    assert '.SENSE_P_0(mdi_a_0_p_core)' in wrapper and '.P_0(mdi_a_0_p)' in wrapper
    assert 'module RepeaterPadframe(' in abstract
    report['rejected_faults'] = rejected
    return report


if __name__ == '__main__':
    print(json.dumps(run(json.loads(Path(sys.argv[1]).read_text()),
                         Path(sys.argv[2]).read_text(), Path(sys.argv[3]).read_text()), indent=2))
