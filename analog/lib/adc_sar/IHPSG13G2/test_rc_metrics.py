import unittest
from rc_metrics import effective

class Networks(unittest.TestCase):
    def test_parallel_series_and_open_stub(self):
        r=effective([('g','a',10),('g','a',10),('a','b',3),('b','stub',100)],'g',['g','a','b'])
        for actual,expected in zip(r['resistance_Ohm'],[0,5,8]):self.assertAlmostEqual(actual,expected)
    def test_bridge(self):
        r=effective([('g','a',1),('g','b',1),('a','out',1),('b','out',1),('a','b',1)],'g',['out'])
        self.assertAlmostEqual(r['resistance_Ohm'][0],1)
    def test_disconnected_and_nonphysical_rejected(self):
        for rs in [[('a','b',1)],[('g','b',0)],[('g','b',-1)],[('g','b',float('nan'))]]:
            with self.assertRaises(ValueError):effective(rs,'g',['b'])

if __name__=='__main__':unittest.main()
