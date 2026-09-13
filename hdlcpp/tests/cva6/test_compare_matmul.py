import unittest

from compare_matmul import equivalent, parse_run


class ComparisonTest(unittest.TestCase):
    def output(self, total=100, work=90):
        return (f'PASSED\nprogram *** SUCCESS *** (tohost = 0) after {total} cycles\n'
                f'CVA6_BENCH reset_cycles=10 work_cycles={work} total_cycles={total} work_seconds=1.25\n')

    def test_pass(self):
        reference = parse_run(self.output(), 0)
        self.assertTrue(reference['passed'])
        self.assertTrue(equivalent(reference, reference))

    def test_timeout_cannot_be_a_performance_result(self):
        result = parse_run(self.output().replace('*** SUCCESS ***', '*** FAILED ***'), 1)
        self.assertFalse(result['passed'])
        self.assertFalse(equivalent(parse_run(self.output(), 0), result))

    def test_missing_guest_output(self):
        self.assertFalse(parse_run(self.output().replace('PASSED\n', ''), 0)['passed'])

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
