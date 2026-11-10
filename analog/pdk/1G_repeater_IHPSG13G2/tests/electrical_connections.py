#!/usr/bin/env python3
"""Reject disconnected circuit terminals and wrong digital/analog pin bindings."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from electrical_connections import check, join


def run(data, rtl):
    report = check(data)
    join(data, rtl)
    rejected = []

    def negative(name, mutate):
        broken = copy.deepcopy(data)
        mutate(broken)
        try:
            check(broken)
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError('accepted ' + name)

    negative('open MDI transmitter', lambda d: d['anchors'].__setitem__('tx7.P', d['anchors']['N_7']))
    negative('wrong receive lane', lambda d: d['anchors'].__setitem__('rx4.input.P', d['anchors']['SENSE_P_0']))
    negative('bypassed receive protection', lambda d: d['anchors'].__setitem__('rx0.input.P', d['anchors']['P_0']))
    negative('transmitter behind sense resistor', lambda d: d['anchors'].__setitem__('tx0.P', d['anchors']['SENSE_P_0']))
    negative('unprotected FLP receive', lambda d: d['anchors'].__setitem__('flp1.RX_N', d['anchors']['N_5']))
    negative('wrong slice comparator', lambda d: next(p for p in d['ports'] if p['name'] == 'DECISION_3_12').__setitem__('digital', 'comparisons_in[3][11]'))
    negative('wrong trial bit', lambda d: next(p for p in d['ports'] if p['name'] == 'TRIAL_7_12_7').__setitem__('digital', 'trial_out[124][6]'))
    negative('wrong conversion slice', lambda d: next(p for p in d['ports'] if p['name'] == 'CONVERT_3_12').__setitem__('digital', 'convert_out[59]'))
    negative('bypassed conversion gate', lambda d: d['anchors'].__setitem__('rx3.s12.decision.EVALUATE', d['anchors']['rx3.evaluation.OUT']))
    negative('missing comparator', lambda d: d['ports'].remove(next(p for p in d['ports'] if p['name'] == 'DECISION_0_0')))
    negative('shorted samples', lambda d: d['anchors'].__setitem__('rx1.s0.sampler.OUT', d['anchors']['rx0.s0.sampler.OUT']))
    negative('wrong core supply', lambda d: d['anchors'].__setitem__('rx5.s12.Vlogic', d['anchors']['VDD']))
    negative('disconnected startup clear', lambda d: d['anchors'].__setitem__('rx0.s3.decision.CLEAR', d['anchors']['VSS']))
    negative('ungated evaluation', lambda d: d['anchors'].__setitem__('rx7.s5.EVALUATE', d['anchors']['ADC_CLOCK']))
    negative('wrong processing clock', lambda d: d['anchors'].__setitem__('processing.OUT', d['anchors']['PROCESSING_RAW']))
    negative('missing hold storage', lambda d: d.__setitem__('elements', [e for e in d['elements'] if not (
        e['kind'] == 'capacitor' and e['nodes'] == [d['anchors']['rx0.s0.sampler.OUT'], d['anchors']['VSS']])]))
    for name, broken in [('missing clear RTL port', rtl.replace('clear_out[8]', 'unbound_clear[8]', 1)),
                         ('missing ADC slots', rtl.replace('comparisons_in[8]', 'comparisons_in[7]', 1))]:
        try:
            join(data, broken)
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError('accepted ' + name)
    report['rejected_faults'] = rejected
    return report


if __name__ == '__main__':
    print(json.dumps(run(json.loads(Path(sys.argv[1]).read_text()), Path(sys.argv[2]).read_text()), indent=2))
