#!/usr/bin/env python3
"""Reproducible native-graph cross-design benchmark with independent oracles."""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

WORKLOADS = {
    'alu': 'Eight independent 64-bit multiply/add/xor datapaths, 192 rounds each.',
    'shared': 'A 768-round shared combinational cone feeding eight state cones; measures duplicated fan-in.',
    'bram': '1 MiB mutable RAM per instance, dependent random reads and ordered same-address writes.',
    'large-ram': '64 MiB mutable RAM per instance; 768 MiB with 12 actors, random dependent reads/writes.',
    'control': 'Small conditional counters, shifts and multiplexers; dispatch overhead negative control.',
    'pipeline': '64 clocked stages, four arithmetic rounds per stage, eight sampled outputs.',
    'dependent': 'One instance with one long 2048-round serial dependency chain; limited parallelism control.',
    'bus': 'Twelve actors and a shared external-memory controller, one response/clock, 7..11-clock latency and backpressure.',
    'mixed': 'Three arithmetic-heavy actors followed by nine small controllers; uneven static job costs.',
}
MODES = ['direct', 'batch1', 'batch2', 'batch4', 'batch6',
         'dynamic2', 'dynamic4', 'dynamic6', 'inner2', 'inner4', 'inner6']

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/g++')
    parser.add_argument('--workloads', choices=WORKLOADS, nargs='+', default=list(WORKLOADS))
    parser.add_argument('--repeat', type=int, default=9)
    parser.add_argument('--target-seconds', type=float, default=0.35)
    parser.add_argument('--max-sample-seconds', type=float, default=1.0)
    parser.add_argument('--min-aggregate-seconds', type=float, default=0.15,
                        help='Repeat identical traces when individual cheap samples are too short')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--reuse-build', action='store_true')
    parser.add_argument('--quick', action='store_true', help='256 clocks per variant; correctness smoke test')
    args = parser.parse_args()
    if args.repeat < 1 or min(args.target_seconds,args.max_sample_seconds,args.min_aggregate_seconds) <= 0:
        parser.error('Repeat and duration limits must be positive')
    root = Path(__file__).resolve().parents[2]
    source = Path(__file__).with_suffix('.cc')
    work = args.work.resolve(); work.mkdir(parents=True, exist_ok=True)
    toolchain = TestToolchain(args.cxx, None, work)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = dict(status='running', kind='native_graph_cross_design', device_performance=False,
                  started_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  source_sha256=sha(source), runner_sha256=sha(Path(__file__)),
                  native_header_sha256=sha(root/'include/cpphdl_graph_native.h'),
                  runtime_header_sha256=sha(root/'include/cpphdl_graph_threads.h'),
                  batch_header_sha256=sha(root/'include/cpphdl_graph_batch.h'),
                  allowed_cpus=sorted(os.sched_getaffinity(0)), compiler=args.cxx,
                  repeats=args.repeat, target_seconds=args.target_seconds,
                  min_aggregate_seconds=args.min_aggregate_seconds,
                  max_sample_seconds=args.max_sample_seconds, workloads={},
                  synchronization='Snapshot all inputs, one graph step per actor, join, then consume outputs on every clock. No multi-clock batching.',
                  oracle='Independent scalar C++ state equations, full mutable-memory digest and shared-controller traffic digest. Every measured run verified.',
                  host_load='Other existing jobs remain running; this is a shared-host measurement.',
                  limitations=['Graph-level generated workloads, plus the separately recorded mapped FP32 MAC experiments; not a survey of every cpphdl frontend/design.',
                               'Large mutable RAM measures simulator working-set pressure; it is not a physical FPGA DRAM timing model.',
                               'Cheap samples repeat identical full traces to reach the aggregate duration floor. Individual traces can remain short.'])
    output = work/'results.json'
    def save():output.write_text(json.dumps(report,indent=2)+'\n')
    save()
    def run(command, log, timeout=300):
        with log.open('w') as stream:
            stream.write(' '.join(map(str,command))+'\n'); stream.flush()
            result=subprocess.run(toolchain.command(command),env=toolchain.env,
                                  stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
        if result.returncode:raise RuntimeError(log.read_text()[-12000:])
    for name in args.workloads:
        directory=work/name;directory.mkdir(exist_ok=True)
        flags=['-std=c++23','-DGENERAL_KIND='+str(list(WORKLOADS).index(name)),'-I'+str(root/'include')]
        if not args.reuse_build:
            print('BUILD',name,flush=True)
            run([args.cxx,'-O1',*flags,'-DGENERAL_EMIT',source,'-o',directory/'emit'],directory/'build-emitter.log')
            run([directory/'emit',directory],directory/'emit.log')
            run([args.cxx,'-O2','-pthread',*flags,'-I'+str(directory),source,'-o',directory/'bench'],directory/'build-bench.log')
            (directory/'build-source.sha256').write_text(sha(source)+'\n')
        elif (directory/'build-source.sha256').read_text().strip()!=sha(source):
            raise RuntimeError('Stale benchmark source in '+str(directory))
        plans=[list(map(int,line.split())) for line in (directory/'plans.tsv').read_text().splitlines()]
        report['workloads'][name]=dict(description=WORKLOADS[name], instances=1 if name=='dependent' else 12,
            plans=[dict(requested_lanes=x[0],actual_lanes=x[1],base_nodes=x[2],planned_nodes=x[3]) for x in plans],
            executable_sha256=sha(directory/'bench'), samples={mode:[] for mode in MODES})
        save()
    if args.build_only:
        report['status']='built';save();return
    for name in args.workloads:
        entry=report['workloads'][name]; executable=work/name/'bench'
        def measurement(mode,cycles,aggregate=False):
            def once():
                result=subprocess.run([executable,mode,str(cycles),str(entry['instances']),'1'],
                    env=toolchain.env,capture_output=True,text=True,timeout=90)
                if result.returncode:raise RuntimeError(name+' '+mode+': '+result.stdout+result.stderr)
                item=json.loads(result.stdout)
                if not item['verified']:raise RuntimeError('Unverified timing sample')
                return item
            item=once();subsamples=[item]
            elapsed=item['seconds']
            while aggregate and not args.quick and elapsed<args.min_aggregate_seconds and len(subsamples)<1024:
                extra=once()
                if any(extra[key]!=item[key] for key in ('fingerprint','memory_hash','traffic_hash')):
                    raise RuntimeError('Identical trace repetitions diverged')
                subsamples.append(extra);elapsed+=extra['seconds']
            item=dict(item)
            item['seconds']=elapsed/len(subsamples)
            item['cpu_seconds']=sum(v['cpu_seconds'] for v in subsamples)/len(subsamples)
            item['effective_cpus']=item['cpu_seconds']/item['seconds']
            item['aggregate_measured_seconds']=elapsed
            item['subsample_count']=len(subsamples)
            item['subsample_durations']=[v['seconds'] for v in subsamples]
            item['host_ns_per_instance_clock']=item['seconds']*1e9/(cycles*entry['instances'])
            return item
        if args.quick:cycles=256
        else:
            probes={mode:measurement(mode,128) for mode in MODES}
            direct=probes['direct']['seconds'];slowest=max(r['seconds'] for r in probes.values())
            cycles=max(256,min(4194304,int(128*args.target_seconds/direct),int(128*args.max_sample_seconds/slowest)))
            entry['calibration']=probes
        entry['cycles']=cycles;save()
        print('MEASURE',name,'cycles',cycles,'instances',entry['instances'],flush=True)
        for repetition in range(args.repeat):
            names=MODES[repetition%len(MODES):]+MODES[:repetition%len(MODES)]
            if repetition%2:names.reverse()
            for mode in names:
                item=measurement(mode,cycles,aggregate=True);entry['samples'][mode].append(item);save()
                print(name,repetition,mode,'ns/actor-clock',round(item['host_ns_per_instance_clock'],1),
                      'CPUs',item['effective_cpus'],flush=True)
        traces={(r['fingerprint'],r['memory_hash'],r['traffic_hash']) for samples in entry['samples'].values() for r in samples}
        if len(traces)!=1:raise RuntimeError(name+': timing variants produced different traces')
        summary={}
        baseline=statistics.median(r['host_ns_per_instance_clock'] for r in entry['samples']['direct'])
        for mode,samples in entry['samples'].items():
            times=[r['host_ns_per_instance_clock'] for r in samples]
            paired=[b['seconds']/r['seconds'] for b,r in zip(entry['samples']['direct'],samples)]
            summary[mode]=dict(median_host_ns_per_instance_clock=statistics.median(times),
                median_effective_cpus=statistics.median(r['effective_cpus'] for r in samples),
                speedup=baseline/statistics.median(times), min_host_ns=min(times),max_host_ns=max(times),
                coefficient_of_variation=statistics.stdev(times)/statistics.mean(times) if len(times)>1 else 0,
                per_round_speedups=paired,min_sample_seconds=min(r['seconds'] for r in samples),
                min_aggregate_measured_seconds=min(r['aggregate_measured_seconds'] for r in samples))
        entry['summary']=summary;entry['status']='passed';save()
        print('SUMMARY',name,' '.join(mode+'='+str(round(v['speedup'],2))+'x' for mode,v in summary.items()),flush=True)
    report['status']='passed';report['finished_at']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    report['timed_samples']=sum(len(samples) for entry in report['workloads'].values() for samples in entry['samples'].values())
    report['geomean_speedup_all_workloads']={mode:math.exp(statistics.mean(math.log(entry['summary'][mode]['speedup'])
        for entry in report['workloads'].values())) for mode in MODES}
    save()

if __name__=='__main__':main()
