"""Keep the weak PLL predriver and its larger buffer in the oscillator row."""
from collections import Counter
import math

from reference_clock_placement import placement_order


def buffered_clock_order(devices):
    ring = [(i, d) for i, d in enumerate(devices) if d.get('role', '').startswith('ring_')]
    control = {net for net, count in Counter(d['gate'] for _, d in ring
               if d['role'] == 'ring_n').items() if count == 4}
    mirrors = {d['a'] for _, d in ring if d['a'] == d['gate']}
    predrivers = [d for d in devices if d.get('role') in ('predriver_n', 'predriver_p')]
    predrive = {d['a'] for d in predrivers}
    if len(ring) != 14 or len(control) != 1 or len(mirrors) != 1 or len(predrivers) != 2 or len(predrive) != 1:
        raise ValueError('unreviewed buffered PLL topology')
    power = {'VDD', 'VSS', 'VLOGIC'}
    oscillator_nets = {d[k] for _, d in ring for k in ('a', 'b', 'gate')} - power
    critical_nets = oscillator_nets | predrive
    ring_names = {'RealizedMOS' + str(index) for index, _ in ring}

    def order(parts, dbu):
        oscillator = [i for i, (cell, _, pins, _) in enumerate(parts)
                      if cell.name in ring_names or set(pins.values()) & critical_nets]
        support = [i for i in range(len(parts)) if i not in oscillator]
        ordered, details, starts = [], {}, []
        for name, group in [('oscillator', oscillator), ('support', support)]:
            width = sum(math.ceil((parts[i][0].bbox().width() * dbu + 10) * 100) / 100 for i in group)
            if not group or width > 1800:
                raise ValueError('buffered clock group does not fit one row')
            outside = set().union(*(set(parts[i][2].values()) for i in range(len(parts))
                                   if i not in group)) - power
            weights = {net: 20 for net in oscillator_nets}
            weights.update({net: 40 for net in predrive})
            local, detail = placement_order([parts[i] for i in group], dbu,
                power_nets=power, net_weights=weights, row_width_um=1800, left_anchored_nets=outside)
            chosen = [group[i] for i in local]
            starts.append(parts[chosen[0]][0].name)
            ordered += chosen
            details[name] = dict(width_um=width, optimization=detail,
                                component_order=[parts[i][0].name for i in chosen])
        if sorted(ordered) != list(range(len(parts))):
            raise ValueError('buffered clock placement omitted a physical component')
        return ordered, dict(strategy='keep both clock buffers with oscillator feedback',
            row_width_um=1800, row_start_components=starts, groups=details,
            predriver_nets=sorted(predrive), component_order=[parts[i][0].name for i in ordered])
    return order


def finish_buffered_ground_rails(layout, top, info):
    """Join same-net landing shoulders after rearranging the clock components."""
    import klayout.db as db

    rails = [rail for rail in info['upper_clock_supply_rails'] if rail['net'] == 'VSS']
    if len(rails) != 2 or not info.get('upper_ground_straps'):
        raise ValueError('buffered landing repair requires both physical ground straps')
    for rail in rails:
        # TopVia2 landing pads are 4 um tall. Making the entire ground rail
        # equally tall removes narrow notches between neighboring shoulders.
        # No gap between different electrical nets is filled.
        left, right, y = rail['x_min_um'], rail['x_max_um'], rail['y_um']
        top.shapes(layout.layer(126, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (left - .2, y - 2, right + .2, y + 2))))
    info['continuous_ground_landing_width_um'] = 4
