#!/usr/bin/env python3
"""Render actual mask polygons and optional placement-derived element labels."""
import argparse,json,tempfile,xml.etree.ElementTree as ET
from pathlib import Path
import klayout.db as db
import klayout.lay as lay
from element_names import names_for, source_hashes

LAYERS={1:'#60db7c',5:'#e77773',6:'#d6f3fa',8:'#54b9f2',19:'#e6effb',10:'#d4cedf',
        29:'#efbaa0',30:'#f57e87',49:'#c5d49a',50:'#9bdd63',66:'#e7b57a',67:'#e9ce5b',
        125:'#ffe3be',126:'#ffe3be',133:'#f8ad61',134:'#f8ad61'}

def label_region_image(path, labels, box, width, height):
    """Put a region's hierarchical names in the margins with locator lines."""
    from PIL import Image, ImageDraw, ImageFont

    image=Image.open(path).convert('RGBA')
    overlay=Image.new('RGBA',image.size,(0,0,0,0))
    draw=ImageDraw.Draw(overlay)
    font_path=Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
    font=ImageFont.truetype(str(font_path),18) if font_path.is_file() else ImageFont.load_default(size=18)
    positioned=[]
    for x,y,name in labels:
        px=(x-box.left)/box.width()*width
        py=(box.top-y)/box.height()*height
        if not 0<=px<width or not 0<=py<height:raise ValueError('element outside rendered region: '+name)
        positioned.append((px,py,name))
    positioned.sort(key=lambda item:(item[0],item[2]))
    rows=(35,95,155,height-175,height-115,height-55)
    for band,row_y in enumerate(rows):
        items=positioned[band::len(rows)]
        boxes=[]
        for px,py,name in items:
            bounds=draw.textbbox((0,0),name,font=font)
            text_width=bounds[2]-bounds[0];text_height=bounds[3]-bounds[1]
            boxes.append([0,row_y,text_width,text_height,px,py,name])
        free=width-40-sum(entry[2] for entry in boxes)
        if free<0:raise ValueError(f'element labels exceed region width by {-free:.1f} px')
        gap=free/(len(boxes)-1) if len(boxes)>1 else 0
        cursor=20
        for entry in boxes:
            entry[0]=cursor
            cursor+=entry[2]+gap
        for left,y,text_width,text_height,px,py,name in boxes:
            if left<18 or left+text_width>width-18:
                raise ValueError(f'element label outside canvas: {name}; '
                                 f'left={left:.1f}, right={left+text_width:.1f}, width={width}')
            center_x=left+text_width/2
            endpoint_y=y+text_height+6 if y<height/2 else y-6
            draw.line((center_x,endpoint_y,px,py),fill=(255,255,255,105),width=1)
            draw.ellipse((px-2,py-2,px+2,py+2),fill=(255,255,255,230))
            draw.rounded_rectangle((left-4,y-4,left+text_width+4,y+text_height+4),
                                   radius=3,fill=(16,25,35,220))
            draw.text((left,y),name,font=font,fill=(255,255,255,255))
    Image.alpha_composite(image,overlay).convert('RGB').save(path,optimize=True)
    return len(positioned)

def element_positions(layout,components,semantic_names):
    semantic={item['physical_name']:item['hierarchical_name'] for item in semantic_names}
    labels=[]
    for part in components:
        physical=part['name'];cell=layout.cell(physical)
        if cell is None:raise ValueError('missing placed element cell: '+physical)
        center=cell.dbbox().center();origin=part['origin_um']
        labels.append((origin[0]+center.x,origin[1]+center.y,semantic[physical]))
    if len(labels)!=137 or len({name for _,_,name in labels})!=137:
        raise ValueError('expected 137 distinct physical element names')
    return labels

def image_box(box,width,height):
    center=box.center();span=max(box.width(),box.height()*width/height)*1.03
    return db.DBox(center.x-span/2,center.y-span*height/width/2,
                   center.x+span/2,center.y+span*height/width/2)

