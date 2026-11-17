import math
import unittest
from transitions import LOW, HIGH, LSB, TOLERANCE, locate, verify_interiors


class SyntheticADC:
    def __init__(self, missing=None):
        self.samples=0; self.missing=missing

    def query(self, value, phase, direction, transition):
        # Independent exact quantizer with a known offset and gain perturbation.
        code=math.floor((value-.002)/(LSB*1.001))
        if self.missing is not None and code>=self.missing: code+=1
        row=dict(requested_V=value,code=code,sample=self.samples)
        self.samples+=1
        return row


class TransitionSearch(unittest.TestCase):
    def test_search_both_directions_against_exact_transfer(self):
        adc=SyntheticADC()
        endpoints=[adc.query(v,'endpoint','up',-1) for v in (LOW,HIGH)]
        for direction,codes in [('up',range(90,98)),('down',range(97,89,-1))]:
            previous=None; found=[]
            for code in codes:
                b=locate(adc,code,direction,endpoints,previous)
                exact=.002+code*LSB*1.001
                self.assertLessEqual(b['low']['requested_V'],exact)
                self.assertGreaterEqual(b['high']['requested_V'],exact)
                self.assertLessEqual(b['high']['requested_V']-b['low']['requested_V'],TOLERANCE)
                found.append(b); previous=b
            verify_interiors(adc,found,direction)

    def test_missing_code_is_not_hidden_by_bisection(self):
        adc=SyntheticADC(missing=94)
        endpoints=[adc.query(v,'endpoint','up',-1) for v in (LOW,HIGH)]
        found=[]; previous=None
        for code in range(92,97):
            previous=locate(adc,code,'up',endpoints,previous); found.append(previous)
        with self.assertRaisesRegex(RuntimeError,'no proven nonzero interval|repeatability failure'):
            verify_interiors(adc,found,'up')

    def test_repeatability_failure_is_detected(self):
        adc=SyntheticADC()
        endpoints=[adc.query(v,'endpoint','up',-1) for v in (LOW,HIGH)]
        found=[locate(adc,k,'up',endpoints) for k in (90,91)]
        original=adc.query
        count=0
        def drifting(*args):
            nonlocal count
            row=original(*args); count+=1
            if count==2: row['code']+=1
            return row
        adc.query=drifting
        with self.assertRaisesRegex(RuntimeError,'repeatability failure'):
            verify_interiors(adc,found,'up')


if __name__=='__main__': unittest.main()
