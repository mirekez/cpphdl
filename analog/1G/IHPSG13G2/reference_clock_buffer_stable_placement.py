"""Move the clock buffer locally while retaining the reviewed oscillator order."""
import math


def stable_buffered_order(devices, reference_order):
    roles = {d.get('role'): i for i, d in enumerate(devices)
             if d.get('role') in ('predriver_n', 'predriver_p', 'driver_n', 'driver_p')}
    if set(roles) != {'predriver_n', 'predriver_p', 'driver_n', 'driver_p'}:
        raise ValueError('missing clock buffer transistor roles')
    predrive = {devices[roles[role]]['a'] for role in ('predriver_n', 'predriver_p')}
    if len(predrive) != 1:
        raise ValueError('clock predriver outputs are disconnected')
    first = {'RealizedMOS' + str(roles[r]) for r in ('predriver_n', 'predriver_p')}
    second = {'RealizedMOS' + str(roles[r]) for r in ('driver_n', 'driver_p')}

    def order(parts, dbu):
        baseline, evidence = reference_order(parts, dbu)
        names = {part[0].name: i for i, part in enumerate(parts)}
        if len(names) != len(parts):
            raise ValueError('duplicate physical clock component names')
        original = [names[n] for n in evidence['groups']['oscillator']['component_order']]
        additions = [i for i in baseline if i not in original and set(parts[i][2].values()) & predrive]
        added_names = {parts[i][0].name for i in additions}
        if len(additions) != 5 or not second <= added_names or not first <= {parts[i][0].name for i in original}:
            raise ValueError('unreviewed clock buffer/storage regrouping')
        # Retain all oscillator and support ordering. Insert only the two
        # buffer transistors and their three storage components at the
        # existing predriver, avoiding a fresh oscillator-wide permutation.
        insertion = 1 + max(j for j, i in enumerate(original) if parts[i][0].name in first)
        oscillator = original[:insertion] + additions + original[insertion:]
        support = [i for i in baseline if i not in oscillator]
        ordered, groups, starts = [], {}, []
        for name, group in [('oscillator', oscillator), ('support', support)]:
            width = sum(math.ceil((parts[i][0].bbox().width() * dbu + 10) * 100) / 100 for i in group)
            if not group or width > 1800:
                raise ValueError('stable buffered clock row exceeds its physical width budget')
            starts.append(parts[group[0]][0].name)
            ordered += group
            groups[name] = dict(width_um=width, component_order=[parts[i][0].name for i in group],
                optimization=dict(strategy='retain reference order and move the buffer at its predriver'))
        if sorted(ordered) != list(range(len(parts))):
            raise ValueError('stable clock regrouping omitted or duplicated a component')
        return ordered, dict(strategy='local buffer relocation with unchanged oscillator ordering',
            row_width_um=1800, row_start_components=starts, groups=groups,
            relocated_components=sorted(added_names), predriver_nets=sorted(predrive),
            component_order=[parts[i][0].name for i in ordered])
    return order
