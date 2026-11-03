"""Discard only provisional native LVS comparison data, retaining both graphs.

KLayout's short native format stores layout/extraction in J, reference in H,
and comparison results in Z. A malformed Z record must not force another
extraction or a lossy SPICE round trip. The replacement has an empty comparison;
it is not LVS evidence until a new complete comparison and audit pass.
"""
import hashlib
from pathlib import Path


def reset_comparison(source, output):
    source, output = Path(source), Path(output)
    prefix = hashlib.sha256()
    original = hashlib.sha256()
    sections = []
    retained = 0
    last = b''
    try:
        with source.open('rb') as reader, output.open('xb') as writer:
            for number, line in enumerate(reader, 1):
                original.update(line)
                if number == 1:
                    if line != b'#%lvsdb-klayout\n':
                        raise ValueError('short native LVS database required')
                elif line[:1] not in (b' ', b'\t', b'\n'):
                    if line == b')\n':
                        pass
                    elif line in (b'J(\n', b'H(\n', b'Z(\n'):
                        sections.append(line[:1])
                        if sections != [b'J', b'H', b'Z'][:len(sections)]:
                            raise ValueError('unexpected native LVS section order')
                    else:
                        raise ValueError('unsupported top-level native LVS record')
                if b'Z' not in sections:
                    writer.write(line)
                    prefix.update(line)
                    retained += len(line)
                last = line
            if sections != [b'J', b'H', b'Z'] or last != b')\n':
                raise ValueError('incomplete native LVS database')
            writer.write(b'Z(\n)\n')
        # Independently reread the actual retained bytes, not the writer buffer.
        with output.open('rb') as reader:
            check = hashlib.sha256()
            remaining = retained
            while remaining:
                chunk = reader.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise ValueError('truncated retained circuit data')
                check.update(chunk)
                remaining -= len(chunk)
            if check.digest() != prefix.digest() or reader.read() != b'Z(\n)\n':
                raise ValueError('native circuit data changed')
        return dict(source_sha256=original.hexdigest(), retained_bytes=retained,
                    retained_sha256=prefix.hexdigest(), comparison_discarded=True,
                    qualified=False)
    except BaseException:
        # Never remove a pre-existing destination on an exclusive-create error.
        if 'writer' in locals():
            output.unlink(missing_ok=True)
        raise
