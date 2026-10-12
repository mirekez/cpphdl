"""Small, real Git repositories for offline script integration tests."""
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True,
                                   stderr=subprocess.PIPE).strip()


def commit(repo):
    git(repo, 'add', '.')
    git(repo, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
        'commit', '-qm', 'fixture')
    return git(repo, 'rev-parse', 'HEAD')


def configure_product(product, checkout, circt=None):
    product.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'check_versions.py', product / 'check_versions.py')
    try:
        revision = git(checkout, 'rev-parse', 'HEAD')
    except subprocess.CalledProcessError:
        try:
            git(checkout, 'rev-parse', '--show-toplevel')
        except subprocess.CalledProcessError:
            subprocess.run(['git', '-C', str(checkout), 'init', '-q'], check=True)
        revision = commit(checkout)
    lock = {'chipyard': {'commit': revision, 'required_submodules': [], 'overrides': {}},
            'circt': {'commit': git(circt, 'rev-parse', 'HEAD') if circt else 'a' * 40,
                      'sdk_version': 'firtool-1.75.0'}}
    (product / 'revisions.json').write_text(json.dumps(lock))
    return lock
