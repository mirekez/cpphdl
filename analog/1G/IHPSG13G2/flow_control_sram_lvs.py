#!/usr/bin/env python3
"""Prepare and audit the complete routed SRAM/control assembly without black boxes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

import klayout.db as db

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from checks import check_drc
from embedded_sram_lvs import EmbeddedSramCorrespondence
from sram_library import digest, verified_library
from sram_lvs_audit import sheet_resistance

MACRO = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'
TOP = 'FlowControlledRepeater'


def verify(hashes):
    for name, expected in hashes.items():
        if digest(name) != expected:
            raise ValueError('assembly input changed: ' + name)


def combine_hashes(*groups):
    result = {}
    for group in groups:
        if not isinstance(group, dict) or not group:
            raise ValueError('missing assembly provenance')
        for name, value in group.items():
            if name in result and result[name] != value:
                raise ValueError('conflicting assembly provenance: ' + name)
            result[name] = value
    return result


def physical_signature(layout):
    """Include every nontext shape and every instance transform/array repetition."""
    result = hashlib.sha256()
    for cell in layout.each_cell():
        result.update(str(cell.cell_index()).encode() + b'\n')
        for layer in layout.layer_indexes():
            result.update(str(layout.get_info(layer)).encode() + b'\n')
            for shape in sorted(str(s) for s in cell.shapes(layer).each() if not s.is_text()):
                result.update(shape.encode() + b'\n')
        for instance in sorted(str((i.cell_index, str(i.cplx_trans), str(i.a), str(i.b), i.na, i.nb))
                               for i in cell.each_inst()):
            result.update(instance.encode() + b'\n')
    return result.hexdigest()


def scoped_alignment(text):
    old = '    flatten_names = nl.each_circuit.map(&:name).reject { |name| keep_circuits.include?(name.upcase) }'
    new = '''    sram_subtree_names = []
    visit_sram = lambda do |c|
      unless sram_subtree_names.include?(c.name.upcase)
        sram_subtree_names << c.name.upcase
        c.each_subcircuit.each { |child| visit_sram.call(child.circuit_ref) }
      end
    end
    visit_sram.call(root)
    flatten_names = nl.each_circuit.map(&:name).select { |name| sram_subtree_names.include?(name.upcase) && !keep_circuits.include?(name.upcase) }'''
    if text.count(old) != 1:
        raise ValueError('SRAM hierarchy alignment insertion point changed')
    return text.replace(old, new)


def copy_extraction_deck(fixture, work):
    """Keep the PDK runner's version contract with its isolated rule deck."""
    versions = fixture / 'pdk/versions.txt'
    if not versions.is_file():
        raise ValueError('prepared SRAM fixture lacks required pdk/versions.txt')
    relative = Path('pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs')
    deck = work / relative
    shutil.copytree(fixture / relative, deck)
    shutil.copyfile(versions, work / 'pdk/versions.txt')
    (deck / 'sg13g2.lvs').write_text(scoped_alignment((deck / 'sg13g2.lvs').read_text()))
    return deck


