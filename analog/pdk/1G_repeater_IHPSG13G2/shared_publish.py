"""Publish one current shared-die GDS and its actual layer images."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import shutil
import sys
from shared_run import ROOT,digest


def run(args):
    sys.path.insert(0,str(ROOT/'out/pdf-renderer'))
    from package_receive_results import render,LAYERS,BACKGROUND,label
    from PIL import Image,ImageDraw
    work=args.work.resolve()
    receipt=json.loads((work/'images/checks.json').read_text())
    legal=json.loads((work/'legalization.json').read_text())
    connections=json.loads((work/'connectivity.json').read_text())
    if receipt['status'] not in ('connected_unqualified','routed_unqualified') or legal['status']!='geometrically_legal' or connections['status']!='physical_connections_checked':
        raise ValueError('shared physical placement and connectivity checks required')
    for name,value in receipt['artifact_sha256'].items():
        if digest(work/'images'/name)!=value:
            raise ValueError('changed stream artifact '+name)
    destination=args.output.resolve()
    destination.mkdir(parents=True,exist_ok=True)
    names=('repeater_development.gds.gz','repeater_full_resolution.png','repeater_layers.png',
           'development_provenance.json','repeater_full_resolution.json','README.md')
    backup=work/'previous-publication'
    if not backup.exists():
        backup.mkdir()
        for name in names:
            if (destination/name).exists():
                shutil.copy2(destination/name,backup/name)
    gds=destination/'repeater_development.gds.gz'
    shutil.copy2(work/'images/repeater_shared_die.gds.gz',gds)
    shutil.copy2(work/'images/repeater_full_resolution.png',destination/'repeater_full_resolution.png')
    side=receipt['die_side_um']
    canvas=Image.new('RGB',(3200,3360),BACKGROUND)
    draw=ImageDraw.Draw(canvas)
    groups=[('Complete geometry',list(LAYERS)),('Active / poly / contacts',['Active','Poly','Contact']),
            ('Metal 1–3',['Metal1','Via1','Metal2','Via2','Metal3']),
            ('Metal 4–5 / top metal',['Metal4','Via4','Metal5','TopVia1','TopMetal1','TopVia2','TopMetal2'])]
    for i,(title,layers) in enumerate(groups):
        image,_=render(gds,work/'images',f'layer-{i}',layers,[0,0,side,side],1600,1600)
        x,y=(i%2)*1600,(i//2)*1680
        label(draw,(x+24,y+20),title,28)
        canvas.paste(image,(x,y+80))
    canvas.save(destination/'repeater_layers.png')
    result=dict(receipt,generated_utc=datetime.now(timezone.utc).isoformat(),
                status='shared_placement_unrouted' if receipt['status']=='connected_unqualified' else 'routed_unqualified',
                legalization=legal,connectivity=connections,
                artifact_sha256={name:digest(destination/name) for name in names[:3]},
                qualifications=dict(timing=False,full_chip_drc=False,full_chip_lvs=False,power_grid=False))
    (destination/'development_provenance.json').write_text(json.dumps(result,indent=2)+'\n')
    (destination/'repeater_full_resolution.json').write_text(json.dumps(dict(
        generated_utc=result['generated_utc'],gds_sha256=digest(gds),top='RepeaterSharedDie',
        png_sha256=digest(destination/'repeater_full_resolution.png'),status=result['status'],
        drawing_layers=LAYERS,renderer_sha256=digest(Path(__file__).with_name('shared_stream.py'))),indent=2)+'\n')
    (destination/'README.md').write_text('''# Current 1G shared-die placement

The current GDS contains **879,909 digital cells, 118,019 analog physical leaves,
eight SRAMs and the IHP pad ring on one 11.44 × 11.44 mm die**. Digital PHY,
DSP, PCS, forwarding and control cells share placement rows. Analog leaves
implement all 67,572 elaborated circuit laws, including all eight lanes and
104 receiver slices. The old collection of standalone candidate blocks is no
longer the source of this merged GDS.

- [Current GDSII](repeater_development.gds.gz): open `RepeaterSharedDie`.
- [Full-resolution image](repeater_full_resolution.png).
- [Layer views](repeater_layers.png).
- [Actual inventory and checks](development_provenance.json).

![Shared die](repeater_full_resolution.png)

**Legal placement and physical terminal connectivity are checked. Full-chip
routing, core power distribution, DRC/LVS and electrical qualification remain
unfinished. Timing closure does not gate this as-is PnR pass.** Unresolved
analog controls and digital configuration/clock inputs remain explicit;
this is not a fabrication-ready ASIC.

Large resistors and transistors are expanded into actual series/parallel
physical leaves. Ten explicit 1 GOhm resistors consume 50,000 segments and
much of the die area. Reducing that area requires changing and validating
the circuit/model, not omitting devices from the drawing.

The other per-module images in this folder are historical module views;
they do not define the current full-die inventory.

Regeneration and resumable PnR commands are documented in
[shared-space.md](../../../pdk/1G_repeater_IHPSG13G2/shared-space.md).
Embedded IHP geometry is distributed under [Apache-2.0](IHP-LICENSE.txt).
''')
    print(json.dumps(dict(status=result['status'],gds=str(gds),image=str(destination/'repeater_full_resolution.png'))))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'1G/IHPSG13G2/results')
    run(parser.parse_args())
