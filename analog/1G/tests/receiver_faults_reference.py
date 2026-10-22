#!/usr/bin/env python3
"""Receiver fault injection at the encoded MDI boundary, independent of the DUT."""
import argparse
from collections import deque
from pathlib import Path
import random
import zlib
from transmit_reference import Reference
from receiver_reference import LATENCY


def generate(path):
    encoder = Reference(Path(__file__).resolve().parent)
    randomizer = random.Random(0xBAD55D)
    count = 0
    with path.open("w") as output:
        output.write("# reset master pma samples expected mask require_lock error_window\n")
        for master in (0, 1):
            encoder.reset(0x123456789)
            pending = deque([(0, 1023)] * LATENCY)
            def emit(data=0, enable=0, raw=None, expected=None, mask=1023, reset=0, pma=1, lock=0, window=0):
                nonlocal count
                *_, packed = encoder.step(0, 0, 0, master, 2, 1, 1, enable, 0, data)
                if raw is not None:
                    packed = raw
                samples = sum((128 + 32 * (((packed >> (3 * lane)) & 7) - 2)) << (8 * lane)
                              for lane in range(4))
                if reset:
                    pending.clear()
                    pending.extend([(0, 1023)] * LATENCY)
                want, compare_mask = pending.popleft()
                pending.append((256 | data if enable else 0, mask) if expected is None else (expected, mask))
                if not pma:
                    want, compare_mask = 512, 1023
                output.write(f"{reset} {master} {pma} {samples} {want} {compare_mask} {lock} {window}\n")
                count += 1
            def idle(length, **kwargs):
                for _ in range(length):
                    emit(**kwargs)
            def frame(**kwargs):
                body = bytes(randomizer.randrange(256) for _ in range(60))
                values = b'\x55' * 7 + b'\xd5' + body + zlib.crc32(body).to_bytes(4, 'little')
                for index, value in enumerate(values):
                    emit(value, 1, **kwargs)
            emit(reset=1)
            idle(192)
            frame(lock=1)
            idle(64, lock=1)
            # Corrupt SSD2: neither delimiter may be reported as valid GMII.
            for index in range(72):
                emit(0x55, 1, raw=1170 if index == 1 else None, expected=0, mask=256, window=1)
            idle(224, expected=0, mask=256, window=1)
            frame(lock=1)
            idle(64, lock=1)
            # Corrupt first CSReset. A premature end must produce an error,
            # then recover; only the undecided tail is exempt from byte checks.
            for index in range(72):
                emit(0x55, 1, mask=1023 if index < 40 else 0, window=2)
            emit(raw=1170, mask=0, window=2)
            idle(40, mask=0, window=2)
            idle(224, expected=0, mask=256, window=2)
            frame(lock=1)
            idle(64, lock=1)
            # Unannounced remote scrambler restart: no receiver seed is loaded.
            encoder.reset(0x1DEADBEEF)
            idle(288, expected=0, mask=256, window=3)
            frame(lock=1)
            idle(64, lock=1)
            # PMA failure overrides buffered data immediately, then reacquires.
            for index in range(72):
                emit(0x55, 1, pma=int(not 40 <= index < 44), mask=0, window=4)
            idle(256, expected=0, mask=256, window=4)
            frame(lock=1)
            idle(64, lock=1)
    print(f"{count} malformed-SSD/ESD, seed-restart, PMA-loss and recovery vectors")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
