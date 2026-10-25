import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lvs_disconnected_ports import disconnected_inputs


class MappedDisconnectedInputs(unittest.TestCase):
    def test_only_unique_unused_input_bits(self):
        module = dict(ports=dict(comparisons_in=dict(direction='input', bits=list(range(16)))),
                      cells=dict(receiver=dict(connections=dict(A=list(range(13))))))
        disconnected_inputs(module, ['comparisons_in[13]', 'comparisons_in[14]', 'comparisons_in[15]'])
        for names in ([], ['missing'], ['comparisons_in[12]'], ['comparisons_in[13]'] * 2):
            with self.assertRaises(ValueError):
                disconnected_inputs(module, names)
        module['ports']['alias'] = dict(direction='output', bits=[13])
        with self.assertRaises(ValueError):
            disconnected_inputs(module, ['comparisons_in[13]'])
        with self.assertRaises(ValueError):
            disconnected_inputs(module, ['alias'])
        module['ports']['comparisons_in']['bits'][14] = '0'
        with self.assertRaises(ValueError):
            disconnected_inputs(module, ['comparisons_in[14]'])


if __name__ == '__main__':
    unittest.main()
