"""Lossless auditing of ngspice's repeated binary64 time-point records."""
import gzip
import hashlib
import json
import math
from pathlib import Path
import sys


def normalize_roundoff_duplicates(path, *, epsilon_units=16):
    """Drop only equal-time records within 16 eps of the last kept values.

    No time interpolation, ordering repair or electrical tolerance is applied.
    The audit retains every omitted original line, so the original byte stream
    can be reconstructed and its SHA256 verified after waveform compression.
    """
    if type(epsilon_units) is not int or not 1 <= epsilon_units <= 64:
        raise ValueError('roundoff allowance must be an integer from 1 to 64 epsilon units')
    path = Path(path)
    compressed = path.suffix == '.gz'
    base = path.with_suffix('') if compressed else path
    storage = path.resolve() if path.is_symlink() else path
    storage_base = storage.with_suffix('') if compressed else storage
    temporary = storage_base.with_name(storage_base.name + '.normalize.tmp' + ('.gz' if compressed else ''))
    audit_path = base.with_name(base.name + '.roundoff.json')
    original, canonical = hashlib.sha256(), hashlib.sha256()
    duplicates, previous, kept = [], None, 0
    try:
        with (gzip.open(path, 'rb') if compressed else path.open('rb')) as source, \
             (gzip.open(temporary, 'wb', compresslevel=6) if compressed else temporary.open('wb')) as destination:
            header = next(source)
            columns = header.split()
            if not columns or columns[0] != b'time':
                raise ValueError('invalid SPICE trace header')
            original.update(header)
            canonical.update(header)
            destination.write(header)
            for number, line in enumerate(source, 2):
                original.update(line)
                values = [float(v) for v in line.split()]
                if len(values) != len(columns) or not all(math.isfinite(v) for v in values):
                    raise ValueError('nonrectangular/nonfinite SPICE record at line ' + str(number))
                if previous is not None and values[0] <= previous[0]:
                    if values[0] != previous[0] or any(abs(a - b) > epsilon_units * sys.float_info.epsilon * max(1., abs(a), abs(b))
                                                     for a, b in zip(values[1:], previous[1:])):
                        raise ValueError('backward time or changed electrical state at repeated SPICE time')
                    duplicates.append(dict(after_rows=kept, original_line=line.decode('ascii')))
                    continue
                destination.write(line)
                canonical.update(line)
                previous = values
                kept += 1
        audit = dict(version=1, epsilon_units=epsilon_units,
                     rule=f'equal binary64 time; all voltages within {epsilon_units} eps of the last retained state',
                     original_sha256=original.hexdigest(), canonical_sha256=canonical.hexdigest(),
                     retained_rows=kept, duplicate_records=duplicates)
        # Check byte-for-byte reconstruction before replacing the original.
        verify_roundoff_audit(temporary, audit)
        audit_path.write_text(json.dumps(audit, indent=2) + '\n')
        temporary.replace(storage)
        return audit_path
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def verify_roundoff_audit(path, audit):
    path = Path(path)
    if isinstance(audit, (str, Path)):
        audit = json.loads(Path(audit).read_text())
    if audit.get('version') != 1:
        raise ValueError('unsupported SPICE roundoff audit')
    epsilon_units = audit.get('epsilon_units', 16)
    if type(epsilon_units) is not int or not 1 <= epsilon_units <= 64:
        raise ValueError('invalid roundoff allowance in SPICE audit')
    original, canonical = hashlib.sha256(), hashlib.sha256()
    records = audit['duplicate_records']
    index, rows, previous = 0, 0, None
    with (gzip.open(path, 'rb') if path.suffix == '.gz' else path.open('rb')) as source:
        header = next(source)
        original.update(header)
        canonical.update(header)
        columns = len(header.split())

        def restore():
            nonlocal index
            while index < len(records) and records[index]['after_rows'] == rows:
                line = records[index]['original_line'].encode('ascii')
                values = [float(v) for v in line.split()]
                if (previous is None or len(values) != columns or values[0] != previous[0]
                        or not all(math.isfinite(v) for v in values)
                        or any(abs(a - b) > epsilon_units * sys.float_info.epsilon * max(1., abs(a), abs(b))
                               for a, b in zip(values[1:], previous[1:]))):
                    raise ValueError('invalid omitted SPICE roundoff record')
                original.update(line)
                index += 1

        for line in source:
            restore()
            values = [float(v) for v in line.split()]
            if (len(values) != columns or not all(math.isfinite(v) for v in values)
                    or (previous is not None and values[0] <= previous[0])):
                raise ValueError('noncanonical SPICE trace in roundoff audit')
            original.update(line)
            canonical.update(line)
            previous = values
            rows += 1
        restore()
    if (index != len(records) or rows != audit['retained_rows']
            or canonical.hexdigest() != audit['canonical_sha256']
            or original.hexdigest() != audit['original_sha256']):
        raise ValueError('SPICE roundoff audit failed byte reconstruction')
    return len(records)
