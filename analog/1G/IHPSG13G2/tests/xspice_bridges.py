"""Check that large bridge vectors remain complete, ordered and bounded."""
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from xspice_bridges import dac_bridge_cards


class BridgeCards(unittest.TestCase):
    def test_every_channel_appears_once_in_order(self):
        for count in (9, 36, 64, 65, 144):
            with self.subTest(count=count):
                inputs = ['host_bit' + str(i) for i in range(count)]
                outputs = ['bit' + str(i) for i in range(count)]
                cards = dac_bridge_cards(inputs, outputs, 'host_output', 'host_controls').splitlines()
                actual_inputs, actual_outputs = [], []
                for index, card in enumerate(cards):
                    match = re.fullmatch(r'Ahost_controls(\d+) \[([^]]+)\] \[([^]]+)\] host_output', card)
                    self.assertIsNotNone(match)
                    self.assertEqual(int(match[1]), index)
                    a, b = match[2].split(), match[3].split()
                    self.assertEqual(len(a), len(b))
                    self.assertLessEqual(len(a), 64)
                    actual_inputs.extend(a)
                    actual_outputs.extend(b)
                self.assertEqual(actual_inputs, inputs)
                self.assertEqual(actual_outputs, outputs)

    def test_invalid_card_is_rejected_before_simulation(self):
        for a, b, model, name in (([], [], 'm', 'b'), (['x'], [], 'm', 'b'),
                (['x'], ['y'], 'm\nquit', 'b'), (['x]'], ['y'], 'm', 'b')):
            with self.subTest(a=a, b=b), self.assertRaises(ValueError):
                dac_bridge_cards(a, b, model, name)


if __name__ == '__main__':
    unittest.main()
