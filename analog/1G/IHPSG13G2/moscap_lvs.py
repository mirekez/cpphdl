"""Build an audited local LVS adapter; never modify the installed PDK.

The pinned MOSCAP bulk port is a derived Metal1 region. Its original connection
rules link it to diffusion, but omit the link to the routing Metal1 region.
Add only those two geometrical connections. No intended net names, schematic
equivalence, device matching, ports, DRC rules or tolerances are changed.
"""
import hashlib
from pathlib import Path
import shutil


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare_moscap_lvs(pdk, destination):
    source = Path(pdk).resolve()
    relative = Path('ihp-sg13g2/libs.tech/klayout/tech/lvs')
    original, target = source / relative, Path(destination) / relative
    if target.exists():
        raise ValueError('LVS adapter destination must be new')
    target.mkdir(parents=True)
    shutil.copytree(original / 'rule_decks', target / 'rule_decks')
    for name in ('run_lvs.py', 'sg13g2.lvs'):
        shutil.copyfile(original / name, target / name)
    shutil.copyfile(source / 'versions.txt', Path(destination) / 'versions.txt')
    rule = target / 'rule_decks/cap_connections.lvs'
    before = rule.read_text()
    after = before
    added = []
    for kind in ('n', 'p'):
        anchor = f'connect({kind}moscap_bulk_port, {kind}moscap_diff_port)'
        addition = f'connect({kind}moscap_bulk_port, metal1_con)'
        if before.count(anchor) != 1 or addition in before:
            raise ValueError('review changed upstream MOSCAP connectivity before applying this adapter')
        after = after.replace(anchor, anchor + '\n' + addition)
        added.append(addition)
    restored = after
    for addition in added:
        restored = restored.replace('\n' + addition, '', 1)
    if restored != before:
        raise ValueError('LVS adapter changed more than its two declared connections')
    rule.write_text(after)
    manifest = []
    for path in target.rglob('*'):
        if path.is_file():
            upstream = original / path.relative_to(target)
            if path != rule and path.read_bytes() != upstream.read_bytes():
                raise ValueError('unexpected LVS adapter change')
            manifest.append(dict(relative_path=str(path.relative_to(target)),
                                 upstream_sha256=digest(upstream), adapter_sha256=digest(path)))
    return target / 'run_lvs.py', dict(added_geometrical_connections=added, files=manifest,
        installed_pdk_modified=False, same_as_unmodified_upstream=False,
        scope='LVS connectivity adapter only; all DRC and matching rules remain unchanged')
