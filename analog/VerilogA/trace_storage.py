"""Store complete simulation traces losslessly with verified reconstruction."""
import gzip
import hashlib
from pathlib import Path
import shutil
import os
import threading
import tempfile
import time
from contextlib import contextmanager


def stream_digest(stream):
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b''):
        result.update(block)
    return result.hexdigest()


def exists(path):
    # A dangling link is still previous evidence and must not be overwritten.
    return path.exists() or path.is_symlink()


def storage_file(target):
    """Keep the artifact path stable while optionally placing bytes on scratch."""
    directory = os.environ.get('CPPHDL_ANALOG_TRACE_SCRATCH')
    if not directory:
        return target
    scratch = Path(directory).expanduser().resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix='analog-trace-', suffix='.gz', dir=scratch)
    os.close(descriptor)
    storage = Path(name)
    try:
        target.symlink_to(storage)
        return storage
    except BaseException:
        storage.unlink(missing_ok=True)
        raise


def archive_trace(path):
    path = Path(path)
    target = path.with_name(path.name + '.gz')
    if exists(target):
        raise ValueError('trace archive already exists: ' + str(target))
    original_bytes = path.stat().st_size
    with path.open('rb') as source:
        original_hash = stream_digest(source)
    storage = storage_file(target)
    try:
        with path.open('rb') as source, gzip.open(storage, 'wb', compresslevel=6) as destination:
            shutil.copyfileobj(source, destination)
        with gzip.open(target, 'rb') as reconstructed:
            if stream_digest(reconstructed) != original_hash:
                raise ValueError('trace archive failed original-byte reconstruction')
        with path.open('rb') as source:
            if stream_digest(source) != original_hash:
                raise ValueError('original trace changed during archival')
        with target.open('rb') as source:
            archive_hash = stream_digest(source)
        metadata = dict(original_sha256=original_hash, original_bytes=original_bytes,
            archive_sha256=archive_hash, archive_bytes=target.stat().st_size,
            compression_level=6, storage_path=str(storage.resolve()),
            scope='complete original byte stream; no sample or precision reduction')
        path.unlink()
        return target, metadata
    except BaseException:
        target.unlink(missing_ok=True)
        if storage != target:
            storage.unlink(missing_ok=True)
        raise


class TraceCapture:
    """Receive a complete producer byte stream without a temporary raw copy."""
    def __init__(self, original):
        self.original = Path(original)
        self.path = self.original.with_name(self.original.name + '.gz')
        self.metadata = None
        self.error = None
        self.producer_eof = threading.Event()
        if exists(self.original) or exists(self.path):
            raise ValueError('trace capture would overwrite existing evidence')
        self.original.parent.mkdir(parents=True, exist_ok=True)
        self.storage = storage_file(self.path)
        try:
            os.mkfifo(self.original)
            self.thread = threading.Thread(target=self.receive, daemon=True)
            self.thread.start()
        except BaseException:
            self.original.unlink(missing_ok=True)
            self.path.unlink(missing_ok=True)
            if self.storage != self.path:
                self.storage.unlink(missing_ok=True)
            raise

    def receive(self):
        try:
            original_hash, size = hashlib.sha256(), 0
            with self.original.open('rb') as source, gzip.open(self.storage, 'wb', compresslevel=6) as destination:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    original_hash.update(block)
                    size += len(block)
                    destination.write(block)
            self.producer_eof.set()
            with gzip.open(self.path, 'rb') as reconstructed:
                if stream_digest(reconstructed) != original_hash.hexdigest():
                    raise ValueError('stream capture failed original-byte reconstruction')
            with self.path.open('rb') as archive:
                archive_hash = stream_digest(archive)
            self.metadata = dict(original_sha256=original_hash.hexdigest(), original_bytes=size,
                archive_sha256=archive_hash, archive_bytes=self.path.stat().st_size,
                compression_level=6, storage_path=str(self.storage.resolve()),
                scope='complete producer byte stream through a FIFO; verified decompression; no sample or precision reduction')
        except BaseException as error:
            self.error = error

    def finish(self):
        # A failed producer may never open its output. An empty temporary
        # writer releases the reader without inventing any data.
        deadline = time.monotonic() + 5
        while self.thread.is_alive() and not self.producer_eof.is_set() and time.monotonic() < deadline:
            try:
                descriptor = os.open(self.original, os.O_WRONLY | os.O_NONBLOCK)
                os.close(descriptor)
                break
            except OSError:
                time.sleep(.01)
        # Verification reads the complete archive again and may legitimately
        # take longer than a small fixed join timeout. The surrounding runner
        # owns the job deadline; finishing must wait for its verifier.
        self.thread.join()
        self.original.unlink(missing_ok=True)
        if self.error is not None:
            raise self.error


@contextmanager
def capture_trace(path):
    """The producer must exit before leaving this context, including on failure."""
    capture = TraceCapture(path)
    try:
        yield capture
    finally:
        capture.finish()
