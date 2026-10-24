"""Independent lane arithmetic and transaction deadlines for the DSP bank."""
import argparse
import math
import random

from filter_pipeline_reference import Reference


def generate(path):
    rng = random.Random(0x4D5F125)
    lanes = [Reference() for _ in range(4)]
    fault = False
    pending = set()
    accepted = {}
    count = observed = 0

    with path.open('w') as out:
        def emit(samples=0x80808080, local=1170, desired=1170, tag=0,
                 clipped=0, train=0, valid=1, restart=0, reset=0):
            nonlocal count, observed, fault
            codes = [(local >> (3 * i)) & 7 for i in range(4)]
            targets = [(desired >> (3 * i)) & 7 for i in range(4)]
            invalid = valid and (any(c > 4 for c in codes) or (train and any(c > 4 for c in targets)))
            unavailable = bool(restart or fault or invalid)
            ready = all(l.valid for l in lanes) and not unavailable
            busy = not unavailable and bool(pending or (valid and train))
            word = sum(l.output << (8 * i) for i, l in enumerate(lanes))
            tag_out = clip_out = 0
            if ready:
                # A six-clock calculation is observed before the seventh edge.
                tag_out, clip_out = accepted[count - 7]
                clip_out |= any(l.output in (0, 255) for l in lanes)
                observed += 1
            inputs = [samples, local, desired, tag, clipped, train, valid, restart, reset]
            expected = [word, tag_out, ready, fault, clip_out, busy]
            out.write(' '.join(str(int(v)) for v in inputs + expected) + '\n')
            if reset or restart or fault or invalid:
                for lane in lanes:
                    lane.step(128, 1170, 2, 0, 0, 1)
                pending.clear()
                accepted.clear()
                fault = not (reset or restart)
            else:
                for i, lane in enumerate(lanes):
                    lane.step((samples >> (8 * i)) & 255, local, targets[i], train, valid, 0)
                pending.difference_update([count])
                if valid:
                    accepted[count] = (tag, clipped)
                    if train:
                        pending.add(count + 10)
            accepted.pop(count - 8, None)
            count += 1

        emit(reset=1)
        previous = [0] * 4
        previous_local = [0] * 4
        sequence = 65520
        for clock in range(38000):
            remote = [rng.randrange(-2, 3) for _ in range(4)]
            local = [rng.randrange(-2, 3) for _ in range(4)]
            samples = []
            for i in range(4):
                signal = .75 * remote[i] + .25 * previous[i]
                signal += sum((.30 if i == j else .045) * (.75 * x + .25 * previous_local[j])
                              for j, x in enumerate(local))
                signal += rng.uniform(-.01, .01)
                samples.append(max(0, min(255, math.floor(128 + 32 * signal + .5))))
            emit(sum(v << (8 * i) for i, v in enumerate(samples)),
                 sum((v + 2) << (3 * i) for i, v in enumerate(local)),
                 sum((v + 2) << (3 * i) for i, v in enumerate(remote)) if clock < 32768 else 4095,
                 sequence & 65535, clipped=clock % 211 == 0, train=clock < 32768)
            previous, previous_local = remote, local
            sequence += 1
            if clock % 31 == 0:
                # Unaccepted codes can be illegal; previous pipeline work drains.
                emit(local=4095, desired=4095, train=1, valid=0)
        for _ in range(14):
            emit(valid=0)
        for lane in range(4):
            emit(restart=1)
            for i in range(14):
                emit(samples=rng.randrange(1 << 32), tag=i, train=1)
            emit(local=1170 | (7 << (3 * lane)))
            for _ in range(14):
                emit()
            emit(restart=1)
            emit(desired=1170 | (5 << (3 * lane)), train=1)
            for _ in range(14):
                emit()
        for i in range(100):
            emit(samples=rng.randrange(1 << 32), tag=i, train=i % 2, reset=i % 17 == 0)
        for _ in range(14):
            emit(valid=0)
    return dict(clocks=count, valid_observations=observed, lanes=4,
                reference='four independent transaction-scheduled scalar arithmetic models')


if __name__ == '__main__':
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    print(generate(parser.parse_args().output))
