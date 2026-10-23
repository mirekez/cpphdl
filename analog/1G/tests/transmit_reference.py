#!/usr/bin/env python3
"""Independent Clause 40 algebra/table oracle and GMII transmit stimuli.

No implementation headers or generated RTL are read. Auxiliary bits are derived
by multiplying polynomials over GF(2), rather than copying the DUT XOR networks.
The byte-to-vector oracle reads the separately obtained IEEE table fixtures.
"""
import argparse
import csv
from pathlib import Path
import random


def multiply(a, b):
    out = 0
    while b:
        if b & 1:
            out ^= a
        a <<= 1
        b >>= 1
    return out


def expansion_masks():
    outputs = []
    for polynomial in (1, (1 << 4) | (1 << 6), (1 << 1) | (1 << 5)):
        masks = []
        for _ in range(4):
            masks.append(polynomial)
            polynomial = multiply(polynomial, (1 << 3) | (1 << 8))
        outputs.append(masks)
    return outputs


MASKS = expansion_masks()


def expand(state):
    outputs = [sum(((state & mask).bit_count() % 2) << bit for bit, mask in enumerate(masks))
               for masks in MASKS]
    return outputs  # Sy, Sx, Sg


class Reference:
    def __init__(self, fixtures):
        self.normal, self.special = {}, {}
        for filename, table in (("pcs_data.csv", self.normal), ("pcs_controls.csv", self.special)):
            with (fixtures / filename).open() as stream:
                for row in list(csv.reader(stream))[1:]:
                    first, subset, *levels = map(int, row)
                    table[first, subset] = levels
        self.reset(1)

    def reset(self, seed):
        self.state = [bool(seed & (1 << k)) for k in range(33)]
        self.fault = not seed
        self.history = [False] * 4
        self.errs = [False] * 4
        self.trellis = [False] * 3
        self.previous_sy = 0
        self.cycle = 0
        self.armed = False
        self.defer = False
        self.termination_bad = False

    def step(self, reset, load, seed, master, mode, link, local, enable, error, data):
        if reset or load:
            self.reset(1 if reset else seed)
            return (0, 0, 0, 0, 0, 0, 13, 0, int(self.fault), 1170)
        sy, sx, sg = expand(sum(int(v) << k for k, v in enumerate(self.state)))
        enabled = self.armed and link and mode == 2 and enable
        err = self.armed and link and mode == 2 and error
        if not link or mode != 2:
            self.armed = False
        elif not enable and not error:
            self.armed = True
        # Build a Boolean bit array in wire order; this intentionally does not
        # reproduce the DUT's integer shifts/masks and subset packing.
        sc = [(sy >> 0) & 1] + [0] * 7
        if mode != 0:
            for k in (1, 2, 3):
                sc[k] = ((sy if self.cycle % 2 == 0 else self.previous_sy) >> k) & 1
                if self.cycle % 2:
                    sc[k] ^= 1
        else:
            sc[0] = 0
        if self.history[1]:
            sc[4:] = [(sx >> k) & 1 for k in range(4)]
        sd = list(sc)
        csreset = self.history[1] and not enabled
        for k in range(8):
            if self.history[1]:
                sd[k] ^= (data >> k) & 1
        if csreset:
            sd[6:8] = self.trellis[:2]
        if not self.history[1]:
            sd[2] ^= bool(local)
            sd[0] ^= not enabled and err and data == 15
            sd[1] ^= not enabled and err and data != 15
        cs = [self.trellis[2], False, False]
        if self.history[1]:
            cs[1] = bool(sd[6]) != self.trellis[0]
            cs[2] = bool(sd[7]) != self.trellis[1]
        sd.append(cs[0])
        subset = 4 * sd[6] + 2 * sd[7] + sd[8]
        code = sum(int(v) << k for k, v in enumerate(sd))
        condition = 12
        if enabled and not self.history[1]:
            condition = 6 if self.history[0] else 5
            self.defer |= err
        elif enabled and self.history[1]:
            condition = 1 if err or self.defer else 0
            self.defer = False
        elif csreset:
            condition = 2 if self.termination_bad or (err and data != 15) else 3 if err else 4
            self.termination_bad |= err and data != 15
        elif not self.history[1] and self.history[3]:
            if self.termination_bad or (err and self.errs[0] and self.errs[1] and data != 15):
                condition = 11
            elif self.history[2]:
                condition = 7
            else:
                condition = 10 if err else 9 if self.errs[0] else 8
        else:
            self.termination_bad = False
        if mode == 0 or mode == 3 or self.fault:
            condition = 13
        self.fault |= mode == 3
        signs = sg ^ (15 if self.history[1] or self.history[3] else 0)
        if condition == 13:
            quartet = [0] * 4
        elif condition in (0, 12):
            if condition == 12 and (subset != 0 or code & 63 >= 16):
                raise ValueError("reference generated invalid idle")
            quartet = self.normal[code & 63, subset]
        else:
            quartet = self.special[condition, subset]
        packed = sum(((-v if signs & (1 << k) else v) + 2) << (3 * k) for k, v in enumerate(quartet))
        self.history = [enabled] + self.history[:3]
        self.errs = [err] + self.errs[:3]
        self.trellis = cs
        self.previous_sy = sy
        self.cycle += 1
        feedback = self.state[32] != self.state[12 if master else 19]
        self.state = [feedback] + self.state[:32]
        return (sx, sy, sg, code, sum(int(v) << k for k, v in enumerate(cs)), subset,
                condition, signs, int(self.fault), packed)


