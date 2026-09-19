import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from profile_context_replay import callgrind_report, parse_result, sample_report, symbols


class ProfileParserTests(unittest.TestCase):
    def test_result_requires_equivalent_checked_work(self):
        output = "seconds=1.500000000 cycles=92528 checked=46264 checksum=0123456789abcdef\n"
        self.assertEqual(parse_result(output, 92528, 46264)["seconds"], 1.5)
        for invalid in ("", output + output, output.replace("92528", "46264"),
                        output.replace("checked=46264", "checked=1")):
            with self.subTest(output=invalid), self.assertRaises(ValueError):
                parse_result(invalid, 92528, 46264)

    def test_callgrind_separates_inclusive_edges_from_self_cost(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile"
            path.write_text("positions: line\nevents: Ir Dr Dw\n"
                            "summary: 113 22 6\n"
                            "fn=(1) main\n1 10 2 1\ncfn=(2) helper\ncalls=4 0\n* 100 20 5\n"
                            "fn=(2)\n0 100 20 5\nfn=(1)\n+1 3\ntotals: 113 22 6\n")
            result = callgrind_report(path)
            self.assertEqual(result["totals"], dict(Ir=113, Dr=22, Dw=6))
            self.assertEqual(result["functions"][0]["calls"], 4)
            self.assertEqual(result["edges"], [dict(caller="main", callee="helper", calls=4)])
            original = path.read_text()
            path.write_text(original.replace("summary: 113", "summary: 112"))
            with self.assertRaisesRegex(ValueError, "summary"):
                callgrind_report(path)
            path.write_text(original.replace("summary: 113", "summary: 122"))
            self.assertEqual(callgrind_report(path)["summary_delta"]["Ir"], 9)
            path.write_text(original)
            path.write_text(path.read_text().replace("totals: 113", "totals: 114"))
            with self.assertRaisesRegex(ValueError, "totals"):
                callgrind_report(path)


@unittest.skipUnless(shutil.which("g++") and shutil.which("gcc"), "C/C++ compilers required")
class WorkGateTests(unittest.TestCase):
    def test_initialization_clocks_and_checker_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "probe.cc"
            source.write_text(r'''
#include <chrono>
#include <cstdint>
#include <cstdio>
volatile uint64_t sink;
__attribute__((noinline)) void checker() {
    const auto stamp = std::chrono::steady_clock::now();
    uint64_t value = stamp.time_since_epoch().count();
    for (unsigned index = 0; index < 300000000; ++index) value = value * 3 + index;
    sink = value;
}
__attribute__((noinline)) void work() {
    uint64_t value = 1;
    for (unsigned index = 0; index < 300000000; ++index) value = value * 7 + index;
    sink = value;
}
int main() {
    checker();
    const auto start = std::chrono::steady_clock::now();
    work();
    const auto end = std::chrono::steady_clock::now();
    std::printf("%lld\n", (long long)(end-start).count());
}
''')
            binary = root / "probe"
            library = root / "profile.so"
            gate = Path(__file__).with_name("context_replay") / "WorkProfile.c"
            subprocess.run(["g++", "-O2", source, "-o", binary], check=True)
            subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                            gate, "-ldl", "-o", library], check=True)
            start, size, _ = next(row for row in symbols(binary) if row[2] == "main")
            prefix = root / "capture"
            environment = dict(os.environ, LD_PRELOAD=str(library), WORK_PROFILE_BINARY=str(binary),
                               WORK_PROFILE_OUTPUT=str(prefix), WORK_PROFILE_MAIN_START=hex(start),
                               WORK_PROFILE_MAIN_END=hex(start + size), WORK_PROFILE_MODE="sample")
            subprocess.run([binary], env=environment, check=True, capture_output=True)
            metadata = json.loads(prefix.with_suffix(".json").read_text())
            self.assertEqual(metadata["monotonic_calls"], 2)
            self.assertEqual(metadata["ignored_calls"], 1)
            result = sample_report(prefix)
            counts = {row["name"]: row["samples"] for row in result["functions"]}
            self.assertGreater(counts.get("work()", 0), 10)
            self.assertNotIn("checker()", counts)


if __name__ == "__main__":
    unittest.main()
