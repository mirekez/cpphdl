"""Apply IHP's fill patterns to this standalone macro's extent.

No sealring is invented. Foundry exclusions remain intact. The fill window is
adapted from EdgeSeal.holes to source.extent, as required for a standalone block.
"""
import gc,os,re
from pathlib import Path
import pya
tech=Path(os.environ['PDK_ROOT'])/'ihp-sg13g2/libs.tech/klayout/tech'
for group in ('ActGatP','Metal','TopMetal'):
    macro=pya.Macro(str(tech/f'macros/sg13g2_filler_{group}.lym'))
    if 'EdgeSeal.holes' not in macro.text:raise ValueError('PDK fill window changed')
    macro.text=macro.text.replace('EdgeSeal.holes','source.extent')
    if group=='ActGatP':
        if macro.text.count('.fill(')!=2:raise ValueError('PDK active/poly fill changed')
        macro.text='deep\n'+macro.text.replace('.fill(','.flatten.fill(')
    if group=='Metal':
        macro.text,count=re.subn(r"('distance_m([1-5])' => )\d+(?:\.\d+)?",r'\g<1>1.0',macro.text)
        if count!=5:raise ValueError('PDK metal-fill configuration changed')
    macro.run();del macro;gc.collect();print('Filled',group,flush=True)
pya.CellView.active().layout().write(output_file)
