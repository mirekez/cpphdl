"""Vendor ELF note bytes must not prevent inspection of structured ELF metadata."""
import sys
import unittest

from ElfTools import readelf_output


class ElfOutputChecks(unittest.TestCase):
    def test_invalid_note_bytes_preserve_isa_requirement(self):
        output = readelf_output(sys.executable, '-c',
            "import sys; sys.stdout.buffer.write("
            "b'.note.gnu.property\\nx86 ISA needed: x86-64-v3\\n'"
            "+ b'.gnu.build.attributes: \\xff\\xfe\\x80\\n')")
        self.assertIn('.note.gnu.property', output)
        self.assertIn('x86-64-v3', output)
        self.assertIn('\ufffd', output)

    def test_invalid_bytes_do_not_invent_isa_note(self):
        output = readelf_output(sys.executable, '-c',
            "import sys; sys.stdout.buffer.write(b'.gnu.build.attributes: \\xff\\n')")
        self.assertNotIn('.note.gnu.property', output)

    def test_invalid_stderr_preserves_dynamic_metadata(self):
        output = readelf_output(sys.executable, '-c',
            "import sys; print('INTERP\\n(NEEDED) Shared library: [libc++.so.1]', flush=True); "
            "sys.stderr.buffer.write(b'warning: vendor name \\xff\\n')")
        self.assertIn('INTERP', output)
        self.assertIn('(NEEDED) Shared library: [libc++.so.1]', output)

    def test_tool_failure_still_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'bad ELF'):
            readelf_output(sys.executable, '-c',
                "import sys; sys.stderr.buffer.write(b'bad ELF \\xff\\n'); sys.exit(1)")


if __name__ == '__main__':
    unittest.main()
