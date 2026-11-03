"""Bind an optional local allocator to the receiver's OpenROAD processes."""
from pathlib import Path

CONFIGURATION = 'narenas:1,dirty_decay_ms:0,muzzy_decay_ms:0,retain:false,abort_conf:true'


def configuration(library):
    if library is None:
        return {}, dict(implementation='system'), []
    path = Path(library).resolve(strict=True)
    # LD_PRELOAD separates filenames on whitespace and colons, without escaping.
    if any(c.isspace() or c == ':' for c in str(path)):
        raise ValueError('allocator path cannot contain whitespace or colons')
    return (dict(LD_PRELOAD=str(path), MALLOC_CONF=CONFIGURATION),
            dict(implementation='jemalloc', library=str(path), configuration=CONFIGURATION), [path])


def environment(base, selected):
    result = dict(base)
    # Only the explicitly selected, hashed library/configuration may be loaded.
    result.pop('LD_PRELOAD', None)
    result.pop('MALLOC_CONF', None)
    result.update(selected)
    return result
