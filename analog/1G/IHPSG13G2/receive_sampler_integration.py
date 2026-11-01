"""Replace sampler laws at their actual ADC boundary, retaining every parent load."""
from collections import Counter
import math
import re

PORTS = ('VDD', 'VSS', 'IN', 'CLOCK', 'OUT')


def separate_samplers(deck, inventory, metadata, plan):
    """Consume six MOS and five charge branches per observed receive slice."""
    count = metadata['slices']
    if count not in (1, 4, 13) or not math.isclose(metadata['sample_storage_F'], 1.5e-12):
        raise ValueError('unsupported physical sampler bank')
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
    if set(observations) != set(map(str, range(10 + 3 * count))) or deck.count('.control') != 1:
        raise ValueError('changed conversion observation boundary')
    lines = deck.splitlines()
    cards = {}
    for line in lines:
        fields = line.split()
        if fields and fields[0].startswith(('Xphysical', 'Cdevice')):
            if fields[0] in cards:
                raise ValueError('duplicate physical contribution')
            cards[fields[0]] = fields
    by_id = {entry['device']: entry for entry in inventory}
    if len(by_id) != len(inventory):
        raise ValueError('duplicate process ownership')
    if any('Xphysical' + identifier not in cards for identifier in by_id):
        raise ValueError('missing mapped physical transistor')
    if (Counter(d['index'] for d in plan['devices']) != Counter((0, 1, 3, 4, 5, 6)) or
            Counter(c['index'] for c in plan['capacitors']) != Counter((2, 7, 8, 9, 10))):
        raise ValueError('changed sampler physical plan')
    removed, bindings, audits = set(), [], []
    all_switches = {i for i, entry in by_id.items() if entry['source'] == 'mos_track_hold.hpp'}
    consumed_switches = set()
    private = set()
    for slice_index in range(count):
        mapping = dict(VDD=observations['4'], VSS='0', IN=observations['3'],
                       CLOCK=observations[str(11 + 2 * slice_index)],
                       OUT=observations[str(10 + 2 * slice_index)])
        if len(set(mapping.values())) != len(mapping):
            raise ValueError('aliased sampler ports')
        # Find the PMOS main switch by its oriented channel and owned IC.
        mains = [i for i in all_switches if cards['Xphysical' + i][1] == mapping['OUT']
                 and cards['Xphysical' + i][3] == mapping['IN']]
        if len(mains) != 1:
            raise ValueError('missing or ambiguous sampler main switch')
        owner = by_id[mains[0]]['instance']
        switch_ids = {i for i in all_switches if by_id[i]['instance'] == owner}
        if len(switch_ids) != 4 or switch_ids & consumed_switches:
            raise ValueError('sampler owner is missing, shared or duplicated')
        mapping['CLOCK_N'] = cards['Xphysical' + mains[0]][2]
        if mapping['CLOCK_N'] in set(mapping[p] for p in PORTS) | private:
            raise ValueError('aliased private complement node')
        private.add(mapping['CLOCK_N'])
        selected = []
        complement_owners = set()
        for device in plan['devices']:
            expected = [mapping[device[k]] for k in ('a', 'gate', 'b', 'body')]
            source = 'inverter.hpp' if device['index'] < 2 else 'mos_track_hold.hpp'
            candidates = [i for i, entry in by_id.items() if entry['source'] == source
                          and cards['Xphysical' + i][1:5] == expected]
            if len(candidates) != 1:
                raise ValueError('sampler transistor orientation or ownership changed')
            identifier, = candidates
            entry, card = by_id[identifier], cards['Xphysical' + identifier]
            model = 'sg13_hv_' + device['polarity'] + 'mos'
            expected_parameters = dict(w=f"{device['width_um']:g}u", l=f"{device['length_um']:g}u",
                                       ng=str(device['fingers']))
            parameters = dict(field.split('=', 1) for field in card[6:])
            if (card[5] != model or entry['model'] != model or
                    entry['body'] != expected[3] or
                    entry['width_um'] != device['width_um'] or
                    entry['length_um'] != device['length_um'] or
                    set(parameters) != set(expected_parameters) or
                    len(parameters) != len(card[6:])):
                raise ValueError('sampler process geometry differs from layout')
            for key, value in expected_parameters.items():
                if not math.isclose(float(parameters[key].removesuffix('u')),
                                    float(value.removesuffix('u')), rel_tol=1e-12, abs_tol=0):
                    raise ValueError('sampler dimensions or fingers changed')
                if key in ('w', 'l') and not parameters[key].endswith('u'):
                    raise ValueError('sampler physical units changed')
            if source == 'inverter.hpp':
                complement_owners.add(entry['instance'])
            elif identifier not in switch_ids:
                raise ValueError('sampler device belongs to a different IC')
            selected.append('Xphysical' + identifier)
        if len(complement_owners) != 1 or owner in complement_owners:
            raise ValueError('complement inverter ownership changed')
        for capacitor in plan['capacitors']:
            candidates = [name for name, card in cards.items() if name.startswith('Cdevice')
                and card[1:3] == [mapping[capacitor['a']], mapping[capacitor['b']]]
                and len(card) == 5 and math.isclose(float(card[3]), capacitor['capacitance_F'],
                                                  rel_tol=1e-12, abs_tol=0)]
            if len(candidates) != 1 or float(cards[candidates[0]][4].removeprefix('IC=')) != 0:
                raise ValueError('missing, duplicated or precharged sampler storage')
            selected.append(candidates[0])
        if len(set(selected)) != 11 or removed & set(selected):
            raise ValueError('sampler charge is owned more than once')
        removed.update(selected)
        consumed_switches.update(switch_ids)
        bindings.append({p: mapping[p] for p in PORTS})
        audits.append(dict(slice=slice_index, owner=owner, complement_owner=next(iter(complement_owners)),
                           removed=selected, boundary=bindings[-1]))
    if consumed_switches != all_switches:
        raise ValueError('unobserved sampler remains in the parent')
    kept = [line for line in lines if not line.split() or line.split()[0] not in removed]
    if len(lines) - len(kept) != 11 * count:
        raise ValueError('sampler removal count differs')
    # The host explicitly initializes its private complement node to zero.
    # The replacement starts cold under UIC, with zero capacitor charge; do
    # not leave an orphan initial condition at the removed flat node.
    initial_conditions = []
    for node in private:
        matches = [line for line in kept if re.fullmatch(r'\.ic V\(' + node + r'\)=([-+0-9.eE]+)', line)]
        if len(matches) != 1 or float(matches[0].split('=')[1]) != 0:
            raise ValueError('changed private sampler initial state')
        initial_conditions.extend(matches)
    kept = [line for line in kept if line not in initial_conditions]
    # Any remaining reference, including an observation or behavioural
    # expression, to a private node means this is not a five-port IC boundary.
    if private & set(re.findall(r'\bn\d+\b', '\n'.join(kept))):
        raise ValueError('private sampler complement is loaded outside its IC')
    host = '\n'.join(kept) + '\n'
    return host, bindings, dict(slices=audits, removed_laws=len(removed),
        removed_zero_initial_conditions=sorted(initial_conditions), retained_parent_lines=len(kept))


def compose(host, bindings, model):
    """Insert one checked model and one instance per actual held-input port."""
    headers = re.findall(r'^\.subckt (\w+) ([^\n]+)$', model, re.M | re.I)
    if (len(headers) != 1 or headers[0][1].split() != list(PORTS) or
            len(re.findall(r'^\.ends\b', model, re.M | re.I)) != 1 or
            re.search(r'^\.(control|end|include|lib)\b', model, re.M | re.I) or
            host.count('.control') != 1 or re.search(r'\bReceiveSamplerModel\b', host, re.I)):
        raise ValueError('changed sampler model interface')
    name = headers[0][0]
    model = re.sub(r'\b' + re.escape(name) + r'\b', 'ReceiveSamplerModel', model)
    instances = []
    for index, binding in enumerate(bindings):
        if set(binding) != set(PORTS) or len(set(binding.values())) != len(PORTS):
            raise ValueError('invalid sampler instance ports')
        instances.append(f'Xreceive_sampler{index} ' + ' '.join(binding[p] for p in PORTS) +
                         ' ReceiveSamplerModel')
    return host.replace('.control', model + '\n' + '\n'.join(instances) + '\n.control', 1)
