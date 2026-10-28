"""Candidate whole-current-cell PSP charge ownership, separate from literal MIM.

This plan does not change the default transmitter. Electrical qualification
must establish its complete loaded response before adopting the realization.
"""
from pathlib import Path

from transmit_storage_layout import storage_partition, create_storage
from transmit_current_binding import bind_current_cell


def charge_plan(directory, current_geometry):
    """Account for all 84 storage intentions against the bound MOS topology.

    The explicit tail/lower storage, shared filters and line compensation stay
    physical. Eight gate/source and gate/drain charge approximations per cell
    are assigned to that cell's four PSP devices, not to an extra MIM bank.
    PSP charge is bias dependent; this is not a numerical capacitance match.
    """
    directory = Path(directory)
    original = storage_partition(directory)
    groups, modeled, retained = {}, [], []
    retained.extend(original['groups']['shared']['capacitors'])
    for bank in ('main', 'history'):
        for index in range(4):
            name = f'{bank}_{index}'
            geometry = current_geometry[bank]
            devices = bind_current_cell(directory / (name + '.va'), geometry, bank == 'history')
            group = original['groups'][name]
            # Index only selects the independently validated declaration role;
            # terminals are checked again against the actual physical MOS.
            ownership = ((0, 'G', 'S'), (1, 'G', 'S'), (0, 'G', 'D'), (1, 'G', 'D'),
                         (3, 'G', 'S'), (3, 'G', 'D'), (2, 'G', 'S'), (2, 'G', 'D'))
            assignments = []
            for capacitor, (device_index, first, second) in zip(group['capacitors'][2:], ownership):
                physical = geometry['devices'][device_index]
                terminals = [physical['nets'][first], physical['nets'][second]]
                if capacitor['terminals'] != terminals:
                    raise ValueError('generic charge does not belong to the selected MOS terminals')
                assignments.append(dict(element=capacitor['element'], cell=name,
                    device_index=device_index, role=devices[device_index]['role'],
                    model=devices[device_index]['model'], model_terminals=[first, second],
                    terminals=terminals, generic_capacitance_F=capacitor['capacitance_F'],
                    realization='intrinsic PSP terminal charge'))
            kept = group['capacitors'][:2]
            groups[name] = dict(ports={p: group['ports'][p] for p in ('TAIL', 'LOWER', 'VSS')},
                                capacitors=kept, modeled_charge=assignments)
            modeled.extend(assignments)
            retained.extend(kept)
    expected = sorted(c['element'] for group in original['groups'].values() for c in group['capacitors'])
    claimed = [c['element'] for c in retained + modeled]
    if len(retained) != 20 or len(modeled) != 64 or sorted(claimed) != expected or len(set(claimed)) != 84:
        raise ValueError('incomplete or duplicate transmitter charge ownership')
    return dict(status='candidate_unqualified', groups=groups,
                retained_storage=retained, modeled_charge=modeled,
                retained_capacitors=20, modeled_charge_contributions=64,
                scope='whole-current-cell process realization; no electrical equivalence or GDS qualification claimed')


def create_charge_storage(pdk, directory, current_geometry, history=False):
    plan = charge_plan(directory, current_geometry)
    group = plan['groups']['history_0' if history else 'main_0']
    name = 'TransmitHistoryChargeStorage' if history else 'TransmitMainChargeStorage'
    layout, top, info = create_storage(pdk, name, ['TAIL', 'LOWER', 'VSS'], group)
    info['process_realization'] = dict(status=plan['status'], storage='psp-charge',
        retained_capacitors=2, modeled_charge_contributions=8,
        scope=plan['scope'])
    return layout, top, info
