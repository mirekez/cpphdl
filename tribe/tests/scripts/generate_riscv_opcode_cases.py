#!/usr/bin/env python3
"""Generate tribe spec decode test opcode cases from riscv-opcodes files."""

from __future__ import annotations

import pathlib
import re
import sys


SPEC_GROUPS = [
    ("RV32I", "kRv32iOpcodeCases", [
        "lb", "lh", "lw", "lbu", "lhu",
        "sb", "sh", "sw",
        "addi", "slti", "sltiu", "xori", "ori", "andi", "slli", "srli", "srai",
        "add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and",
        "beq", "bne", "blt", "bge", "bltu", "bgeu",
        "jal", "jalr", "lui", "auipc",
    ]),
    ("RV32M", "kRv32mOpcodeCases", [
        "mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu",
    ]),
    ("RV32C", "kRv32cOpcodeCases", [
        "c.addi4spn", "c.lw", "c.sw", "c.addi", "c.jal", "c.li", "c.addi16sp",
        "c.srli", "c.srai", "c.andi", "c.sub", "c.xor", "c.or", "c.and",
        "c.j", "c.beqz", "c.bnez", "c.slli", "c.lwsp", "c.mv", "c.add",
        "c.jr", "c.jalr", "c.swsp",
    ]),
]

SELECTED = [name for _, _, names in SPEC_GROUPS for name in names]

EXTENSION_FILES = [
    "rv_i",
    "rv32_i",
    "rv_m",
    "rv_c",
    "rv32_c",
]

ASSIGN_RE = re.compile(r"^(\d+)(?:\.\.(\d+))?=(0x[0-9a-fA-F]+|0b[01]+|\d+)$")


def parse_value(text: str) -> int:
    return int(text, 0)


def parse_opcode_line(line: str) -> tuple[str, int, int] | None:
    line = line.split("#", 1)[0].strip()
    if not line or line.startswith("$import"):
        return None

    parts = line.split()
    if not parts:
        return None

    if parts[0] == "$pseudo_op":
        if len(parts) < 3:
            return None
        name = parts[2]
        tokens = parts[3:]
    else:
        name = parts[0]
        tokens = parts[1:]

    match = 0
    mask = 0
    for token in tokens:
        m = ASSIGN_RE.match(token)
        if not m:
            continue

        hi = int(m.group(1))
        lo = int(m.group(2) or m.group(1))
        if hi < lo:
            hi, lo = lo, hi

        width = hi - lo + 1
        value = parse_value(m.group(3))
        field_mask = ((1 << width) - 1) << lo
        mask |= field_mask
        match |= (value << lo) & field_mask

    return name, match, mask


def load_cases(opcodes_dir: pathlib.Path) -> dict[str, tuple[int, int]]:
    extensions_dir = opcodes_dir / "extensions"
    cases: dict[str, tuple[int, int]] = {}

    for filename in EXTENSION_FILES:
        path = extensions_dir / filename
        if not path.exists():
            continue
        for line in path.read_text().splitlines():
            parsed = parse_opcode_line(line)
            if parsed is None:
                continue
            name, match, mask = parsed
            if name in SELECTED:
                cases.setdefault(name, (match, mask))

    missing = [name for name in SELECTED if name not in cases]
    if missing:
        raise SystemExit("missing selected riscv-opcodes entries: " + ", ".join(missing))

    return cases


def width_for(name: str, match: int, mask: int) -> int:
    if name.startswith("c.") or ((match | mask) <= 0xffff and (match & 0x3) != 0x3):
        return 16
    return 32


def write_header(path: pathlib.Path, cases: dict[str, tuple[int, int]]) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "struct RiscvOpcodeCase {",
        "    const char* spec;",
        "    const char* name;",
        "    uint32_t match;",
        "    uint32_t mask;",
        "    uint8_t width;",
        "};",
        "",
        "// Generated from riscv/riscv-opcodes by generate_riscv_opcode_cases.py.",
    ]

    for spec_name, array_name, names in SPEC_GROUPS:
        lines.append("")
        lines.append(f"inline constexpr std::array<RiscvOpcodeCase, {len(names)}> {array_name} = {{{{")
        for name in names:
            match, mask = cases[name]
            width = width_for(name, match, mask)
            lines.append(f'    {{"{spec_name}", "{name}", 0x{match:08x}u, 0x{mask:08x}u, {width}}},')
        lines.append("}};")

    lines.append("")
    path.write_text("\n".join(lines))


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: generate_riscv_opcode_cases.py <riscv-opcodes-dir> <output-header>", file=sys.stderr)
        return 2

    opcodes_dir = pathlib.Path(argv[1]).resolve()
    output = pathlib.Path(argv[2]).resolve()
    cases = load_cases(opcodes_dir)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_header(output, cases)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
