"""Bind every elaborated analog law to a physical leaf, without module fences.

This is an as-is implementation inventory. Explicit candidate bindings for the
FLP circuit remain unqualified; placement does not imply process signoff.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import sys

ANALOG = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ANALOG / '1G/IHPSG13G2'))
from cascoded_driver_process import BINDINGS as TX
from core_receive_dac_process import TABLE as DAC
from reference_clock_process import BINDINGS as CLOCK
from receive_frontend_process import selections, match_parameters


def near(a, b):
    return math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-15)


def table_geometry(e, table):
    sign = 1 if e['kind'] == 'nmos' else -1
    found = [r for r in table if all(near(a, b) for a, b in
             zip((e['beta'], e['threshold'], sign), r[1:4]))]
    geometries = {(r[5], r[6], 'hv' if '_hv_' in r[4] else 'lv') for r in found}
    if len(geometries) != 1:
        raise ValueError('ambiguous or absent process binding: ' + repr(e))
    return next(iter(geometries))


def bind(data):
    components = {c['id']: c for c in data['components']}
    groups = defaultdict(list)
    ports = {p['name']: p['node'] for p in data['ports']}
    for i, e in enumerate(data['elements']):
        if e['kind'] in ('nmos', 'pmos'):
            groups[e['owner']].append((i, e))
    bindings, candidates = {}, []
    for owner, members in groups.items():
        files = {Path(e['file']).name for _, e in members}
        if len(files) != 1:
            raise ValueError('mixed MOS source sites in component')
        filename = next(iter(files))
        ancestors, current = [], owner
        while current is not None:
            if current in [x['id'] for x in ancestors]:
                raise ValueError('cyclic component hierarchy')
            ancestors.append(components[current])
            current = components[current]['parent']
        context = ' '.join(c['class'] for c in ancestors)
        pars = [(e['beta'], e['threshold'], 1 if e['kind'] == 'nmos' else -1) for _, e in members]
        widths, geometries = None, None
        if filename in ('cascoded_core_transmit_driver.hpp', 'biased_driver.hpp'):
            geometries = [table_geometry(e, TX) for _, e in members]
        elif filename == 'cascoded_level_shifter.hpp':
            geometries = [table_geometry(e, DAC + TX) for _, e in members]
        elif filename == 'inverter.hpp' and all(near(e['threshold'], .5) for _, e in members):
            geometries = [table_geometry(e, TX) for _, e in members]
        elif filename == 'inverter.hpp' and near(pars[0][0], .003) and 'differential_receive_gain' in context:
            if not match_parameters(pars, [(.003, .7, -1), (.003, .7, 1)]):
                raise ValueError('changed gain control inverter')
            widths = [30, 10]
        elif filename == 'inverter.hpp' and near(pars[0][1], .7) and any(near(pars[0][0], b) for b in (.003, .006, .04)):
            geometries = [table_geometry(e, TX if near(pars[0][0], .003) else DAC) for _, e in members]
        elif filename == 'cascode_ota.hpp':
            expected = [(.03,.7,1)]*2 + [(.06,.7,1)]*2 + [(.03,.7,-1)]*2 + [(.06,.7,-1)]*2 + [(.06,.7,1)]
            if not match_parameters(pars, expected):
                raise ValueError('changed cascode OTA')
            widths = [100, 100, 200, 200, 300, 300, 600, 600, 200]
        elif filename == 'differential_receive_gain.hpp':
            expected = [(.03,.7,1)]*6 + [p for _ in range(4) for p in ((.03,.7,1),(.02,.7,-1))] + [(.03,.7,1)]
            if not match_parameters(pars, expected):
                raise ValueError('changed differential gain')
            widths = [100]*6 + [w for _ in range(4) for w in (100,200)] + [100]
        elif filename in ('lead_receive_common_reference.hpp', 'receive_reference.hpp', 'shunt_voltage_buffer.hpp'):
            if not match_parameters(pars, [(.03,.7,1)]):
                raise ValueError('changed receive bias')
            widths = [150 if filename == 'shunt_voltage_buffer.hpp' else 100]
        elif filename == 'nmos_source_follower.hpp':
            choices = {0.1: 400, 0.2: 800, 0.4: 1600}
            if len(pars) != 1 or pars[0][1:] != (.7, 1) or pars[0][0] not in choices:
                raise ValueError('changed follower')
            widths = [choices[pars[0][0]]]
        elif filename == 'nmos_resistor_amplifier.hpp' and match_parameters(pars, [(.0075,.7,1)]*2):
            widths = [25,25]
        elif filename == 'mos_track_hold.hpp':
            if not match_parameters(pars, [(.03,.7,1),(.015,.7,-1),(.016,.7,1),(.0084,.7,-1)]):
                raise ValueError('changed held sample switches')
            widths = [37.5,75,20,42]
        elif filename == 'core_receive_slice.hpp':
            if not match_parameters(pars, [(.016,.35,p) for p in (-1,1,-1,-1,1,1)]):
                raise ValueError('changed receive evaluation gate')
            geometries = [(16 if e['kind'] == 'nmos' else 32, .13, 'lv') for _, e in members]
        elif 'core_flp_frontend' in context and filename in ('flp_frontend.hpp','nmos_differential_pair.hpp','pmos_current_mirror.hpp'):
            # No existing qualified PSP binding covers these generic FLP ICs.
            # Use explicit HV candidates and retain that limitation in output.
            choices = {(.01,.7,1):30, (.01,.7,-1):60, (.1,.7,1):300, (.1,.7,-1):600,
                       (.001,.35,1):10, (.001,.35,-1):20}
            widths = [choices[p] for p in pars]
            candidates.append(owner)
        elif all(near(e['threshold'], .35) for _, e in members):
            geometries = [table_geometry(e, CLOCK) for _, e in members]
        else:
            widths = selections(filename, pars)
        if widths is not None:
            geometries = [(w, .13 if filename == 'level_down.hpp' and near(e['threshold'], .35) else .45,
                           'lv' if filename == 'level_down.hpp' and near(e['threshold'], .35) else 'hv')
                          for w, (_, e) in zip(widths, members)]
        if len(geometries) != len(members):
            raise ValueError('lost MOS binding')
        for (i,e), (w,l,domain) in zip(members, geometries):
            fingers = next(n for n in range(math.ceil(w/10), math.ceil(w)+1)
                           if (1 if n == 1 and domain == 'lv' else 2) <= w/n <= 10
                           and near(w/n*100, round(w/n*100)))
            bindings[i] = dict(width_um=w, length_um=l, fingers=fingers, domain=domain,
                               polarity=e['kind'][0], candidate=owner in candidates)
    devices = []
    for i,e in enumerate(data['elements']):
        d = dict(name=f'analog_d{i}', source_id=i, owner=e['owner'], kind=e['kind'])
        if e['kind'] in ('nmos','pmos'):
            d.update(bindings[i])
            a,b,g = e['nodes']
            d['connections'] = dict(D=a, S=b, G=g, B=ports['VSS'] if d['polarity']=='n' else ports['VLOGIC' if d['domain']=='lv' else 'VDD'])
        elif e['kind'] in ('resistor','capacitor'):
            a,b = e['nodes']
            d['value'] = e['r'] if e['kind']=='resistor' else e['c']
            d['connections'] = dict(PLUS=a, MINUS=b)
            if e['kind']=='resistor':
                d['connections']['SUB'] = ports['VSS']
        else:
            raise ValueError('unbound law: '+e['kind'])
        devices.append(d)
    return dict(status='bound_unqualified', full_chip_qualified=False, devices=devices,
                counts=dict(Counter(d['kind'] for d in devices)), candidate_flp_owners=candidates,
                ports=data['ports'], components=data['components'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--electrical', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    result=bind(json.loads(args.electrical.read_text()))
    result['electrical_sha256']=hashlib.sha256(args.electrical.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('devices','ports','components')}))

if __name__=='__main__':
    main()
