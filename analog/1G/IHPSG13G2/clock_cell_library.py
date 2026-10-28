"""Read reviewed cell interfaces without altering foundry transistor parameters."""
from pathlib import Path
import re


def definitions(source, cells):
    """Keep reviewed cell interfaces and unaltered foundry device parameters."""
    result = []
    for name, (ports, count) in cells.items():
        blocks = re.findall(r'^\.subckt ' + name + r' ' + ports + r'\n.*?^\.ends\s*$',
                            source, re.M | re.S)
        if len(blocks) != 1:
            raise ValueError('changed PDK clock-cell interface: ' + name)
        block = blocks[0]
        devices = [line.split() for line in block.splitlines()[1:-1] if line.strip()]
        if len(devices) != count or len({row[0] for row in devices}) != count:
            raise ValueError('changed PDK clock-cell device inventory: ' + name)
        for row in devices:
            if (not row[0].startswith('X') or len(row) != 14 or
                    row[5] not in ('sg13_lv_nmos', 'sg13_lv_pmos') or
                    row[4] != ('VSS' if row[5] == 'sg13_lv_nmos' else 'VDD') or
                    {item.split('=')[0] for item in row[6:]} != {'w', 'l', 'ng', 'ad', 'as', 'pd', 'ps', 'm'}):
                raise ValueError('unreviewed PDK clock-cell circuit: ' + name)
        result.append(block)
    return '\n'.join(result) + '\n'


def library_path(pdk):
    return Path(pdk).resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/spice/sg13g2_stdcell.spice'
