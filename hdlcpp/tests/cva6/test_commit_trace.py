import unittest

from check_commit_trace import compare_records, cpphdl_records, verilator_records


class CommitTraceTest(unittest.TestCase):
    def native(self, ordinal=1, destination=13, value='00000002', trap=0):
        return (f'CPPCOMMIT cycle=508 ordinal={ordinal} lane=0 order=0 pc=0x80003086 '
                f'next=0x00000000 insn=0xfe442683 trap={trap} cause=0x00000000 '
                f'rd=x{destination} data=0x{value}\n')

    def reference(self, destination=13, value='0000000000000002'):
        return (f'core 0: 0x80003086 lw x13, -28(s0)\n'
                f'3 0x0000000080003086 (0xfe442683) x {destination} 0x{value}\n')

    def test_equal_records(self):
        report = compare_records(verilator_records(self.reference()),
                                 cpphdl_records(self.native()))
        self.assertTrue(report['equivalent'])
        self.assertEqual(report['matched_prefix'], 1)
        self.assertIsNone(report['first_difference'])

    def test_first_mismatch(self):
        report = compare_records(verilator_records(self.reference()),
                                 cpphdl_records(self.native(value='00000000')))
        self.assertFalse(report['equivalent'])
        self.assertEqual(report['first_difference']['ordinal'], 1)
        self.assertEqual(report['first_difference']['verilator'][-1], 2)
        self.assertEqual(report['first_difference']['cpphdl'][-1], 0)

    def test_x0_data_is_not_architectural(self):
        self.assertTrue(compare_records(verilator_records(self.reference(destination=0)),
                                        cpphdl_records(self.native(destination=0, value='ffffffff')))
                        ['equivalent'])

    def test_no_destination(self):
        text = '3 0x0000000080003086 (0xfe442683) mem 0x80025004 0x2\n'
        self.assertTrue(compare_records(verilator_records(text),
                                        cpphdl_records(self.native(destination=0)))['equivalent'])

    def test_missing_or_extra_commit(self):
        record = verilator_records(self.reference())
        for reference, candidate in ((record, []), ([], record), (record, record * 2)):
            self.assertFalse(compare_records(reference, candidate)['equivalent'])
        self.assertFalse(compare_records([], [])['equivalent'])

    def test_reject_malformed_or_trapping_commit(self):
        for text in (self.native(trap=1), self.native(ordinal=2),
                     self.native(destination=32), self.native().replace(' data=', ' broken=')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                cpphdl_records(text)

    def test_reject_duplicate_commit(self):
        with self.assertRaises(ValueError):
            cpphdl_records(self.native() * 2)


if __name__ == '__main__':
    unittest.main()
