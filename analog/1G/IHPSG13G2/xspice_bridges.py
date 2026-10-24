"""Bound vector bridge cards without changing channel order or model laws."""
import re


def dac_bridge_cards(inputs, outputs, model, name):
    """Keep each two-vector bridge below ngspice 47's fixed token array.

    A 144-input/144-output card overruns MIF_INP2A's BSIZE_SP token
    collector, even when that run appears successful. A maximum of 64
    channels per card leaves room for the parser's additional tokens.
    Every instance uses the same DAC model and retains its own channel's
    state, transitions and delay.
    """
    if not inputs or len(inputs) != len(outputs):
        raise ValueError('bridge input and output channel counts differ or are empty')
    if any(not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', value)
           for value in [*inputs, *outputs, model, name]):
        raise ValueError('unsupported bridge identifier')
    return '\n'.join(f'A{name}{start // 64} [{" ".join(inputs[start:start + 64])}] '
                     f'[{" ".join(outputs[start:start + 64])}] {model}'
                     for start in range(0, len(inputs), 64))
