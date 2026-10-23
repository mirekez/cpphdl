#!/usr/bin/env python3
"""Check every host control against an independently clocked native controller."""
import argparse
import bisect
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from hardware import configure_cleanup, execute, source_dependencies
from compare import read_spice
from xspice_live import headers
from xspice_bridges import dac_bridge_cards


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    host = SOURCE.with_name('sim_adc_controller.cpp')
    include = headers(args.ngspice)
    hashes = source_dependencies([host], ANALOG.parent, args.cxx,
        ['-std=c++17', '-I' + str(ANALOG.parent / 'include'), '-I' + str(include)])
    installation = args.ngspice.resolve().parents[1]
    paths = [SOURCE, SOURCE.parents[1] / 'xspice_live.py', SOURCE.parents[1] / 'xspice_bridges.py', args.bench.resolve(), args.ngspice.resolve(),
             installation / 'share/ngspice/scripts/spinit']
    paths += list((installation / 'lib/ngspice').glob('*.cm')) + list((include / 'ngspice').glob('*.h'))
    hashes.update({str(p): digest(p) for p in paths})
    library = work / 'controller.so'
    with (work / 'build.log').open('w') as log:
        execute([args.cxx, '-std=c++17', '-O1', '-fPIC', '-shared', '-I' + str(ANALOG.parent / 'include'),
                 '-I' + str(include), str(host), '-o', str(library)], stdout=log, stderr=-2,
                check=True, timeout=120, memory_exclusive=False)
    cases = {}
    for count in (1, 4, 16):
        bursts = 2 if count == 16 else 1
        clocks, launches = count * bursts + 24, count * bursts
        case = work / str(count)
        case.mkdir(exist_ok=True)
        # Keep the simulation string relative: ngspice lowercases some quoted
        # paths when a model card has several string parameters.
        import shutil
        shutil.copyfile(library, case / 'controller.so')
        inputs = ['dec' + str(i) for i in range(count)]
        digital_inputs = ['din' + str(i) for i in range(count)]
        digital_outputs = ['dout' + str(i) for i in range(count * 9)]
        analog_outputs = ['out' + str(i) for i in range(count * 9)]
        deck = ['C++RTL XSPICE controller port and clock contract',
                'Vclock clock 0 PULSE(0 1 2n 2p 2p 1.998n 4n)']
        for i in range(count):
            state = lambda epoch: ((epoch + i * 7) >> (i % 5)) & 1
            points = ['0', str(1.2 * state(0))]
            for epoch in range(1, clocks):
                time = epoch * 4e-9
                previous, current = state(epoch - 1), state(epoch)
                points += [format(time, '.17g'), str(1.2 * previous),
                           format(time + .1e-9, '.17g'), str(1.2 * current)]
            deck.append(f'Vdecision{i} dec{i} 0 PWL({" ".join(points)})')
        deck += [f'Ainputs [clock {" ".join(inputs)}] [dclock {" ".join(digital_inputs)}] inputs',
                 f'Acontroller [dclock {" ".join(digital_inputs)}] [{" ".join(digital_outputs)}] null controller',
                 dac_bridge_cards(digital_outputs, analog_outputs, 'outputs', 'outputs'),
                 '.model inputs adc_bridge(in_low=.36 in_high=.84 rise_delay=1p fall_delay=1p)',
                 '.model outputs dac_bridge(out_low=0 out_high=1 out_undef=.5 t_rise=100p t_fall=100p)',
                 f'.model controller d_cosim(simulation="./controller.so" sim_args=["{count}" "{bursts}" "controls.csv" "bridge_events.csv"] delay=1p)']
        for i in range(count):
            terms = [f'{1 << bit}*v(out{i * 9 + bit})' for bit in range(9)]
            deck.append(f'Bencoded{i} encoded{i} 0 V=' + '+'.join(terms))
        deck += ['.control', 'set num_threads=1', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
                 f'tran 10p {clocks * 4e-9:.17g} 0 5p', 'wrdata spice.dat ' + ' '.join(
                     f'v(encoded{i})' for i in range(count)), 'quit', '.endc', '.end']
        (case / 'bench.cir').write_text('\n'.join(deck) + '\n')
        with (case / 'ngspice.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=case,
                    stdout=log, stderr=-2, check=True, timeout=120)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found', (case / 'ngspice.log').read_text(), re.I):
            raise ValueError('XSPICE port smoke simulation failed')
        with (case / 'controls.csv').open() as stream:
            actual = list(csv.DictReader(stream))
        if [int(row['clock']) for row in actual] != list(range(-1, clocks)):
            raise ValueError('missing/extra XSPICE controller clock')
        server = subprocess.Popen([str(args.bench.resolve()), 'server', str(count)], text=True,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            header = server.stdout.readline()
            expected = [next(csv.DictReader([header, server.stdout.readline()]))]
            for clock in range(clocks):
                mask = int(expected[-1]['convert']) & ((1 << count) - 1)
                feedback = sum((((clock + i * 7) >> (i % 5)) & 1) << i for i in range(count)) & mask
                server.stdin.write(f'{feedback} {int(clock < launches)}\n')
                server.stdin.flush()
                expected.append(next(csv.DictReader([header, server.stdout.readline()])))
            server.stdin.close()
            if server.wait(timeout=5) or expected != actual:
                raise ValueError('continuous XSPICE controls differ from the independently stepped native controller')
        finally:
            if server.poll() is None:
                server.kill()
                server.wait()
            for stream in (server.stdin, server.stdout, server.stderr):
                if not stream.closed:
                    stream.close()
        rows = read_spice(case / 'spice.dat', ['time_s'] + ['encoded' + str(i) for i in range(count)])
        times = [row[0] for row in rows]
        worst = 0.0
        for record in actual[1:]:
            time = (int(record['clock']) + .5) * 4e-9 + .3e-9
            index = bisect.bisect_left(times, time)
            a, b = rows[index - 1], rows[index]
            for i in range(count):
                value = a[i + 1] + (b[i + 1] - a[i + 1]) * (time - a[0]) / (b[0] - a[0])
                target = 255 - int(record['trial' + str(i)]) + 256 * ((int(record['acquire']) >> i) & 1)
                worst = max(worst, abs(value - target))
        if worst > .01:
            raise ValueError('XSPICE output bit/polarity differs: ' + str(worst))
        cases[str(count)] = dict(clocks=clocks, every_control_exact=True, maximum_encoded_output_error=worst,
            artifact_sha256={str(p): digest(p) for p in case.iterdir() if p.is_file()})
    if any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('XSPICE host sources changed during qualification')
    (work / 'checks.json').write_text(json.dumps(dict(status='pass', cases=cases, source_sha256=hashes), indent=2) + '\n')
    print('PASS continuous C++RTL XSPICE controller: 1/4/16 slices, reuse, every input/control and encoded DAC output')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
