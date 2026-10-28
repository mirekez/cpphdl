"""Reserve real disk blocks before starting an expensive waveform producer."""
from contextlib import contextmanager
import errno
import gzip
import hashlib
import os
import threading

from trace_storage import TraceCapture, stream_digest


class ReservedCapture(TraceCapture):
    def __init__(self, original, capacity):
        if type(capacity) is not int or capacity <= 0:
            raise ValueError('positive compressed trace capacity required')
        self.capacity = capacity
        self.reservation_ready = threading.Event()
        super().__init__(original)
        self.reservation_ready.wait()
        if self.error is not None:
            self.thread.join()
            self.original.unlink(missing_ok=True)
            self.path.unlink(missing_ok=True)
            if self.storage != self.path:
                self.storage.unlink(missing_ok=True)
            raise self.error

    def receive(self):
        try:
            original_hash, size = hashlib.sha256(), 0
            # GzipFile must use this open file: gzip.open(..., 'wb') would
            # truncate it and release the reservation before output begins.
            with self.storage.open('w+b') as raw:
                os.posix_fallocate(raw.fileno(), 0, self.capacity)
                self.reservation_ready.set()

                class BoundedOutput:
                    def write(output, data):
                        if raw.tell() + len(data) > self.capacity:
                            raise OSError(errno.ENOSPC, 'compressed trace exceeded its reserved capacity')
                        return raw.write(data)

                    def flush(output):
                        raw.flush()

                try:
                    with self.original.open('rb') as source, gzip.GzipFile(
                            fileobj=BoundedOutput(), mode='wb', compresslevel=6, mtime=0) as destination:
                        for block in iter(lambda: source.read(1024 * 1024), b''):
                            original_hash.update(block)
                            size += len(block)
                            destination.write(block)
                finally:
                    # Retain only actual bytes, including an incomplete stream
                    # on failure; unused reserved blocks are always released.
                    raw.truncate(raw.tell())
            self.producer_eof.set()
            with gzip.open(self.path, 'rb') as reconstructed:
                if stream_digest(reconstructed) != original_hash.hexdigest():
                    raise ValueError('reserved trace failed original-byte reconstruction')
            with self.path.open('rb') as archive:
                archive_hash = stream_digest(archive)
            self.metadata = dict(original_sha256=original_hash.hexdigest(), original_bytes=size,
                archive_sha256=archive_hash, archive_bytes=self.path.stat().st_size,
                compression_level=6, storage_path=str(self.storage.resolve()),
                reserved_capacity_bytes=self.capacity,
                scope='complete producer stream with preallocated disk blocks and verified reconstruction; no sample reduction')
        except BaseException as error:
            self.error = error
        finally:
            self.reservation_ready.set()


@contextmanager
def capture_reserved_trace(path, capacity):
    capture = ReservedCapture(path, capacity)
    try:
        yield capture
    finally:
        capture.finish()
