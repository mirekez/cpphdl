"""Real upper-metal supply conductors and via arrays for the one-slice row.

Existing transistor terminals and signal routing are retained. External supply
ports enter at the centers of the upper rails. DRC/LVS/RC must be rerun.
"""
import math
import klayout.db as db

def add_power(layout,top,g):
    if not math.isclose(layout.dbu,.001) or 'upper_power' in g:raise ValueError('unsupported power overlay')
    def box(layer,x0,y0,x1,y1):top.shapes(layout.layer(layer,0)).insert(db.Box(*(round(v/.001) for v in (x0,y0,x1,y1))))
    def cuts(layer,x,y,nx,ny,pitch,size):
        for i in range(nx):
            for j in range(ny):
                cx=x+(i-(nx-1)/2)*pitch;cy=y+(j-(ny-1)/2)*pitch
                box(layer,cx-size/2,cy-size/2,cx+size/2,cy+size/2)
    def source_landing(x,y):
        # Connect at the source's Metal4 escape, bypassing its long, narrow
        # vertical branch. The 2x2 Via4 array fits the existing 0.82 um column.
        box(50,x-.41,y-.41,x+.41,y+.41)
        box(67,x-.84,y-.84,x+.84,y+.84)
        cuts(66,x,y,2,2,.42,.19)
        cuts(125,x,y,2,2,.84,.42)
    rails={}
    for net in ('VDD','VSS'):
        old=g['terminals'][net];points=[p['location_um'][0] for p in g['escapes'] if p['net']==net]
        left,right=min(points)-.2,max(points)+.2;y=old['y_um']
        if old['layer']!=67:raise ValueError('power overlay requires original Metal5 supply')
        if net=='VDD':
            upper=top.bbox().top*.001+20
            # The first/last source stems lie near the original M5 endpoints;
            # retain a full 0.5 um TopMetal2 enclosure around their via cuts.
            box(134,left-2,upper-10,right+2,upper+10)
            stems=[]
            for part in g['components']:
                if part['kind']!='mos' or part['connections']['S']!='VDD':continue
                e=next(e for e in g['escapes'] if e['component']==part['name'] and e['terminal']=='S')
                x,endpoint=e['location_um']
                box(126,x-1.9,y-1.9,x+1.9,upper+4)
                source_landing(x,endpoint)
                # Separate vertical collectors leave TopMetal1 clear for
                # VSS stems crossing the original VDD Metal5 rail.
                cuts(125,x,y,2,2,.84,.42)
                box(126,x-2,upper-4,x+2,upper+4)
                cuts(133,x,upper,2,4,1.96,.9);stems.append(x)
            if not stems:raise ValueError('no VDD source collectors')
            rails[net]=dict(layer=134,width_um=20,x0_um=left-2,x1_um=right+2,y_um=upper,stems=stems,topvia2_per_stem=8)
        else:
            upper=y;box(134,left,upper-15,right,upper+15)
            box(126,left,upper-2,right,upper+2)
            count=0
            for i in range(math.floor((right-left-4)/8.4)+1):
                x=left+2+i*8.4;box(126,x-2,y-2,x+2,y+2)
                cuts(125,x,y,4,4,.84,.42);cuts(133,x,y,2,2,1.96,.9);count+=1
            stems=[]
            for part in g['components']:
                if part['kind']!='mos' or part['connections']['S']!='VSS':continue
                e=next(e for e in g['escapes'] if e['component']==part['name'] and e['terminal']=='S')
                x,endpoint=e['location_um']
                box(126,x-1.9,upper-1.9,x+1.9,endpoint+1.9)
                source_landing(x,endpoint)
                # Join the continuous ground collector; its regular via
                # arrays avoid overlapping cuts at arbitrary source pitches.
                stems.append(x)
            rails[net]=dict(layer=134,width_um=30,x0_um=left,x1_um=right,y_um=upper,landings=count,topvia1_per_landing=16,topvia2_per_landing=4,stems=stems)
        # Move the existing external port, preserving a single authoritative
        # physical location in GDS, DEF and the extracted model.
        old_point=db.Point(round(old['x_um']/.001),round(old['y_um']/.001))
        for shape in list(top.each_shape(layout.layer(67,25))):
            if shape.is_text() and shape.text.string==net:shape.delete()
        for shape in list(top.each_shape(layout.layer(67,2))):
            if shape.bbox().contains(old_point):shape.delete()
        x=round((left+right)/2,3)
        top.shapes(layout.layer(134,2)).insert(db.Box(round((x-.16)/.001),round((upper-.16)/.001),round((x+.16)/.001),round((upper+.16)/.001)))
        top.shapes(layout.layer(134,25)).insert(db.Text(net,db.Trans(round(x/.001),round(upper/.001))))
        g['terminals'][net]=dict(layer=134,x_um=x,y_um=upper)
    g['upper_power']=rails
    # The sampling capacitor's bottom plate must not discharge through the
    # narrow Metal4 signal escape: extraction measured about 14 ohm there.
    # Connect its Metal5 electrode directly to the TopMetal2 ground rail,
    # outside the TopMetal1 top electrode, preserving both capacitor plates.
    storage=[]
    for part in g['components']:
        if part['kind']!='capacitor' or part['connections'].get('MINUS')!='VSS':continue
        pg=part['geometry']
        if pg.get('requested_capacitance_F',0)<1e-12:continue
        pin=pg['terminals']['MINUS']
        if pin['layer']!=67:raise ValueError('unreviewed storage ground electrode')
        dx,dy=part['origin_um'];bounds=layout.cell(part['name']).bbox()
        x=dx+bounds.right*.001+6;y=dy+pin['y_um']
        px=dx+pin['x_um']
        box(67,px-.2,y-1.6,x+2,y+1.6)
        box(67,x-2,y-2,x+2,y+2)
        box(126,x-2,y-2,x+2,y+2)
        cuts(125,x,y,4,4,.84,.42)
        cuts(133,x,y,2,2,1.96,.9)
        ground=rails['VSS']['y_um'];box(134,x-2,ground-2,x+2,y+2)
        storage.append(dict(component=part['name'],x_um=x,y_um=y,topvia1=16,topvia2=4))
    if len(storage)!=1:raise ValueError('expected exactly one main sampling capacitor')
    g['storage_returns']=storage
