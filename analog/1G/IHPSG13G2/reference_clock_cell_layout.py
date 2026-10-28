"""Compact phase-detector layout from its intended PDK-cell circuit."""
from clock_cell_layout import create_cells
from reference_clock_cells import CELLS, PULSE_STAGES, detector


def create_detector(pdk):
    # Local capture loops flank the common reset network. Mirroring the
    # feedback delay chain keeps its clock input beside its pulse gates.
    order = ([f'Xref_delay{i}' for i in range(PULSE_STAGES)] +
        ['Xref_pulse_hold', 'Xref_pulse', 'Xref_pulse_parallel',
         'Xref_q0', 'Xref_q1', 'Xref_qn0', 'Xref_qn1', 'Xup',
         'Xreset0', 'Xreset1', 'Xpulses', 'Xreset_true', 'Xdown',
         'Xfb_qn0', 'Xfb_qn1', 'Xfb_q1', 'Xfb_q0',
         'Xfb_pulse_parallel', 'Xfb_pulse', 'Xfb_pulse_hold'] +
        [f'Xfb_delay{i}' for i in reversed(range(PULSE_STAGES))])
    options = dict(compact_ports=True, order=order, escape_width_um=.24,
        mirrored=tuple(f'Xfb_delay{i}' for i in range(PULSE_STAGES)))
    _, _, initial = create_cells(pdk, detector(), CELLS, **options)
    spans = initial['tracks']
    priority = ['ref_SET_B', 'fb_SET_B', 'UP_RAW', 'DOWN_RAW', 'UP_N', 'DOWN_N',
                'RESET_B', 'RESET', 'PULSE_ANY', 'ref_HOLD_B', 'fb_HOLD_B', 'REF', 'FEEDBACK']
    nets = [net for net in priority if net in spans] + sorted(set(spans) - set(priority) - {'VSS'})
    tracks = []
    for net in nets:
        left, _, right, _ = spans[net]
        for group in tracks:
            # Disjoint horizontal routes share a height, with a 0.24 um gap.
            # Full local DRC and strict LVS independently check the result.
            if all(right + 240 <= spans[other][0] or spans[other][2] + 240 <= left
                   for other in group):
                group.append(net)
                break
        else:
            tracks.append([net])
    return create_cells(pdk, detector(), CELLS, track_groups=tracks, **options)
