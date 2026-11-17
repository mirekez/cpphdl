"""Coupling inventory must not lose a branch or turn it into a ground shunt."""
import unittest
from loading import inventory

class Loading(unittest.TestCase):
    def test_distributed_aliases_coupling_and_internal_branches(self):
        circuit='R r1 p p1 5\nR r2 n n1 7\nC c1 p1 n1 2e-15\nC c2 p1 g 3e-15\nC c3 p p1 9e-15\n'
        result=inventory(circuit,[('device.P','p'),('device.N','n')],
                         {'device.P':'positive','device.N':'negative','g':'ground'},['positive','negative'])
        self.assertAlmostEqual(result['positive']['explicit_incident_F']/1e-15,5)
        self.assertAlmostEqual(result['negative']['explicit_incident_F']/1e-15,2)
        self.assertAlmostEqual(result['positive']['couplings_F']['negative']/1e-15,2)
        self.assertNotIn('ground',result['negative']['couplings_F'])
    def test_shorted_logical_nets_and_bad_components_rejected(self):
        for circuit in ['R r p n 1\n','C c p n -1\n','C c p n nan\n','R r p q 0\n']:
            with self.subTest(circuit=circuit),self.assertRaises(ValueError):
                inventory(circuit,[],{'p':'positive','n':'negative'},['positive','negative'])

if __name__=='__main__':unittest.main()
