import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from capture_bus import instrument_requests
from check_context_replay import file_hash, native_layout_flags, verify_manifest


class ContextReplayTests(unittest.TestCase):
    def test_native_layout_is_explicit(self):
        self.assertEqual(native_layout_flags(False), [])
        self.assertEqual(native_layout_flags(True), ["-DCPPHDL_NATIVE_PACKED"])

    def test_observer_preserves_producer(self):
        name = "ariane_testharness__DOT__i_axi_xbar__DOT__slv_reqs"
        source = f"#include <cstdint>\nvoid producer() {{\n    VlWide<24>/*747:0*/ {name};\n    produce();\n}}\n"
        observed = instrument_requests(source)
        callback = f"\n    cpphdl_observe_bus_requests({name}.data());"
        self.assertEqual(observed.split("\n", 1)[1].replace(callback, ""), source)
        self.assertLess(observed.index("produce();"), observed.index(callback))

    def test_missing_or_ambiguous_boundary_rejected(self):
        declaration = "VlWide<24>/*747:0*/ ariane_testharness__DOT__i_axi_xbar__DOT__slv_reqs;"
        for source in ("", declaration, "\nvoid producer() {\n" + declaration,
                       "\nvoid producer() {\n" + declaration * 2 + "\n}\n"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                instrument_requests(source)

    def test_changed_build_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            payload = Path(directory) / "input"
            manifest = Path(directory) / "manifest.json"
            payload.write_text("original")
            manifest.write_text(json.dumps({str(payload): file_hash(payload)}))
            verify_manifest(manifest)
            payload.write_text("changed")
            with self.assertRaisesRegex(RuntimeError, "changed"):
                verify_manifest(manifest)


@unittest.skipUnless(shutil.which("g++"), "C++ compiler required for trace decoder tests")
class BusTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / "decode.cc"
        source.write_text('#include "BusTrace.h"\nint main(int, char** argv) {\n'
                          'try { read_bus_trace(argv[1]); return 0; } catch (...) { return 1; }\n}\n')
        cls.binary = cls.directory / "decode"
        subprocess.run(["g++", "-std=c++17", "-I" + str(Path(__file__).with_name("context_replay")),
                        str(source), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def decode(self, contents):
        trace = self.directory / "trace.bin"
        trace.write_bytes(contents)
        return subprocess.run([str(self.binary), str(trace)], check=False).returncode

    def test_valid_record(self):
        self.assertEqual(self.decode(bytes(800)), 0)

    def test_empty_or_truncated(self):
        for size in (0, 799, 801):
            with self.subTest(size=size):
                self.assertEqual(self.decode(bytes(size)), 1)

    def test_reset_and_padding(self):
        for word, value in ((0, 1), (0, 2), (24, 1 << 12), (71, 1 << 8),
                            (81, 1 << 4), (199, 1 << 16)):
            words = [0] * 200
            words[word] = value
            with self.subTest(word=word, value=value):
                self.assertEqual(self.decode(struct.pack("<200I", *words)), 1)


if __name__ == "__main__":
    unittest.main()
