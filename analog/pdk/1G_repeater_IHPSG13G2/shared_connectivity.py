"""Audit real OpenDB terminals and retain unresolved analog control ports.

Run with OpenROAD -python after placement. Internal analog/digital crossings
stop being artificial top-level I/O pins; unresolved controls remain explicit.
"""
import json
import os
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from shared_floorplan import digital_bit


def run():
    import odb
    work=Path(os.environ['SHARED_WORK'])
    plan=json.loads((work/'analog.json').read_text())
    leaves=json.loads((work/'leaves/leaves.json').read_text())
    database=odb.dbDatabase.create()
    odb.read_db(database,str(work/'placed.odb'))
    block=database.getChip().getBlock()
    ports={p['name']:p for p in plan['ports']}
    canonical={ports['VDD']['node']:'AVDD',ports['VLOGIC']['node']:'VDD',ports['VSS']['node']:'VSS'}
    internal=[]
    for p in plan['ports']:
        if p['digital']:
            term=block.findBTerm(digital_bit(p['digital']))
            if term is None:
                raise ValueError('missing RTL crossing: '+p['digital'])
            canonical[p['node']]=term.getNet().getName()
            internal.append(term.getName())
    for lane in range(8):
        for polarity in ('P','N'):
            pad=f'mdi_{"a" if lane<4 else "b"}_{lane%4}_{polarity.lower()}'
            canonical[ports[f'{polarity}_{lane}']['node']]=pad
            canonical[ports[f'SENSE_{polarity}_{lane}']['node']]=pad+'_core'
    def expected(node):
        name=canonical.get(node,'analog_n'+str(node))
        net=block.findNet(name)
        if net is None:
            raise ValueError('missing circuit net '+name)
        return net
    checked=0
    for d in leaves['instances']:
        inst=block.findInst(d['name'])
        if inst is None or inst.getMaster().getName()!=d['master']:
            raise ValueError('missing or wrong analog leaf '+d['name'])
        for pin,node in d['connections'].items():
            terminal=inst.findITerm(pin)
            if terminal is None or terminal.getNet() is None or terminal.getNet().getId()!=expected(node).getId():
                raise ValueError('wrong physical connection '+d['name']+'.'+pin)
            checked+=1
    if len({expected(ports[p]['node']).getId() for p in ('VDD','VLOGIC','VSS')})!=3:
        raise ValueError('shorted power domains')
    exposed=[]
    for p in plan['ports']:
        if p['digital'] or p['direction']!='input' or p['name'].startswith('SENSE_'):
            continue
        term=odb.dbBTerm_create(expected(p['node']),p['name'])
        term.setIoType('INPUT')
        exposed.append(p['name'])
    if len(exposed)!=88:
        raise ValueError('changed unresolved analog control inventory')
    # Pad-internal ports also have physical drivers/loads in the same database.
    internal+=['enable_in','active_out']
    for name in set(internal):
        term=block.findBTerm(name)
        if term is not None:
            odb.dbBTerm_destroy(term)
    odb.write_db(database,str(work/'connected.odb'))
    report=dict(status='physical_connections_checked',full_chip_qualified=False,
                physical_analog_leaves=len(leaves['instances']),checked_leaf_terminals=checked,
                digital_crossings=sum(bool(p['digital']) for p in plan['ports']),
                unresolved_analog_control_ports=exposed,remaining_top_ports=len(block.getBTerms()),
                power_grid_qualified=False,timing_gate=False)
    (work/'connectivity.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('status','checked_leaf_terminals','digital_crossings','remaining_top_ports')}),flush=True)

if __name__=='__main__':
    run()
