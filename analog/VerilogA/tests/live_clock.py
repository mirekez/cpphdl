"""Actual ngspice breakpoints must follow noisy RC clock crossings and stop."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from live import NgspiceSession, NgspiceVoltageClock


def run(binary, work, step):
    work.mkdir(parents=True, exist_ok=True)
    points = [(0, 0), (1, 0), (2, 1.2), (3, 1.2), (4, .6), (5, .6),
              (6, .79), (7, .41), (8, .6), (9, .6), (10, 0), (11, 0),
              (12, .6), (13, .6), (14, .79), (15, .6), (16, 1.2),
              (17, .6), (18, .41), (19, .79), (20, .6), (21, 0), (22, 0)]
    (work / 'clock.cir').write_text('Actual noisy clock through a loaded RC path\n'
        + 'Vinput source 0 PWL(' + ' '.join(f'{ns}n {v}' for ns, v in points) + ')\n'
        + 'Rinput source clock 50\nCinput clock 0 100f IC=0\n'
        + '.options method=gear maxord=1 reltol=1e-6 vntol=1e-9\n.end\n')
    events = []
    with NgspiceSession(binary, work, work / 'interactive.log') as session:
        session.command('set numdgt=17\nsource clock.cir\nstop when time = .1n\n'
                        f'tran {step:.17g} 22n 0 {step:.17g} uic')
        clock = NgspiceVoltageClock(session, 'v(clock)', .4, .8)
        while not clock.ended:
            events.append(clock.advance(22e-9))
        if [event['edge'] for event in events] != ['rising', 'falling', 'rising', 'falling', None]:
            raise ValueError('in-band noise created or erased voltage-clock edges')
        for event, expected in zip(events, (1 + 2/3, 9 + 1/3, 15 + 1/3, 20 + 1/3)):
            # The finite RC adds approximately 5 ps to each ramp crossing.
            if abs(event['time_s'] - expected * 1e-9 - 5e-12) > 2 * step:
                raise ValueError('ngspice edge differs from the independent ramp/RC crossing')
        try:
            clock.advance(23e-9)
        except ValueError:
            pass
        else:
            raise ValueError('a finished voltage clock resumed past its bound')
    (work / 'ambiguous.cir').write_text('Unresolved clock startup\nVinput source 0 DC .6\n'
        'Rinput source clock 50\nCinput clock 0 100f IC=0\n.end\n')
    with NgspiceSession(binary, work, work / 'ambiguous.log') as session:
        session.command('set numdgt=17\nsource ambiguous.cir\nstop when time = .1n\n'
                        f'tran {step:.17g} 1n 0 {step:.17g} uic')
        try:
            NgspiceVoltageClock(session, 'v(clock)', .4, .8)
        except ValueError as error:
            if 'ambiguous' not in str(error):
                raise
        else:
            raise ValueError('ngspice clock startup invented a known state')
    return events


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    result = args.work / 'checks.json'
    result.unlink(missing_ok=True)
    cases = [run(args.ngspice.resolve(), args.work / str(index), step)
             for index, step in enumerate((10e-12, 5e-12))]
    maximum = max(abs(a['time_s'] - b['time_s']) for a, b in zip(*cases))
    if maximum > 15e-12:
        raise ValueError('voltage clock timing did not converge under step refinement')
    result.write_text(json.dumps(dict(status='pass', cases=cases, maximum_refinement_s=maximum), indent=2) + '\n')
    print('PASS achieved electrical clock edges, in-band noise, end guard and ambiguous startup')
