"""Static pulsed-storage PFD realized with unmodified IHP standard cells.

The foundry circuits implement the generic detector purpose through explicit
ports. Extracted electrical tests qualify this process realization separately.
"""
from clock_cell_library import definitions as cell_definitions, library_path


PULSE_STAGES = 5
CELLS = {
    'sg13g2_inv_2': ('Y A VDD VSS', 2),
    'sg13g2_inv_4': ('Y A VDD VSS', 2),
    'sg13g2_nand2_2': ('Y A B VDD VSS', 4),
    'sg13g2_a221oi_1': ('Y A1 A2 B1 B2 C1 VDD VSS', 10),
}


def definitions(source):
    return cell_definitions(source, CELLS)


def detector():
    lines = ['.subckt IHPPhaseDetector VDD VSS REF FEEDBACK CLEAR UP UP_N DOWN DOWN_N RESET_B']
    for side, clock, state in (('ref', 'REF', 'UP'), ('fb', 'FEEDBACK', 'DOWN')):
        previous = clock
        for index in range(PULSE_STAGES):
            node = f'{side}_delay{index}'
            lines.append(f'X{side}_delay{index} {node} {previous} VDD VSS sg13g2_inv_2')
            previous = node
        # A rising edge produces an active-low set pulse. Its separate hold
        # driver prevents the common reset wire from loading local capture.
        lines += [
            f'X{side}_pulse {side}_SET_B {clock} {previous} VDD VSS sg13g2_nand2_2',
            f'X{side}_pulse_hold {side}_HOLD_B {clock} {previous} VDD VSS sg13g2_nand2_2',
            f'X{side}_pulse_parallel {side}_SET_B {clock} {previous} VDD VSS sg13g2_nand2_2',
        ]
        for index in range(2):
            lines.append(f'X{side}_q{index} {state}_RAW {side}_SET_B {state}_N VDD VSS sg13g2_nand2_2')
        for index in range(2):
            lines.append(f'X{side}_qn{index} {state}_N {state}_RAW RESET_B VDD VSS sg13g2_nand2_2')
    lines.append('Xpulses PULSE_ANY fb_HOLD_B ref_HOLD_B VDD VSS sg13g2_nand2_2')
    # Both captured edges or external CLEAR assert reset. Feedback holds it
    # until input set pulses finish, preventing capture on clear release.
    for index in range(2):
        lines.append(f'Xreset{index} RESET_B UP_RAW DOWN_RAW RESET PULSE_ANY CLEAR VDD VSS sg13g2_a221oi_1')
    lines += [
        'Xreset_true RESET RESET_B VDD VSS sg13g2_inv_2',
        # The complementary outputs are the reset-dominant stored states.
        # A second buffer here delayed reset completion in extracted testing.
        'Xup UP UP_N VDD VSS sg13g2_inv_4',
        'Xdown DOWN DOWN_N VDD VSS sg13g2_inv_4',
        '.ends',
    ]
    return '\n'.join(lines) + '\n'
