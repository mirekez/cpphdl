"""Preserve independently verified disconnected interface pins during LVS."""
import hashlib
import json
from pathlib import Path
import shutil

SOURCE = Path(__file__).resolve()


def disconnected_inputs(module, names):
    """Require unique, unused input bits in the actual mapped synthesis graph."""
    available = {}
    for name, port in module['ports'].items():
        for index, bit in enumerate(port['bits']):
            label = name if len(port['bits']) == 1 else f'{name}[{index}]'
            available[label] = (port['direction'], bit)
    if not names or len(set(names)) != len(names):
        raise ValueError('disconnected ports must be explicit and unique')
    for name in names:
        if name not in available:
            raise ValueError('missing mapped disconnected port: ' + name)
        direction, bit = available[name]
        if direction != 'input' or not isinstance(bit, int):
            raise ValueError('disconnected port must be a physical input: ' + name)
        if sum(other == bit for _, other in available.values()) != 1:
            raise ValueError('disconnected port aliases another public pin: ' + name)
        if any(bit in bits for cell in module['cells'].values() for bits in cell['connections'].values()):
            raise ValueError('disconnected port feeds a mapped device: ' + name)


def prepare(technology, destination, top, module, names):
    disconnected_inputs(module, names)
    destination.mkdir()
    upstream = technology / 'lvs'
    helper = SOURCE.with_name('disconnected_ports.rb')
    runner = (upstream / 'run_lvs.py').read_text()
    version_expression = 'Path(__file__).resolve().parents[5] / "versions.txt"'
    if runner.count(version_expression) != 1:
        raise ValueError('upstream LVS version lookup changed')
    versions = (upstream / 'run_lvs.py').resolve().parents[5] / 'versions.txt'
    runner = runner.replace(version_expression, 'Path(' + repr(str(versions)) + ')')
    (destination / 'run_lvs.py').write_text(runner)
    shutil.copyfile(helper, destination / helper.name)
    (destination / 'rule_decks').symlink_to(upstream / 'rule_decks', target_is_directory=True)
    text = (upstream / 'sg13g2.lvs').read_text()
    begin = 'apply_netlist_options = lambda do |target_netlist, label|\n'
    end = '\nend\n\nif NET_ONLY\n'
    if text.count(begin) != 1 or text.count(end) != 1:
        raise ValueError('upstream LVS preparation structure changed')
    prelude = ('require "json"\nrequire ' + json.dumps(str((destination / helper.name).resolve())) + '\n'
        'cpphdl_disconnected_audit = []\n')
    text = text.replace(begin, prelude + begin +
        f'  keeper = DisconnectedPorts.new(target_netlist, {json.dumps(top)}, {json.dumps(names)}, label)\n')
    text = text.replace(end, '\n  cpphdl_disconnected_audit << keeper.restore\n' +
        '  File.write(File.join(File.dirname($report), "disconnected_ports.json"), JSON.pretty_generate(cpphdl_disconnected_audit))\n' + end)
    deck = destination / 'sg13g2.lvs'
    deck.write_text(text)
    inputs = [SOURCE, helper, versions, upstream / 'run_lvs.py', upstream / 'sg13g2.lvs',
              destination / 'run_lvs.py', destination / helper.name, deck]
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    (destination / 'preparation.json').write_text(json.dumps(dict(top=top, ports=names,
        source_sha256=hashes, scope='preserve only extracted and mapped disconnected public nets; unchanged active transistor comparison'), indent=2) + '\n')
    return destination / 'run_lvs.py', hashes
