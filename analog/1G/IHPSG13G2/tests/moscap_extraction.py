#!/usr/bin/env python3
"""Reject unreviewed MOSCAP model/geometry changes and disconnected returns."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from moscap_extraction import bind, model_wrapper


MODEL = '''.subckt sg13_moscap_n G SUB
+ w=1u l=1u m=1
.include sg13g2_moscap_parm.lib
Nsg13_moscap_n SUB G SUB SUB sg13g2_moscap_n_psp
+ w=w l=l nf=1 mult=m as='w*0.34e-6' ad='w*0.34e-6'
.ends sg13_moscap_n
'''
REFERENCE = '''.subckt ExtractedAnalog G SUB
X0 SUB G SUB SUB sg13_lv_nmos ad=10.2p pd=60.68u as=10.2p ps=60.68u w=30u l=30u
C0 G SUB 90f
.ends
'''
DISTRIBUTED = REFERENCE.replace('X0 SUB G SUB SUB', 'X0 d g s b').replace(
    'C0 G SUB 90f', 'C0 g b 90f\nRg G g 4\nRd SUB d 5\nRs SUB s 6\nRb SUB b 7')


class MOSCAPBinding(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.models = Path(self.tmp.name)
        (self.models / 'sg13g2_moscap_mod.lib').write_text(MODEL)

    def test_physical_returns_retained_and_intrinsic_geometry_owned_by_pdk(self):
        lumped, physical, audit = bind(REFERENCE, DISTRIBUTED, 1, 30, 30, self.models)
        self.assertIn('X0 d g s b ExtractedMOSCAP w=30u l=30u', physical)
        for nodes, resistance in [('SUB d', 5), ('SUB s', 6), ('SUB b', 7)]:
            self.assertRegex(physical, r'Rwire\d+ ' + nodes + ' ' + str(resistance) + r'\n')
        self.assertIn('Nsg13_moscap_n D G S B sg13g2_moscap_n_psp', physical)
        self.assertIn("as='w*0.34e-6' ad='w*0.34e-6'", physical)
        self.assertNotIn('ad=10.2p', physical)
        self.assertEqual(audit['distributed_terminals_retained'], 4)
        self.assertIn('X0 SUB G SUB SUB ExtractedMOSCAP', lumped)

    def test_wrong_model_geometry_count_and_extra_devices_rejected(self):
        for altered in (REFERENCE.replace('sg13_lv_nmos', 'sg13_hv_nmos'),
                        REFERENCE.replace('w=30u', 'w=29u'),
                        REFERENCE.replace('.ends', REFERENCE.splitlines()[1].replace('X0', 'X1') + '\n.ends')):
            with self.subTest(netlist=altered), self.assertRaises(ValueError):
                bind(altered, DISTRIBUTED, 1, 30, 30, self.models)

    def test_disconnected_or_short_return_rejected(self):
        for altered in (DISTRIBUTED.replace('Rb SUB b 7', ''),
                        DISTRIBUTED.replace('Rd SUB d 5', 'Rd G d 5'),
                        DISTRIBUTED.replace('.ends', 'Rshort G SUB 1\n.ends')):
            with self.subTest(netlist=altered), self.assertRaises(ValueError):
                bind(REFERENCE, altered, 1, 30, 30, self.models)

    def test_changed_pdk_wrapper_requires_review(self):
        (self.models / 'sg13g2_moscap_mod.lib').write_text(MODEL.replace('Nsg13_moscap_n SUB G SUB SUB',
                                                                 'Nsg13_moscap_n G SUB SUB SUB'))
        with self.assertRaisesRegex(ValueError, 'contract'):
            model_wrapper(self.models)


if __name__ == '__main__':
    unittest.main()
