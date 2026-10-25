"""Exercise elaborator runtime selection without requiring an old host libstdc++."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--converter', type=Path, required=True)
    parser.add_argument('--root-driver', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='veriloga runtime ') as temporary:
        root = Path(temporary).resolve()
        library = root / 'selected runtime' / 'libstdc++.so.6'
        library.parent.mkdir()
        library.touch()
        alias = root / 'runtime alias'
        alias.symlink_to(library.parent, target_is_directory=True)
        compiler = root / 'compiler with spaces'
        harness = ('#!' + sys.executable + '\nimport os\nfrom pathlib import Path\n'
                   + 'assert os.environ["LD_LIBRARY_PATH"] == '
                   + repr(str(library.parent) + ':/inherited/runtime') + '\n'
                   + 'Path(__file__).with_name("module.va").write_text("module RuntimeProbe; endmodule\\n")\n')
        compiler.write_text(f'''#!{sys.executable}
import os
from pathlib import Path
import sys
if sys.argv[1].startswith('-print-file-name='):
    name = sys.argv[1].split('=', 1)[1]
    print({str(alias / library.name)!r} if name == 'libstdc++.so' else name)
else:
    output = Path(sys.argv[sys.argv.index('-o') + 1])
    output.write_text({harness!r})
    output.chmod(0o755)
''')
        compiler.chmod(0o755)
        source = root / 'source.hpp'
        source.touch()
        output = root / 'out.va'
        mode = ['--verilog-a'] if args.root_driver else []
        env = dict(os.environ, LD_LIBRARY_PATH='/inherited/runtime')
        command = [str(args.converter), *mode, str(source), '--top', 'Probe', '--ports', 'A',
                   '--cxx', str(compiler), '--output', str(output)]
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        assert output.read_text() == 'module RuntimeProbe; endmodule\n'
        assert env['LD_LIBRARY_PATH'] == '/inherited/runtime'
        print('PASS selected compiler runtime, symlinks, spaces and inherited library path')


if __name__ == '__main__':
    main()
