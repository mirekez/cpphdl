#!/usr/bin/env python3
"""Lower authoritative firtool C++ modules with explicit hierarchy boundaries."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

PORT = re.compile(r'_PORT\(cpphdl::logic<(\d+)>\)\s+(\w+);\s*// (input|output)')
CHILD = re.compile(r'^\s*(\w+)\*\s+(inst_\w+);', re.M)


def modules(source):
    result = {}
    for header in sorted(source.glob('*.h')):
        text = header.read_text()
        if not re.search(r'class ' + re.escape(header.stem) + r' : public cpphdl::Module', text):
            continue
        result[header.stem] = dict(name=header.stem, header=header,
            ports=[dict(width=int(w), name=n, direction=d) for w, n, d in PORT.findall(text)],
            children=[dict(type=t, name=n) for t, n in CHILD.findall(text)],
            external='cpphdlExternalState;' in text)
    return result


def seed_module(module, catalog, source, directory):
    directory.mkdir(parents=True, exist_ok=True)
    name = module['name']
    implementation = (source / (name + '.cpp')).read_text()
    for child in module['children']:
        if len(re.findall(r'\b' + re.escape(child['name']) + r'->_work\(', implementation)) != 1:
            raise RuntimeError(f'{name}: expected one work call for {child["name"]}')
    # Access control changes no hardware semantics; only the wrapper needs to
    # expose the existing hierarchy connections as graph partition ports.
    (directory / (name + '.h')).write_text(module['header'].read_text().replace('private:', 'public:'))
    (directory / (name + '.cpp')).write_text(implementation)
    for child in module['children']:
        child_module = catalog[child['type']]
        lines = ['#pragma once', '#include "cpphdl_support.h"',
                 f'class {child["type"]} : public cpphdl::Module {{ public:']
        lines += [f'_PORT(cpphdl::logic<{p["width"]}>) {p["name"]};' for p in child_module['ports']]
        lines += ['cpphdl::reg<cpphdl::logic<1>> __graph_enable, __graph_reset;',
                  'void _assign() {}',
                  'void _work(bool reset) { __graph_enable._next = 1; __graph_reset._next = reset; }',
                  'void _strobe() { __graph_enable.strobe(); __graph_reset.strobe(); }', '};']
        (directory / (child['type'] + '.h')).write_text('\n'.join(lines) + '\n')
    lines = [f'#include "{name}.cpp"', 'class GraphPartition : public cpphdl::Module { public:', f'{name} dut;']
    bindings, outputs = [], []
    for p in module['ports']:
        suffix = 'in' if p['direction'] == 'input' else 'out'
        port = f'r_{p["name"]}_{suffix}'
        lines.append(f'_PORT(cpphdl::logic<{p["width"]}>) {port};')
        if suffix == 'in':
            bindings.append(f'dut.{p["name"]} = _ASSIGN({port}());')
        else:
            outputs.append(f'{port} = _ASSIGN(dut.{p["name"]}());')
    for child in module['children']:
        for p in catalog[child['type']]['ports']:
            suffix = 'out' if p['direction'] == 'input' else 'in'
            port = f'c_{child["name"]}_{p["name"]}_{suffix}'
            lines.append(f'_PORT(cpphdl::logic<{p["width"]}>) {port};')
            if suffix == 'in':
                bindings.append(f'dut.{child["name"]}->{p["name"]} = _ASSIGN({port}());')
            else:
                outputs.append(f'{port} = _ASSIGN(dut.{child["name"]}->{p["name"]}());')
    lines += ['void _assign() {', *bindings, 'dut._assign();', *outputs, '}', 'void _work(bool reset) {']
    for child in module['children']:
        lines += [f'dut.{child["name"]}->__graph_enable._next = 0;',
                  f'dut.{child["name"]}->__graph_reset._next = 0;']
    lines += ['dut._work(reset);', '}', 'void _strobe() { dut._strobe(); }',
              '};', 'extern GraphPartition cpphdl_top;']
    seed = directory / 'partition.cc'
    seed.write_text('\n'.join(lines) + '\n')
    return seed


def link_plan(catalog, output, top):
    lines, hosts = [], []
    def visit(name, path, parent='', member=''):
        module = catalog[name]
        if module['external']:
            host = dict(path=path, type=name, ports=module['ports'], index=len(hosts))
            hosts.append(host)
            lines.append('host ' + ' '.join(map(json.dumps, [path, name, parent, member])))
            lines.extend(f'pin {p["direction"]} {json.dumps(p["name"])} {p["width"]}' for p in module['ports'])
        else:
            lines.append('part ' + ' '.join(map(json.dumps, [path, str(output / name / 'module.graph'), parent, member])))
            for child in module['children']:
                visit(child['type'], path + '.' + child['name'], path, child['name'])
    visit(top, 'top')
    (output / 'link.plan').write_text('\n'.join(lines) + '\n')
    (output / 'hosts.json').write_text(json.dumps(hosts, indent=2) + '\n')
    return hosts


def runner(hosts, output):
    types = sorted({h['type'] for h in hosts})
    lines = ['#include "model.h"', '#include "cpphdl_runtime.h"', '#include "cpphdl_support.h"',
             '#include <cstdio>', '#include <cstdlib>', '#include <memory>', '#include <string>',
             '#include <vector>']
    lines += [f'#include "{name}.h"' for name in types]
    lines += ['long _system_clock = 0;',
        'template<size_t W, size_t N> void pack(std::array<uint32_t,N>& a, const cpphdl::logic<W>& v) {',
        'a.fill(0); for(size_t b=0;b<W;++b) if(v.get(b)) a[b/32] |= uint32_t(1) << (b%32); }',
        'template<size_t W, size_t N> void unpack(cpphdl::logic<W>& v, const std::array<uint32_t,N>& a) {',
        'v=0; for(size_t b=0;b<W;++b) v.set(b, (a[b/32] >> (b%32)) & 1); }',
        'int main(int argc, char** argv) {',
        'if(argc<2) { std::fprintf(stderr,"usage: %s program.riscv [FESVR options]\\n",argv[0]); return 2; }',
        'std::vector<std::string> storage(argv,argv+argc); storage.emplace_back(std::string("+loadmem=")+argv[1]);',
        'std::vector<char*> arguments; for(auto& s:storage) arguments.push_back(s.data());',
        'firtool_cpphdl_runtime::configure(arguments.size(),arguments.data());',
        'auto allocation = std::make_unique<cpphdl_native::Model>(); auto& graph = *allocation;']
    for h in hosts:
        ident = f'h{h["index"]}'
        lines.append(f'{h["type"]} {ident};')
        for p in h['ports']:
            if p['direction'] == 'input':
                local = f'{ident}_{p["name"]}'
                lines += [f'cpphdl::logic<{p["width"]}> {local}=0;',
                          f'{ident}.{p["name"]} = [&]() {{ return &{local}; }};']
        lines.append(f'{ident}._assign();')
    lines += ['uint64_t limit=50000000, progress=0;',
        'if(auto* s=std::getenv("CPPHDL_MAX_CYCLES")) limit=std::strtoull(s,nullptr,0);',
        'if(auto* s=std::getenv("CPPHDL_PROGRESS_CYCLES")) progress=std::strtoull(s,nullptr,0);',
        'try { for(uint64_t cycle=0;cycle<limit;++cycle) {',
        '++_system_clock; graph.r_clock[0]=1; graph.r_reset[0]=cycle<10;']
    for h in hosts:
        ident = f'h{h["index"]}'
        for p in h['ports']:
            if p['direction'] == 'output':
                lines.append(f'pack(graph.{ident}_{p["name"]}, {ident}.{p["name"]}());')
    lines.append('graph.evaluate<true>();')
    # All host inputs belong to the same pre-commit transaction snapshot.
    for h in hosts:
        ident = f'h{h["index"]}'
        for p in h['ports']:
            if p['direction'] == 'input':
                lines.append(f'unpack({ident}_{p["name"]}, graph.{ident}_{p["name"]});')
    for h in hosts:
        ident = f'h{h["index"]}'
        lines.append(f'if(graph.host_control_{h["index"]}_enable[0]) {ident}._work(graph.host_control_{h["index"]}_work_reset[0]);')
        lines.append(f'{ident}._strobe();')
    lines += ['if(progress && (cycle+1)%progress==0) std::fprintf(stderr,"[CPPHDL graph] cycle %llu\\n",(unsigned long long)(cycle+1));',
        'if(uint32_t exit=firtool_cpphdl_runtime::exitCode()) {',
        'std::fprintf(stderr,"Native graph simulation finished after %llu cycles (code %u)\\n",(unsigned long long)(cycle+1),exit>>1);',
        'return exit>>1; }',
        '} } catch(const cpphdl_exception& e) { std::fprintf(stderr,"%s\\n",e.text.c_str()); return 2; }',
        'catch(const std::exception& e) { std::fprintf(stderr,"%s\\n",e.what()); return 2; }',
        'std::fprintf(stderr,"Native graph simulation timed out after %llu cycles\\n",(unsigned long long)limit); return 124;', '}']
    (output / 'runner.cpp').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cpphdl', required=True, type=Path)
    parser.add_argument('--include', required=True, type=Path)
    parser.add_argument('--module', action='append')
    parser.add_argument('--top', default='TestHarness')
    parser.add_argument('--jobs', type=int, default=1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    source, output = args.source.resolve(), args.output.resolve()
    catalog = modules(source)
    output.mkdir(parents=True, exist_ok=True)
    reachable = set()
    def visit(name):
        if name in reachable: return
        reachable.add(name)
        for child in catalog[name]['children']: visit(child['type'])
    visit(args.top)
    selected = args.module or sorted(n for n in reachable if not catalog[n]['external'])
    base = hashlib.sha256(args.cpphdl.read_bytes())
    for header in sorted(args.include.glob('*.h')):
        base.update(header.name.encode()); base.update(header.read_bytes())
    base.update((source / 'cpphdl_support.h').read_bytes())
    def lower(index, name):
        module = catalog[name]
        if module['external']:
            raise RuntimeError('external modules require a host model: ' + name)
        directory = output / name
        seed = seed_module(module, catalog, source, directory)
        graph = directory / 'module.graph'
        digest = base.copy()
        for path in sorted(directory.glob('*')):
            if path.suffix in ('.h', '.cpp', '.cc'):
                digest.update(path.name.encode()); digest.update(path.read_bytes())
        fingerprint = digest.hexdigest()
        stamp = directory / 'fingerprint'
        if graph.is_file() and stamp.is_file() and stamp.read_text() == fingerprint:
            print(f'[{index + 1}/{len(selected)}] Reusing {name}', flush=True)
            return
        graph.unlink(missing_ok=True)
        print(f'[{index + 1}/{len(selected)}] Lowering {name}', flush=True)
        command = [str(args.cpphdl.resolve()), '--lower-cpp-graph', str(seed), str(graph),
                   'cpphdl_top', '--', '-std=c++23', '-I' + str(directory),
                   '-I' + str(source), '-I' + str(args.include.resolve())]
        with (directory / 'lower.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{name} extraction failed ({result.returncode}): {directory / "lower.log"}')
        stamp.write_text(fingerprint)
        print(f'  {name}: {graph.stat().st_size} bytes', flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = [pool.submit(lower, index, name) for index, name in enumerate(selected)]
        try:
            for task in as_completed(pending): task.result()
        except BaseException:
            for task in pending: task.cancel()
            raise
    if not args.module:
        hosts = link_plan(catalog, output, args.top)
        if args.top == 'TestHarness': runner(hosts, output)


if __name__ == '__main__':
    main()
