#!/usr/bin/env python3
"""Integer timing contract, independent of the C++ register implementation."""
import argparse
import random

UNIT = 1 << 24


def generate(path):
    rng = random.Random(0xC10C125)
    integral, phase = 0, 0
    state = [UNIT, UNIT, 0, 0, 0, 0]
    count = 0
    jobs = {}
    with open(path, 'w') as output:
        def emit(early=128, middle=128, late=128, advance=1, track=1, reset=0):
            nonlocal integral, phase, state, count
            state[4:] = [0, 0]
            if reset:
                jobs.clear()
                integral = phase = 0
                state = [UNIT, UNIT, 0, 0, 0, 0]
            else:
                if count - 3 in jobs:
                    job = jobs[count - 3]
                    integral = max(-2147484, min(2147484, integral + job['error'])) if job['track'] else integral
                    drift = (abs(integral) // 64) * (-1 if integral < 0 else 1)
                    job['period'] = UNIT - drift
                    job['interval'] = job['period'] - (160 * job['error'] if job['track'] else 0)
                if count - 6 in jobs:
                    job = jobs.pop(count - 6)
                    phase = (phase + job['interval']) % UNIT
                    state = [job['interval'], job['period'], phase, job['error'] % 16384, job['activity'], 1]
                if advance:
                    jobs[count] = dict(error=max(-4096, min(4096, (early - late) * (middle - 128))),
                                       track=track, activity=int(abs(early - late) > 1))
            output.write(' '.join(map(str, [early, middle, late, advance, track, reset] + state)) + '\n')
            count += 1
        emit(reset=1)
        for _ in range(4096):
            emit(255, 255, 0)
        assert integral == 2147484
        for _ in range(4096):
            emit(0, 255, 255)
        assert integral == -2147484
        for _ in range(128):
            emit(255, 255, 0, track=0)
        assert integral == -2147484
        for _ in range(8192):
            emit(*(rng.randrange(256) for _ in range(3)), advance=int(rng.randrange(8) != 0),
                 track=int(rng.randrange(8) != 0), reset=int(rng.randrange(128) == 0))
    print(f'{count} timing-loop states, both frequency clamps, holdover, stalls and reset')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    generate(parser.parse_args().output)
