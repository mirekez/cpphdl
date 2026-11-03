"""Load the exact filled SRAM layout/reference pair from a completed LVS flow."""
import hashlib
import json
from pathlib import Path


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verified_library(receipt):
    receipt = Path(receipt).resolve()
    flow = json.loads(receipt.read_text())
    if flow.get('status') != 'filled_graph_correspondence_pass' or flow.get('stage') != 'complete':
        raise ValueError('completed filled SRAM graph verification required')
    root = receipt.parent
    fixture = root / 'fixture'
    proof = root / 'correspondence/checks.json'
    if Path(flow.get('correspondence_receipt', '')).resolve() != proof:
        raise ValueError('SRAM correspondence must belong to the supplied flow')
    comparison = json.loads(proof.read_text())
    audit = comparison.get('independent_audit', {})
    if (comparison.get('status') != 'graph_correspondence_pass' or audit.get('status') != 'pass'
            or flow.get('independent_audit') != audit):
        raise ValueError('complete independent SRAM audit required')
    preparation = json.loads((fixture / 'preparation.json').read_text())
    if preparation.get('status') != 'prepared' or not preparation.get('fill'):
        raise ValueError('filled SRAM preparation required')
    hashes = {}
    for group in (flow.get('source_sha256'), comparison.get('source_sha256'),
                  preparation.get('source_sha256'), preparation.get('artifact_sha256')):
        if not isinstance(group, dict) or not group:
            raise ValueError('missing SRAM provenance')
        for name, value in group.items():
            if name in hashes and hashes[name] != value:
                raise ValueError('conflicting SRAM provenance: ' + name)
            hashes[name] = value
    gds, reference = fixture / 'macro.gds', fixture / 'reference.cir'
    for path in (gds, reference):
        if str(path) not in flow['source_sha256'] or str(path) not in preparation['artifact_sha256']:
            raise ValueError('SRAM layout/reference is not bound to completed extraction')
    database = root / 'correspondence/macro.lvsdb'
    if Path(comparison.get('database', '')).resolve() != database:
        raise ValueError('SRAM database must belong to the supplied flow')
    hashes[str(database)] = comparison['database_sha256']
    for name, expected in hashes.items():
        if digest(name) != expected:
            raise ValueError('SRAM input or artifact changed: ' + name)
    for path in (receipt, proof, fixture / 'preparation.json', Path(__file__).resolve()):
        hashes[str(path)] = digest(path)
    return dict(gds=gds, reference=reference, source_sha256=hashes,
                full_sram_qualified=False, full_chip_qualified=False)
