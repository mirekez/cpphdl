import unittest

from compare_matmul import equivalent, parse_run, simulation_command


class ComparisonTest(unittest.TestCase):
    def test_native_tracer_uses_same_elf(self):
        elf = '/tmp/program with spaces.riscv'
        self.assertEqual(simulation_command('/tmp/verilator', 'verilator', elf, 100),
                         ['/tmp/verilator', '--seed=1', '--max-cycles=100', elf,
                          '+elf_file=' + elf])
        self.assertEqual(simulation_command('/tmp/cpphdl', 'cpphdl', elf, 100),
                         ['/tmp/cpphdl', elf, '100'])

    def output(self, total=100, work=90):
        return (f'PASSED\nprogram *** SUCCESS *** (tohost = 0) after {total} cycles\n'
                f'CVA6_BENCH reset_cycles=10 work_cycles={work} total_cycles={total} work_seconds=1.25\n')

    def test_pass(self):
        reference = parse_run(self.output(), 0)
        self.assertTrue(reference['passed'])
        self.assertTrue(equivalent(reference, reference))

    def test_actual_graph_edges(self):
        output = self.output() + 'CVA6_GRAPH_EDGES total_cycles=100\n'
        self.assertTrue(parse_run(output, 0, require_graph_edges=True)['passed'])
        for invalid in (self.output(), output.replace('total_cycles=100\n', 'total_cycles=101\n'),
                        output + 'CVA6_GRAPH_EDGES total_cycles=100\n'):
            self.assertFalse(parse_run(invalid, 0, require_graph_edges=True)['passed'])

    def test_large_workload_signature(self):
        signature = 'CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0x1234abcd\n'
        reference = parse_run(signature + self.output(), 0)
        self.assertTrue(reference['passed'])
        self.assertEqual(reference['workload']['macs'], 32768)
        self.assertTrue(equivalent(reference, parse_run(signature + self.output(), 0)))
        for mismatch in (signature.replace('1234abcd', '1234abce'),
                         signature.replace('repeats=1 macs=32768', 'repeats=2 macs=65536'), ''):
            self.assertFalse(equivalent(reference, parse_run(mismatch + self.output(), 0)))

    def test_invalid_large_workload_signature(self):
        signature = 'CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0x1234abcd\n'
        for invalid in (signature + signature, signature.replace('macs=32768', 'macs=12'),
                        signature.replace('size=32', 'size=0'), signature.replace('checksum=0x', 'bad=0x')):
            self.assertFalse(parse_run(invalid + self.output(), 0)['passed'])

    def test_timeout_cannot_be_a_performance_result(self):
        result = parse_run(self.output().replace('*** SUCCESS ***', '*** FAILED ***'), 1)
        self.assertFalse(result['passed'])
        self.assertFalse(equivalent(parse_run(self.output(), 0), result))

    def test_missing_guest_output(self):
        self.assertFalse(parse_run(self.output().replace('PASSED\n', ''), 0)['passed'])

    def test_repeated_guest_output(self):
        self.assertFalse(parse_run(self.output() + 'PASSED\n', 0)['passed'])

    def test_inconsistent_clocks(self):
        self.assertFalse(parse_run(self.output(work=89), 0)['passed'])

    def test_different_completed_clocks(self):
        self.assertFalse(equivalent(parse_run(self.output(), 0),
                                    parse_run(self.output(101, 91), 0)))

    def test_missing_or_duplicate_statistics(self):
        self.assertFalse(parse_run('PASSED', 0)['passed'])
        self.assertFalse(parse_run(self.output() * 2, 0)['passed'])


if __name__ == '__main__':
    unittest.main()
