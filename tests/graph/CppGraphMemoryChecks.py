"""Keep dependency and Clang AST growth linear for large module hierarchies."""
import argparse
from pathlib import Path
import re
import resource
import subprocess
import tempfile


def address_space_limit():
    limit = 768 * 1024 * 1024
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='scaling-', dir=args.work))

    def lower(name, source):
        fixture = work / (name + '.cc')
        fixture.write_text('#include <cpphdl.h>\nusing namespace cpphdl;\n' + source)
        result = subprocess.run([args.cpphdl, '--lower-cpp-graph', str(fixture),
                                 str(work / (name + '-graph.cc')), 'cpphdl_top', '--',
                                 '-std=c++23', '-I' + str(root / 'include')],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                preexec_fn=address_space_limit, timeout=60)
        (work / (name + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{name}: {result.stdout[-8000:]}\nArtifacts: {work}')
        stats = dict((key, int(value)) for key, value in
                     re.findall(r'(nodes|getter_reads|getter_edges|ast_bytes)=(\d+)', result.stdout))
        if len(stats) != 4:
            raise AssertionError('Missing graph statistics: ' + result.stdout)
        print(name, stats, flush=True)
        return stats

    def chain(stages):
        ports = []
        methods = []
        for stage in range(stages):
            ports.extend(f'_PORT(logic<32>) stage{stage}_field{i}_in;' for i in range(64))
            previous = f'stage{stage-1}_comb_func()' if stage else '0'
            updates = '\n'.join(f'stage{stage}_comb += uint32_t(stage{stage}_field{i}_in());'
                                for i in range(64))
            methods.append(f'''_LAZY_COMB(stage{stage}_comb, uint32_t)
                stage{stage}_comb = {previous};
                {updates}
                return stage{stage}_comb;
            }}''')
        return f'''
class Chain : public Module {{
public:
    {''.join(ports)}
    _PORT(logic<32>) output_out;
    {''.join(methods)}
    void _assign() {{ output_out = _ASSIGN_COMB(stage{stages-1}_comb_func()); }}
}};
extern Chain cpphdl_top;
'''

    small, large = lower('chain80', chain(80)), lower('chain160', chain(160))
    if small['getter_reads'] < 80 * 64 or large['getter_reads'] < 160 * 64:
        raise AssertionError('Each cached stage must depend on 64 distinct fields')
    for key in ('nodes', 'getter_reads', 'getter_edges'):
        if not small[key] or not small[key] < large[key] <= small[key] * 2 + 128:
            raise AssertionError(f'Nonlinear {key}: {small[key]} -> {large[key]}')

    def ports(count):
        members = '\n'.join(f'Leaf<{i}> leaf_{i};' for i in range(count))
        bindings = '\n'.join(f'leaf_{i}.input_in = _ASSIGN(input_in()); leaf_{i}._assign();'
                             for i in range(count))
        terms = ' + '.join(f'leaf_{i}.output_out()' for i in range(count)) or 'input_in()'
        return f'''
template<unsigned ID> class Leaf : public Module {{
public:
    _PORT(logic<32>) input_in;
    _PORT(logic<32>) output_out;
    void _assign() {{ output_out = _ASSIGN(logic<32>(uint32_t(input_in()) + ID)); }}
}};
class Ports : public Module {{
public:
    _PORT(logic<32>) input_in;
    _PORT(logic<32>) output_out;
    {members}
    void _assign() {{ {bindings} output_out = _ASSIGN({terms}); }}
}};
extern Ports cpphdl_top;
'''

    baseline = lower('ports0', ports(0))['ast_bytes']
    middle = lower('ports128', ports(128))['ast_bytes'] - baseline
    full = lower('ports256', ports(256))['ast_bytes'] - baseline
    if not 0 < middle < full <= middle * 2.5 + 1024 * 1024:
        raise AssertionError(f'Nonlinear incremental AST storage: {middle} -> {full}')


if __name__ == '__main__':
    main()
