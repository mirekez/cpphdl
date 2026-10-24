#!/usr/bin/env python3
"""Independent integer dot-product/NLMS oracle and noisy-channel contracts."""
from collections import deque
import argparse
import math
import random


def truncate(numerator, denominator):
    return (1 if numerator >= 0 else -1) * (abs(numerator) // denominator)


class FilterReference:
    def __init__(self):
        self.reset()

    def reset(self):
        self.weights = [21845, -7282, 2427, -809] + [0] * 20
        self.receive = deque([0] * 4, maxlen=4)
        self.local = [deque([0] * 5, maxlen=5) for _ in range(4)]
        self.output, self.valid, self.fault = 128, 0, 0

    def step(self, sample, local, desired, training, advance, reset):
        self.valid = 0
        if reset:
            self.reset()
        elif advance and not self.fault:
            codes = [(local >> (lane * 3)) & 7 for lane in range(4)]
            self.receive.appendleft(sample - 128)
            for history, code in zip(self.local, codes):
                history.appendleft((2 - code) * 32)
            features = list(self.receive) + [x for history in self.local for x in history]
            filtered = truncate(sum(w * x for w, x in zip(self.weights, features)), 16384)
            self.output = max(0, min(255, filtered + 128))
            self.valid = 1
            if training:
                error = max(-255, min(255, (desired - 2) * 32 - filtered))
                energy = max(1, sum(x * x for x in features))
                normalization = 2 ** (energy - 1).bit_length()
                self.weights = [max(-131071, min(131071, w + truncate(16384 * error * x, normalization)))
                                for w, x in zip(self.weights, features)]
            if any(code > 4 for code in codes) or (training and desired > 4):
                self.output, self.valid, self.fault = 128, 0, 1
        return [self.output, self.valid, self.fault] + [w & 262143 for w in self.weights]


def generate(path):
    rng = random.Random(0x1000BA5E)
    oracle = FilterReference()
    count = 0
    scored = errors = raw_errors = 0
    squared = 0
    with open(path, 'w') as output:
        def emit(sample=128, local=1170, desired=2, training=0, advance=1, reset=0):
            nonlocal count
            inputs = [sample, local, desired, training, advance, reset]
            expected = oracle.step(*inputs)
            output.write(' '.join(str(int(value)) for value in inputs + expected) + '\n')
            count += 1
            return expected

        # Cold/reset, all 625 legal four-pair references, held state and rails.
        emit(reset=1)
        for code in range(625):
            values = [(code // (5 ** lane)) % 5 for lane in range(4)]
            local = sum(x << (3 * lane) for lane, x in enumerate(values))
            emit((code * 193) & 255, local, code % 5, code % 2, code % 7 != 0)
        # Independently synthesize an ISI/echo/NEXT channel. The DUT sees only
        # quantized samples, local TX and a training target during acquisition.
        emit(reset=1)
        remote_previous = 0
        local_previous = [0] * 4
        frozen = None
        for index in range(4096):
            remote = rng.randrange(-2, 3)
            local = [rng.randrange(-2, 3) for _ in range(4)]
            voltage = .75 * remote + .25 * remote_previous
            voltage += sum((.35 if lane == 0 else .055) * (.75 * x + .25 * previous)
                           for lane, (x, previous) in enumerate(zip(local, local_previous)))
            voltage += rng.uniform(-.012, .012)
            sample = max(0, min(255, math.floor(voltage * 32 + .5) + 128))
            packed = sum((x + 2) << (3 * lane) for lane, x in enumerate(local))
            expected = emit(sample, packed, remote + 2 if index < 2048 else 7, index < 2048)
            if index == 2047:
                frozen = oracle.weights.copy()
            if index >= 2056:
                assert oracle.weights == frozen, 'holdout modified coefficients'
                decided = max(-2, min(2, math.floor((expected[0] - 128) / 32 + .5)))
                errors += decided != remote
                raw_errors += max(-2, min(2, math.floor(voltage + .5))) != remote
                squared += ((expected[0] - 128) / 32 - remote) ** 2
                scored += 1
            remote_previous, local_previous = remote, local
        assert errors == 0 and squared / scored < .004, (errors, squared / scored)
        assert raw_errors > 400, raw_errors
        # Sticky invalid-code rejection, explicit recovery, invalid desired
        # ignored in holdout, and stalled cycles must retain every coefficient.
        for lane in range(4):
            emit(reset=1)
            emit(local=1170 | (7 << (3 * lane)))
            for _ in range(7):
                emit(rng.randrange(256))
        emit(reset=1)
        emit(desired=5, training=1)
        emit()
        emit(reset=1)
        for _ in range(256):
            emit(rng.choice([0, 255]), rng.choice([0, 2340]), rng.randrange(5), 1)
        for _ in range(40):
            emit(0, 4095, 7, 1, 0)
    print(f'{count} fixed-point vectors; holdout {scored}, errors {errors}, raw errors {raw_errors}, MSE {squared / scored:.6g}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    generate(parser.parse_args().output)
