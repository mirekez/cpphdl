#!/usr/bin/env python3
"""Run connected, timing-ungated placement/routing with resumable checkpoints."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from tool_process import configure_cleanup, execute

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):
            h.update(chunk)
    return h.hexdigest()


def run(args):
    work=args.work.resolve()
    report=dict(status='running',timing_gate=False,full_chip_qualified=False,stages=[],
                scope='All instantiated analog leaves, digital cells, SRAM and pads in one connected die; no timing qualification.')
    receipt=work/('run-'+args.stage+'.json')
    def save():
        temporary=receipt.with_suffix('.tmp')
        temporary.write_text(json.dumps(report,indent=2)+'\n')
        temporary.replace(receipt)
    config=json.loads((work/'floorplan.json').read_text())
    env=dict(os.environ,SHARED_WORK=str(work),SHARED_PDK=str(args.pdk.resolve()),
             SHARED_DIGITAL_DB=str(args.digital_db.resolve()),SHARED_CORE_SIDE=str(config['core_side_um']),
             SHARED_ANALOG_COUNT=str(config['physical_analog_leaves']), SHARED_ROUTE_ENGINE=args.engine)
    paths=[HERE/n for n in ('shared_analog.py','shared_leaf_layout.py','shared_floorplan.py','shared_pnr.tcl','shared_run.py','shared_cut_rows.py','shared_legalize.tcl','shared_sparse_legalize.py','shared_connectivity.py','shared_boundary_pins.tcl','shared_fanout.py','shared_route.tcl','shared_route_grid.tcl','shared_memory_run.py')]
    paths += [work/'devices.tcl',work/'devices.list',work/'analog.json',work/'leaves/analog.lef',work/'leaves/analog.gds.gz',args.digital_db]
    checkpoint={'place':'floorplan.odb','legalize':'global.odb','route':'connected.odb','global-route':'buffered.odb','detail-route':'global_route.odb'}.get(args.stage)
    if checkpoint:
        paths.append(work/checkpoint)
    paths.append(work/'leaves/leaves.json')
    report['source_sha256']={str(p.resolve()):digest(p) for p in paths}
    save()
    def command(name,argv,timeout=7200):
        report['stage']=name
        save()
        started=time.monotonic()
        if name in ('global-route','detail-route'):
            argv=[sys.executable,HERE/'shared_memory_run.py','--memory-mib',args.memory_mib,
                  '--seconds',args.routing_seconds,'--report',work/(name+'-resources.json'),'--',*argv]
            timeout=args.routing_seconds+30
        with (work/(name+'.log')).open('w') as log:
            result=execute([str(x) for x in argv],env=env,stdout=log,stderr=-2,timeout=timeout,memory_exclusive=True)
        report['stages'].append(dict(name=name,returncode=result.returncode,seconds=time.monotonic()-started))
        save()
        if result.returncode:
            raise RuntimeError(name+' failed; see '+str(work/(name+'.log')))
    try:
        if args.stage in ('floorplan','all'):
            python=args.pdk.resolve()/'.cpphdl/venv/bin/python'
            pad_receipt=work/'pads/padframe.json'
            valid=False
            if pad_receipt.is_file():
                pad=json.loads(pad_receipt.read_text())
                valid=(pad.get('core_opening_um')==config['core_side_um'] and pad.get('common_ground')
                    and digest(work/'pads/repeater_padframe.gds')==pad['gds_sha256']
                    and all(digest(p)==h for p,h in pad['source_sha256'].items()))
            if not valid:
                command('padframe',[python,HERE/'padframe.py','--pdk',args.pdk.resolve(),'--work',work/'pads','--common-ground','--core-size',config['core_side_um']],600)
            command('pad-bindings',[sys.executable,HERE/'shared_floorplan.py','--work',work,'--padframe',work/'pads/padframe.json'],60)
        for stage in (('floorplan','place','route') if args.stage=='all' else (args.stage,)):
            env['SHARED_STAGE']=stage
            if stage in ('route','global-route','detail-route'):
                if stage=='route':
                    command('fanout',[args.openroad.resolve(),'-python',HERE/'shared_fanout.py'])
                for routing in (('global-route','detail-route') if stage=='route' else (stage,)):
                    env['SHARED_ROUTE_STAGE']='global' if routing=='global-route' else 'detail'
                    command(routing,[args.openroad.resolve(),'-exit',HERE/'shared_route.tcl'])
                continue
            if stage!='legalize':
                command(stage,[args.openroad.resolve(),'-exit',HERE/'shared_pnr.tcl'])
            if stage in ('place','legalize'):
                command('legalize',[args.openroad.resolve(),'-python',HERE/'shared_sparse_legalize.py'])
                command('connectivity',[args.openroad.resolve(),'-python',HERE/'shared_connectivity.py'])
                command('boundary-pins',[args.openroad.resolve(),'-exit',HERE/'shared_boundary_pins.tcl'])
            if stage=='floorplan':
                command('row-cut',[args.openroad.resolve(),'-python',HERE/'shared_cut_rows.py'])
        report['status']={'floorplan':'connected_floorplan','place':'placed_unrouted','legalize':'placed_unrouted','route':'routed_unqualified','global-route':'globally_routed_unqualified','detail-route':'routed_unqualified','all':'routed_unqualified'}[args.stage]
        report['artifact_sha256']={str(p.resolve()):digest(p) for p in work.iterdir() if p.suffix in ('.def','.odb')}
        if any(digest(p)!=h for p,h in report['source_sha256'].items()):
            raise ValueError('inputs changed during physical run')
    except Exception as error:
        report.update(status='failed',error=str(error))
        save()
        print(json.dumps(report['error']),flush=True)
        return 1
    save()
    print(json.dumps(dict(status=report['status'],stages=report['stages'])),flush=True)
    return 0

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',choices=('floorplan','place','legalize','route','global-route','detail-route','all'),required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--pdk',type=Path,default=ROOT/'pdk/IHPSG13G2')
    parser.add_argument('--digital-db',type=Path,default=ROOT/'build/repeater-unified-control-repair/placed.odb')
    parser.add_argument('--openroad',type=Path,default=ROOT/'out/repeater-toolchain/bin/openroad')
    parser.add_argument('--engine', choices=('fastroute','cugr'), default='fastroute')
    parser.add_argument('--memory-mib', type=int, default=4200)
    parser.add_argument('--routing-seconds', type=int, default=1800)
    configure_cleanup()
    raise SystemExit(run(parser.parse_args()))
