"""Use the fixed CVA6 DPI adapter without importing its former converter."""
from pathlib import Path


def write_dpi_adapter_header(root):
    data = Path(__file__).resolve().parents[2] / 'hdlcpp/tests/cva6/conversion.in'
    section = 'adapters/corev_apu/tb/dpi_adapters.h'
    header = data.read_text().split('@@ ' + section + '\n', 1)[1].split('\n@@ ', 1)[0]
    target = root / 'corev_apu/tb/dpi_adapters.h'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(header + '\n')
