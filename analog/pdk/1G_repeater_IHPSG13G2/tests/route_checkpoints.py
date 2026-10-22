"""Retain the newest in-progress route and its predecessor, never placement."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import threading

source = Path(__file__).resolve().parents[1] / 'route_checkpoints.py'
spec = importlib.util.spec_from_file_location('snapshot_retention', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as temporary:
    work = Path(temporary)
    directory = work / 'route_snapshots'
    with module.retain_route_snapshots(work):
        placement = directory / 'placed.odb'
        placement.write_bytes(b'placement must survive')
        for i in range(6):
            (directory / f'drt_iter{i}.odb').write_bytes(bytes([i]) * 1000)
        newest = directory / 'drt_iter6.odb'
        with newest.open('wb') as stream:
            stream.write(b'in progress')
            stream.flush()
            for _ in range(40):
                if not (directory / 'drt_iter4.odb').exists():
                    break
                threading.Event().wait(.05)
            assert newest.exists() and (directory / 'drt_iter5.odb').exists()
            assert not (directory / 'drt_iter4.odb').exists()
            stream.write(b' completed')
    report = json.loads((directory / 'retention.json').read_text())
    assert placement.read_bytes() == b'placement must survive'
    assert [r['iteration'] for r in report['retained']] == [5, 6]
    assert [r['iteration'] for r in report['discarded_rebuildable_snapshots']] == list(range(5))
    assert report['retained'][-1]['sha256'] == hashlib.sha256(b'in progress completed').hexdigest()
    assert 'unverified' in report['scope']
print('PASS bounded route snapshots, open latest-file preservation, hashes and placement protection')
