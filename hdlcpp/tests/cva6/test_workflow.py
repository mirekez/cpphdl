import argparse
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import cva6_workflow as workflow
from build_cpp_model import ObjectCache, collection_cache_key, instantiation_declarations, unity_sources


class WorkflowTests(unittest.TestCase):
    def arguments(self, root):
        return argparse.Namespace(work_root=root, cpu=None, cpus4=None, force=False,
                                  timeout=30, keep_build_files=False, keep_traces=False,
                                  trials=5, max_cycles=2_000_000)

    def instance(self, root):
        with patch.dict(os.environ, {'CPPHDL_CXX': '/bin/true'}):
            return workflow.Workflow(self.arguments(root))

    def test_wrappers_forward_arguments_and_work_from_another_directory(self):
        expected = {
            '.build_cva6.sh': ['prepare'], '.build_cva6_hdlcpp.sh': ['convert'],
            '.build_all.sh': ['build-all'], '.compare.sh': ['compare'],
        }
        for mode in workflow.MODES:
            suffix = 'cpphdl' if mode == 'cpphdl' else mode
            expected['.build_cva6_' + suffix + '.sh'] = ['build', mode]
            expected['.run_cva6_' + suffix + '.sh'] = ['run', mode]
        with tempfile.TemporaryDirectory(prefix='cva6-wrappers-', dir='/tmp') as directory:
            root = Path(directory)
            interpreter = root / 'python3'
            interpreter.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            interpreter.chmod(0o755)
            environment = dict(os.environ, PATH=str(root) + ':' + os.environ['PATH'])
            for name, arguments in expected.items():
                script = workflow.SUPPORT / name
                subprocess.run(['bash', '-n', script], check=True)
                result = subprocess.check_output([script, '--work-root', str(root / 'with spaces')],
                                                 cwd=root, env=environment, text=True).splitlines()
                self.assertEqual(result, ['-B', str(workflow.SUPPORT / 'cva6_workflow.py'),
                                          *arguments, '--work-root', str(root / 'with spaces')])

    def test_help_does_not_create_artifacts(self):
        with tempfile.TemporaryDirectory(prefix='cva6-help-', dir='/tmp') as directory:
            destination = Path(directory) / 'unused'
            for script in workflow.SUPPORT.glob('.*.sh'):
                subprocess.run([script, '--work-root', destination, '--help'],
                               check=True, stdout=subprocess.DEVNULL)
            self.assertFalse(destination.exists())

    def test_artifacts_and_cleanup_are_confined(self):
        with self.assertRaises(ValueError):
            self.instance(Path('/tmp'))
        with self.assertRaises(ValueError):
            self.instance(workflow.SUPPORT)
        with tempfile.TemporaryDirectory(prefix='cva6-cleanup-', dir='/tmp') as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                workflow.remove(root, root)
            link = root / 'outside'
            link.symlink_to('/home')
            with self.assertRaises(ValueError):
                workflow.remove(link, root)

    def test_executable_preserves_cxx_driver_symlink_name(self):
        with tempfile.TemporaryDirectory(prefix='cva6-cxx-driver-', dir='/tmp') as directory:
            driver = Path(directory) / 'clang++'
            driver.symlink_to('/bin/true')
            self.assertEqual(workflow.executable(driver), str(driver))

    def test_build_signature_checks_snapshot_and_compiler_contents(self):
        with tempfile.TemporaryDirectory(prefix='cva6-input-signature-', dir='/tmp') as directory:
            root = Path(directory)
            instance = self.instance(root)
            instance.source.mkdir()
            rtl = instance.source / 'top.sv'
            rtl.write_text('module top; endmodule\n')
            workflow.save(root / 'source.json', dict(files={'top.sv': workflow.digest(rtl)}))
            compiler = root / 'clang++'
            compiler.write_text('compiler version one')
            instance.cxx = str(compiler)
            instance.verilator = '/bin/true'
            initial = instance.signature('verilator')
            compiler.write_text('compiler version two')
            self.assertNotEqual(initial, instance.signature('verilator'))
            rtl.write_text('module different; endmodule\n')
            with self.assertRaisesRegex(RuntimeError, 'source snapshot changed'):
                instance.signature('verilator')

    def test_temporary_space_accounts_for_user_quota(self):
        with patch.object(workflow.shutil, 'disk_usage', return_value=argparse.Namespace(free=1000)):
            for quota, expected in ((200, 200), (2000, 1000), (None, 1000), (0, 0)):
                with patch.object(workflow, 'quota_remaining', return_value=quota):
                    self.assertEqual(workflow.temporary_free('/tmp'), expected)
            with patch.object(workflow, 'quota_remaining', side_effect=OSError):
                self.assertEqual(workflow.temporary_free('/tmp'), 1000)

    def test_low_quota_refuses_to_start_a_stage(self):
        with tempfile.TemporaryDirectory(prefix='cva6-quota-', dir='/tmp') as directory:
            root = Path(directory)
            with patch.object(workflow, 'temporary_free', return_value=0), \
                    patch.object(workflow.subprocess, 'Popen') as process:
                with self.assertRaisesRegex(RuntimeError, 'user quota'):
                    workflow.checked(['/bin/true'], root, root / 'run.log')
                process.assert_not_called()

    def test_compressed_objects_link_and_invalidate(self):
        compiler = os.environ.get('CPPHDL_TEST_CXX')
        if not compiler:
            self.skipTest('set CPPHDL_TEST_CXX for compiler-backed tests')
        with tempfile.TemporaryDirectory(prefix='cva6-object-cache-', dir='/tmp') as directory:
            root = Path(directory)
            source, dependency, target = root / 'main.cpp', root / 'value.h', root / 'main.o'
            source.write_text('#include "value.h"\nint main(){return VALUE;}\n')
            dependency.write_text('#define VALUE 0\n')
            flags = ['-std=c++23', '-O2']
            cache = ObjectCache(root / 'cache', [dependency])
            self.assertFalse(cache.split(source, flags))
            self.assertTrue(cache.split(source, flags, remember=True))
            self.assertTrue(ObjectCache(root / 'cache', [dependency]).split(source, flags))
            self.assertFalse(cache.split(source, ['-O0']))
            subprocess.run([compiler, *flags, '-c', source, '-o', target], check=True)
            original = target.read_bytes()
            compressed = cache.store(source, flags, target)
            self.assertFalse(target.exists())
            self.assertEqual(cache.lookup(source, flags), compressed)
            with workflow.ExitStack() as stack:
                linker_input = cache.materialize(compressed, stack)
                self.assertEqual(Path(linker_input).read_bytes(), original)
                subprocess.run([compiler, linker_input, '-o', root / 'run'], check=True)
                subprocess.run([root / 'run'], check=True)
            self.assertIsNone(cache.lookup(source, ['-O0']))
            dependency.write_text('#define VALUE 1\n')
            self.assertIsNone(ObjectCache(root / 'cache', [dependency]).lookup(source, flags))
            self.assertFalse(ObjectCache(root / 'cache', [dependency]).split(source, flags))
            source.write_text('int main(){return 1;}\n')
            self.assertIsNone(cache.lookup(source, flags))

    def test_unity_deduplicates_only_identical_types_and_member_signatures(self):
        with tempfile.TemporaryDirectory(prefix='cva6-unity-', dir='/tmp') as directory:
            root = Path(directory)
            paths = []
            definitions = [('cpphdl_opt_t0', 'Cell();', '_work(bool);'),
                           ('cpphdl_opt_t1', 'Cell();', '_work(bool);', '_work();'),
                           ('cpphdl_opt_t2', 'Cell();', '_work(bool);')]
            (root / 'types.h').write_text('#pragma once\ntemplate<int Tag> struct Cell {\n'
                'int value; Cell(): value(0) {} void _work(bool) { value += Tag; }\n'
                'void _work() { value += 3; } };\n'
                'using cpphdl_opt_t0 = Cell<1>; using cpphdl_opt_t1 = Cell<1>; using cpphdl_opt_t2 = Cell<2>;\n')
            for index, definition in enumerate(definitions):
                alias, constructor, *members = definition
                path = root / f'inst-{index}.cpp'
                path.write_text('#include "types.h"\n' + f'template {alias}::{constructor}\n' +
                                ''.join(f'template void {alias}::{member}\n' for member in members))
                paths.append(path)
            groups = unity_sources(root, paths, set(),
                                   dict(cpphdl_opt_t0='Cell1', cpphdl_opt_t1='Cell1', cpphdl_opt_t2='Cell2'))
            self.assertEqual(len(groups), 1)
            text = groups[0][0].read_text()
            self.assertNotIn('template cpphdl_opt_t1::Cell()', text)
            self.assertNotIn('template void cpphdl_opt_t1::_work(bool)', text)
            self.assertIn('template void cpphdl_opt_t1::_work()', text)
            self.assertIn('template void cpphdl_opt_t2::_work(bool)', text)
            if os.environ.get('CPPHDL_TEST_CXX'):
                main = root / 'main.cpp'
                main.write_text('#include "types.h"\nint main() { Cell<1> first; Cell<2> second;\n'
                                'first._work(false); first._work(); second._work(false);\n'
                                'return first.value != 4 || second.value != 2; }\n')
                subprocess.run([os.environ['CPPHDL_TEST_CXX'], '-std=c++23', '-O2', groups[0][0],
                                main, '-o', root / 'run'], check=True, timeout=60)
                subprocess.run([root / 'run'], check=True)

    def test_failed_command_is_recorded_and_propagated(self):
        with tempfile.TemporaryDirectory(prefix='cva6-command-', dir='/tmp') as directory:
            root = Path(directory)
            with patch.object(workflow, 'temporary_free', return_value=1024**3), \
                    self.assertRaises(RuntimeError):
                workflow.checked(['/bin/sh', '-c', 'echo sentinel; exit 7'], root, root / 'stage.log')
            record = json.loads((root / 'stage.json').read_text())
            self.assertEqual(record['status'], 7)
            self.assertIn('sentinel', (root / 'stage.log').read_text())

    def test_optimized_unity_batches_have_a_lower_memory_bound(self):
        with tempfile.TemporaryDirectory(prefix='cva6-unity-bounds-', dir='/tmp') as directory:
            root = Path(directory)
            sources = []
            for index in range(24):
                source = root / f'inst-{index}.cpp'
                source.write_text(f'int function_{index}();\n')
                sources.append(source)
            constructors = {source.stem for source in sources[:12]}
            batches = unity_sources(root, sources, constructors, {})
            sizes = [(optimization, source.read_text().count('#line 1 '))
                     for source, optimization in batches]
            self.assertEqual(sizes, [('-O0', 8), ('-O0', 4),
                                     ('-O2', 4), ('-O2', 4), ('-O2', 4)])

    def test_instantiation_partition_preserves_overloads_and_rejects_other_definitions(self):
        prefix = '#line 1 "inst.cpp"\n#include "types.h"\n\n'
        members = ['template cpphdl_opt_t0::Cell();',
                   'template void cpphdl_opt_t0::_work(bool);',
                   'template void cpphdl_opt_t0::_work();']
        text = prefix + '\n'.join(members) + '\n'
        self.assertEqual(instantiation_declarations(text), (prefix, members))
        for invalid in (text + 'int extra = 0;\n', 'int extra = 0;\n' + text,
                        text.replace(members[1], 'void extra() {}\n' + members[1])):
            self.assertIsNone(instantiation_declarations(invalid))
        compiler = os.environ.get('CPPHDL_TEST_CXX')
        if not compiler:
            return
        with tempfile.TemporaryDirectory(prefix='cva6-member-partition-', dir='/tmp') as directory:
            root = Path(directory)
            (root / 'types.h').write_text('#pragma once\ntemplate<int Tag> struct Cell {\n'
                'int value; Cell():value(0) {} void _work(bool) {value += Tag;}\n'
                'void _work() {value += 3;} }; using cpphdl_opt_t0 = Cell<1>;\n'
                'extern template Cell<1>::Cell(); extern template void Cell<1>::_work(bool);\n'
                'extern template void Cell<1>::_work();\n')
            objects = []
            for index, member in enumerate(members):
                source, target = root / f'part-{index}.cpp', root / f'part-{index}.o'
                source.write_text(prefix + member + '\n')
                subprocess.run([compiler, '-std=c++23', '-O0' if index == 0 else '-O2',
                                '-c', source, '-o', target], check=True, timeout=60)
                objects.append(target)
            main = root / 'main.cpp'
            main.write_text('#include "types.h"\nint main(){Cell<1> cell; cell._work(false);\n'
                            'cell._work(); return cell.value != 4;}\n')
            subprocess.run([compiler, '-std=c++23', main, *objects, '-o', root / 'run'], check=True, timeout=60)
            subprocess.run([root / 'run'], check=True)

    def test_comb_collection_without_pch_uses_only_requested_optimizer(self):
        with tempfile.TemporaryDirectory(prefix='cva6-no-pch-', dir='/tmp') as directory:
            root = Path(directory)
            (root / 'generated').mkdir()
            (root / 'cpphdl_comb_collection_sources.txt').write_text('collection.cpp\n')
            (root / 'collection.cpp').write_text('')
            tool = root / 'cpphdl'
            tool.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n'
                            'for argument do case "$argument" in --optimize-combs-collect=*)\n'
                            'printf collected > "${argument#*=}";; esac; done\n')
            tool.chmod(0o755)
            environment = dict(os.environ, CPPHDL=str(tool), CPPHDL_CLANGXX='/bin/false',
                               CPPHDL_COMB_USE_PCH='0', CPPHDL_COMB_OPTIMIZER_MODE='full',
                               CPPHDL_COMB_WORK_DIR=str(root / 'collections'), CPPHDL_KEEP_COMB_INTERMEDIATES='0')
            result = subprocess.check_output(['bash', workflow.SUPPORT / 'support/cpphdl/run_optimize_combs.sh', root],
                                             env=environment, text=True)
            self.assertEqual(result.splitlines().count('--optimize-combs'), 2)
            self.assertNotIn('-include-pch', result)
            self.assertNotIn('--native-graph', result)
            self.assertNotIn('--optimize-combs-l1', result)
            self.assertFalse(list(root.rglob('*.pch')))

    def test_comb_collection_cache_survives_recreated_sources_and_checks_contents(self):
        with tempfile.TemporaryDirectory(prefix='cva6-comb-cache-', dir='/tmp') as directory:
            root = Path(directory)
            (root / 'generated').mkdir()
            (root / 'include').mkdir()
            (root / 'cpphdl_comb_collection_sources.txt').write_text('cpphdl_comb_collect_000.cpp\n')
            source = root / 'cpphdl_comb_collect_000.cpp'
            source.write_text('')
            (root / 'run_cpphdl_testharness_comb_optimize_seed.cpp').write_text('')
            tool = root / 'cpphdl'
            tool.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n'
                            'for argument do case "$argument" in --optimize-combs-collect=*)\n'
                            'printf collected > "${argument#*=}";; esac; done\n')
            tool.chmod(0o755)
            driver = workflow.SUPPORT / 'support/cpphdl/run_optimize_combs.sh'
            key = collection_cache_key(root, root / 'include', tool, driver)
            environment = dict(os.environ, CPPHDL=str(tool), CPPHDL_CLANGXX='/bin/false',
                CPPHDL_COMB_USE_PCH='0', CPPHDL_COMB_OPTIMIZER_MODE='full', CPPHDL_COMB_COLLECTION_KEY=key,
                CPPHDL_COMB_WORK_DIR=str(root / 'collections'), CPPHDL_KEEP_COMB_INTERMEDIATES='1')
            def run():
                return subprocess.check_output(['bash', driver, root], env=environment, text=True,
                                               stderr=subprocess.STDOUT)
            self.assertIn('--optimize-combs-collect=', run())
            source.write_text('')
            self.assertEqual(key, collection_cache_key(root, root / 'include', tool, driver))
            self.assertNotIn('--optimize-combs-collect=', run())
            collection = root / 'collections/collection_000.bin'
            collection.write_text('corrupt')
            self.assertIn('--optimize-combs-collect=', run())
            self.assertEqual(collection.read_text(), 'collected')
            collection.with_suffix('.bin.sha256').write_text('')
            self.assertIn('--optimize-combs-collect=', run())
            self.assertFalse(list((root / 'collections').glob('*.tmp')))
            source.write_text('changed collection source')
            self.assertNotEqual(key, collection_cache_key(root, root / 'include', tool, driver))
            source.write_text('')
            (root / 'include/runtime.h').write_text('changed runtime')
            self.assertNotEqual(key, collection_cache_key(root, root / 'include', tool, driver))

    def test_comb_collector_splits_only_memory_failures_and_reuses_children(self):
        with tempfile.TemporaryDirectory(prefix='cva6-comb-split-', dir='/tmp') as directory:
            root = Path(directory)
            (root / 'generated').mkdir()
            source = root / 'collection.cpp'
            source.write_text('#include "context.h"\n' + ''.join(
                f'cpphdl_opt_t{index}* cpphdlCombsCollect000_{index:03d} = nullptr;\n' for index in range(4)))
            (root / 'cpphdl_comb_collection_sources.txt').write_text(source.name + '\n')
            tool = root / 'cpphdl'
            tool.write_text('#!/usr/bin/env python3\nimport pathlib,sys\narguments=sys.argv[1:]\n'
                'collect=next((item.split("=",1)[1] for item in arguments if item.startswith("--optimize-combs-collect=")),None)\n'
                'if collect:\n source=pathlib.Path(arguments[arguments.index("--")-1])\n'
                ' if source.read_text().count("nullptr;")>1:\n  print("LLVM ERROR: out of memory"); sys.exit(1)\n'
                ' pathlib.Path(collect).write_text(source.read_text())\n'
                'print(" ".join(arguments))\n')
            tool.chmod(0o755)
            environment = dict(os.environ, CPPHDL=str(tool), CPPHDL_CLANGXX='/bin/false',
                CPPHDL_COMB_USE_PCH='0', CPPHDL_COMB_OPTIMIZER_MODE='full', CPPHDL_COMB_COLLECTION_KEY='verified',
                CPPHDL_COMB_WORK_DIR=str(root / 'collections'), CPPHDL_KEEP_COMB_INTERMEDIATES='1')
            driver = workflow.SUPPORT / 'support/cpphdl/run_optimize_combs.sh'
            def run():
                return subprocess.run(['bash', driver, root], env=environment, text=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            first = run()
            self.assertEqual(first.returncode, 0, first.stdout)
            self.assertEqual(first.stdout.count('--optimize-combs-load='), 4)
            self.assertEqual(len(list((root / 'collections').glob('*.bin'))), 4)
            second = run()
            self.assertEqual(second.returncode, 0, second.stdout)
            self.assertNotIn('--optimize-combs-collect=', second.stdout)
            self.assertNotIn('splitting comb collection:', second.stdout)
            self.assertEqual(second.stdout.count('--optimize-combs-load='), 4)
            tool.write_text('#!/bin/sh\necho "syntax error"; exit 7\n')
            for path in (root / 'collections').glob('*.bin.sha256'):
                path.unlink()
            failure = run()
            self.assertNotEqual(failure.returncode, 0)
            self.assertIn('syntax error', failure.stdout)

    def test_runtime_refresh_does_not_regenerate_unchanged_systemverilog(self):
        with tempfile.TemporaryDirectory(prefix='cva6-convert-cache-', dir='/tmp') as directory:
            root = Path(directory)
            fixture = root / 'fixture'
            support = fixture / 'support/cpphdl'
            support.mkdir(parents=True)
            (fixture / 'convert.sh').write_text('converter')
            (support / 'cva6_params.tsv').write_text('parameters')
            runner = support / 'run_cpphdl_testharness.cpp'
            runner.write_text('old runner')
            instance = self.instance(root)
            instance.ordinary.mkdir()
            (root / 'source.json').write_text('{}')
            generated = instance.ordinary / 'generated.h'
            generated.write_text('unchanged conversion')
            copied_runner = instance.ordinary / runner.name
            copied_runner.write_text(runner.read_text())
            prior = dict(source_sha256=workflow.digest(root / 'source.json'),
                         hdlcpp_sha256=workflow.digest('/bin/true'), seconds=1,
                         support={str(path.relative_to(fixture)): workflow.digest(path)
                                  for path in (fixture / 'convert.sh', support / 'cva6_params.tsv', runner)},
                         files={path.name: workflow.digest(path) for path in (generated, copied_runner)})
            workflow.save(root / 'ordinary.json', prior)
            runner.write_text('new runtime, same hardware')
            with patch.object(workflow, 'SUPPORT', fixture), \
                    patch.object(instance, 'freeze_tool', return_value=Path('/bin/true')), \
                    patch.object(instance, 'run_command', return_value=dict(seconds=2)) as command:
                instance.convert()
                command.assert_not_called()
                self.assertEqual(copied_runner.read_text(), runner.read_text())
                (support / 'cva6_params.tsv').write_text('changed hardware')
                instance.convert()
                command.assert_called_once()
                self.assertEqual(command.call_args.kwargs['environment']['CPPHDL_CVA6_SKIP_OPTIMIZE'], '1')

    def test_build_thread_counts_and_cache_invalidation(self):
        with tempfile.TemporaryDirectory(prefix='cva6-build-mode-', dir='/tmp') as directory:
            root = Path(directory)
            instance = self.instance(root)
            (root / 'source.json').write_text('{}')
            instance.cpphdl = instance.hdlcpp = '/bin/true'
            signature = ['first']
            commands = []

            def build(command, label, **kwargs):
                commands.append(command)
                output = Path(command[command.index('--output') + 1])
                (output / 'graph').mkdir(parents=True)
                (output / 'run').write_text('binary')
                return dict(seconds=1, peak_rss_bytes=1024)

            with patch.object(instance, 'signature', side_effect=lambda mode: signature[0]), \
                    patch.object(instance, 'run_command', side_effect=build):
                instance.build('cpphdl-native-graph')
                instance.build('cpphdl-native-graph')
                self.assertEqual(len(commands), 1)
                self.assertEqual(commands[0][-2:], ['--threads', '1'])
                instance.build('cpphdl-native-graph-4threads')
                self.assertEqual(commands[-1][-2:], ['--threads', '4'])
                signature[0] = 'changed'
                with self.assertRaises(RuntimeError):
                    instance.ready('cpphdl-native-graph')
                instance.build('cpphdl-native-graph')
                self.assertEqual(len(commands), 3)

    def test_compare_attempts_all_builds_but_never_runs_a_failed_variant(self):
        with tempfile.TemporaryDirectory(prefix='cva6-compare-mode-', dir='/tmp') as directory:
            root = Path(directory)
            instance = self.instance(root)
            built = []
            commands = []

            def build(mode):
                built.append(mode)
                if mode == 'cpphdl':
                    raise RuntimeError('deliberate failure')
                return dict(seconds=1)

            def run(command, label):
                commands.append(command)
                output = Path(command[command.index('--output') + 1])
                output.mkdir(parents=True)
                workflow.save(output / 'comparison.json', dict(equivalent=True, total_cycles=396778,
                    median_work_seconds=dict(verilator=7.8, cpphdl=4.4)))

            output = io.StringIO()
            with patch.object(instance, 'build', side_effect=build), \
                    patch.object(instance, 'ready', return_value=root / 'fake'), \
                    patch.object(instance, 'run_command', side_effect=run), redirect_stdout(output):
                with self.assertRaises(RuntimeError):
                    instance.compare()
            self.assertEqual(built, list(workflow.MODES))
            self.assertEqual(len(commands), 3)
            self.assertIn('--cpphdl-cpus', commands[-1])
            self.assertTrue(all('--check-commits' in command for command in commands))
            self.assertTrue(all('--native-graph' in command for command in commands[-2:]))
            table = output.getvalue().splitlines()
            self.assertIn('FAIL', next(line for line in table if line.startswith('cpphdl ')))
            self.assertFalse(json.loads((root / 'comparison.json').read_text())['passed'])


if __name__ == '__main__':
    unittest.main()
