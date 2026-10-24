#!/usr/bin/env python3
"""Check coarse-gain/fixed-point handoff against independent window and rational models."""
from fractions import Fraction
from pathlib import Path
from hardware import configure_cleanup
from pma_test_pattern import arguments, run
from receive_gain_control_reference import Acquisition, GAINS


class Reference:
    def __init__(self):
        self.control = Acquisition()
        self.launched = False
        self.remaining = 0
        self.offset, self.gain = 128, 256
        self.ready = self.busy = self.fault = False
        self.pending = (128, 256)

    def tick(self, reset, enable, restart, training, advance, adc_fault, sample, lower, upper, target):
        control = self.control
        launch = enable and training and not restart and not adc_fault and not self.launched and control.ready and not control.fault
        calibration_enable = enable and not restart and not adc_fault and not control.changed and not control.fault
        captured = (control.centre, control.span, target)
        result = control.tick(reset, enable, restart, training and not self.launched, advance,
                              adc_fault or self.fault, sample, lower, upper)
        if reset or not calibration_enable:
            self.offset, self.gain = 128, 256
            self.remaining = 0
            self.ready = self.busy = self.fault = False
        elif launch:
            centre, span, target = captured
            self.ready = False
            if span == 0 or target == 0:
                self.offset, self.gain = 128, 256
                self.remaining = 0
                self.busy, self.fault = False, True
            else:
                self.pending = centre, (256 * target + span // 2) // span
                self.remaining = 16
                self.busy, self.fault = True, False
        elif self.remaining:
            self.remaining -= 1
            if not self.remaining:
                self.offset, self.gain = self.pending
                self.ready, self.busy = True, False
        if reset or restart or not enable:
            self.launched = False
        elif launch:
            self.launched = True
        mask, level, coarse_ready, changed, weak, overload, fault, span, centre, settled = result
        fault = fault or self.fault
        ready = enable and not restart and not adc_fault and self.ready and coarse_ready and not fault
        return tuple(map(int, (mask, level, self.offset, self.gain, span, centre, ready,
                              self.busy, settled, weak, overload, fault)))


def generate(path):
    model = Reference()
    count = 0
    with path.open('w') as output:
        def tick(**overrides):
            nonlocal count
            values = dict(reset=0, enable=1, restart=0, training=1, advance=1, adc_fault=0,
                          sample=128, lower=16, upper=38, target=128)
            values.update(overrides)
            expected = model.tick(**values)
            output.write(' '.join(map(str, (*values.values(), *expected))) + '\n')
            count += 1
            return expected

        def acquire(amplitude=8, target=128, until_busy=False):
            for index in range(32000):
                excursion = round(Fraction(amplitude) * GAINS[model.control.level])
                result = tick(sample=max(0, min(255, 128 + (excursion if index & 1 else -excursion))), target=target)
                if (result[7] if until_busy else result[6] or result[9] or result[10] or result[11]):
                    return result
            raise AssertionError('acquisition failed to reach a bounded terminal state')

        tick(reset=1)
        levels = set()
        # Synthetic ADC closure tests orchestration; it is not a transistor loop.
        for amplitude in (0, 1, 2, 3, 4, 5, 8, 16, 40):
            for target in (32, 128):
                tick(restart=1)
                result = acquire(amplitude, target)
                if result[6]:
                    levels.add(result[1])
                    assert result[2] == result[5]
                    assert result[3] == (256 * target + result[4] // 2) // result[4]
                    frozen = result
                    for index in range(1024):
                        assert tick(training=index & 1, sample=255 if index & 1 else 0, target=1) == frozen
                elif amplitude == 0:
                    assert result[9] and result[1] == 4
                elif amplitude == 40:
                    assert result[10] and result[1] == 0
        assert levels == set(range(5)), levels
        # Check handoff cancellation at its initial, middle and final phases.
        # The range-divider unit separately covers every division phase.
        for cancellation in ('reset', 'restart', 'enable'):
            for delay in (0, 1, 7, 15):
                tick(restart=1)
                acquire(amplitude=16, until_busy=True)
                for _ in range(delay):
                    tick(sample=0, target=0)
                result = tick(**{cancellation: 0 if cancellation == 'enable' else 1})
                assert result[2:4] == (128, 256) and not result[6] and not result[7]
        tick(restart=1)
        acquire()
        assert tick(adc_fault=1)[11]
        for _ in range(17):
            assert tick()[11] and not tick()[6]
        tick(restart=1)
        acquire(target=0)
        for _ in range(17):
            result = tick()
            assert result[11] and not result[6] and result[0] == 30
        tick(enable=0)
        for lower, upper in ((0, 38), (38, 38), (16, 36)):
            assert tick(lower=lower, upper=upper)[11]
            tick(restart=1)
        # An interrupted training window retains only accepted samples.
        for index in range(4096):
            tick(training=0, sample=255)
        for index in range(1024):
            tick(sample=115 if index & 1 else 141)
            tick(training=0, sample=0)
            tick(advance=0, sample=255)
        for _ in range(20):
            result = tick(advance=0)
        assert result[6] and result[4:6] == (26, 128)
    return count


if __name__ == '__main__':
    configure_cleanup()
    source = Path(__file__).resolve()
    run(arguments(), source=source, top='ReceiveAnalogAcquisition', generate_vectors=generate,
        reference=source.with_name('receive_gain_control_reference.py'), clock_hz=250000000,
        scope='coarse-gain freeze and digital-scale handoff against synthetic ADC samples; no analog-loop, conversion or physical timing proof')
