"""Independent symbol lists and bit-array recurrence for Clause 40 test modes."""
from itertools import cycle, islice
from pathlib import Path
import sys

MODE1 = [value + 2 for peak in (2, -2, 1, -1) for value in [peak, *([0] * 127)]]
MODE1 += [value + 2 for peak in (2, -2, 2, -2) for value in [peak] * 128]
MODE1 += [2] * 1024
MAP4 = (2, 3, 4, 1, 2, 3, 0, 1)


def mode4(seed):
    bits = [(seed >> bit) & 1 for bit in range(11)]
    visited, result = set(), []
    while tuple(bits) not in visited:
        visited.add(tuple(bits))
        index = bits[0] + 2 * (bits[1] ^ bits[4]) + 4 * (bits[2] ^ bits[4])
        result.append(MAP4[index])
        bits = [bits[10] ^ bits[8], *bits[:10]]
    if len(visited) != 2047 or bits != [(seed >> bit) & 1 for bit in range(11)]:
        raise ValueError('reference scrambler is not a complete nonzero cycle')
    return result


def generate(path):
    rows = []
    def emit(reset, enabled, mode, seed, restart, expected):
        rows.append([reset, enabled, mode, seed, restart, *expected])
    def quiet(reset=1, enabled=1, mode=0, seed=0, fault=0):
        emit(reset, enabled, mode, seed, 0, (2, 0, 0, fault))
    def run(mode, count, seed=2047, restart=False):
        pattern = MODE1 if mode == 1 else (4, 0) if mode in (2, 3) else mode4(seed)
        for index, symbol in enumerate(islice(cycle(pattern), count)):
            emit(0, 1, mode, seed, int(restart and index == 0), (symbol, 1, int(mode == 3), 0))
    if len(MODE1) != 2048:
        raise ValueError('incomplete waveform reference')
    for mode in range(8):
        quiet(reset=1, mode=mode)
        quiet(reset=0, enabled=0, mode=mode)
    run(1, 3 * 2048 + 23)
    for offset in (0, 1, 127, 128, 511, 512, 639, 768, 1023, 2047):
        quiet()
        run(1, offset)
        run(1, 2051, restart=True)
    run(2, 4099)
    run(3, 4099)
    run(2, 17)
    for seed in (1, 2047, 0x155, 0x400):
        run(4, 2 * 2047 + 19, seed, restart=True)
    # Changing a seed without restarting must not perturb an active recurrence.
    quiet()
    stream = cycle(mode4(1))
    for index in range(3000):
        emit(0, 1, 4, 1 if index < 100 else 0, 0, (next(stream), 1, 0, 0))
    quiet()
    quiet(reset=0, mode=4, seed=0, fault=1)
    quiet(reset=0, mode=4, seed=1, fault=1)
    run(4, 2047, seed=1, restart=True)
    for invalid in (5, 6, 7):
        quiet(reset=0, mode=invalid, fault=1)
        quiet(reset=0, mode=invalid, fault=1)
        run(1, 2051)
    quiet(reset=0, mode=0)
    run(1, 2051)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(''.join(' '.join(map(str, row)) + '\n' for row in rows))
    return len(rows)


if __name__ == '__main__':
    print(generate(sys.argv[1]), 'independent test-pattern clocks')
