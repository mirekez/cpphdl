"""Bound disk use of OpenROAD's sequential per-iteration routing snapshots."""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import threading


@contextmanager
def retain_route_snapshots(work):
    directory = Path(work) / 'route_snapshots'
    directory.mkdir(exist_ok=True)
    stopped = threading.Event()
    errors, discarded = [], []

    def candidates():
        return sorted((int(match[1]), path) for path in directory.glob('drt_iter*.odb')
                      if (match := re.fullmatch(r'drt_iter(\d+)\.odb', path.name)))

    def digest(path):
        result = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                result.update(block)
        return result.hexdigest()

    def sweep(final=False):
        files = candidates()
        # The router writes these in sequence. Once two newer snapshots exist,
        # an older file is closed; the newest file may still be in progress.
        for iteration, path in files[:-2]:
            discarded.append(dict(iteration=iteration, bytes=path.stat().st_size, sha256=digest(path)))
            path.unlink()
        report = dict(scope='unverified route candidates; placement checkpoint and final checks remain required',
                      discarded_rebuildable_snapshots=discarded,
                      retained=[dict(iteration=i, path=str(path), bytes=path.stat().st_size,
                                     **(dict(sha256=digest(path)) if final else {})) for i, path in files[-2:]])
        temporary = directory / 'retention.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(directory / 'retention.json')

    def observe():
        while not stopped.wait(.5):
            try:
                sweep()
            except Exception as error:
                errors.append(error)
                return

    thread = threading.Thread(target=observe, name='route-snapshot-retention', daemon=True)
    thread.start()
    try:
        yield
    finally:
        stopped.set()
        thread.join()
        sweep(final=True)
        if errors:
            raise RuntimeError('routing checkpoint retention failed') from errors[0]
