#!/usr/bin/env python3
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--hdlcpp', required=True)
    parser.add_argument('--sv2v', required=True)
    parser.add_argument('--yosys', required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    source_root = Path(__file__).resolve().parents[2]
    fixtures = Path(__file__).resolve().parent
    args.work.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('word_frontend', source_root / 'tools/hdlcpp-word.py')
    frontend = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(frontend)
    assert frontend.normalize_literals("16'habcd[7-:4]") == "4'hc"
    assert frontend.normalize_literals("16'habcd[4+:4]") == "4'hc"
    assert frontend.normalize_literals('"16\'habcd[7-:4]"') == '"16\'habcd[7-:4]"'
    assert frontend.normalize_literals("// 16'habcd[7-:4]\n") == "// 16'habcd[7-:4]\n"
    try:
        frontend.normalize_literals("16'habcd[1-:4]")
        raise AssertionError('accepted out-of-range constant slice')
    except ValueError:
        pass
    module = {'ports': {'port': {}}, 'netnames': {
        'mask': {'hide_name': 1, 'attributes': {}, 'bits': ['1', 7, 7, 7]},
        'port': {'hide_name': 1, 'attributes': {}, 'bits': ['1', 7, 7, 7]},
        'mixed': {'hide_name': 1, 'attributes': {}, 'bits': ['1', 7, 8]},
        'const': {'hide_name': 1, 'attributes': {}, 'bits': ['1', '0']},
        'unsafe;name': {'hide_name': 1, 'attributes': {}, 'bits': ['1', 7]},
    }}
    assert frontend.scalar_aliases(module) == ['mask']

    with tempfile.TemporaryDirectory(prefix='word-', dir=args.work) as temporary:
        root = Path(temporary)

        def run(command, success=True):
            result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=240)
            if (result.returncode == 0) != success:
                for log in root.rglob('*.log'):
                    print(str(log) + '\n' + log.read_text()[-4000:], file=sys.stderr)
                raise AssertionError((command, result.returncode, result.stdout, result.stderr))
            return result

        flags = ['-std=c++23', '-I' + str(args.runtime), '-I' + str(source_root / 'include')]
        expressions = root / 'expressions.cc'
        result = run([args.cpphdl, '--lower-word-model', fixtures / 'WordExpressions.cc', expressions,
                      '--', *flags])
        assert 'lowered concatenation trees:' in result.stdout
        assert '::cpphdl::netlist::assemble(' in expressions.read_text()
        assert 'Unrelated{}.val()' in expressions.read_text()
        executable = root / 'expressions'
        run([args.cxx, *flags, '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             expressions, '-o', executable])
        print(run([executable]).stdout, end='')
        before = expressions.read_bytes()
        run([args.cpphdl, '--lower-word-model', fixtures / 'WordExpressions.cc', expressions,
             '--', *flags], success=False)
        assert expressions.read_bytes() == before
        for name, body in [('missing', '#include "missing-word-header.h"\n'),
                           ('invalid', 'int broken = ;\n')]:
            invalid = root / (name + '.cc')
            destination = root / (name + '-out.cc')
            invalid.write_text(body)
            run([args.cpphdl, '--lower-word-model', invalid, destination, '--', *flags], success=False)
            assert not destination.exists()

        source = root / 'WordPipeline.sv'
        shutil.copyfile(fixtures / source.name, source)
        generated = root / 'generated'
        run([args.hdlcpp, '--word-model', '--top', 'WordPipeline', '--output', generated,
             '--sv2v', args.sv2v, '--yosys', args.yosys, source])
        assert json.loads((generated / 'manifest.json').read_text())['status'] == 'complete'
        bundle = root / 'cpp-only'
        bundle.mkdir()
        for name in ('model.cc', 'model.h'):
            shutil.copyfile(generated / name, bundle / name)
        source.unlink()
        shutil.rmtree(generated)

        def build(source, output):
            run([args.cpphdl, '--word-model', '--cxx', args.cxx, '--runtime', args.runtime,
                 '--output', output, '--runner', fixtures / 'WordPipelineRun.cc', source])
            manifest = json.loads((output / 'manifest.json').read_text())
            assert manifest['status'] == 'complete'
            assert not any('yosys' == Path(command[0]).name or 'sv2v' == Path(command[0]).name
                           for command in manifest['commands'])

        output = root / 'run'
        build(bundle / 'model.cc', output)
        print(run([output / 'run']).stdout, end='')
        stale = root / 'stale-runner'
        stale.mkdir()
        shutil.copyfile(fixtures / 'WordPipelineRun.cc', stale / 'Run.cc')
        (stale / 'model.h').write_text('#ifndef CXXRTL_DESIGN_HEADER\n#error stale model\n#endif\n')
        manifest = json.loads((output / 'manifest.json').read_text())
        command = manifest['commands'][-1]
        command = [str(stale / 'Run.cc') if value == str(fixtures / 'WordPipelineRun.cc') else
                   str(stale / 'run') if value == str(output / 'run') else value for value in command]
        run(command)
        run([stale / 'run'])
        contents = (bundle / 'model.cc').read_text()
        marker = '::eval(performer *performer) {'
        assert contents.count(marker) == 1
        (bundle / 'model.cc').write_text(contents.replace(marker, marker + '\nreturn true;'))
        mutant = root / 'mutant'
        build(bundle / 'model.cc', mutant)
        run([mutant / 'run'], success=False)
        print('C++ mutation changes behavior; no RTL fallback')


if __name__ == '__main__':
    main()
