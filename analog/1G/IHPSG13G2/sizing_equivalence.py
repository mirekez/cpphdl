"""Prove a sizing-only ECO against retained, Liberty-derived cell models.

Only drive variants with exactly identical model bodies are interchangeable.
Every instance, port and surviving pin connection must remain unchanged.
This is functional connectivity evidence, not timing qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from buffer_equivalence import parse, connectivity, BUFFERS


def models(path):
    text = Path(path).read_text()
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    result = {}
    pattern = r'\s*module\s+(\w+)\s*(\(.*?\);.*?)\bendmodule\s*'
    end = 0
    for match in re.finditer(pattern, text, re.S):
        if text[end:match.start()].strip() or match[1] in result:
            raise ValueError('unexpected text or duplicate cell model')
        # Models produced by read_liberty have no macros, includes or hierarchy.
        if '`' in match[2]:
            raise ValueError('preprocessed cell model is unsupported')
        result[match[1]] = re.sub(r'\s+', '', match[2])
        end = match.end()
    if text[end:].strip() or not result:
        raise ValueError('expected retained Liberty-derived Verilog cell models')
    return result


def compare(before_path, after_path, model_path):
    before, after = parse(before_path), parse(after_path)
    if before['module'] != after['module'] or before['interface'] != after['interface']:
        raise ValueError('sizing changed the module interface')
    if set(before['cells']) != set(after['cells']):
        raise ValueError('sizing inserted or removed instances')
    library = models(model_path)
    changed = []
    for name, (old, pins) in before['cells'].items():
        new, new_pins = after['cells'][name]
        if set(pins) != set(new_pins):
            raise ValueError('sizing changed the cell interface: ' + name)
        if old != new:
            variants = [re.fullmatch(r'(sg13g2_\w+)_(1|2|4|8|16)', master)
                        for master in (old, new)]
            same_family = all(variants) and variants[0][1] == variants[1][1]
            transparent_delay_change = old in BUFFERS and new in BUFFERS
            if (not (same_family or transparent_delay_change)
                    or old not in library or new not in library
                    or library[old] != library[new]):
                raise ValueError('cell replacement lacks identical functional models: ' + name)
            changed.append(dict(instance=name, before=old, after=new))
            # Normalize only proven replacements before comparing endpoints.
            after['cells'][name] = (old, new_pins)
    if connectivity(before, set()) != connectivity(after, set()):
        raise ValueError('sizing changed pin connectivity')
    return dict(status='pass', top=before['module'], resized_cells=len(changed),
                replacements=changed, cell_count=len(before['cells']),
                input_sha256={str(Path(p).resolve()): hashlib.sha256(Path(p).read_bytes()).hexdigest()
                              for p in (before_path, after_path, model_path)},
                scope='unchanged endpoints with identical Liberty-derived drive-variant logic models',
                timing_qualified=False, physical_qualified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('before', 'after', 'models', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    args.output.write_text(json.dumps(compare(args.before, args.after, args.models), indent=2) + '\n')
