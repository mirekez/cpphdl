"""Build the full converted CVA6 testharness through authoritative C++ graph lowering."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ordinary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--riscv', type=Path, required=True)
    parser.add_argument('--chunk-size', type=int, default=512)
    parser.add_argument('--split-chunks', type=int, default=64)
    parser.add_argument('--threads', type=int, default=1)
    parser.add_argument('--resume-graph', action='store_true',
                        help='verify an existing frontend checkpoint, then re-emit and compile its native code')
    args = parser.parse_args()
    output = args.output.resolve()
    if Path('/tmp') not in output.parents:
        parser.error('build artifacts must be inside /tmp')
    if output.exists() and any(output.iterdir()) and not args.resume_graph:
        parser.error('output must be a new or empty directory')
    if args.resume_graph and (output / 'run').exists():
        parser.error('resume is only for an incomplete build, not an existing executable')
    if args.chunk_size < 1:
        parser.error('use a positive chunk size for bounded full-design compilation')
    if args.split_chunks < 1:
        parser.error('use a positive translation-unit chunk limit for full-design compilation')
    if args.threads < 1:
        parser.error('use a positive native thread count')
    ordinary = args.ordinary.resolve(strict=True)
    if not (ordinary / 'all_generated.h').is_file():
        parser.error('ordinary conversion is incomplete: all_generated.h missing')
    output.mkdir(parents=True, exist_ok=True)
    support = Path(__file__).resolve().parent
    repo = support.parents[2]
    seed = output / 'native_graph_top.cpp'
    if args.resume_graph:
        if not seed.is_file() or seed.read_bytes() != (support / seed.name).read_bytes():
            parser.error('resume checkpoint does not match the current C++ root')
    else:
        shutil.copy2(support / seed.name, seed)
    runner = ordinary / 'run_cpphdl_testharness.cpp'
    adapter = support / 'run_native_graph_model.cpp'
    input_paths = sorted(set(ordinary.rglob('*.h')) | set((repo / 'include').rglob('*.h')) |
                         {seed, runner, adapter, repo / 'tools/cpphdl-graph-emit.cpp', args.cpphdl.resolve()})
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    input_hashes = {str(path): digest(path) for path in input_paths}
    commands = []

    def run(command, name):
        command = [str(argument) for argument in command]
        commands.append(command)
        environment = os.environ.copy()
        environment.setdefault('CPPHDL_GRAPH_PROGRESS', '1')
        with (output / (name + '.log')).open('w') as log:
            result = subprocess.run(command, cwd=output, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{name} failed with status {result.returncode}: {output / (name + ".log")}')

    graph = output / 'graph'
    if args.resume_graph:
        checkpoint = json.loads((graph / 'manifest.json').read_text())
        if checkpoint['status'] != 'complete':
            raise RuntimeError('frontend checkpoint did not complete')
        if checkpoint['optimize_threads'] != args.threads:
            raise RuntimeError('resume checkpoint has a different native thread count')
        dependency_text = (graph / 'dependencies.d').read_text().replace('\\\n', '')
        dependencies = [Path(path) if Path(path).is_absolute() else graph / path
                        for path in shlex.split(dependency_text.split(':', 1)[1])]
        for path in [seed, args.cpphdl.resolve(), *dependencies]:
            if checkpoint['inputs'].get(str(path.resolve())) != digest(path):
                raise RuntimeError(f'frontend input changed since graph generation: {path}')
        if digest(graph / 'model.h') != checkpoint['artifacts']['model.h']:
            raise RuntimeError('native checkpoint was modified after emission')
        reference_hash = checkpoint.get('frontend_model_sha256', checkpoint['artifacts']['model.h'])
        emitter = repo / 'tools/cpphdl-graph-emit.cpp'
        run([args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include'), emitter,
             '-o', graph / 'lower-split'], 'build-split-emitter')
        reference = graph / 'verified-model.h'
        run([graph / 'lower-split', graph / 'model.graph', reference, args.chunk_size, args.threads,
             graph / 'verified-memories.tsv', '0'], 'verify-graph-checkpoint')
        if digest(reference) != reference_hash:
            raise RuntimeError('serialized graph does not reproduce the recorded native model')
        reference.unlink()
        for part in sorted(graph.glob('model.part.*.cpp')):
            if checkpoint['artifacts'].get(part.name) != digest(part):
                raise RuntimeError(f'native part is not an unchanged checkpoint artifact: {part}')
            part.unlink()
        run([graph / 'lower-split', graph / 'model.graph', graph / 'model.h', args.chunk_size, args.threads,
             graph / 'memories.tsv', args.split_chunks], 'emit-split-model')
        checkpoint['resumed_graph_sha256'] = digest(graph / 'model.graph')
        checkpoint['frontend_model_sha256'] = reference_hash
        checkpoint['frontend_inputs'] = {str(path.resolve()): digest(path)
                                         for path in [seed, args.cpphdl.resolve(), *dependencies]}
        checkpoint['inputs'] = {path: digest(Path(path)) for path in checkpoint['inputs']}
        checkpoint['commands'].extend(commands)
        checkpoint['split_chunks'] = args.split_chunks
        checkpoint['artifacts'] = {path.name: digest(path) for path in
                                   [graph / 'model.h', *sorted(graph.glob('model.part.*.cpp'))]}
        (graph / 'manifest.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    else:
        run([args.cpphdl.resolve(), '--native-graph', '--top', 'elaboration_root',
         '--chunk-size=' + str(args.chunk_size), '--optimize-threads=' + str(args.threads), '--cxx', args.cxx,
         '--split-chunks=' + str(args.split_chunks),
         '--frontend-flag=-DSYNTHESIS', '--frontend-flag=-I' + str(ordinary),
         '--frontend-flag=-I' + str(args.riscv.resolve() / 'include'),
         '--output', graph, seed], 'native-graph')
    thread_count = re.search(r'__cpphdl_thread_count = (\d+);', (graph / 'model.h').read_text())
    host_lanes = int(thread_count.group(1)) if thread_count else 1
    if host_lanes != args.threads:
        raise RuntimeError(f'expected {args.threads} native lanes, emitted {host_lanes}; refusing a mislabeled comparison')
    memories = []
    for line in (output / 'graph/memories.tsv').read_text().splitlines():
        index, width, depth, name = line.split('\t', 3)
        if name.endswith('.dut.i_sram.i_tc_sram_wrapper[0].i_tc_sram.sram'):
            memories.append((int(index), int(width), int(depth), name))
    if len(memories) != 1 or memories[0][1:3] != (64, 33554432):
        raise RuntimeError(f'expected one unmodified 256 MiB CVA6 SRAM, found {memories}')
    (output / 'native_memory.h').write_text(f'#define CVA6_GRAPH_SRAM memory{memories[0][0]}\n')
    run([args.cxx, '-std=c++23', '-O2', '-g0', '-I' + str(output / 'graph'),
         '-I' + str(output), '-I' + str(ordinary), '-I' + str(repo / 'include'),
         '-I' + str(args.riscv.resolve() / 'include'), runner, adapter,
         *sorted((output / 'graph').glob('model.part.*.cpp')),
         '-L' + str(args.riscv.resolve() / 'lib'), '-lfesvr', '-pthread', '-latomic',
         '-o', output / 'run'], 'build-runner')
    if any(digest(path) != input_hashes[str(path)] for path in input_paths):
        raise RuntimeError('build inputs changed during compilation; refusing mixed-generation measurements')
    manifest = dict(backend='cva6-ordinary-cpp-native-graph', host_lanes=host_lanes, chunk_size=args.chunk_size,
                    split_chunks=args.split_chunks,
                    commands=commands, sram=memories[0],
                    inputs=input_hashes,
                    binary_sha256=digest(output / 'run'))
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(output / 'run', flush=True)


if __name__ == '__main__':
    main()
