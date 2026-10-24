#!/usr/bin/env python3
"""Fixed channel stimuli and independent Ethernet truth for DSP -> pair -> PCS.

Targets are supplied only during supervised adaptation. This verifies the
datapath interface, not autonomous recovery of training targets from a cable.
"""
import argparse
import json
import math
from pathlib import Path
import random
import zlib

from transmit_reference import Reference


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    summaries = []
    for case in range(4):
        master, impaired = case // 2, case % 2
        rng = random.Random(0x445350 + case)
        remote, local = (Reference(Path(__file__).resolve().parent) for _ in range(2))
        remote.reset(rng.randrange(1, 1 << 33))
        local.reset(rng.randrange(1, 1 << 33))
        permutation = [2, 0, 3, 1] if impaired else [0, 1, 2, 3]
        delays = [8, 3, 0, 5] if impaired else [0] * 4
        signs = 0b1001 if impaired else 0
        source, frames, samples = [], [], []
        previous, previous_local = [0] * 4, [0] * 4
        train_count = 32768

        def emit(data=0, enable=0, mode=1):
            nonlocal previous, previous_local
            n = len(source)
            symbol = remote.step(0, 0, 0, master, mode, 1, 1, enable, 0, data)[-1]
            # An independent local transmitter remains active during receive.
            local_symbol = local.step(0, 0, 0, not master, 2, 1, 1,
                                      n % 2048 >= 32, 0, rng.randrange(256))[-1]
            source.append([(symbol >> (3 * i) & 7) - 2 for i in range(4)])
            tx = [(local_symbol >> (3 * i) & 7) - 2 for i in range(4)]
            physical = [(source[n - delays[p]][permutation[p]] if n >= delays[p] else 0) *
                        (-1 if signs & (1 << p) else 1) for p in range(4)]
            word = 0
            for p in range(4):
                value = .75 * physical[p] + .25 * previous[p]
                if impaired:
                    value += sum((.30 if p == j else .045) * (.75 * tx[j] + .25 * previous_local[j])
                                 for j in range(4))
                    value += rng.uniform(-.01, .01)
                adc = math.floor(128 + 32 * value + .5)
                if not 0 < adc < 255:
                    raise ValueError('fixture clipped before the receiver')
                word |= adc << (8 * p)
            train = n < train_count
            desired = sum((v + 2) << (3 * p) for p, v in enumerate(physical)) if train else 4095
            samples.append((word, local_symbol, desired, (65520 + n) & 65535, int(train)))
            previous, previous_local = physical, tx

        for _ in range(train_count + 6000):
            emit()
        for _ in range(512):
            emit(mode=2)
        for length in (64, 1518):
            payload = bytes(rng.randrange(256) for _ in range(length - 4))
            frame = b'\x55' * 7 + b'\xd5' + payload + zlib.crc32(payload).to_bytes(4, 'little')
            frames.append(list(frame))
            for byte in frame:
                emit(byte, 1, 2)
            for _ in range(24):
                emit(mode=2)
        for _ in range(512):
            emit(mode=2)
        mapping = sum(permutation.index(logical) << (2 * logical) for logical in range(4))
        a_delay = delays[permutation.index(0)]
        taps = sum((8 + a_delay - delays[permutation.index(logical)]) << (5 * logical)
                   for logical in range(4))
        with (directory / f'case{case}.txt').open('w') as output:
            output.write(f'{master} {len(samples)} {mapping} {taps} {len(frames)}\n')
            for row in samples:
                output.write(' '.join(map(str, row)) + '\n')
            for frame in frames:
                output.write(str(len(frame)) + ' ' + ' '.join(map(str, frame)) + '\n')
        summaries.append(dict(case=case, master=master, impaired=impaired, symbols=len(samples),
                              train_symbols=train_count, frames=len(frames), bytes=sum(map(len, frames)),
                              physical_to_logical=permutation, polarity_mask=signs, delay_symbols=delays))
    (directory / 'cases.json').write_text(json.dumps(summaries, indent=2) + '\n')
    return summaries


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    print(generate(parser.parse_args().directory))
