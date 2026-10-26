"""Build ordinary C++HDL execution or only the comb optimizer, never native graph."""

import argparse
from contextlib import ExitStack
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import subprocess
import sys


def file_digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def anonymous_object():
    if hasattr(os, 'memfd_create'):
        return os.memfd_create('cpphdl-object')
    syscall = {'x86_64': 319, 'aarch64': 279, 'riscv64': 279}.get(platform.machine())
    if syscall is None:
        raise RuntimeError('anonymous object linking requires Linux memfd_create')
    libc = ctypes.CDLL(None, use_errno=True)
    descriptor = libc.syscall(syscall, b'cpphdl-object', 0)
    if descriptor < 0:
        raise OSError(ctypes.get_errno(), 'memfd_create')
    return descriptor


class ObjectCache:
    def __init__(self, directory, dependencies):
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=True)
        self.dependencies = sorted(set(dependencies))
        self.inputs = {str(path): file_digest(path) for path in self.dependencies}

    def key(self, source, flags):
        value = dict(inputs=self.inputs, source=str(source), source_sha256=file_digest(source), flags=flags)
        return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()

    def lookup(self, source, flags):
        key = self.key(source, flags)
        compressed = self.directory / (key + '.o.gz')
        record = self.directory / (key + '.json')
        if compressed.is_file() and record.is_file():
            if json.loads(record.read_text())['sha256'] == file_digest(compressed):
                return compressed
        return None

    def store(self, source, flags, target):
        key = self.key(source, flags)
        compressed = self.directory / (key + '.o.gz')
        temporary = compressed.with_suffix('.tmp')
        with target.open('rb') as original, gzip.open(temporary, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        temporary.replace(compressed)
        (self.directory / (key + '.json')).write_text(json.dumps(dict(
            source=str(source), flags=flags, sha256=file_digest(compressed))) + '\n')
        target.unlink()
        return compressed

    def split(self, source, flags, remember=False):
        record = self.directory / (self.key(source, flags) + '.split')
        if remember:
            record.touch()
        return record.is_file()

    @staticmethod
    def materialize(compressed, stack):
        descriptor = anonymous_object()
        stack.callback(os.close, descriptor)
        with gzip.open(compressed, 'rb') as original, os.fdopen(os.dup(descriptor), 'wb') as output:
            shutil.copyfileobj(original, output)
        return f'/proc/{os.getpid()}/fd/{descriptor}'


def compilation_order(sources, chunks):
    return sorted(range(len(sources)),
                  key=lambda index: (index >= 2, sources[index][0] not in chunks, index))


def unity_sources(work, instantiations, constructors, canonical_types, batch_size=8, runtime_batch_size=4):
    seen = {'-O0': set(), '-O2': set()}
    groups = {'-O0': [], '-O2': []}
    initialization = []
    ordered = [source for source in instantiations if source.stem in constructors]
    ordered += [source for source in instantiations if source.stem not in constructors]
    for source in ordered:
        default = '-O0' if source.stem in constructors else '-O2'
        text = source.read_text()
        fragments = [(default, text)]
        declarations = instantiation_declarations(text)
        if declarations:
            prefix, members = declarations
            partitions = {'-O0': [], '-O2': []}
            for member in members:
                optimization = '-O2' if member.startswith('template void ') and '::_assign(' not in member else '-O0'
                partitions[optimization].append(member)
            fragments = [(optimization, text if len(selected) == len(members) else prefix + '\n'.join(selected) + '\n')
                         for optimization, selected in partitions.items() if selected]
        for optimization, fragment in fragments:
            lines = []
            for line in fragment.splitlines():
                match = re.match(r'^template (?:void )?(cpphdl_opt_t\d+)::(.+);$', line)
                if match:
                    key = (canonical_types[match[1]], match[2])
                    if key in seen[optimization]:
                        continue
                    seen[optimization].add(key)
                lines.append(line)
            destination = initialization if optimization == '-O0' and default != '-O0' else groups[optimization]
            destination.append(f'#line 1 "{source.name}"\n' + '\n'.join(lines) + '\n')
    result = []
    offsets = {'-O0': 0, '-O2': 0}
    for optimization, bodies in [('-O0', groups['-O0']), ('-O0', initialization), ('-O2', groups['-O2'])]:
        limit = batch_size if optimization == '-O0' else min(batch_size, runtime_batch_size)
        batches = []
        pending = []
        for body in bodies:
            if 'cpphdl_optimized_root_' in body:
                if pending:
                    batches.append(pending)
                    pending = []
                batches.append([body])
            else:
                pending.append(body)
                if len(pending) == limit:
                    batches.append(pending)
                    pending = []
        if pending:
            batches.append(pending)
        for index, batch in enumerate(batches):
            source = work / f'unity-{optimization[1:]}-{offsets[optimization] + index}.cpp'
            source.write_text('\n'.join(batch))
            result.append((source, optimization))
        offsets[optimization] += len(batches)
    return result


def instantiation_declarations(text):
    matches = list(re.finditer(r'^template (?:void )?cpphdl_opt_t\d+::[^\n]+;[ \t]*$', text, re.MULTILINE))
    if not matches:
        return None
    prefix = text[:matches[0].start()]
    if any(line.strip() and not line.lstrip().startswith('#') for line in prefix.splitlines()):
        return None
    if text[matches[-1].end():].strip() or any(
            text[first.end():second.start()].strip() for first, second in zip(matches, matches[1:])):
        return None
    return prefix, [match.group() for match in matches]


def collection_cache_key(work, include, cpphdl, driver):
    paths = sorted({*work.rglob('*.h'), *work.glob('cpphdl_comb_collect_*.cpp'),
                    work / 'run_cpphdl_testharness_comb_optimize_seed.cpp',
                    *include.rglob('*.h'), Path(cpphdl), driver})
    inputs = {str(path): file_digest(path) for path in paths}
    return hashlib.sha256(json.dumps(dict(schema=1, mode='full', pch=False, inputs=inputs),
                                    sort_keys=True).encode()).hexdigest()


def compiler_address_space(source, optimization, piece_count):
    if source.name.startswith('unity-O') and (optimization == '-O2' or piece_count > 1):
        return 4_000_000_000
    return None


def compiler_environment():
    environment = dict(os.environ)
    for name, value in (('MALLOC_ARENA_MAX', '1'), ('MALLOC_TRIM_THRESHOLD_', '0'),
                        ('MALLOC_MMAP_THRESHOLD_', '65536')):
        environment.setdefault(name, value)
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('ordinary', 'output', 'riscv'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('hdlcpp', 'cpphdl', 'cxx'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--mode', choices=('default', 'optimize-combs'), required=True)
    parser.add_argument('--cache-dir', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(Path('/tmp')) or output == Path('/tmp'):
        parser.error('output must be below /tmp')
    if output.exists() and any(output.iterdir()):
        parser.error('output must be new or empty')
    cache_directory = (args.cache_dir or output / 'objects').resolve()
    if not cache_directory.is_relative_to(Path('/tmp')) or cache_directory == Path('/tmp'):
        parser.error('object cache must be below /tmp')
    support = Path(__file__).resolve().parent
    repo = support.parents[2]
    work = output / 'work'
    shutil.copytree(args.ordinary, work)
    for path in (support / 'support/cpphdl').glob('run_cpphdl_testharness*'):
        if path.is_file():
            shutil.copy2(path, work / path.name)
    commands = []

    def run(command, label, environment=None, address_space=None):
        command = list(map(str, command))
        commands.append(command)
        def compiler_limit():
            resource.setrlimit(resource.RLIMIT_AS, (address_space, address_space))
        with (output / (label + '.log')).open('w') as log:
            result = subprocess.run(command, cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                    preexec_fn=compiler_limit if address_space else None)
        if result.returncode:
            raise RuntimeError(f'{label} failed ({result.returncode}); see {output / (label + ".log")}')

    environment = dict(os.environ, HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE='24',
                       HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE='200000')
    run([args.hdlcpp, '--optimize', 'run_cpphdl_testharness_optimize_seed.cpp'],
        'specialize', environment)
    makefile = (work / 'Makefile.optimize').read_text()
    constructors = set(re.findall(r'^build/opt/([^:]+)\.o: override CXXFLAGS.*CONSTRUCTOR_CXXFLAGS',
                                  makefile, re.MULTILINE))
    instantiations = sorted(work.glob('cpphdl_optimized_inst_*.cpp'))
    if not instantiations:
        raise RuntimeError('hdlcpp specialization emitted no implementation units')
    model_sources = sorted(work.glob('run_cpphdl_testharness_model_*.cpp'))
    if args.mode == 'optimize-combs':
        run([sys.executable, '-B', support / 'support/cpphdl/prepare_optimize_combs.py', work,
             '--collection-chunk-size', '16', '--collection-max-definition-bytes', '1000000',
             '--collection-isolate-definition-bytes', '1000000'], 'prepare-combs')
        driver = support / 'support/cpphdl/run_optimize_combs.sh'
        collection_key = collection_cache_key(work, repo / 'include', args.cpphdl, driver)
        environment.update(CPPHDL=args.cpphdl, CPPHDL_CLANGXX=args.cxx,
                           CPPHDL_COMB_OPTIMIZER_MODE='full', CPPHDL_COMB_USE_PCH='0',
                           CPPHDL_COMB_WORK_DIR=str(cache_directory / 'collections' / collection_key),
                           CPPHDL_COMB_COLLECTION_KEY=collection_key,
                           CPPHDL_KEEP_COMB_INTERMEDIATES='1', CPPHDL_REUSE_COMB_COLLECTIONS='0',
                           CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK='1000')
        run(['bash', driver, work], 'optimize-combs', environment)
        model_sources = [path for path in model_sources if path.stem not in
                         ('run_cpphdl_testharness_model_bind', 'run_cpphdl_testharness_model_cycle')]
        model_sources += [work / 'run_cpphdl_testharness_optimized_bind.cpp',
                          work / 'run_cpphdl_testharness_optimized_cycle.cpp']
        model_sources += sorted(work.glob('cpphdl_opt_t0_optimized_combs*.cpp'))
    flags = ['-std=c++23', '-g0', '-w', '-mcmodel=medium', '-fno-strict-aliasing',
             '-I' + str(repo / 'include'), '-I' + str(work), '-I' + str(args.riscv / 'include')]
    if 'clang' in Path(args.cxx).name:
        flags.append('-fno-crash-diagnostics')
    aliases = re.findall(r'^using (cpphdl_opt_t\d+) = ', (work / 'cpphdl_optimized_externs.h').read_text(), re.MULTILINE)
    probe = work / 'canonical-types.cpp'
    probe.write_text('#include "cpphdl_optimized_externs.h"\n#include <cstdio>\n#include <typeinfo>\nint main() {\n' +
                     ''.join(f'std::puts(typeid({alias}).name());\n' for alias in aliases) + '}\n')
    run([args.cxx, *flags, '-O0', probe, '-o', work / 'canonical-types'], 'canonical-types-build')
    run([work / 'canonical-types'], 'canonical-types')
    canonical = (output / 'canonical-types.log').read_text().splitlines()
    if len(canonical) != len(aliases):
        raise RuntimeError('compiler type-identity probe returned the wrong alias count')
    runtime = work / 'unity-runtime.cpp'
    chunks = [path for path in model_sources if path.name.startswith('cpphdl_opt_t0_optimized_combs')]
    runtime.write_text(''.join(f'#include "{path.name}"\n' for path in model_sources if path not in chunks))
    sources = [(work / 'run_cpphdl_testharness.cpp', '-O2'), (runtime, '-O2'),
               *unity_sources(work, instantiations, constructors, dict(zip(aliases, canonical))),
               *((path, '-O2') for path in chunks)]
    objects = {}
    dependencies = [*work.rglob('*.h'), *(repo / 'include').rglob('*.h'),
                    *(args.riscv / 'include').rglob('*.h'),
                    *work.glob('run_cpphdl_testharness*.cpp'), Path(args.cxx)]
    cache = ObjectCache(cache_directory, dependencies)

    def compile_source(source, optimization, label):
        target = work / (label + '.o')
        compile_flags = [*flags, optimization]
        cached = cache.lookup(source, compile_flags)
        if cached:
            print(f'{source.name}: verified compressed object cache', flush=True)
            return [cached]
        pieces = [piece for piece in re.split(r'(?=^#line 1 ")', source.read_text(), flags=re.MULTILINE)
                  if piece.strip()] if source.name.startswith('unity-O') else []
        declarations = instantiation_declarations(pieces[0]) if len(pieces) == 1 else None
        if declarations:
            prefix, members = declarations
            initialization_members = [member for member in members if not member.startswith('template void ')
                                      or '::_assign(' in member]
            if optimization == '-O2' and initialization_members and len(initialization_members) != len(members):
                children = []
                for suffix, child_optimization, body in (
                        ('initialization', '-O0', initialization_members),
                        ('cycles', '-O2', [member for member in members if member not in initialization_members])):
                    child = source.with_name(source.stem + '-' + suffix + '.cpp')
                    child.write_text(prefix + '\n'.join(body) + '\n')
                    children.extend(compile_source(child, child_optimization, label + '-' + suffix))
                return children
            if len(members) > 1:
                pieces = [prefix + member + '\n' for member in members]
        def compile_halves():
            midpoint = len(pieces) // 2
            children = []
            for index, body in enumerate((pieces[:midpoint], pieces[midpoint:])):
                child = source.with_name(source.stem + '-' + str(index) + '.cpp')
                child.write_text('\n'.join(body))
                children.extend(compile_source(child, optimization, label + '-' + str(index)))
            return children

        if len(pieces) > 1 and cache.split(source, compile_flags):
            return compile_halves()
        try:
            run([args.cxx, *compile_flags, '-c', source, '-o', target], 'compile-' + label,
                environment=compiler_environment(),
                address_space=compiler_address_space(source, optimization, len(pieces)))
            return [cache.store(source, compile_flags, target)]
        except RuntimeError:
            with (output / ('compile-' + label + '.log')).open('rb') as stream:
                diagnostic = stream.read(4096).lower()
            if b'out of memory' not in diagnostic or len(pieces) < 2:
                raise
            print(f'{source.name}: splitting {len(pieces)} sources after compiler memory limit', flush=True)
            cache.split(source, compile_flags, remember=True)
            return compile_halves()

    for completed, index in enumerate(compilation_order(sources, chunks), start=1):
        source, optimization = sources[index]
        objects[index] = compile_source(source, optimization, str(index))
        print(f'{args.mode}: compiled {completed}/{len(sources)} {source.name}', flush=True)
    # Concrete CVA6 types produce enormous, highly repetitive ELF symbol tables.
    # Keep completed objects compressed; anonymous linker inputs avoid a second
    # on-disk copy without changing any generated equations or runtime options.
    with ExitStack() as stack:
        inputs = [cache.materialize(compressed, stack)
                  for index in range(len(sources)) for compressed in objects[index]]
        run([args.cxx, *flags, *inputs, '-L' + str(args.riscv / 'lib'), '-lfesvr', '-pthread', '-latomic',
             '-Wl,-s', '-o', output / 'run'], 'link')
    (output / 'manifest.json').write_text(json.dumps(dict(mode=args.mode, commands=commands,
        build_only_specialization='hdlcpp --optimize; no native-graph or additional cpphdl optimizer flags'), indent=2) + '\n')


if __name__ == '__main__':
    main()
