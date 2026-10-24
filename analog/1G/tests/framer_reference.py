#!/usr/bin/env python3
"""Independent control/tag contracts from Clause 40 transmit conditions."""
import argparse
from pathlib import Path
from transmit_reference import Reference


def generate(path):
    encoder = Reference(Path(__file__).resolve().parent)
    count = 0
    with path.open("w") as out:
        out.write("# reset lock pma symbols sg sx low_mask polarity tag control negate restart remote_ok\n")
        def write(row, remote=0):
            nonlocal count
            out.write(' '.join(map(str, row + [remote])) + '\n')
            count += 1
        for master in (0, 1):
            for polarity in (0, 5, 10, 15):
                encoder.reset(0x123456789)
                write([1, 1, 1, 1170, 0, 0, 0, polarity, 0, 0, 0, 0])
                def emit(data=0, enable=0, error=0, second_tail=False):
                    previous_sy, odd = encoder.previous_sy, encoder.cycle % 2
                    sx, sy, sg, _, _, subset, condition, negate, _, packed = encoder.step(
                        0, 0, 0, master, 2, 1, 1, enable, error, data)
                    low = sy if not odd else ((previous_sy ^ 14) & 14) | (sy & 1)
                    raw = sum((4 - ((packed >> (3 * lane)) & 7) if polarity & (1 << lane)
                               else ((packed >> (3 * lane)) & 7)) << (3 * lane) for lane in range(4))
                    tag = {0: 0x500, 1: 0x700, 4: 0, 5: 0, 6: 0x80155, 7: 0, 8: 0, 12: 0}[condition]
                    control = (1 if condition == 0 else 2 if condition == 6 else
                               9 | (subset << 4) if condition == 1 else
                               (13 if second_tail else 9) | (subset << 4) if condition == 4 else 0)
                    write([0, 1, 1, raw, sg, sx, low, polarity, tag | (((sx << 4) | low) << 11),
                           control, negate ^ polarity, 0], remote=1)
                for _ in range(16):
                    emit()
                for frame in range(4):
                    for index in range(72 + 16 * frame):
                        emit(data=(index * 83 + frame) & 255, enable=1, error=int(index == 1 or index == 48))
                    emit()
                    emit(second_tail=True)
                    for _ in range(14):
                        emit()
        # Invalid SSD2 must leave RX_DV low; four consecutive correct idle
        # observations clear the error before a fresh delimiter is accepted.
        write([1, 1, 1, 1170, 0, 0, 0, 0, 0, 0, 0, 0])
        write([0, 1, 1, 2340, 0, 0, 0, 0, 0, 0, 0, 0])
        write([0, 1, 1, 1170, 0, 0, 0, 0, 0x20E, 0, 0, 0])
        for index in range(4):
            write([0, 1, 1, 1170, 0, 0, 0, 0, 0x20E if index < 3 else 0, 0, 0, 0])
        write([0, 1, 1, 2340, 0, 0, 0, 0, 0, 0, 0, 0])
        write([0, 1, 1, 292, 0, 0, 0, 0, 0x80155, 2, 0, 0])
        write([0, 1, 0, 1170, 0, 0, 0, 0, 0x200, 0, 0, 0])
        # Receiver status is carried by the C-pair idle bit. It must be decoded
        # from aligned symbols rather than sampled during a data code group.
        write([1, 1, 1, 1170, 0, 0, 0, 0, 0, 0, 0, 0])
        for index in range(512):
            remote = (index // 7) & 1
            write([0, 1, 1, 1042 if remote else 1170, 0, 0, 0, 0, 0, 0, 0, 0], remote=remote)
        write([0, 0, 1, 1042, 0, 0, 0, 0, 0, 0, 0, 0])
    print(f"{count} independently checked framer observations")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    generate(parser.parse_args().output)
