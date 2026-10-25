"""Exercise the pinned checkout helper without downloading Slang."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
pin = (root / 'hdlcpp/slang-revision.txt').read_text().strip()
assert len(pin) == 40 and all(c in '0123456789abcdef' for c in pin)
with tempfile.TemporaryDirectory(prefix='cpphdl-slang-pin-') as temporary:
    work = Path(temporary)
    checkout = work / 'slang'
    shutil.copy2(root / 'hdlcpp/setup_slang.sh', work / 'setup_slang.sh')

    def run(*args, success=True):
        result = subprocess.run(args, cwd=work, capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout + result.stderr

    run('git', 'init', '-q', str(checkout))
    def git(*args):
        return run('git', '-C', str(checkout), *args).strip()
    git('config', 'user.email', 'test@example.invalid')
    git('config', 'user.name', 'Pin Regression')
    git('config', 'commit.gpgsign', 'false')
    git('config', 'core.hooksPath', '/dev/null')
    (checkout / 'source').write_text('pinned\n')
    git('add', 'source')
    git('commit', '-qm', 'pinned')
    revision = git('rev-parse', 'HEAD')
    (work / 'slang-revision.txt').write_text(revision + '\n')
    (checkout / 'source').write_text('newer\n')
    git('commit', '-qam', 'newer')
    run('bash', str(work / 'setup_slang.sh'))
    assert git('rev-parse', 'HEAD') == revision
    assert git('rev-parse', '--abbrev-ref', 'HEAD') == 'HEAD'
    run('bash', str(work / 'setup_slang.sh'))
    (checkout / 'source').write_text('local edit\n')
    assert 'local changes' in run('bash', str(work / 'setup_slang.sh'), success=False)
    assert (checkout / 'source').read_text() == 'local edit\n'
    (work / 'slang-revision.txt').write_text('master\n')
    assert 'Invalid Slang revision' in run('bash', str(work / 'setup_slang.sh'), success=False)
print('PASS Slang pin: exact detached revision, repeatable setup, dirty checkout protection, no branch pins')
