"""Load fresh qualified TX components and build both connected circuit flows."""
import json
from pathlib import Path
import re

from extract_analog import digest
from resistor_body_extraction import adapt
from resistor_array_extraction import adapt as adapt_array
from transmit_bias_binding import bind_current_bias
from transmit_current_binding import bind_current_cell, expose_storage_anchors
from transmit_physical_assembly import INTERFACES, assemble
from transmit_translation_bank import signature, with_exposure_probe
from transmit_translator_layout import realization
from transmit_translation_bias_layout import circuit as replica_circuit
from transmit_translator_extracted import reference as translator_reference
from transmit_translation_bias import reference as replica_reference
from transmit_bias import reference as current_bias_reference


def load_models(directory, proof_root):
    directory, proof_root = Path(directory), Path(proof_root)
    hashes, proofs = {}, {}

    def merge(group):
        for path, value in group.items():
            if path in hashes and hashes[path] != value:
                raise ValueError('inconsistent transmitter component provenance: ' + path)
            if not Path(path).is_file() or digest(path) != value:
                raise ValueError('stale transmitter component: ' + path)
            hashes[path] = value

    def evidence(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ('source_sha256', 'artifact_sha256'):
                    merge(child)
                else:
                    evidence(child)
        elif isinstance(value, list):
            for child in value:
                evidence(child)

    for name in ('transmit_translator_psp_layout', 'transmit_translation_bias', 'transmit_bias',
                 'transmit_current_cell', 'transmit_pullup', 'transmit_storage',
                 'moscap_layout', 'mos_gate_storage_layout', 'moscap_storage_extraction',
                 'mos_gate_storage_extraction'):
        path = (proof_root / name / 'checks.json').resolve()
        proof = json.loads(path.read_text())
        if proof.get('status') != 'pass':
            raise ValueError('passing transmitter component required: ' + name)
        evidence(proof)
        merge({str(path): digest(path)})
        proofs[name] = proof

    # A model read below must be covered by the existing evidence, not merely
    # present next to an older passing receipt.
    def read(path):
        path = Path(path).resolve()
        keys = [p for p in hashes if Path(p).resolve() == path]
        if not keys or any(digest(path) != hashes[p] for p in keys):
            raise ValueError('unqualified transmitter model file: ' + str(path))
        return path.read_text()

    physical, schematic, bindings = {}, {}, {}
    def put(kind, actual, reference, original='ExtractedAnalog', reference_name='Reference', ports=None):
        ports = list(ports or INTERFACES[kind])
        physical[kind] = (actual, original, ports)
        schematic[kind] = (reference, reference_name, ports)

    tx = proofs['transmit_translator_psp_layout']
    tx_work = Path(tx['work'])
    if tx['geometry']['process_realization']['storage'] != 'psp-charge' or not tx['missing_device_rejected']:
        raise ValueError('qualified translator process realization required')
    expected = signature(*realization(tx_work / 'export/translator.va', 'psp-charge')[:2])
    for index in range(22):
        if signature(*realization(directory / f'translator_{index}.va', 'psp-charge')[:2]) != expected:
            raise ValueError('actual transmitter translator differs from physical fixture')
    put('translator', with_exposure_probe(read(tx_work / 'extraction/conserved.spice'), 'ExtractedAnalog'),
        with_exposure_probe(translator_reference(directory / 'translator_0.va', 'psp-charge'),
                            'ReferenceTranslator'), reference_name='ReferenceTranslator')

    replica = proofs['transmit_translation_bias']
    replica_work = Path(replica['work'])
    if not replica['missing_replica_rejected'] or replica_circuit(directory / 'translation_bias.va') != replica_circuit(
            replica_work / 'export/translation_bias.va'):
        raise ValueError('actual translation replica differs from qualified geometry')
    extraction = replica_work / 'extraction'
    circuit, audit = adapt(read(extraction / 'named/capacitance.spice'), read(extraction / 'conserved.spice'),
                           read(extraction / 'named/ExtractedAnalog.ext'), INTERFACES['translation_bias'])
    bindings['replica_bodies'] = audit
    put('translation_bias', circuit, replica_reference(replica['geometry']), reference_name='ReferenceBias')

    bias = proofs['transmit_bias']
    if not bias['missing_switch_rejected']:
        raise ValueError('bias fault rejection required')
    bindings['current_bias'] = bind_current_bias(directory / 'current_bias.va', bias['geometry'])
    extraction = Path(bias['work']) / 'extraction'
    circuit, audit = adapt(read(extraction / 'capacitance.spice'), read(extraction / 'conserved.spice'),
                           read(extraction / 'ExtractedAnalog.ext'), INTERFACES['current_bias'])
    bindings['bias_bodies'] = audit
    put('current_bias', circuit, current_bias_reference(bias['geometry']), reference_name='ReferenceBias')

    cells = proofs['transmit_current_cell']
    for bank in ('main', 'history'):
        record = cells['cases'][bank]
        if not record['cut_gate_rejected'] or record['local_drc_violations']:
            raise ValueError('qualified current-cell geometry and cut-gate rejection required')
        for index in range(4):
            bind_current_cell(directory / f'{bank}_{index}.va', record['geometry'], bank == 'history')
        actual = expose_storage_anchors(read(Path(cells['work']) / bank / 'extraction/conserved.spice'), 'CurrentCell')
        lines = ['.subckt Reference ' + ' '.join(INTERFACES[bank])]
        for index, device in enumerate(record['geometry']['devices']):
            width, length = device['geometry']['total_width_um'], device['geometry']['length_um']
            if width % 5:
                raise ValueError('unreviewed current-cell folding')
            nodes = ' '.join(device['nets'][p] for p in ('D', 'G', 'S', 'B'))
            for finger in range(int(width // 5)):
                lines.append(f'Xm{index}_{finger} {nodes} sg13_hv_nmos w=5u l={length:.17g}u')
        put(bank, actual, '\n'.join(lines + ['.ends Reference', '']), original='CurrentCell')

    storage = proofs['transmit_storage']
    for case in ('main', 'history', 'output'):
        record = storage['cases'][case]
        if not record['missing_storage_rejected'] or not record['missing_substrate_tap_rejected']:
            raise ValueError('storage/substrate fault rejection required')
        actual = read(Path(storage['work']) / case / 'extraction/conserved.spice')
        ports = list(record['geometry']['terminals'])
        lines = ['.subckt Reference ' + ' '.join(ports)]
        for index, part in enumerate(record['geometry']['components']):
            g, nodes = part['geometry'], part['connections']
            if part['kind'] != 'capacitor' or 'unit' in g:
                raise ValueError('unexpected local transmitter storage implementation')
            lines.append(f'Xc{index} {nodes["PLUS"]} {nodes["MINUS"]} cap_cmim '
                         f'w={g["width_um"]:.17g}u l={g["length_um"]:.17g}u')
        put(case + '_storage', actual, '\n'.join(lines + ['.ends Reference', '']), ports=ports)

    for kind, proof_name, case in (
            ('bias_storage', 'moscap_storage_extraction', 'forty_cells'),
            ('cascode_storage', 'mos_gate_storage_extraction', 'two_hundred_twenty_cells')):
        proof = proofs[proof_name]
        record = proof['cases'][case]
        if record['required_storage_F'] != (400e-12 if kind == 'bias_storage' else 50e-12):
            raise ValueError('bias-storage qualification target changed')
        extraction = Path(proof['work']) / case / 'extraction'
        actual = read(extraction / ('physical.spice' if kind == 'bias_storage' else 'conserved.spice'))
        lines = ['.subckt Reference G SUB']
        if kind == 'bias_storage':
            lines.append('Xcap G SUB sg13_moscap_n w=30u l=30u m=40')
        else:
            lines.append('Xcap SUB G SUB SUB sg13_hv_nmos w=100u l=.5u ng=10 m=220')
        put(kind, actual, '\n'.join(lines + ['.ends Reference', '']))

    pullup = proofs['transmit_pullup']
    if not pullup['missing_section_rejected']:
        raise ValueError('pullup fault rejection required')
    extraction = Path(pullup['work']) / 'extraction'
    # The original pullup receipt embeds its qualified model in bench.cir but
    # does not nest the extraction receipt. Verify both and require identical
    # adapted equations before admitting those intermediate files.
    extraction_receipt = extraction / 'extraction.json'
    extraction_proof = json.loads(extraction_receipt.read_text())
    if extraction_proof.get('status') != 'pass':
        raise ValueError('passing pullup extraction required')
    evidence(extraction_proof)
    merge({str(extraction_receipt.resolve()): digest(extraction_receipt)})
    circuit, audit = adapt_array(read(extraction / 'capacitance.spice'), read(extraction / 'conserved.spice'),
        read(extraction / 'ExtractedAnalog.ext'), read(extraction / 'ExtractedAnalog.res.ext'), INTERFACES['pullup'], 'SUB')
    if circuit not in read(Path(pullup['work']) / 'bench.cir'):
        raise ValueError('adapted pullup differs from the qualified DC experiment')
    bindings['pullup_bodies'] = audit
    put('pullup', circuit, pullup['geometry']['process_netlist'], reference_name='PhysicalPullup')
    models, reports = {}, {}
    for flow, parts in (('schematic', schematic), ('physical', physical)):
        models[flow], reports[flow] = assemble(directory, parts)
    return models, dict(source_sha256=hashes, bindings=bindings, assembly=reports)
