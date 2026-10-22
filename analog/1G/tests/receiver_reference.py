#!/usr/bin/env python3
"""Independent GMII frame -> IEEE coding equations -> quantized receive samples.

The receive DUT gets ADC values and PMA status only. The expected GMII stream is
retained exclusively by this checker; neither seed nor expected data is supplied
to the receiver. Frames contain real preamble/SFD and Ethernet CRC-32/FCS.
"""
import argparse
from collections import deque
from pathlib import Path
import random
import zlib
from transmit_reference import Reference


LATENCY = 39


def generate(path):
    oracle = Reference(Path(__file__).resolve().parent)
    randomizer = random.Random(0x1EEE8023)
    count = 0
    with path.open("w") as output:
        output.write("# reset remote_master pma_ok samples expected_gmii comparison_mask require_lock\n")
        for master in (0, 1):
            for polarity in range(16):
                oracle.reset(randomizer.randrange(1, 1 << 33))
                queue = deque([(0, 1023)] * LATENCY)
                clock = 0
                def emit(data=0, enable=0, error=0, reset=0):
                    nonlocal count, clock
                    *_, packed = oracle.step(0, 0, 0, master, 2, 1, 1, enable, error, data)
                    samples = 0
                    for lane in range(4):
                        level = ((packed >> (3 * lane)) & 7) - 2
                        if polarity & (1 << lane):
                            level = -level
                        sample = 128 + level * 32 + randomizer.randrange(-6, 7)
                        samples |= sample << (8 * lane)
                    if reset:
                        queue.clear()
                        queue.extend([(0, 1023)] * LATENCY)
                    expected, mask = queue.popleft()
                    queue.append((768, 768) if enable and error else (256 | data, 1023) if enable else (0, 1023))
                    output.write(f"{reset} {master} 1 {samples} {expected} {mask} {int(clock >= 192)}\n")
                    clock += 1
                    count += 1
                emit(reset=1)
                for _ in range(192):
                    emit()
                for frame_index, size in enumerate((64, 1518)):
                    payload = bytes(randomizer.randrange(256) for _ in range(size - 4))
                    frame = b'\x55' * 7 + b'\xd5' + payload + zlib.crc32(payload).to_bytes(4, "little")
                    for index, data in enumerate(frame):
                        # A transmit error inside DATA must reach GMII RX_ER.
                        emit(data, 1, int(frame_index == 0 and polarity % 3 == 0 and index == 40))
                    for _ in range(12):
                        emit()
                for _ in range(LATENCY + 4):
                    emit()
    print(f"{count} independently encoded receiver vectors; 64 Ethernet frames with FCS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
