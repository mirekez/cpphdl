"""Lower the actual sampling-input wire/contact resistance without changing W/L.

The V11 input had 29--68 ohm between its pin and sampling diffusions. Retain
every device/net and strengthen only the input conductor and its source access.
This overlay must precede fill and requires fresh DRC, LVS and RC extraction.
"""
import math
import klayout.db as db
from source_access import strengthen_sources


def strengthen_input(layout,top,geometry):
    if not math.isclose(layout.dbu,.001) or geometry.get('via_columns')!=2:
        raise ValueError('input overlay requires the reviewed 1 nm / two-column row')
    if 'input_route' in geometry:raise ValueError('input overlay already applied')
    def box(cell,layer,x0,y0,x1,y1):
        cell.shapes(layout.layer(layer,0)).insert(db.Box(*(round(v/.001) for v in (x0,y0,x1,y1))))
    def cut(cell,layer,x,y):box(cell,layer,x-.095,y-.095,x+.095,y+.095)
    port=geometry['terminals']['IN'];y=port['y_um']
    if port['layer']!=67:raise ValueError('input port must start on Metal5')
    escapes=[e for e in geometry['escapes'] if e['net']=='IN']
    xs=[e['location_um'][0] for e in escapes]
    # At the existing 1 um signal-track pitch, 1 um width retains 0.38 um
    # clearance to adjacent 0.24 um rails (> the PDK 0.24 um wide-line rule).
    box(top,67,min(xs)-.83,y-.5,max(xs)+.83,y+.5)
    for escape in escapes:
        x,endpoint=escape['location_um']
        # Existing columns are 1.56 um apart. Width 1.66 leaves 0.32 um
        # clearance to a neighboring 0.82 um Metal4 escape.
        box(top,50,x-.83,y-.2,x+.83,endpoint+.2)
        for dx in (-.63,.63):cut(top,66,x+dx,y)
    source_access=strengthen_sources(layout,top,geometry,{'IN'},metal4_halfwidth=.83)
    if len(source_access)!=2:raise ValueError('expected complementary sampling pair')
    geometry['input_route']=dict(metal5_width_um=1.,metal4_width_um=1.66,
                                 via4_per_escape=4,source_access=source_access)
