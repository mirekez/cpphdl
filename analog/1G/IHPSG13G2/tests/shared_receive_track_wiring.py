"""Check all13 exported acquisition cells connect to actual translated loads."""
from collections import defaultdict
from pathlib import Path
import json
import re
import sys


def inspect(model, deck):
    groups = defaultdict(list)
    owner = file = None
    for line in model.splitlines():
        match = re.search(r'// device (\d+), IC ([^,]+), .*/([^/]+[.]hpp):\d+', line)
        if match:
            owner, file = match.groups()[1:]
        match = re.search(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+),', line)
        if match:
            groups[owner, file].append(match.groups())
    obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
    by_file = lambda f: [p for (_, name), p in groups.items() if name == f]
    def unique(matches):
        if len(matches) != 1:
            raise ValueError('missing or aliased TRACK connection')
        return matches[0]
    inverters = by_file('inverter.hpp')
    outputs = []
    for i in range(17):
        control, complement, low, low_n = (obs[str(97 + 4 * i + j)] for j in range(4))
        parts = unique([p for p in by_file('cascoded_level_shifter.hpp')
                        if len(p) == 6 and p[5][2] == control])
        if (parts[4][2] != complement or [parts[j][1] for j in (2, 3)] != [low, low_n]
                or any(parts[j][2] != obs['3'] for j in (2, 3))):
            raise ValueError('changed translator bias or core exposure connection')
        outputs.append(tuple(unique([p for p in inverters if p[0][2] == parts[j][0]])[0][0]
                             for j in (1, 0)))
    if outputs[13][0] != obs['5']:
        raise ValueError('shared strobe bypasses translation')
    pulse_nand = unique([p for p in by_file('cmos_logic.hpp') if p[0][0] == obs['168']])
    if [p[2] for p in pulse_nand[:2]] != [obs['5'], obs['167']]:
        raise ValueError('pulse shaper bypasses translated strobe or delay')
    for output, gate in ((obs['167'], obs['166']), (obs['165'], obs['168'])):
        inverter = unique([p for p in inverters if p[0][0] == output])
        if any(p[2] != gate for p in inverter):
            raise ValueError('pulse inverter connection changed')
    samplers = by_file('mos_track_hold.hpp')
    gates = by_file('shared_receive_track.hpp')
    latches = by_file('cmos_logic.hpp')
    for i in range(13):
        select, track, clock_n, held, state_n, set_n, reset_n = (obs[str(6 + 7 * i + j)] for j in range(7))
        if select != obs[str(97 + 4 * i)]:
            raise ValueError('selection probe bypasses translator')
        setting = unique([p for p in gates if len(p) == 12 and p[0][0] == set_n])
        resetting = unique([p for p in gates if len(p) == 8 and p[1][0] == reset_n])
        if [p[2] for p in setting[:6]] != [outputs[14][0], outputs[15][0], obs['165'],
                outputs[(i + 11) % 13][0], outputs[i][1], outputs[16][0]]:
            raise ValueError('incorrect enable/precharge/open/close/allow connection')
        if [p[2] for p in resetting[:4]] != [outputs[14][1], obs['165'], outputs[i][0], outputs[15][1]]:
            raise ValueError('closing incorrectly qualified or disconnected')
        for output, active, feedback in ((track, set_n, state_n), (state_n, reset_n, track)):
            parts = unique([p for p in latches if p[0][0] == output])
            if [p[2] for p in parts[:2]] != [active, feedback]:
                raise ValueError('local retention loop disconnected')
        sampler = unique([p for p in samplers if p[0][1] == held])
        if sampler != [(obs['0'], held, track), (held, obs['0'], clock_n),
                       (held, held, clock_n), (held, held, track)]:
            raise ValueError('sampler main/dummy load disconnected or reordered')
        inverter = unique([p for p in inverters if p[0][0] == clock_n])
        if any(p[2] != track for p in inverter):
            raise ValueError('sampler complement bypasses local state')
    return dict(status='pass', slices=13, physical_translators=17,
                scope='exported translator/state/sampler wiring; no analog delay qualification')


if __name__ == '__main__':
    case = Path(sys.argv[1])
    print(json.dumps(inspect((case / 'design.va').read_text(), (case / 'bench.cir').read_text()), indent=2))
