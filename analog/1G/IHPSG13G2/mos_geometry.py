"""Bind schematic total widths to fingers within the pinned PSP model range."""
import math
import re


def size_mos_instances(deck):
    """Preserve terminals and total W/L; reject unsupported or invalid geometry.

    The pinned sg13g2_mos{lv,hv}_parm.lib headers give W <= 10 um.
    The public subcircuits interpret w as total width and ng as finger count.
    Explicit ng remains a design choice and is validated, never overwritten.
    """
    inventory, output = [], []

    def microns(parameters, key):
        matches = re.findall(r'\b' + key + r'=([-+0-9.eE]+)([unpm]?)\b', parameters)
        if len(matches) != 1:
            raise ValueError('MOS needs one explicit numeric ' + key)
        number, suffix = matches[0]
        value = float(number) * {'': 1e6, 'u': 1, 'n': .001, 'p': 1e-6, 'm': 1000}[suffix]
        if not math.isfinite(value) or value <= 0:
            raise ValueError('invalid MOS dimension')
        return value

    for line in deck.splitlines():
        if line.lstrip().startswith(('X', 'x')) and re.search(r'\bsg13_(lv|hv)_[np]mos\b', line):
            match = re.fullmatch(r'\s*(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+'
                                 r'(sg13_(lv|hv)_([np]mos))\s+(.+)', line)
            if not match:
                raise ValueError('unsupported IHP MOS instance syntax: ' + line)
            name, drain, gate, source, body, model, domain, polarity, parameters = match.groups()
            width, length = microns(parameters, 'w'), microns(parameters, 'l')
            explicit = re.findall(r'\bng=(\S+)', parameters)
            if len(explicit) > 1 or (explicit and not re.fullmatch(r'[1-9][0-9]*', explicit[0])):
                raise ValueError('MOS finger count must be a positive integer')
            fingers = int(explicit[0]) if explicit else max(1, math.ceil(width / 10 - 1e-12))
            finger_width = width / fingers
            minimum_width = .15 if domain == 'lv' else .30
            minimum_length = .13 if domain == 'lv' else .45 if polarity == 'nmos' else .40
            if not (minimum_width - 1e-12 <= finger_width <= 10 + 1e-12
                    and minimum_length - 1e-12 <= length <= 10 + 1e-12):
                raise ValueError('IHP MOS dimension outside pinned PSP range: ' + name)
            if not explicit:
                line += ' ng=' + str(fingers)
            inventory.append(dict(instance=name, model=model, drain=drain, gate=gate, source=source,
                                  body=body, total_width_um=width, length_um=length,
                                  fingers=fingers, finger_width_um=finger_width))
        output.append(line)
    if not inventory:
        raise ValueError('no explicit IHP MOS instances to qualify')
    return '\n'.join(output) + '\n', inventory
