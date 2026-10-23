#!/usr/bin/env python3
"""Window-based black-box gain acquisition contract, independent of RTL registers."""
from fractions import Fraction

GAINS = (Fraction(4, 5), Fraction(5, 4), Fraction(2), Fraction(7, 2), Fraction(8))


class Acquisition:
    def reset(self):
        self.level = 0
        self.wait = 4096
        self.samples = []
        self.pending = None
        self.ready = self.changed = self.weak = self.overload = self.fault = False
        self.span, self.centre = 0, 128

    def __init__(self):
        self.reset()

    def tick(self, reset, enable, restart, training, advance, adc_fault, sample, lower, upper):
        self.changed = False
        if reset or restart or not enable:
            self.reset()
        elif self.fault or adc_fault or not (0 < lower < upper and Fraction(upper, lower) >= Fraction(16, 7)):
            self.reset()
            self.fault = True
        elif self.wait:
            self.wait -= 1
        elif training:
            changed = False
            if self.pending is not None:
                phase, low, high = self.pending
                if phase == 'range':
                    self.span, self.centre = high - low, (high + low + 1) // 2
                    self.pending = ('decision', low, high)
                else:
                    self.pending = None
                    self.ready = lower <= self.span <= upper
                    self.weak = self.span < lower and self.level == 4
                    self.overload = self.span > upper and self.level == 0
                    previous = self.level
                    if self.span < lower:
                        self.level = min(4, self.level + 1)
                    elif self.span > upper:
                        self.level = max(0, self.level - 1)
                    changed = previous != self.level
                    if changed:
                        self.wait, self.changed, self.ready = 4096, True, False
                        self.samples.clear()
            if advance and not changed:
                self.samples.append(sample)
                if len(self.samples) == 1024:
                    self.pending = ('range', min(self.samples), max(self.samples))
                    self.samples.clear()
        return (31 - (1 << self.level), self.level, int(self.ready), int(self.changed),
                int(self.weak), int(self.overload), int(self.fault), self.span, self.centre,
                int(not self.wait and not self.fault))


def generate(path):
    model = Acquisition()
    count = 0
    with path.open('w') as output:
        def tick(**overrides):
            nonlocal count
            values = dict(reset=0, enable=1, restart=0, training=1, advance=1,
                          adc_fault=0, sample=128, lower=16, upper=38)
            values.update(overrides)
            expected = model.tick(**values)
            output.write(' '.join(map(str, (*values.values(), *expected))) + '\n')
            count += 1
            return expected

        def settle():
            for index in range(4096):
                result = tick(sample=(index * 17) & 255)
                assert result[-1] == int(index == 4095), 'settling interval is not 4096 clocks'

        def drain():
            tick(advance=0)
            return tick(advance=0)

        def window(low, high, **overrides):
            for index in range(1024):
                result = tick(sample=high if index & 1 else low, **overrides)
            return drain()

        tick(reset=1)
        settle()
        # Include the final sample and exercise the full unsigned ADC range.
        for _ in range(1023):
            tick(sample=0)
        tick(sample=255)
        result = drain()
        assert result[5] and result[7:9] == (255, 128)
        for _ in range(2048):
            assert tick(training=0, sample=0)[5], 'training freeze lost status'
        result = window(120, 136)
        assert result[2] and result[7] == 16
        result = window(109, 147)
        assert result[2] and result[7] == 38
        for level in range(1, 5):
            result = window(127, 129)
            assert result[1] == level and result[3] and not result[-1]
            settle()
        assert window(127, 129)[4], 'weak input at maximum gain was not reported'
        assert window(110, 146)[2], 'weak-input recovery failed'
        result = window(0, 255)
        assert result[1] == 3 and result[3]
        settle()
        # Pauses must not count as accepted samples or restart settling.
        for index in range(1023):
            tick(sample=115 if index & 1 else 141)
            tick(advance=0, sample=0)
        tick(sample=141)
        result = drain()
        assert result[2] and result[7] == 26
        assert tick(adc_fault=1)[6]
        for _ in range(17):
            assert tick()[6], 'ADC fault was not latched'
        assert not tick(restart=1)[6]
        for lower, upper in ((0, 38), (38, 38), (39, 38), (16, 36)):
            assert tick(lower=lower, upper=upper)[6]
            assert not tick(enable=0)[6]
        settle()
        # Synthetic ADC closure covers the entire gain ladder. This is a
        # controller test, separate from the physical analog-loop tests.
        for amplitude in (Fraction(0), Fraction(1, 4), Fraction(1), Fraction(2), Fraction(3),
                          Fraction(4), Fraction(8), Fraction(16), Fraction(40)):
            tick(restart=1)
            for _ in range(7):
                settle() if model.wait else None
                for index in range(1024):
                    excursion = round(amplitude * GAINS[model.level])
                    sample = max(0, min(255, 128 + (excursion if index & 1 else -excursion)))
                    tick(sample=sample)
                result = drain()
            if amplitude < 1:
                assert result[4] and result[1] == 4
            elif amplitude > 20:
                assert result[5] and result[1] == 0
            else:
                assert result[2] and not result[3], 'gain acquisition did not converge'
        # Measurement arithmetic overlaps subsequent accepted samples. The
        # first two samples of the next window must not be dropped.
        tick(restart=1)
        settle()
        for block, (low, high) in enumerate(((119, 137), (115, 141), (120, 136))):
            for index in range(1024):
                result = tick(sample=high if index & 1 else low)
                if block == 1 and index == 1:
                    assert result[2] and result[7] == 18
                if block == 2 and index == 1:
                    assert result[2] and result[7] == 26
        assert drain()[7] == 16
        # A training pause also freezes an in-flight measurement decision.
        tick(restart=1)
        settle()
        for _ in range(1023):
            tick(sample=0)
        tick(sample=255)
        for _ in range(17):
            result = tick(training=0)
            assert result[7] == 0 and not result[5]
        assert tick(advance=0)[7] == 255
        assert tick(advance=0)[5]
        tick(reset=1)
        assert tick(enable=0) == (30, 0, 0, 0, 0, 0, 0, 0, 128, 0)
    return count
