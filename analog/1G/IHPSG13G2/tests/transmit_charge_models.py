"""Compose candidate PSP current-cell charge without changing literal TX models."""
import copy
import json
from pathlib import Path
import re

from extract_analog import digest
from input_charge import verify_files
from transmit_charge_realization import charge_plan
from transmit_physical_assembly import INTERFACES, namespace_model
from transmit_physical_models import load_models as load_literal_models


def load_charge_models(directory, proof_root, storage_proof):
    models, evidence = load_literal_models(directory, proof_root)
    hashes = evidence['source_sha256']
    proof_path = Path(storage_proof).resolve()
    proof = json.loads(proof_path.read_text())
    if proof.get('status') != 'pass' or set(proof['cases']) != {'main', 'history'}:
        raise ValueError('passing retained-storage geometry/extraction required')

    def merge(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ('source_sha256', 'artifact_sha256'):
                    verify_files(child)
                    for path, sha in child.items():
                        if path in hashes and hashes[path] != sha:
                            raise ValueError('conflicting retained-storage evidence')
                        hashes[path] = sha
                else:
                    merge(child)
        elif isinstance(value, list):
            for child in value:
                merge(child)
    merge(proof)
    hashes[str(proof_path)] = digest(proof_path)

    def read(path):
        path = Path(path).resolve()
        if not any(Path(p).resolve() == path and digest(path) == sha for p, sha in hashes.items()):
            raise ValueError('unqualified charge realization input: ' + str(path))
        return path.read_text()

    cells = json.loads(read(Path(proof_root) / 'transmit_current_cell/checks.json'))
    plan = charge_plan(directory, {bank: cells['cases'][bank]['geometry'] for bank in ('main', 'history')})
    baseline = dict(models)
    for bank in ('main', 'history'):
        record = proof['cases'][bank]
        geometry = record['geometry']
        if (not record['missing_capacitor_rejected'] or not record['missing_substrate_tap_rejected']
                or geometry['process_realization']['storage'] != 'psp-charge'
                or len(geometry['components']) != 2):
            raise ValueError('retained-storage proof lacks ownership or fault rejection')
        ports = list(geometry['terminals'])
        if set(ports) != {'TAIL', 'LOWER', 'VSS'} or len(ports) != 3:
            raise ValueError('changed retained-storage interface')
        actual = read(Path(proof['work']) / bank / 'extraction/conserved.spice')
        schematic = ['.subckt Reference ' + ' '.join(ports)]
        for index, part in enumerate(geometry['components']):
            g, nodes = part['geometry'], part['connections']
            if part['kind'] != 'capacitor' or 'unit' in g:
                raise ValueError('unexpected retained-storage geometry')
            schematic.append(f'Xc{index} {nodes["PLUS"]} {nodes["MINUS"]} cap_cmim '
                f'w={g["width_um"]:.17g}u l={g["length_um"]:.17g}u')
        schematic = '\n'.join(schematic + ['.ends Reference', ''])
        for flow, circuit, original in (('physical', actual, 'ExtractedAnalog'),
                                        ('schematic', schematic, 'Reference')):
            root = 'ConnectedTransmitter_' + bank + '_storage_' + original
            pattern = re.compile(r'^\.subckt ' + re.escape(root) +
                r' ([^\n]+)\n.*?^\.ends(?:[ \t]+' + re.escape(root) + r')?[ \t]*\n', re.M | re.S)
            matches = list(pattern.finditer(models[flow]))
            if len(matches) != 1:
                raise ValueError('ambiguous literal local-storage model')
            old_ports = matches[0][1].split()
            if len(old_ports) != 9 or set(old_ports) != set(INTERFACES[bank + '_storage']):
                raise ValueError('changed complete-TX storage interface')
            retained, name = namespace_model(circuit, original, 'Retained' + bank.title(), ports)
            # Preserve the complete module's external interface. Only its
            # retained TAIL/LOWER storage is connected here; the current-cell
            # PSP devices own the eight gate/source and gate/drain intentions.
            wrapper = (retained + '.subckt ' + root + ' ' + ' '.join(old_ports) + '\n'
                'Xretained ' + ' '.join(ports) + ' ' + name + '\n.ends ' + root + '\n')
            models[flow], count = pattern.subn(lambda _: wrapper, models[flow])
            if count != 1:
                raise ValueError('local-storage replacement count changed')
    for flow in models:
        assembly = evidence['assembly'][flow] = copy.deepcopy(evidence['assembly'][flow])
        records = {item['name']: item for item in assembly['instances']}
        for name, group in plan['groups'].items():
            storage = records[name + '_storage']
            kept = [item['element'] for item in group['capacitors']]
            owned = [item['element'] for item in group['modeled_charge']]
            if sorted(storage['elements']) != sorted(kept + owned):
                raise ValueError('literal/candidate ownership mismatch')
            storage['elements'] = kept
            records[name]['elements'] = sorted(records[name]['elements'] + owned)
            records[name]['modeled_charge'] = group['modeled_charge']
        claimed = [element for item in assembly['instances'] for element in item['elements']]
        if sorted(claimed) != list(range(assembly['original_contributions'])):
            raise ValueError('candidate full TX omits or duplicates an intention')
        assembly['charge_realization'] = dict(retained_capacitors=20, modeled_charge_contributions=64)
    evidence['charge_ownership'] = plan
    evidence['literal_models_sha256'] = {}
    import hashlib
    for flow, model in baseline.items():
        evidence['literal_models_sha256'][flow] = hashlib.sha256(model.encode()).hexdigest()
    return models, evidence
