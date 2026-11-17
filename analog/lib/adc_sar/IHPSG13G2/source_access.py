"""Physical multi-row source contacts for a reviewed straight-finger MOS row."""
import math
import klayout.db as db

def strengthen_sources(layout,top,geometry,nets,metal4_halfwidth=.41,widen_bus=False):
    if not math.isclose(layout.dbu,.001) or geometry.get('via_columns')!=2:
        raise ValueError('source access requires the reviewed 1 nm / two-column row')
    if metal4_halfwidth not in (.41,.83):
        raise ValueError('unreviewed source escape width')
    def box(cell,layer,x0,y0,x1,y1):
        cell.shapes(layout.layer(layer,0)).insert(db.Box(*(round(v/.001) for v in (x0,y0,x1,y1))))
    def cut(cell,layer,x,y):box(cell,layer,x-.095,y-.095,x+.095,y+.095)
    source_access=[]
    for part in geometry['components']:
        if part['kind']!='mos' or part['connections']['S'] not in nets:continue
        g=part['geometry'];cell=layout.cell(part['name'])
        sx,sy=g['terminals_um']['S']
        if g['finger_width_um']<3 or sy!=.45:
            raise ValueError('unreviewed source strap geometry')
        # Each alternating source diffusion has a Metal1 stripe and one
        # original Via1 at y=.45. Add two aligned rows, not overlapping cuts.
        # Diffusion, channel, contacts and the PDK PCell remain untouched.
        layer=layout.layer(19,0)
        centers=[s.bbox().center().x*.001 for s in cell.each_shape(layer)
                 if s.is_box() and abs(s.bbox().center().y*.001-sy)<1e-9]
        if len(centers)!=g['source_straps']:raise ValueError('source Via1 inventory changed')
        for x in centers:
            box(cell,8,x-.16,sy-.16,x+.16,sy+.84+.16)
            box(cell,10,x-.2,sy-.2,x+.2,sy+.84+.2)
            for dy in (.42,.84):cut(cell,19,x,sy+dy)
        if widen_bus:box(cell,10,min(centers)-.2,sy-.2,max(centers)+.2,sy+1.04)
        dx,dy=part['origin_um'];px,py=dx+sx,dy+sy
        escape=next(e for e in geometry['escapes'] if e['component']==part['name'] and e['terminal']=='S')
        ex,ey=escape['location_um']
        if abs(ey-py)>1e-9:raise ValueError('source escape is not aligned')
        # Double the M2/M3 and M3/M4 access rows, retaining the original cuts.
        box(top,10,px-.41,py-.2,px+.41,py+.62)
        box(top,30,min(ex,px)-.41,py-.2,max(ex,px)+.41,py+.62)
        box(top,50,ex-metal4_halfwidth,py-.2,ex+metal4_halfwidth,py+.62)
        for offset in (-.21,.21):
            cut(top,29,px+offset,py+.42)
            cut(top,49,ex+offset,py+.42)
        source_access.append(dict(component=part['name'],source_straps=len(centers),via1_per_strap=3,via2=4,via3=4))
    return source_access
