#!/usr/bin/env python3
"""Transaction-scheduled oracle: no hardware adder or pipeline register model."""
from collections import deque
import argparse
import math
import random
from filter_reference import truncate


class Reference:
    def __init__(self):
        self.clock = 0
        self.reset()

    def reset(self):
        self.weights = [21845, -7282, 2427, -809] + [0] * 20
        self.receive = deque([0] * 4, maxlen=4)
        self.local = [deque([0] * 5, maxlen=5) for _ in range(4)]
        self.jobs = {}
        self.output, self.valid, self.fault = 128, 0, 0

    def step(self, sample, local, desired, training, advance, reset):
        t = self.clock
        self.clock += 1
        self.valid = 0
        codes = [(local >> (lane * 3)) & 7 for lane in range(4)]
        invalid = advance and (any(code > 4 for code in codes) or (training and desired > 4))
        if reset:
            self.reset()
        elif invalid or self.fault:
            self.jobs.clear()
            self.output, self.valid, self.fault = 128, 0, 1
        else:
            # Capture weights before any correction committed by this edge.
            if t - 1 in self.jobs:
                job = self.jobs[t - 1]
                job['sum'] = sum(w * x for w, x in zip(self.weights, job['features']))
            if t - 6 in self.jobs:
                job = self.jobs[t - 6]
                value = truncate(job['sum'], 16384)
                self.output, self.valid = max(0, min(255, value + 128)), 1
                job['error'] = max(-255, min(255, job['target'] * 32 - value))
            if t - 10 in self.jobs:
                job = self.jobs.pop(t - 10)
                if job['training']:
                    energy = max(1, sum(x * x for x in job['features']))
                    denominator = 2 ** (energy - 1).bit_length()
                    self.weights = [max(-131071, min(131071, w + truncate(1024 * job['error'] * x, denominator)))
                                    for w, x in zip(self.weights, job['features'])]
            if advance:
                self.receive.appendleft(sample - 128)
                for history, code in zip(self.local, codes):
                    history.appendleft((2 - code) * 32)
                self.jobs[t] = dict(features=list(self.receive) + [x for h in self.local for x in h],
                                    target=desired - 2, training=bool(training))
        return [self.output, self.valid, self.fault] + [w & 262143 for w in self.weights]


def generate(path):
    rng = random.Random(0x125BA5E)
    oracle = Reference()
    count = errors = raw_errors = scored = 0
    squared = 0
    with open(path, 'w') as output:
        def emit(sample=128, local=1170, desired=2, training=0, advance=1, reset=0):
            nonlocal count
            inputs = [sample, local, desired, training, advance, reset]
            expected = oracle.step(*inputs)
            output.write(' '.join(str(int(x)) for x in inputs + expected) + '\n')
            count += 1
            return expected

        emit(reset=1)
        for _ in range(625):
            codes = [rng.randrange(5) for _ in range(4)]
            emit(rng.randrange(256), sum(x << (3 * i) for i, x in enumerate(codes)), rng.randrange(5),
                 rng.randrange(2), rng.randrange(7) != 0)
        emit(reset=1)
        previous_remote = 0
        previous_local = [0] * 4
        wanted = deque([None] * 6)
        frozen = None
        for index in range(32768 + 4096):
            remote = rng.randrange(-2, 3)
            local = [rng.randrange(-2, 3) for _ in range(4)]
            voltage = .75 * remote + .25 * previous_remote
            voltage += sum((.35 if lane == 0 else .055) * (.75 * x + .25 * old)
                           for lane, (x, old) in enumerate(zip(local, previous_local)))
            voltage += rng.uniform(-.012, .012)
            sample = max(0, min(255, math.floor(voltage * 32 + .5) + 128))
            packed = sum((x + 2) << (3 * lane) for lane, x in enumerate(local))
            got = emit(sample, packed, remote + 2 if index < 32768 else 7, index < 32768)
            expected_remote = wanted.popleft()
            wanted.append(remote)
            if index == 32778:
                frozen = oracle.weights.copy()
            if index >= 32786:
                assert oracle.weights == frozen, 'drained holdout changed weights'
                errors += max(-2, min(2, math.floor((got[0] - 128) / 32 + .5))) != expected_remote
                raw_errors += max(-2, min(2, math.floor(voltage + .5))) != remote
                squared += ((got[0] - 128) / 32 - expected_remote) ** 2
                scored += 1
            previous_remote, previous_local = remote, local
        assert errors == 0 and squared / scored < .004, (errors, squared / scored)
        assert raw_errors > 1000
        # Stalls drain in-flight transactions. Reset and invalid codes discard
        # pending observations/corrections instead of releasing stale data.
        for _ in range(12):
            emit(0, 4095, 7, 1, 0)
        for lane in range(4):
            emit(reset=1)
            for _ in range(12):
                emit(rng.randrange(256), training=1)
            emit(local=1170 | (7 << (lane * 3)))
            for _ in range(10):
                emit()
        emit(reset=1)
        emit(desired=5, training=1)
        for _ in range(10):
            emit()
        emit(reset=1)
        for _ in range(100):
            emit(rng.randrange(256), training=1, reset=rng.randrange(13) == 0)
        for _ in range(12):
            emit(advance=0)
    print(f'{count} pipelined filter vectors; holdout {scored}, errors {errors}, raw errors {raw_errors}, MSE {squared / scored:.6g}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    generate(parser.parse_args().output)
