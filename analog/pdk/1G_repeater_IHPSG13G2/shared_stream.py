#!/usr/bin/env python3
"""Stream the connected shared DEF, real leaf geometry and pad-ring extras."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

import klayout.db as db
from shared_run import digest, ROOT

MACRO='RM_IHPSG13_2P_1024x16_c2_bm_bist'


def run(args):
    work=args.work.resolve()
    pdk=args.pdk.resolve()/'ihp-sg13g2'
    techdir=pdk/'libs.tech/klayout/tech'
    std=pdk/'libs.ref/sg13g2_stdcell'
    sram=pdk/'libs.ref/sg13g2_sram'
    io=pdk/'libs.ref/sg13g2_io'
    source=work/(args.stage+'.def')
    if not source.is_file():
        raise ValueError('missing physical checkpoint '+str(source))
    if args.stage=='floorplan':
        raise ValueError('digital cells must be placed before full-die publication')
    tech=db.Technology()
    tech.load(str(techdir/'sg13g2.lyt'))
    options=tech.load_layout_options
    config=options.lefdef_config
    config.lef_files=[str(work/'route_tech.lef'),str(std/'lef/sg13g2_stdcell.lef'),
                      str(sram/f'lef/{MACRO}.lef'),str(io/'lef/sg13g2_io.lef'),str(work/'leaves/analog.lef')]
    config.map_file=str(techdir/'sg13g2.map')
    config.produce_cell_outlines=False
    config.produce_placement_blockages=False
    config.produce_regions=False
    config.produce_obstructions=False
    config.produce_blockages=False
    options.lefdef_config=config
    layout=db.Layout()
    layout.read(str(source),options)
    top=layout.cell('RepeaterDigital')
    if top is None:
        raise ValueError('missing connected design')
    count=Counter(i.cell.name for i in top.each_inst())
    analog_count=sum(v for n,v in count.items() if n.startswith('analog_leaf_'))
    planned=json.loads((work/'floorplan.json').read_text())
    if analog_count!=planned['physical_analog_leaves'] or count[MACRO]!=8:
        raise ValueError('shared stream lost analog devices or SRAM')
    # DEF contributes actual route geometry. Replace only LEF leaf placeholders.
    for cell in layout.each_cell():
        if cell.cell_index()!=top.cell_index() and not cell.name.startswith('VIA_'):
            cell.clear()
    libraries=[std/'gds/sg13g2_stdcell.gds',sram/f'gds/{MACRO}.gds',io/'gds/sg13g2_io.gds',work/'leaves/analog.gds.gz']
    for path in libraries:
        layout.read(str(path))
    if any(i.cell.is_empty() for i in top.each_inst()):
        raise ValueError('physical master missing from GDS libraries')
    # The padframe builder contains real ground joins, seal ring and decaps.
    # Its I/O placements are already present in DEF; do not duplicate them.
    extras=db.Layout()
    extras.read(str(work/'pads/repeater_padframe.gds'))
    frame=extras.cell('RepeaterPadframe')
    placed=json.loads((work/'pads/padframe.json').read_text())
    io_names={p['cell'] for p in placed['placements']}
    for inst in list(frame.each_inst()):
        if inst.cell.name in io_names:
            inst.delete()
    extras_cell=layout.create_cell('SharedPadRingExtras')
    extras_cell.copy_tree(frame)
    top.insert(db.CellInstArray(extras_cell.cell_index(),db.Trans()))
    result=db.Layout()
    result.dbu=layout.dbu
    final=result.create_cell('RepeaterSharedDie')
    final.copy_tree(top)
    args.output.mkdir(parents=True,exist_ok=True)
    output=args.output/'repeater_shared_die.gds.gz'
    opts=db.SaveLayoutOptions()
    opts.write_context_info=False
    result.write(str(output),opts)
    physical_count=sum(count.values())
    report=dict(status=args.stage+'_unqualified',full_chip_qualified=False,timing_gate=False,
        top=final.name,physical_instances=physical_count,analog_leaves=analog_count,sram_macros=8,
        fixed_logic_modules=0,die_side_um=planned['die_side_um'],
        scope='One shared die with all instantiated analog leaves, digital cells, SRAM and pads. Power-grid, timing, DRC/LVS and external-control closure remain unqualified.',
        source_sha256={str(p):digest(p) for p in [source,*libraries,work/'pads/repeater_padframe.gds',Path(__file__)]},
        artifact_sha256={output.name:digest(output)})
    if args.image_width:
        sys.path.insert(0,str(ROOT/'out/pdf-renderer'))
        from package_receive_results import render,LAYERS
        side=planned['die_side_um']
        image,_=render(output,args.output,'drawing',list(LAYERS),[0,0,side,side],args.image_width,args.image_width)
        image.save(args.output/'repeater_full_resolution.png')
        report['artifact_sha256']['repeater_full_resolution.png']=digest(args.output/'repeater_full_resolution.png')
    (args.output/'checks.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('status','physical_instances','analog_leaves','die_side_um')}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--stage',choices=('global','placed','connected','global_route','routed'),default='placed')
    parser.add_argument('--pdk',type=Path,default=ROOT/'pdk/IHPSG13G2')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--image-width',type=int,default=8000)
    run(parser.parse_args())
