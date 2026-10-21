#!/usr/bin/env python3
"""Independent forwarding, frame/FCS, and minimum-IFG checks on native evidence."""
import argparse
import csv
from pathlib import Path
import struct
import zlib


def check(path):
    with path.open(newline="") as file:
        rows = [{name: int(value) for name, value in row.items()} for row in csv.DictReader(file)]
    if len(rows) != 20513:
        raise ValueError("missing forwarding test cycles")
    for index, row in enumerate(rows):
        if row["cycle"] != index:
            raise ValueError("nonsequential forwarding cycles")
        active = int(not row["reset"] and row["enable"] and row["link_a"] and row["link_b"])
        for output, source in (("tx_a", "rx_b"), ("tx_b", "rx_a")):
            expected = row[source] if active and row[source] & 256 else 0
            if row[output] != expected or row["active"] != active:
                raise ValueError("forwarding/control mismatch")
    frames = 0
    for output in ("tx_a", "tx_b"):
        packet = bytearray()
        gap = 12
        for row in rows[1:19465]:
            word = row[output]
            if word & 256:
                if not packet and gap < 12:
                    raise ValueError("inter-frame gap shorter than 12 octets")
                packet.append(word & 255)
                gap = 0
            else:
                gap += 1
                if packet:
                    if packet[:8] != b"\x55" * 7 + b"\xd5" or len(packet) not in (72, 1526):
                        raise ValueError("preamble, SFD or frame length mismatch")
                    if zlib.crc32(packet[8:-4]) != struct.unpack("<I", packet[-4:])[0]:
                        raise ValueError("Ethernet FCS was corrupted or incorrectly ordered")
                    frames += 1
                    packet.clear()
        if packet:
            raise ValueError("truncated frame")
    if frames != 48:
        raise ValueError("missing independent bidirectional Ethernet frames")
    print("PASS: 20513 forwarding cycles and 48 frames with independent CRC32 and 12-octet IFG checks")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vectors", type=Path)
    args = parser.parse_args()
    check(args.vectors)
