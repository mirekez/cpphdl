#!/usr/bin/env python3
"""Compare routed clock/control transistors with independently composed CDL."""
import argparse
import json
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
from checks import check_lvs
from integrate_clock_control import CLOCK, CONTROL, ROOT, TOP, physical_name
from integrate_processing_clock import namespace_circuit
from package_development_layout import digest
from tool_process import configure_cleanup, execute


def reference(clock, control, pdk):
    # Compose the original transistor schematics; the routed DEF/netlist is
    # deliberately not the golden source for these signal assignments.
    analog = namespace_circuit((clock / 'compact.cir').read_text(), '', 'ProcessingClockDelivery')
    analog = re.sub(r'\bProcessingClockDelivery\b', 'ClockDeliveryMacro_devices', analog)
    digital = (control / 'core.cdl').read_text()
    digital = re.sub(r'\bProcessingClockControl\b', 'ClockControlMacro_devices', digital)
    def ports(text, name):
        flat = re.sub(r'\n\+\s*', ' ', text)
        return re.search(r'^\.SUBCKT ' + name + r' (.+)$', flat, re.M)[1].split()
    ap = ports(analog, 'ClockDeliveryMacro_devices')
    dp = ports(digital, 'ClockControlMacro_devices')
    assert ap[-1] == 'PSUB'
    analog_ports = ap[:-1]
    assignment = {'pcs_clk':'RAW', 'ready_pcs_out':'ADMIT', 'clear_out':'CLEAR',
                  'VDD':'VDD', 'VSS':'VSS'}
    for kind in ('coarse','fine'):
        for bit in range(8):
            assignment[f'{kind}_n_out[{bit}]'] = f'{kind.upper()}_N{bit}'
    external = sorted((set(dp) - set(assignment)) | {'VDD','VSS','OUT'})
    reference = [f'.include "{pdk / "ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl"}"',
        analog, digital,
        '.SUBCKT ClockDeliveryMacro ' + ' '.join(ap),
        # Preserve the process substrate terminal through the physical
        # hierarchy. VSS wiring does not justify an ideal substrate short.
        'Xdevices ' + ' '.join(ap) + ' ClockDeliveryMacro_devices',
        '.ENDS ClockDeliveryMacro',
        '.SUBCKT ClockControlMacro ' + ' '.join(physical_name(p) for p in dp),
        'Xdevices ' + ' '.join(physical_name(p) for p in dp) + ' ClockControlMacro_devices',
        '.ENDS ClockControlMacro',
        '.SUBCKT ' + TOP + ' ' + ' '.join(physical_name(p) for p in external),
        # The substrate joins the controller's VSS contacts at system scope;
        # it remains a separate pin inside the analog macro hierarchy.
        'Xdelivery ' + ' '.join(analog_ports) + ' VSS ClockDeliveryMacro',
        'Xcontrol ' + ' '.join(assignment.get(p, physical_name(p)) for p in dp) + ' ClockControlMacro',
        '.ENDS ' + TOP, '']
    return '\n'.join(reference), [physical_name(p) for p in external]


def run(args):
    args.work.mkdir(parents=True,exist_ok=True)
    report = dict(status='running', full_chip_qualified=False, electrical_qualified=False)
    def save():
        (args.work / 'checks.json').write_text(json.dumps(report,indent=2)+'\n')
    try:
        report['source_sha256'] = {str(p.resolve()):digest(p) for p in
            (args.gds, CLOCK / 'compact.cir', CONTROL / 'core.cdl', Path(__file__),
             HERE / 'integrate_processing_clock.py', HERE / 'checks.py',
             args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl')}
        text, ports = reference(CLOCK, CONTROL, args.pdk.resolve())
        netlist = args.work / 'reference.cir'
        netlist.write_text(text)
        save()
        env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
        runner = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech/lvs/run_lvs.py'
        with (args.work / 'lvs.log').open('w') as log:
            result = execute([sys.executable,str(runner),'--layout',str(args.gds.resolve()),
                '--netlist',str(netlist.resolve()),'--topcell',TOP,'--run_mode','deep',
                '--run_dir',str((args.work/'lvs').resolve())],env=env,stdout=log,stderr=-2,
                timeout=args.timeout,memory_exclusive=True)
        report['exit_code']=result.returncode
        database=args.work/'lvs'/(args.gds.stem+'.lvsdb')
        report['lvs']=check_lvs(database,TOP,ports,20938)
        if result.returncode or report['lvs']['instantiated_devices'] != 20938:
            raise ValueError('LVS failed or device inventory changed')
        for name, expected in report['source_sha256'].items():
            if digest(Path(name)) != expected:
                raise ValueError('LVS input changed: ' + name)
        report.update(status='pass',lvs_qualified=True)
        print('PASS transistor LVS: 20,938 devices in the connected clock/control system')
    except BaseException as error:
        report.update(status='fail',error=str(error))
        raise
    finally:
        report['artifact_sha256']={str(p):digest(p) for p in args.work.rglob('*')
            if p.is_file() and p.name!='checks.json'}
        save()


if __name__=='__main__':
    configure_cleanup()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gds',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--pdk',type=Path,default=ROOT/'pdk/IHPSG13G2')
    parser.add_argument('--tools',type=Path,default=ROOT/'out/repeater-toolchain')
    parser.add_argument('--timeout',type=int,default=900,
                        help='bound filled-layout extraction and comparison in seconds')
    run(parser.parse_args())
