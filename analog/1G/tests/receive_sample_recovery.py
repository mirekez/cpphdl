#!/usr/bin/env python3
"""Qualify fixed-sample timing recovery in native and generated RTL."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from hardware import configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def audit_trace(path):
    counts = Counter()
    with path.open() as f:
        for line in f:
            fields = [int(x) for x in line.rstrip().split(',')]
            if len(fields) != 20 or any(x < 0 for x in fields):
                raise ValueError('malformed full-clock trace')
            counts[fields[0]] += 1
    expected = Counter({i: 42028 for i in range(12)})
    expected.update({i: 33 for i in range(100,103)})
    expected.update({i: 25 for i in range(110,113)})
    expected[120] = 23
    if counts != expected:
        raise ValueError('missing full-clock/reset/fault trace states')
    return sum(counts.values())


def metrics(path):
    rows = []
    pattern = re.compile(r'^case (\d+) ppm=(\S+) delay_UI=(\S+) phase=(\S+) maxphase=(\S+) errors=(\d+) slips=(\d+) arithmetic_counts=(\S+) period_ppm=(\S+)$')
    for line in path.read_text().splitlines():
        m = pattern.fullmatch(line)
        if m:
            values = list(map(float,m.groups()))
            case, ppm, delay, phase, peak, errors, slips, arithmetic, achieved = values
            if not (phase < .08 and peak < .20 and errors == slips == 0 and arithmetic <= 2 and abs(achieved-ppm) < 300):
                raise ValueError('independent fixed-sample metric limit failed')
            rows.append(dict(case=int(case), source_ppm=ppm, common_delay_UI=delay,
                             mean_phase_UI=phase, peak_phase_UI=peak, symbol_errors=int(errors),
                             symbol_slips=int(slips), max_interpolation_error_counts=arithmetic,
                             recovered_period_ppm=achieved))
    if [(r['source_ppm'],r['common_delay_UI']) for r in rows] != [(p,d) for p in (-1500,0,1500) for d in (0,.105,.35,.9)]:
        raise ValueError('missing declared source phase/rate cases')
    if 'PASS fixed-sample recovery and fault/restart checks' not in path.read_text():
        raise ValueError('missing fault/restart completion')
    return rows


def archive(path):
    archive_path = path.with_suffix(path.suffix+'.gz')
    expected = digest(path)
    with path.open('rb') as source, archive_path.open('wb') as target:
        with gzip.GzipFile(filename='',mode='wb',fileobj=target,mtime=0) as compressed:
            shutil.copyfileobj(source,compressed,1 << 20)
    actual = hashlib.sha256()
    with gzip.open(archive_path,'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20),b''):
            actual.update(chunk)
    if actual.hexdigest() != expected:
        raise ValueError('compressed trace changed')
    path.unlink()
    return dict(path=str(archive_path), raw_sha256=expected, archive_sha256=digest(archive_path))


def run(args):
    root = args.work.resolve(); root.mkdir(parents=True,exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-',dir=root))
    fixture = SOURCE.with_suffix('.cpp'); entry = ANALOG/'1G/receive_sample_recovery.cpp'
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    tools = [args.native.resolve(),args.converter.resolve(),args.verilator.resolve(),compiler]
    verilator_bin = args.verilator.resolve().with_name('verilator_bin')
    if verilator_bin.is_file(): tools.append(verilator_bin)
    hashes = source_dependencies([fixture,entry],ANALOG.parent,args.cxx)
    helpers = [SOURCE,SOURCE.with_name('hardware.py'),ANALOG/'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    hashes.update({str(p):digest(p) for p in tools+helpers})
    report = dict(status='running',work=str(work),source_sha256=hashes,stages={},
        scope='fixed-rate sampled PAM5, tagged cubic interpolation and data-derived timing; functional proof, no analog aperture/PCS/250MHz physical timing qualification')
    def save():
        payload=json.dumps(report,indent=2)+'\n'
        (work/'checks.json').write_text(payload); (root/'checks.json').write_text(payload)
    def command(label,argv):
        with (work/(label+'.log')).open('w') as log:
            execute(list(map(str,argv)),cwd=work,env=os.environ,stdout=log,stderr=-2,check=True,timeout=1800)
    save()
    try:
        command('native',[args.native.resolve(),work/'native.csv'])
        count=audit_trace(work/'native.csv'); native_metrics=metrics(work/'native.log')
        report['stages']['native']=dict(status='pass',clocks=count,cases=native_metrics)
        save()
        command('convert',[args.converter.resolve(),'--generated-dir',work/'rtl',
            '--primary_clock','clk','250000000',entry,'--','-I'+str(ANALOG.parent/'include')])
        if 'MISSED CALL' in (work/'convert.log').read_text() or 'Missing register strobe' in (work/'convert.log').read_text():
            raise ValueError('converter reported incomplete state scheduling')
        rtl=[work/'rtl'/name for name in ('Predef_pkg.sv','ReceiveTimingLoop.sv','ReceiveSampleInterpolator.sv','ReceiveSampleRecovery.sv')]
        objects=work/'rtl_obj'
        command('rtl_build',[args.verilator.resolve(),'--cc','--exe','--build','--no-skip-identical','-j','1',
            '--top-module','ReceiveSampleRecovery','-Wno-fatal','--Mdir',objects,
            '-CFLAGS','-DVERILATOR -std=c++17',*rtl,fixture,
            '-MAKEFLAGS','CXX='+args.cxx+' LINK='+args.cxx])
        command('rtl',[objects/'VReceiveSampleRecovery',work/'rtl.csv'])
        if audit_trace(work/'rtl.csv')!=count or metrics(work/'rtl.log')!=native_metrics:
            raise ValueError('RTL independent cases differ from native')
        if digest(work/'native.csv')!=digest(work/'rtl.csv'):
            raise ValueError('RTL full clock/valid/data/tag trajectory differs')
        report['stages']['rtl']=dict(status='pass',clocks=count,exact_native=True,cases=native_metrics)
        report['traces']={flow:archive(work/(flow+'.csv')) for flow in ('native','rtl')}
        if any(digest(p)!=h for p,h in hashes.items()):
            raise ValueError('qualification input changed during run')
        report.update(status='pass',rtl_sha256={str(p):digest(p) for p in rtl},
            artifact_sha256={str(p):digest(p) for p in work.iterdir() if p.is_file() and p.name!='checks.json'})
        print('PASS',count,'complete native/RTL clock states; 12 fixed-sample phase/rate cases')
    except BaseException as error:
        report.update(status='fail',error=str(error));raise
    finally: save()


if __name__=='__main__':
    configure_cleanup()
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('native','converter','verilator','work'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--cxx',default='/usr/bin/c++')
    run(parser.parse_args())
