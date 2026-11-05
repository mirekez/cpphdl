"""Reject a missing PHY, collapsed TX clocks and a swapped forwarding input."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from digital_connections import check, nodes


def run(tree):
    result = check(tree)
    for fault in ('missing_phy', 'shared_tx_clock', 'wrong_forwarding_input', 'missing_sar'):
        damaged = copy.deepcopy(tree)
        modules = damaged['modulesp']
        top = next(m for m in modules if m.get('origName') == 'RepeaterDigital')
        if fault == 'missing_phy':
            top['stmtsp'] = [n for n in top['stmtsp'] if not (n.get('type') == 'CELL' and n['name'] == 'port_b')]
        elif fault == 'shared_tx_clock':
            cell = next(n for n in nodes(top['stmtsp']) if n.get('type') == 'CELL' and n['name'] == 'port_b')
            pin = next(p for p in cell['pinsp'] if p['name'] == 'tx_clk')
            pin['exprp'][0]['name'] = 'tx_a_clk'
        elif fault == 'wrong_forwarding_input':
            assignment = next(n for n in nodes(top['stmtsp']) if n.get('type') == 'ASSIGNW'
                              and n['lhsp'][0].get('name') == 'forwarding__rx_b_in')
            assignment['rhsp'][0]['name'] = 'port_a__receive_out'
        else:
            bank = next(m for m in modules if m.get('origName') == 'InterleavedSARBankController')
            instance = next(n for n in nodes(bank['stmtsp']) if n.get('type') == 'CELL')
            instance['type'] = 'COMMENT'
        try:
            check(damaged)
        except ValueError:
            continue
        raise ValueError('connectivity audit accepted ' + fault)
    result['rejected_mutations'] = 4
    return result


if __name__ == '__main__':
    result = run(json.loads(Path(sys.argv[1]).read_text()))
    print(json.dumps(result, indent=2))
