"""Divide-by-two layout with unloaded internal feedback and a driven output."""
from clock_cell_layout import create_cells
from reference_clock_divider_cells import CELLS, divider


def create_divider(pdk):
    return create_cells(pdk, divider(), CELLS, unused=('Q_RAW',), compact_ports=True,
                        order=['Xclear', 'Xstorage', 'Xoutput'])
