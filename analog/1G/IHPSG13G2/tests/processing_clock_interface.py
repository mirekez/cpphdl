#!/usr/bin/env python3
"""Reject interface substitutions that could conceal a loaded-driver failure."""
import argparse
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from processing_clock_interface import output_cells, connect, launch_cells, qualify_handoff
from processing_clock_gate_guards import waveform
from processing_clock_gate import clock_schedule

NETLIST = '''module ProcessingClockControl();
sg13g2_and2_1 admission_allow (.A(enable_in), .B(net413), .X(admission_enabled));
sg13g2_nand2_2 admission_nand (.A(qualified_ready), .B(admission_enabled), .Y(admission_inverted));
sg13g2_inv_8 admission_driver (.A(admission_inverted), .Y(ready_pcs_out));
endmodule
'''


class InterfaceTests(unittest.TestCase):
    def test_real_launch_branch(self):
        netlist = NETLIST + '''sg13g2_dfrbpq_1 readiness (.RESET_B(reset_b), .D(next_ready),
            .Q(qualified_ready), .CLK(readiness_clock));
        sg13g2_buf_8 clkbuf_readiness (.A(pcs_clk), .X(readiness_clock));'''
        cells, inventory = launch_cells(netlist, self.cdl)
        self.assertEqual([len(c['geometry']) for c in inventory['cells']], [4, 32])
        self.assertIn('Xreadiness_launch qualified_launch launch_clock COMMAND', cells)
        for old, new in [('sg13g2_buf_8', 'sg13g2_buf_4'),
                         ('.CLK(readiness_clock)', '.CLK(pcs_clk)'),
                         ('sg13g2_dfrbpq_1', 'sg13g2_dllrq_1')]:
            with self.subTest(replacement=new), self.assertRaises(ValueError):
                launch_cells(netlist.replace(old, new), self.cdl)

    def test_registered_handoff_pulses(self):
        clocks = clock_schedule()
        pulses = [pulse for i, pulse in enumerate(clocks) if 22 <= i <= 37 or 70 <= i <= 85]
        rows = waveform(pulses)
        self.assertEqual(qualify_handoff(rows, 1.2)['launch_edges_blocked'], 2)
        bad = list(pulses)
        bad[0] = clocks[21]
        with self.assertRaisesRegex(ValueError, 'launch edge'):
            qualify_handoff(waveform(bad), 1.2)
        bad[0] = (pulses[0][0], pulses[0][0] + 1e-9)
        with self.assertRaisesRegex(ValueError, 'runt'):
            qualify_handoff(waveform(bad), 1.2)
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            qualify_handoff(rows[:-10], 1.2)

    def test_real_geometry_and_connections(self):
        cells, inventory = output_cells(NETLIST, self.cdl)
        self.assertEqual([len(c['geometry']) for c in inventory], [6, 4, 2])
        driver = inventory[-1]['geometry']
        self.assertEqual([d['total_width_um'] for d in driver], [5.92, 8.96])
        self.assertEqual([d['fingers'] for d in driver], [8, 8])
        self.assertIn('Xadmission_driver ADMIT admission_inverted VDD 0 sg13g2_inv_8', cells)
        self.assertIn('Xadmission_nand admission_inverted COMMAND admission_enabled VDD 0 sg13g2_nand2_2', cells)

    def test_wrong_function_or_size(self):
        for old, new in [('sg13g2_inv_8', 'sg13g2_inv_4'), ('sg13g2_nand2_2', 'sg13g2_and2_2')]:
            with self.subTest(replacement=new), self.assertRaises(ValueError):
                output_cells(NETLIST.replace(old, new), self.cdl)

    def test_changed_topology(self):
        for old, new in [('.Y(ready_pcs_out)', '.Y(ready_ref_out)'),
                         ('.A(qualified_ready)', '.A(ready)'),
                         ('.B(net413), ', ''), ('.A(admission_inverted)', '.A(admission_enabled)')]:
            with self.subTest(replacement=new), self.assertRaises(ValueError):
                output_cells(NETLIST.replace(old, new), self.cdl)

    def test_duplicate_driver(self):
        with self.assertRaises(ValueError):
            output_cells(NETLIST + NETLIST, self.cdl)

    def test_changed_library_ports_or_devices(self):
        for old, new in [('.SUBCKT sg13g2_inv_8 Y A VDD VSS', '.SUBCKT sg13g2_inv_8 Y A VDD VSS EXTRA'),
                         ('MN0 Y A VSS VSS sg13_lv_nmos m=1 w=5.92u',
                          'MN0 Y A VSS VSS sg13_lv_nmos m=2 w=5.92u')]:
            self.assertIn(old, self.cdl)
            with self.subTest(replacement=new), self.assertRaises(ValueError):
                output_cells(NETLIST, self.cdl.replace(old, new))

    def test_connect_real_output_to_latch(self):
        deck = ('Rphysical7 n6 n2 50\n.control\n' +
                ''.join(f'let observation{i} = v(n{i + 1})\n' for i in range(5)))
        cells, _ = output_cells(NETLIST, self.cdl)
        result = connect(deck, cells)
        self.assertIn('Rphysical7 n6 qualifier_command 50', result)
        self.assertIn('Xadmission_driver n2 admission_inverted n5 0 sg13g2_inv_8', result)
        self.assertIn('Cinterface n2 0 27f', result)
        with self.assertRaises(ValueError):
            connect(deck.replace('Rphysical7', 'Rphysical7 n6 n2 50\nRphysical8'), cells)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    args = parser.parse_args()
    InterfaceTests.cdl = (args.pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl').read_text()
    unittest.main(argv=[sys.argv[0]])
