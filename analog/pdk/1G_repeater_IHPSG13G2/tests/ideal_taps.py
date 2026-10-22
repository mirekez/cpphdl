"""Exercise the PDK reader's ideal-contact source model without layout input."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('pdk', 'tools', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[1]
netlist = work / 'source.cir'
netlist.write_text('''* Independent source-only contact fixture
.SUBCKT TapFixture vdd vss a y
Rguard1 vss substrate ptap1 A=141.253p P=47.54u
Rguard2 vdd nwell ntap1 A=10p P=12u
MN0 y a vss substrate sg13_lv_nmos W=1u L=0.13u
MP0 y a vdd nwell sg13_lv_pmos W=2u L=0.13u
Rpoly y vss 1k $SUB=substrate $[rppd] w=1u l=2u
.ENDS TapFixture
''')
output, audit = work / 'ideal.cir', work / 'audit.json'
with (work / 'prepare.log').open('w') as log:
    subprocess.run([str(args.tools.resolve() / 'bin/klayout'), '-b', '-r', str(source / 'ideal_taps.rb'),
        '-rd', 'pdk=' + str(args.pdk.resolve()), '-rd', 'input=' + str(netlist),
        '-rd', 'top=TapFixture', '-rd', 'output=' + str(output), '-rd', 'audit=' + str(audit)],
        stdout=log, stderr=-2, check=True, timeout=60)
result = json.loads(audit.read_text())
assert len(result['taps']) == 2 and result['non_tap_device_count'] == 3
assert result['non_tap_parameters_changed'] is False
assert {t['type'].lower() for t in result['taps']} == {'ptap1', 'ntap1'}
assert result['pdk_reader_sha256'] and result['source_sha256'] != result['output_sha256']
text = output.read_text().lower()
assert '.subckt tapfixture vdd vss a y' in text
assert 'sg13_lv_nmos' in text and 'sg13_lv_pmos' in text
assert 'ptap1' not in text and 'ntap1' not in text
assert 'mn0 y a vss vss sg13_lv_nmos' in text and 'mp0 y a vdd vdd sg13_lv_pmos' in text
assert 'w=1u' in text and 'w=2u' in text and 'l=0.13u' in text
# An independent external substrate terminal must not silently disappear when
# it is shorted to VSS by the selected contact model.
negative = work / 'distinct_ports.cir'
negative.write_text(netlist.read_text().replace('.SUBCKT TapFixture vdd vss a y',
                                               '.SUBCKT TapFixture vdd vss a y substrate'))
with (work / 'rejected.log').open('w') as log:
    result = subprocess.run([str(args.tools.resolve() / 'bin/klayout'), '-b', '-r', str(source / 'ideal_taps.rb'),
        '-rd', 'pdk=' + str(args.pdk.resolve()), '-rd', 'input=' + str(negative), '-rd', 'top=TapFixture',
        '-rd', 'output=' + str(work / 'rejected.cir'), '-rd', 'audit=' + str(work / 'rejected.json')],
        stdout=log, stderr=-2, timeout=60)
assert result.returncode and 'short distinct external' in (work / 'rejected.log').read_text()
print('PASS source-only PDK ideal-contact preparation; non-tap device parameters preserved')
