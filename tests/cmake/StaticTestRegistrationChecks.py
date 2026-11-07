"""Check static-test registration rules without building the converter."""
import unittest

from StaticTestToolchain import check_test_registrations


class RegistrationChecks(unittest.TestCase):
    converter = '/build/cpphdl-static'
    wrapper = '/build/test-toolchain/cxx'
    real = '/tools/clang++'

    def check(self, command):
        tests = [dict(name='rtl', command=[self.converter, 'model.cpp']),
                 dict(name='control', command=command)]
        check_test_registrations(tests, self.converter, self.wrapper)

    def test_wrapped_compiler(self):
        self.check(['test.py', '--cxx', self.wrapper])

    def test_real_compiler_control(self):
        self.check(['GnuPropertyChecks.py', '--real-cxx', self.real])

    def test_unwrapped_compiler_rejected(self):
        with self.assertRaises(AssertionError):
            self.check(['GnuPropertyChecks.py', '--cxx', self.real])

    def test_shared_converter_rejected(self):
        for arg in ('/build/cpphdl', '-DCPPHDL=/build/cpphdl', '-DCONVERTER=/build/cpphdl'):
            with self.subTest(arg=arg), self.assertRaises(AssertionError):
                self.check([arg])

    def test_static_converter_definitions(self):
        for arg in ('-DCPPHDL=' + self.converter, '-DCONVERTER=' + self.converter):
            with self.subTest(arg=arg):
                check_test_registrations([dict(name='cmake', command=[arg])],
                                         self.converter, self.wrapper)

    def test_missing_converter_rejected(self):
        with self.assertRaises(AssertionError):
            check_test_registrations([dict(name='native', command=['test.py'])],
                                     self.converter, self.wrapper)


if __name__ == '__main__':
    unittest.main()