def generate(output, fixtures):
    oracle = Reference(fixtures)
    inputs = dict(reset=0, load=0, seed=1, master=1, mode=2, link=1, local=1, enable=0, error=0, data=0)
    count = 0
    conditions = set()
    with output.open("w") as stream:
        stream.write("# reset load seed master mode link local enable error data sx sy sg sd cs subset condition signs fault packed\n")
        def emit(**changes):
            nonlocal count
            row = inputs | changes
            values = list(row.values())
            expected = oracle.step(*values)
            stream.write(" ".join(map(str, [*values, *expected])) + "\n")
            conditions.add(expected[6])
            count += 1
        emit(reset=1)
        # All 33 basis states establish the complete linear expansion matrix.
        for master in (0, 1):
            inputs["master"] = master
            for bit in range(33):
                emit(load=1, seed=1 << bit)
                for _ in range(40):
                    emit()
        randomizer = random.Random(0x8023)
        for master in (0, 1):
            inputs["master"] = master
            emit(load=1, seed=0x123456789)
            for _ in range(256):
                emit(mode=1, local=0)
            for frame in range(64):
                for _ in range(12):
                    emit()
                length = (72, 1526, 256, 511)[frame % 4]
                for index in range(length):
                    emit(enable=1, error=int(index == frame % length and frame % 3 == 0),
                         data=0x55 if index < 7 else 0xD5 if index == 7 else randomizer.randrange(256))
                # Cover normal termination, carrier extension ending after zero,
                # one or both ESD symbols, and sticky extension errors.
                extension = (0, 1, 2, 3, 4, 8)[frame % 6]
                for index in range(12):
                    emit(error=int(index < extension), data=0x1F if frame % 7 == 0 and index == 0 else 0x0F)
        emit(load=1, seed=0)
        for _ in range(8):
            emit(enable=1)
        emit(reset=1)
        emit(mode=3)
        emit()
        emit(reset=1)
        for _ in range(4):
            emit(mode=0)
        # An already-active MAC cannot sneak a frame suffix through link re-arm.
        for _ in range(12):
            emit(enable=1, link=0)
        for _ in range(12):
            emit(enable=1)
        emit()
        for _ in range(72):
            emit(enable=1, data=0x55)
        for _ in range(12):
            emit()
    print(f"{count} independent Clause 40 transmit vectors; conditions {sorted(conditions)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.output, Path(__file__).resolve().parent)
