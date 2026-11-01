"""A contacted resistor model with an electrical midpoint for body coupling.

This is a simulation representation of one straight physical resistor, not
two new layout devices. Half-length R3 segments share the original contact
resistance and intrinsic capacitance. External extraction must separately
avoid counting that intrinsic body-to-substrate capacitance twice.
"""
import math
import re


def split_body(name, terminals, midpoint, model, width_m, length_m):
    if (model not in ('rppd', 'rhigh') or len(terminals) != 3
            or any(not math.isfinite(v) or v <= 0 for v in (width_m, length_m))
            or width_m < .5e-6 or length_m / 2 < (.5e-6 if model == 'rppd' else .96e-6)
            or any(not re.fullmatch(r'[A-Za-z0-9_.#$]+', value) for value in (name, midpoint, *terminals))
            or midpoint in terminals):
        raise ValueError('unsupported straight-resistor body representation')
    a, b, substrate = terminals
    # Constants are from the pinned IHP resistors_mod.lib, postsim=0, b=0.
    # Each R3 segment has two endpoint capacitances and contact terms.
    # Quarter-area and half-perimeter preserve their total over four endpoints.
    area = (length_m + .86e-6) * width_m / 4
    perimeter = (length_m + .86e-6 + width_m) / 2
    contact = (35e-6 if model == 'rppd' else 80e-6) / width_m / 2
    parameters = (f'{model} w={width_m:.17g} l={length_m / 2:.17g} '
                  f'a={area:.17g} p={perimeter:.17g} rz={contact:.17g}')
    lines = [f'X{name}_a {a} {midpoint} {substrate} {parameters}',
             f'X{name}_b {midpoint} {b} {substrate} {parameters}']
    return '\n'.join(lines) + '\n', dict(model=model, width_m=width_m, physical_length_m=length_m,
        segment_length_m=length_m / 2, endpoint_area_m2=area, endpoint_perimeter_m=perimeter,
        segment_contact_parameter_Ohm=contact, midpoint=midpoint,
        scope='straight resistor, postsim=0, no mismatch/self-heating; external capacitance partition not established')
