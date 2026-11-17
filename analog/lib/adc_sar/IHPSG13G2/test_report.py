"""A failed completed experiment is evidence; stale/incomplete data is not."""
import json,tempfile,unittest
from pathlib import Path
from provenance import digest
from report import completed_case,LEVELS

class Evidence(unittest.TestCase):
    def fixture(self,root,status='FAIL',levels=LEVELS):
        root=Path(root);case=root/'case';binding=root/'binding'
        case.mkdir();binding.mkdir()
        netlist=root/'physical.spice';netlist.write_text('R1 IN OUT 1\n')
        lowering=binding/'lowering.json'
        lowering.write_text(json.dumps(dict(input_netlist_sha256=digest(netlist))))
        checks=dict(status=status,native=dict(complete=status!='ERROR'),
                    command=['native','--degree','32',str(binding/'circuit.txt')],
                    measurements=dict(samples=[dict(input_V=v) for v in levels]))
        checkfile=case/'checks.json';checkfile.write_text(json.dumps(checks))
        (case/'inputs.json').write_text(json.dumps({str(lowering):digest(lowering)}))
        (case/'manifest.json').write_text(json.dumps(dict(artifact_sha256={str(checkfile):digest(checkfile)})))
        return case,netlist
    def test_completed_failure_is_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            case,netlist=self.fixture(root)
            result,_=completed_case(case,netlist)
            self.assertEqual(result['status'],'FAIL')
    def test_tampered_result_cannot_be_relabelled_pass(self):
        with tempfile.TemporaryDirectory() as root:
            case,netlist=self.fixture(root)
            p=case/'checks.json';p.write_text(p.read_text().replace('FAIL','PASS'))
            with self.assertRaisesRegex(ValueError,'changed'):completed_case(case,netlist)
    def test_other_geometry_cannot_reuse_a_pass(self):
        with tempfile.TemporaryDirectory() as root:
            case,netlist=self.fixture(root,status='PASS')
            netlist.write_text('R1 IN OUT 2\n')
            with self.assertRaisesRegex(ValueError,'another netlist'):completed_case(case,netlist)
    def test_unplaced_diagnostic_cannot_qualify_geometry(self):
        with tempfile.TemporaryDirectory() as root:
            case,netlist=self.fixture(root,status='PASS')
            (Path(root)/'binding/diagnostic.json').write_text('{"diagnostic_only":true}')
            with self.assertRaisesRegex(ValueError,'no matching physical'):completed_case(case,netlist)
    def test_incomplete_run_and_missing_stimulus_are_rejected(self):
        for status,levels in [('ERROR',LEVELS),('PASS',LEVELS[:-1])]:
            with self.subTest(status=status),tempfile.TemporaryDirectory() as root:
                case,netlist=self.fixture(root,status,levels)
                with self.assertRaisesRegex(ValueError,'incomplete'):completed_case(case,netlist)

if __name__=='__main__':unittest.main()
