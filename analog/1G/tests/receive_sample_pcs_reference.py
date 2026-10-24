#!/usr/bin/env python3
"""Independent IEEE-coded symbols and Ethernet frame truth for sampled PCS."""
import argparse
from pathlib import Path
import random
import zlib
from transmit_reference import Reference


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for master in (0, 1):
        randomizer = random.Random(0x504353 + master)
        oracle = Reference(Path(__file__).resolve().parent)
        oracle.reset(randomizer.randrange(1, 1 << 33))
        symbols, frames = [], []

        def emit(data=0, enable=0):
            *_, packed = oracle.step(0, 0, 0, master, 2, 1, 1, enable, 0, data)
            symbols.append(packed)

        for _ in range(20000):
            emit()
        for length in (64, 1518):
            payload = bytes(randomizer.randrange(256) for _ in range(length - 4))
            frame = b'\x55' * 7 + b'\xd5' + payload + zlib.crc32(payload).to_bytes(4, 'little')
            frames.append(frame)
            for byte in frame:
                emit(byte, 1)
            for _ in range(24):
                emit()
        for _ in range(512):
            emit()
        with (directory / f'role{master}.txt').open('w') as output:
            output.write(f'{master} {len(symbols)} {len(frames)}\n')
            output.write(' '.join(map(str, symbols)) + '\n')
            for frame in frames:
                output.write(str(len(frame)) + ' ' + ' '.join(map(str, frame)) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    generate(parser.parse_args().directory)