def prepare(args):
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    library = verified_library(args.sram_lvs)
    fixture = library['gds'].parent
    prepared = json.loads((fixture / 'preparation.json').read_text())
    routed = json.loads(args.routed.read_text())
    geometry = json.loads(args.geometry.read_text())
    if routed.get('status') != 'routed_timing_replay_pass':
        raise ValueError('passing routed timing and replay required')
    if geometry.get('drc_exit_code') != 0 or geometry.get('drc', {}).get('violations') != 0:
        raise ValueError('retained geometry requires a completed full DRC pass')
    hashes = dict(library['source_sha256'])
    for group in (routed['source_sha256'], routed['artifact_sha256'], geometry['artifact_sha256']):
        verify(group)
        hashes.update(group)
    old = Path(geometry['work'])
    route_work = Path(routed['work'])
    for name in ('core.def', 'core.cdl'):
        if digest(old / name) != digest(route_work / name):
            raise ValueError('retained geometry belongs to another routed implementation')
    gds = old / 'flow_control.gds'
    if geometry['artifact_sha256'].get(str(gds)) != digest(gds):
        raise ValueError('unrecorded retained assembly GDS')
    pdk = args.pdk.resolve()
    for name, expected in geometry['source_sha256'].items():
        if Path(name).is_relative_to(pdk):
            if digest(name) != expected:
                raise ValueError('retained DRC PDK input changed: ' + name)
            hashes[name] = expected
    drc_report = Path(geometry['drc']['report'])
    drc = check_drc(drc_report, pdk / 'ihp-sg13g2/libs.tech/klayout/tech/drc')
    for path in (args.routed.resolve(), args.geometry.resolve(), drc_report, Path(__file__).resolve(),
                 ROOT / 'pdk/1G_repeater_IHPSG13G2/checks.py', HERE / 'embedded_sram_lvs.py'):
        hashes[str(path)] = digest(path)
    layout = db.Layout()
    layout.read(str(gds))
    top, sram = layout.cell(TOP), layout.cell(MACRO)
    if top is None or sram is None or sum(i.cell.name == MACRO for i in top.each_inst()) != 8:
        raise ValueError('retained assembly lost its eight SRAM banks')
    before = physical_signature(layout)
    boundary = str(top.bbox())
    subtree = [sram, *(layout.cell(i) for i in sram.called_cells())]
    for old_name, new_name in prepared['name_correspondences'].items():
        cell = layout.cell(old_name)
        if cell is None or layout.cell(new_name) is not None:
            raise ValueError('unexpected embedded SRAM hierarchy')
        cell.name = new_name
    alignment = (HERE / 'sram_lvs_alignment.rb').read_text()
    match = re.search(r'^  keep_circuits = (\[.*\])$', alignment, re.M)
    if match is None:
        raise ValueError('missing retained SRAM boundaries')
    keep = set(json.loads(match[1]))
    removed = 0
    for cell in subtree:
        if cell.name.upper() in keep:
            continue
        for layer in layout.layer_indexes():
            for shape in [s for s in cell.shapes(layer).each() if s.is_text()]:
                shape.delete()
                removed += 1
    if physical_signature(layout) != before or str(top.bbox()) != boundary:
        raise ValueError('SRAM hierarchy preparation changed assembly geometry')
    layout.write(str(work / 'assembly.gds'))
    copy_extraction_deck(fixture, work)
    standard = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
    shutil.copyfile(old / 'core.cdl', work / 'core.cdl')
    (work / 'assembly.cir').write_text('\n'.join('.include "' + str(p) + '"'
        for p in (standard, library['reference'], work / 'core.cdl')) + '\n')
    hashes[str(standard)] = digest(standard)
    verify(hashes)
    artifacts = {str(p): digest(p) for p in work.rglob('*') if p.is_file()}
    report = dict(status='prepared', source_sha256=hashes, artifact_sha256=artifacts,
                  physical_geometry_sha256=before, preserved_nontext_geometry=True,
                  retained_drc=drc, internal_texts_removed=removed,
                  sram_reference=str(library['reference']), full_chip_qualified=False)
    (work / 'preparation.json').write_text(json.dumps(report, indent=2) + '\n')


def ports(path, name):
    headers = re.findall(r'(?im)^\.subckt\s+' + re.escape(name) + r'\s+([^\n]+)',
                         Path(path).read_text().replace('\n+', ' '))
    if len(headers) != 1:
        raise ValueError('missing or ambiguous independent interface')
    result = headers[0].split()
    if len(set(result)) != len(result):
        raise ValueError('duplicate interface port')
    return set(result)


def qualify(args):
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', full_chip_qualified=False)
    try:
        fixture = args.fixture.resolve()
        prepared = json.loads((fixture / 'preparation.json').read_text())
        extraction = json.loads(args.extraction_receipt.read_text())
        if prepared.get('status') != 'prepared' or not prepared.get('preserved_nontext_geometry'):
            raise ValueError('geometry-preserving assembly preparation required')
        if any(extraction['source_sha256'].get(name) != expected
               for name, expected in prepared['artifact_sha256'].items()):
            raise ValueError('extraction is not bound to this prepared assembly')
        hashes = combine_hashes(prepared['source_sha256'], prepared['artifact_sha256'],
                                extraction['source_sha256'], extraction['artifact_sha256'])
        verify(hashes)
        source = args.source.resolve()
        if (not source.is_relative_to(args.extraction_receipt.resolve().parent)
                or extraction['artifact_sha256'].get(str(source)) != digest(source)):
            raise ValueError('native assembly extraction lacks producer provenance')
        for path in (Path(__file__).resolve(), HERE / 'embedded_sram_lvs.py', HERE / 'sram_lvs_compare.py',
                     HERE / 'sram_lvs_audit.py', fixture / 'preparation.json', args.extraction_receipt.resolve()):
            hashes[str(path)] = digest(path)
        database = db.LayoutVsSchematic()
        database.read(str(source))
        comparer = EmbeddedSramCorrespondence(database, MACRO, TOP)
        report['boundary_expansion'] = comparer.expand_column_logic()
        sheet = sheet_resistance(fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs/rule_decks/res_extraction.lvs')
        report.update(comparer.run_assembly(ports(prepared['sram_reference'], MACRO),
                                           ports(fixture / 'core.cdl', TOP), sheet))
        database.write(str(work / 'assembly.lvsdb'))
        verify(hashes)
        report.update(source_sha256=hashes, artifact_sha256={str(work / 'assembly.lvsdb'): digest(work / 'assembly.lvsdb')})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    preparation = commands.add_parser('prepare')
    for name in ('geometry', 'routed', 'sram-lvs', 'pdk', 'output'):
        preparation.add_argument('--' + name, type=Path, required=True)
    qualification = commands.add_parser('qualify')
    for name in ('fixture', 'source', 'extraction-receipt', 'output'):
        qualification.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    (prepare if args.command == 'prepare' else qualify)(args)
