import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


class ConversionResumeTests(unittest.TestCase):
    def test_missing_and_stale_headers_are_regenerated(self):
        script = Path(__file__).with_name('support') / 'cpphdl/tools/convert_cva6.py'
        specification = importlib.util.spec_from_file_location('conversion_resume', script)
        conversion = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(conversion)
        with tempfile.TemporaryDirectory(prefix='cva6-conversion-resume-', dir='/tmp') as directory:
            work = Path(directory)
            source_root = work / 'source'
            source_root.mkdir()
            generator = work / 'hdlcpp'
            generator.write_text('generator')
            os.utime(generator, ns=(20_000_000_000, 20_000_000_000))
            sources = []
            for name in ('missing', 'stale', 'fresh', 'modified'):
                source = source_root / (name + '.sv')
                source.write_text('module ' + name + '; endmodule')
                timestamp = 40_000_000_000 if name == 'modified' else 10_000_000_000
                os.utime(source, ns=(timestamp, timestamp))
                sources.append(source)
            with patch.multiple(conversion, ROOT=source_root, OUT=work / 'output', HDLCPP=generator):
                for source in sources[1:]:
                    header = conversion.generated_header_for_source(source)
                    header.parent.mkdir(parents=True, exist_ok=True)
                    header.write_text('generated')
                    timestamp = 15_000_000_000 if source.stem == 'stale' else 30_000_000_000
                    os.utime(header, ns=(timestamp, timestamp))
                self.assertEqual(conversion.sources_needing_emission(sources),
                                 [sources[0], sources[1], sources[3]])
                os.utime(generator, ns=(50_000_000_000, 50_000_000_000))
                self.assertEqual(conversion.sources_needing_emission(sources), sources)


if __name__ == '__main__':
    unittest.main()
