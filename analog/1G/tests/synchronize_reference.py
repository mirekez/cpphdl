#!/usr/bin/env python3
"""Feed an unseeded receiver real encoded remote idles, polarity swaps and data."""
import argparse
from pathlib import Path
from transmit_reference import Reference, expand


def generate(path):
    reference = Reference(Path(__file__).resolve().parent)
    count = 0
    with path.open("w") as out:
        out.write("# reset remote_master pma_ok check_idle restart symbols lock_contract state sx sy sg mask polarity remote_ok\n")
        def emit(values):
            nonlocal count
            out.write(" ".join(map(str, values)) + "\n")
            count += 1
        for master in (0, 1):
            for polarity in range(16):
                for phase in (0, 7):
                    for seed in (1, 0x123456789, 0x1DEADBEEF):
                        reference.reset(seed)
                        for _ in range(phase):
                            reference.step(0, 0, 0, master, 2, 1, 1, 0, 0, 0)
                        emit([1, master, 1, 1, 0, 1170, 2, 0, 0, 0, 0, 0, polarity, 0])
                        def symbol(index, enable=0, check=1, contract=None, pma=1, reset=0):
                            state = sum(int(v) << k for k, v in enumerate(reference.state))
                            local = int(index % 128 >= 64)
                            old_sy = reference.previous_sy
                            odd = reference.cycle % 2
                            sx, sy, sg, _, _, _, _, _, _, packed = reference.step(
                                0, 0, 0, master, 2, 1, local, enable, 0, index & 255)
                            raw = 0
                            for lane in range(4):
                                code = (packed >> (lane * 3)) & 7
                                if polarity & (1 << lane):
                                    code = 4 - code
                                raw |= code << (3 * lane)
                            mask = sy if not odd else (old_sy ^ 14) & 14 | sy & 1
                            if contract is None:
                                contract = 2 if index < 96 else 1 if index >= 160 else 0
                            emit([reset, master, pma, check, 0, raw, contract, state, sx, sy, sg, mask, polarity, local])
                        for index in range(192):
                            symbol(index)
                        # Receiver continues its own polynomial over a complete
                        # frame. No seed, mask or expected payload enters the DUT.
                        for index in range(192, 256):
                            symbol(index, enable=1, check=0, contract=1)
                        for index in range(256, 260):
                            symbol(index, check=0, contract=1)
                        for index in range(260, 272):
                            symbol(index, contract=1)
                        # PMA loss must revoke lock immediately. Fresh training
                        # acquires again, including odd/even and polarity state.
                        symbol(272, pma=0, contract=2)
                        for index in range(192):
                            symbol(index)
        # No transmitter seed can be reconstructed from zero or a stuck level.
        for packed in (1170, 0, 2340):
            emit([1, 1, 1, 1, 0, packed, 2, 0, 0, 0, 0, 0, 0, 0])
            for _ in range(256):
                emit([0, 1, 1, 1, 0, packed, 2, 0, 0, 0, 0, 0, 0, 0])
    print(f"{count} unseeded acquisition/tracking/polarity/PMA-loss vectors")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
