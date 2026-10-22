#!/usr/bin/env python3
"""Independent rounded-rational result with a sixteen-clock completion contract."""


def generate(path):
    remaining = 0
    offset, gain, ready, busy, fault = 128, 256, 0, 0, 0
    pending = (128, 256)
    count = 0
    with path.open('w') as output:
        def tick(reset=0, enable=1, start=0, centre=128, span=32, target=128):
            nonlocal remaining, offset, gain, ready, busy, fault, pending, count
            if reset or not enable:
                remaining = busy = ready = fault = 0
                offset, gain = 128, 256
            elif start:
                ready = 0
                if not span or not target:
                    offset, gain, busy, remaining, fault = 128, 256, 0, 0, 1
                else:
                    pending = (centre, (target * 256 + span // 2) // span)
                    busy, remaining, fault = 1, 16, 0
            elif remaining:
                remaining -= 1
                if not remaining:
                    offset, gain = pending
                    busy, ready = 0, 1
            values = (offset, gain, ready, busy, fault)
            output.write(' '.join(map(str, (reset, enable, start, centre, span, target, *values))) + '\n')
            count += 1
            return values

        tick(reset=1)
        for target in (1, 64, 128, 255):
            for span in range(1, 256):
                for centre in (0, 128, 255):
                    assert tick(start=1, centre=centre, span=span, target=target)[3]
                    for index in range(16):
                        result = tick(centre=index, span=(span + index) & 255, target=0)
                        assert result[3] == int(index != 15)
                    assert result[0] == centre and result[1] == (target * 256 + span // 2) // span
                    assert result[2] and not result[4]
                    assert tick() == result
        # Replacing a pending request commits only the most recent arguments.
        for delay in range(16):
            tick(start=1, centre=10, span=33, target=64)
            for _ in range(delay):
                tick()
            tick(start=1, centre=240, span=53, target=128)
            for _ in range(16):
                result = tick()
            assert result[:3] == (240, (128 * 256 + 26) // 53, 1)
        for span, target in ((0, 128), (32, 0), (0, 0)):
            assert tick(start=1, span=span, target=target) == (128, 256, 0, 0, 1)
            for _ in range(20):
                assert tick()[-1], 'invalid calibration was not latched'
        for delay in range(16):
            tick(start=1)
            for _ in range(delay):
                tick()
            assert tick(reset=1) == (128, 256, 0, 0, 0)
            for _ in range(20):
                assert tick() == (128, 256, 0, 0, 0)
        tick(start=1)
        assert tick(enable=0) == (128, 256, 0, 0, 0)
    return count
