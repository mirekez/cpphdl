import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('CPPHDL_TEST_CXX'), 'set CPPHDL_TEST_CXX for workload checks')
class MatrixBenchmarkTest(unittest.TestCase):
    def test_matrix_results_and_repetitions(self):
        source = Path(__file__).with_name('matrix_multiply_large.cpp')
        with tempfile.TemporaryDirectory(prefix='cva6-matrix-unit-', dir='/tmp') as directory:
            work = Path(directory)
            stub = work / 'print.cpp'
            stub.write_text('#include <cstdio>\nextern "C" void printstr(const char* text) { std::fputs(text, stdout); }\n')
            for size, repeats in [(1, 1), (3, 2), (16, 1), (32, 1), (128, 1)]:
                subprocess.run([os.environ['CPPHDL_TEST_CXX'], '-std=c++17', '-O2', source, stub,
                                '-DMATRIX_SIZE=' + str(size), '-DMATRIX_REPEATS=' + str(repeats),
                                '-o', work / 'run'], check=True, timeout=60)
                output = subprocess.check_output([work / 'run'], text=True, timeout=30)
                checksum = 2166136261
                for repeat in range(repeats):
                    for row in range(size):
                        for column in range(size):
                            value = (column + 2) * ((row + repeat % 17) * size * (size + 1) // 2
                                                   + size * (size - 1) * (size + 1) // 3)
                            if size <= 32:
                                self.assertEqual(value, sum((row + inner + repeat % 17) *
                                                            (inner + 1) * (column + 2) for inner in range(size)))
                            checksum = ((checksum ^ value) * 16777619) & 0xffffffff
                expected = (f'CVA6_MMUL size={size} repeats={repeats} macs={size**3 * repeats} '
                            f'checksum=0x{checksum:08x}\nPASSED\n')
                self.assertEqual(output, expected)


if __name__ == '__main__':
    unittest.main()
