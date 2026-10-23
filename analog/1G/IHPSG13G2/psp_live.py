"""Checkpointed causal PSP prefix simulation, independent of generic VA hosting."""
import csv
import json
import math
import os
import re
import subprocess

from hardware import execute
from live import NgspiceSession


def live_flow(args, work, prepare_process):
    with (work / 'export.log').open('w') as log:
        execute([str(args.bench.resolve()), str(work), 'export', str(args.slices), '.25', str(args.bursts)],
                stdout=log, stderr=-2, check=True, timeout=60)
    metadata = json.loads((work / 'live.json').read_text())
    experiment, setup = prepare_process(work, metadata)
    deck, control = experiment.split('.control\n', 1)
    commands = control.splitlines()
    start = next(line for line in commands if line.startswith('tran '))
    setup += commands[:commands.index(start)]
    output = [line for line in commands if line.startswith(('let observation', 'wrdata '))]
    low, high = metadata['feedback_low_V'], metadata['feedback_high_V']
    transition = metadata['control_transition_s']
    if ((low, high) != (.36, .84) or not math.isfinite(transition)
            or not 0 < transition < metadata['period_s'] / 4):
        raise ValueError('unknown PSP host control or voltage domain')
    requested_timeout = getattr(args, 'prefix_timeout', None)
    timeout = max(300, args.slices * 120) if requested_timeout is None else requested_timeout
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('PSP prefix timeout must be positive')
    checkpoint = work / 'checkpoint.json'
    saved = json.loads(checkpoint.read_text()) if getattr(args, 'resume', False) else None
    fingerprint = args.checkpoint_fingerprint
    if saved is not None and (saved.get('fingerprint') != fingerprint or saved.get('metadata') != metadata):
        raise ValueError('PSP checkpoint has different source, binary, model or experiment inputs')
    waves = {d['id']: [0, 0 if d['bit'] == 8 else 1] for d in metadata['drivers']}
    (work / 'live.cir').write_text(deck + '.end\n')
    server_log = (work / 'controller.log').open('w')
    server = subprocess.Popen([str(args.bench.resolve()), 'server', str(args.slices)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=server_log, text=True)
    try:
        header = server.stdout.readline().strip().split(',')
        if header[:7] != ['clock', 'data', 'valid', 'slice', 'fault', 'acquire', 'convert']:
            raise ValueError('unknown PSP controller server interface')

        def read_control():
            line = server.stdout.readline()
            fields = list(csv.reader([line]))[0] if line else []
            if len(fields) != len(header):
                raise ValueError('incomplete PSP controller row')
            return {name: int(value) for name, value in zip(header, fields)}

        previous = read_control()
        controls, decisions = [previous], []

        def advance(decision):
            nonlocal previous
            clock = len(decisions)
            if decision['clock'] != clock or abs(decision['time_s'] - (clock + .5) * metadata['period_s']) > 1e-15:
                raise ValueError('PSP checkpoint decision schedule differs')
            server.stdin.write(f'{decision["feedback"]} {int(clock < metadata["launches"])}\n')
            server.stdin.flush()
            current = read_control()
            if current['clock'] != clock:
                raise ValueError('PSP controller clock differs')
            edge = decision['time_s']
            for driver in metadata['drivers']:
                slice_index, bit = driver['slice'], driver['bit']
                def value(row):
                    return (row['acquire'] >> slice_index) & 1 if bit == 8 else 1 - ((row['trial' + str(slice_index)] >> bit) & 1)
                if value(current) != value(previous):
                    waves[driver['id']].extend([edge + transition / 100, value(previous), edge + transition, value(current)])
            controls.append(current)
            decisions.append(decision)
            previous = current

        if saved is not None:
            if (len(saved['controls']) != len(saved['decisions']) + 1 or saved['controls'][0] != previous
                    or len(saved['decisions']) > metadata['clocks']):
                raise ValueError('incomplete PSP checkpoint trajectory')
            for index, decision in enumerate(saved['decisions']):
                advance(decision)
                if controls[-1] != saved['controls'][index + 1]:
                    raise ValueError('replayed C++RTL state differs from the PSP checkpoint')

        def save():
            temporary = checkpoint.with_suffix('.tmp')
            temporary.write_text(json.dumps(dict(fingerprint=fingerprint, metadata=metadata,
                controls=controls, decisions=decisions), indent=2) + '\n')
            os.replace(temporary, checkpoint)
            with (work / 'controls.csv').open('w') as stream:
                writer = csv.DictWriter(stream, header)
                writer.writeheader()
                writer.writerows(controls)
            (work / 'decisions.json').write_text(json.dumps(decisions, indent=2) + '\n')

        with NgspiceSession(args.ngspice, work, work / 'interactive.log') as spice:
            spice.command('\n'.join(setup) + '\nsource live.cir')

            def replay(stop):
                prefix = deck
                for driver in metadata['drivers']:
                    identifier = driver['id']
                    pattern = rf'^Vbit{identifier} bit{identifier} 0 PWL\([^\n]+\)$'
                    points = ' '.join(format(x, '.17g') for x in waves[identifier])
                    prefix, changed = re.subn(pattern, f'Vbit{identifier} bit{identifier} 0 PWL({points})', prefix, flags=re.M)
                    if changed != 1:
                        raise ValueError('changed PSP control source')
                (work / 'prefix.cir').write_text(prefix + '.end\n')
                analysis = start.split()
                analysis[2] = format(stop, '.17g')
                # Discard both plots and the previous circuit. Retaining a
                # fresh PSP circuit at every edge otherwise multiplies memory.
                spice.command('destroy all\nremcirc\nsource prefix.cir\n' + ' '.join(analysis), timeout=timeout)

            save()
            for clock in range(len(decisions), metadata['clocks']):
                edge = (clock + .5) * metadata['period_s']
                mask = previous['convert'] & ((1 << args.slices) - 1)
                observed, feedback = [None] * args.slices, 0
                if mask:
                    replay(edge)
                    if abs(spice.last('time') - edge) > 1e-15:
                        raise ValueError('PSP prefix did not reach its feedback edge')
                    for index, node in enumerate(metadata['decisions']):
                        observed[index] = spice.last('v(' + node + ')')
                        if mask & (1 << index):
                            if low <= observed[index] <= high:
                                raise ValueError(f'unresolved PSP decision at clock {clock}, slice {index}')
                            feedback |= int(observed[index] > high) << index
                advance(dict(clock=clock, time_s=edge, sampled=bool(mask), voltage_V=observed, feedback=feedback))
                save()
                print(f'PSP controller edge {clock + 1}/{metadata["clocks"]}', flush=True)
            replay(float(start.split()[2]))
            spice.command('\n'.join(output), timeout=timeout)
        server.stdin.close()
        if server.wait(timeout=5):
            raise ValueError('PSP controller server failed')
    finally:
        if not server.stdin.closed:
            server.stdin.close()
        if server.poll() is None:
            server.kill()
            server.wait()
        server.stdout.close()
        server_log.close()