def render_named_slice(view,layout,components,semantic_names,bounds,output):
    """Render a full overview and four readable, annotated mask regions."""
    from PIL import Image, ImageDraw, ImageFont

    width=5000;overview_height=330;panel_height=1200;heading_height=52
    labels=element_positions(layout,components,semantic_names)
    panels=[];quarter=bounds.width()/4
    with tempfile.TemporaryDirectory() as temporary:
        temporary=Path(temporary)
        overview=temporary/'overview.png'
        view.save_image_with_options(str(overview),width,overview_height,1,1,1,
                                     image_box(bounds,width,overview_height),False)
        for i in range(4):
            left=bounds.left+i*quarter;right=bounds.left+(i+1)*quarter
            region=db.DBox(left,bounds.bottom,right,bounds.top)
            selected=[item for item in labels if left<=item[0]<(right if i<3 else right+1)]
            if not selected:raise ValueError('empty mask region')
            panel=temporary/f'region_{i+1}.png'
            view_box=image_box(region,width,panel_height)
            view.save_image_with_options(str(panel),width,panel_height,1,1,1,view_box,False)
            label_region_image(panel,selected,view_box,width,panel_height)
            panels.append((panel,left,right,len(selected)))

        total_height=overview_height+4*(heading_height+panel_height)
        image=Image.new('RGB',(width,total_height),(16,25,35))
        draw=ImageDraw.Draw(image)
        font_path=Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
        font=ImageFont.truetype(str(font_path),24) if font_path.is_file() else ImageFont.load_default(size=24)
        image.paste(Image.open(overview).convert('RGB'),(0,0))
        for i,(panel,left,right,count) in enumerate(panels):
            top=overview_height+i*(heading_height+panel_height)
            title=(f'adc_sar_slice / region {i+1} of 4   '
                   f'x = {left:.1f}–{right:.1f} µm   {count} named elements')
            draw.text((24,top+11),title,font=font,fill='white')
            image.paste(Image.open(panel).convert('RGB'),(0,top+heading_height))
        image.save(output,optimize=True)
    return len(labels)

def render(gds,placement,destination,label_all=False,only_full=False):
    destination.mkdir(parents=True,exist_ok=True);g=json.loads(placement.read_text())
    layout=db.Layout();layout.read(str(gds));bounds=layout.top_cell().dbbox()
    semantic_names=None
    if label_all:
        mapping=json.loads(Path(__file__).with_name('element_names.json').read_text())
        semantic_names=names_for(g)
        if mapping['model_source_sha256']!=source_hashes() or mapping['elements']!=semantic_names:
            raise ValueError('semantic element map differs from the active slice topology')
    source=next(p for p in g['components'] if p['name']=='D3')
    x,y=source['origin_um'];sampling=db.DBox(x-30,bounds.bottom,x+360,bounds.top)
    with tempfile.TemporaryDirectory() as temporary:
        properties=ET.Element('layer-properties')
        for number,color in LAYERS.items():
            p=ET.SubElement(properties,'properties')
            for k,v in {'name':str(number),'source':f'{number}/0','frame-color':color,'fill-color':color,'visible':'true','valid':'true','dither-pattern':'I0'}.items():ET.SubElement(p,k).text=v
        lyp=Path(temporary)/'layers.lyp';ET.ElementTree(properties).write(lyp)
        view=lay.LayoutView()
        try:
            view.set_config('background-color','#101923');view.set_config('grid-visible','false');view.set_config('text-visible','false')
            view.load_layout(str(gds),False);view.load_layer_props(str(lyp),0,False);view.max_hier()
            if label_all:
                output=destination/'adc_sar_slice.png'
                print(f'Labeled {render_named_slice(view,layout,g["components"],semantic_names,bounds,output)} elements in {output}')
            outputs=[] if label_all else [('adc_sar_slice',bounds,3200,220)]
            if not only_full:outputs.append(('adc_sar_slice_sampling',sampling,1800,760))
            for name,box,width,height in outputs:
                box=image_box(box,width,height)
                output=destination/(name+'.png')
                view.save_image_with_options(str(output),width,height,1,1,1,box,False)
        finally:view.destroy()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('gds','placement','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--label-all',action='store_true',help='add every white hierarchical element name to the full-slice PNG')
    p.add_argument('--only-full',action='store_true',help='leave the existing sampling crop unchanged')
    a=p.parse_args();render(a.gds,a.placement,a.output,a.label_all,a.only_full)
