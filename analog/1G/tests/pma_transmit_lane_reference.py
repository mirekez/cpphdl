"""Check the registered connection from PCS/test symbols to both current banks."""
import random
from pma_test_pattern_reference import generate as pattern_vectors


def generate(path):
    pattern_path = path.with_name('pattern-reference.txt')
    pattern_vectors(pattern_path)
    rows = [list(map(int, line.split())) for line in pattern_path.read_text().splitlines()]
    rng = random.Random(0x1000BA5E)
    # The pattern fixture already checks all mode boundaries. Add normal
    # traffic, every adjacent symbol pair, invalid symbols and restart recovery.
    rows += [[1, 1, 0, 1, 0, 2, 0, 0, 0]]
    normal = []
    for first in range(5):
        for second in range(5):
            normal.extend([first, second])
    normal += [rng.randrange(5) for _ in range(5000)]
    for invalid in (5, 6, 7):
        normal += [invalid, 2, 4, 0]
    rows += [[0, 1, 0, 1, 0, 2, 0, 0, 0] for _ in normal]
    rows += [[0, 1, 0, 1, 1, 2, 0, 0, 0], [0, 1, 0, 1, 0, 2, 0, 0, 0]]
    normal_start = len(rows) - len(normal) - 2
    previous_mode, previous_symbol, driver_fault = 0, 2, False
    old_pattern = (2, 0, 0, 0)
    output = []
    for index, row in enumerate(rows):
        reset, enable, mode, seed, restart, *pattern = row
        symbol = normal[index - normal_start] if normal_start <= index < normal_start + len(normal) else rng.randrange(5)
        boundary = restart or mode != previous_mode
        clear = reset or not enable or boundary
        accepted = enable and (mode == 0 or old_pattern[1]) and not driver_fault
        code = symbol if mode == 0 else old_pattern[0]
        if clear:
            banks, valid, driver_fault, previous_symbol = [0] * 4, 0, False, 2
        elif accepted and code <= 4:
            # Physical P-side currents decrease as the logical symbol rises.
            banks = [sum(1 << bit for bit in range(4) if bit < 4 - code),
                     sum(1 << bit for bit in range(4) if bit >= 4 - code),
                     sum(1 << bit for bit in range(4) if bit < 4 - previous_symbol),
                     sum(1 << bit for bit in range(4) if bit >= 4 - previous_symbol)]
            valid, previous_symbol = 1, code
        else:
            banks, valid, previous_symbol = [0] * 4, 0, 2
            if enable and mode == 0 and code > 4:
                driver_fault = True
        previous_mode = 0 if reset or not enable else mode
        fault = driver_fault or (mode != 0 and pattern[3])
        slave = mode == 3 and pattern[2]
        output.append([reset, enable, symbol, mode, seed, restart, *banks, valid, int(fault), int(slave)])
        old_pattern = pattern
    path.write_text(''.join(' '.join(map(str, row)) + '\n' for row in output))
    return len(output)
