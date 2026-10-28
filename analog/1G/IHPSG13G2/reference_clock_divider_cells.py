"""PDK-cell divide-by-two candidate with asynchronous active-high clear."""
from clock_cell_library import definitions


CELLS = {
    'sg13g2_dfrbp_2': ('Q Q_N CLK D RESET_B VDD VSS', 34),
    'sg13g2_inv_4': ('Y A VDD VSS', 2),
}


def library(source):
    return definitions(source, CELLS)


def divider():
    # Feedback comes from the stored inverted output; the separate true-output
    # driver delivers the external load without inserting it into that loop.
    return '''.subckt IHPClockDivider VDD VSS CLOCK CLEAR Q Q_N
Xstorage Q_RAW Q_N CLOCK Q_N RESET_B VDD VSS sg13g2_dfrbp_2
Xclear RESET_B CLEAR VDD VSS sg13g2_inv_4
Xoutput Q Q_N VDD VSS sg13g2_inv_4
.ends
'''
