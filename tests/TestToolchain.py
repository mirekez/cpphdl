"""Compiler/runtime selection shared by standalone Python regressions."""
import os
from pathlib import Path
import shlex
import subprocess


class TestToolchain:
    def __init__(self, cxx, verilator, work):
        self.cxx = str(cxx)
        self.verilator = str(verilator) if verilator else None
        self.work = Path(work)
        self.env = os.environ.copy()
        self._sanitizers = None
        library = subprocess.check_output(
            [self.cxx, '-print-file-name=libstdc++.so'], text=True).strip()
        self.library_dir = str(Path(library).resolve().parent) if Path(library).is_file() else ''
        self.library_dirs = list(dict.fromkeys(filter(None, [self.library_dir,
            str(Path(self.cxx).resolve().parent.parent / 'lib')])))
        if self.library_dir:
            self.env['LD_LIBRARY_PATH'] = ':'.join(self.library_dirs) + ':' + self.env.get('LD_LIBRARY_PATH', '')

    def probe(self, flags, name):
        executable = self.work / ('probe-' + name)
        result = subprocess.run([self.cxx, '-std=c++17', '-x', 'c++', '-', *flags,
                                 '-o', str(executable)], input='int main() { return 0; }\n',
                                env=self.env, text=True, capture_output=True, timeout=60)
        if result.returncode == 0:
            result = subprocess.run([str(executable)], env=self.env, text=True,
                                    capture_output=True, timeout=30)
        (self.work / ('probe-' + name + '.log')).write_text(result.stdout + result.stderr)
        executable.unlink(missing_ok=True)
        return result

    def sanitizers(self):
        if self._sanitizers is not None:
            return self._sanitizers
        requested = os.environ.get('CPPHDL_TEST_SANITIZERS', 'auto')
        if requested == 'none':
            self._sanitizers = []
            print('WARNING: sanitizers explicitly disabled by CPPHDL_TEST_SANITIZERS=none', flush=True)
            return self._sanitizers
        selected = 'address,undefined' if requested == 'auto' else requested
        flags = ['-fsanitize=' + selected, '-fno-sanitize-recover=all']
        result = self.probe(flags, 'sanitizers')
        diagnostic = result.stdout + result.stderr
        if (result.returncode and requested == 'auto' and
                'RTLD_DEEPBIND' in diagnostic and 'incompatible with sanitizer runtime' in diagnostic):
            print('WARNING: ASan startup conflicts with an injected RTLD_DEEPBIND library; '
                  'running this regression with UBSan only. See probe-sanitizers.log. '
                  'Set CPPHDL_TEST_SANITIZERS=address,undefined to require ASan.', flush=True)
            flags = ['-fsanitize=undefined', '-fno-sanitize-recover=all']
            result = self.probe(flags, 'ubsan')
        if result.returncode:
            raise RuntimeError('Sanitizer startup probe failed:\n' + result.stdout + result.stderr)
        self._sanitizers = flags
        return flags

    def command(self, command):
        command = list(map(str, command))
        if '-fsanitize=address,undefined' in command:
            position = command.index('-fsanitize=address,undefined')
            command[position:position + 1] = self.sanitizers()
        if command[0] == self.verilator and ('--build' in command or '--binary' in command):
            compiler = shlex.quote(self.cxx)
            command += ['-MAKEFLAGS', f'CXX={compiler} LINK={compiler} AR=ar']
            command += ['-LDFLAGS', ' '.join('-L' + shlex.quote(d) for d in self.library_dirs)]
            if ('--timing' in command or '--binary' in command) and not any('-std=' in arg for arg in command):
                command += ['-CFLAGS', '-std=c++20']
        return command
