"""Bounded IHP PSP/R3/MIM lowering, preserving physical instance geometry."""
import hashlib,json,math,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path[:0]=[str(ROOT/'analog/basic/IHPSG13G2'),str(ROOT/'analog/1G/IHPSG13G2')]
import parameters
from extract_analog import digest


def statements(text):
    result=[]
    for raw in text.splitlines():
        line=raw.split(';')[0].strip()
        if not line or line.startswith('*'):continue
        if line.startswith('+'):
            if not result:raise ValueError('orphan continuation')
            result[-1]+=' '+line[1:]
        else:result.append(line)
    return result


def sequential(text,scope,overrides=None):
    pattern=re.compile(r"([A-Za-z_$][\w$]*)\s*=\s*('[^']*'|\{[^}]*\}|[^\s]+)")
    end=0;out={}
    for m in pattern.finditer(text):
        if text[end:m.start()].strip():raise ValueError('unparsed process parameter')
        name=m[1].lower();value=m[2]
        if name in out:raise ValueError('duplicate process parameter')
        # Only these pinned deterministic R3 terms need syntax beyond PSP.
        if value=="'agauss(0, 1, 1)'":v=0.
        elif re.fullmatch(r'\((a0|p0)>0\)\*\1',value):v=max(0,scope[value[1:3]])
        elif overrides and name in overrides:v=overrides[name]
        else:v=parameters.expression(value.replace('(postsim>0)',str(int(scope.get('postsim',0)>0))),scope)
        out[name]=v;scope[name]=v;end=m.end()
    if text[end:].strip():raise ValueError('unparsed process parameter tail')
    return out


def rppd(pdk,geometry,corner='res_typ'):
    directory=pdk/'ihp-sg13g2/libs.tech/ngspice/models';scope={};active=False
    for line in statements((directory/'cornerRES.lib').read_text()):
        if line.lower().startswith('.lib '):active=line.split()[1]==corner
        elif line.lower().startswith('.endl'):active=False
        elif active and line.lower().startswith('.param '):sequential(line[7:],scope)
        elif active and line.lower()!='.include resistors_mod.lib':raise ValueError('unreviewed resistor corner')
    if not scope or any(scope[k]!=0 for k in ('drsh_rppd','dw_rppd','dl_rppd')):raise ValueError('mismatch R3 is not deterministic qualification')
    section=[];active=False
    for line in statements((directory/'resistors_mod.lib').read_text()):
        if line.lower().startswith('.subckt '):active=line.split()[1]=='rppd'
        elif active and line.lower().startswith('.ends'):break
        elif active:section.append(line)
    if len(section)!=3 or not section[0].startswith('.param ') or not section[1].startswith('NR1 1 bn 2 dt res_rppd '):raise ValueError('R3 wrapper changed')
    scope.update(geometry);sequential(section[0][7:],scope,geometry)
    scope.update(geometry) # Explicit a/p/rz overrides are used by exact body partition.
    instance=sequential(section[1].split('res_rppd ',1)[1],dict(scope))
    # ngspice owns multiplicity; it is not an OpenVAF R3 instance parameter.
    # Unit multiplicity is exact. Reject any future nonunit wrapper explicitly.
    if instance.pop('m',1)!=1:raise ValueError('nonunit R3 multiplicity is unsupported')
    if not section[2].startswith('.model res_rppd r3_cmc '):raise ValueError('R3 model changed')
    model=sequential(section[2].split('r3_cmc ',1)[1],dict(scope))
    return dict(module='r3_cmc',model=model,instance=instance,binary=str(directory.parent/'osdi/r3_cmc.osdi'),
        input_sha256={str(p):digest(p) for p in (directory/'cornerRES.lib',directory/'resistors_mod.lib',directory.parent/'osdi/r3_cmc.osdi')})


def mim(pdk,geometry,temp,corner='cap_typ'):
    directory=pdk/'ihp-sg13g2/libs.tech/ngspice/models';scope={};active=False
    for line in statements((directory/'cornerCAP.lib').read_text()):
        if line.lower().startswith('.lib '):active=line.split()[1]==corner
        elif line.lower().startswith('.endl'):active=False
        elif active and line.lower().startswith('.param '):sequential(line[7:],scope)
    model=next(x for x in statements((directory/'capacitors_mod.lib').read_text()) if x.lower().startswith('.model cmim_core '))
    values=sequential(model.split('(',1)[1].rstrip(')'),dict(scope));w,l=geometry['w']*1e6,geometry['l']*1e6;delta=temp-values['tnom']
    return (w*l*values['cj']+2*(w+l)*values['cjsw'])*(1+values['tc1']*delta+values['tc2']*delta*delta)


def lower(text,pdk,work,temp=27,corner='mos_tt',observations=None):
    work.mkdir(parents=True,exist_ok=True);rows=[f'TEMP {temp:.17g}'];hashes={};bundles={};counts={}
    directory=pdk/'ihp-sg13g2/libs.tech/ngspice/models'
    hashes.update({str(p):digest(p) for p in (directory/'cornerCAP.lib',directory/'capacitors_mod.lib')})
    for name,node in (observations or {}).items():rows.append(f'OBS {name} {node}')
    for line in statements(text):
        f=line.split();name=f[0]
        if name.lower() in ('.subckt','.ends'):continue
        if name[0] in 'RC':
            if len(f) not in (4,5) or (len(f)==5 and f[4].lower() not in ('ic=0','ic=0.0')):raise ValueError('unsupported passive syntax')
            value=parameters.expression(f[3],{})
            # Exact removal of duplicated resistor-body substrate charge can
            # leave a zero capacitor. It contributes no equation or energy.
            if name[0]=='C' and value==0:
                counts['zero_capacitors_omitted']=counts.get('zero_capacitors_omitted',0)+1
                continue
            if not value>0:raise ValueError('nonpositive passive')
            rows.append(f'{name[0]} {name} {f[1]} {f[2]} {value:.17g}');counts[name[0]]=counts.get(name[0],0)+1;continue
        if not name.startswith('X'):raise ValueError('unsupported physical statement: '+line)
        model_index=next((i for i,v in enumerate(f) if v in ('cap_cmim','rppd','sg13_hv_nmos','sg13_hv_pmos','sg13_lv_nmos','sg13_lv_pmos')),None)
        if model_index is None:raise ValueError('unbound process device')
        nodes=f[1:model_index];model=f[model_index];g=parameters.assignments(' '.join(f[model_index+1:]),{})
        if model=='cap_cmim':
            if len(nodes)!=2 or not set(g)<={'w','l','ic'}:raise ValueError('unsupported MIM')
            value=mim(pdk,g,temp);rows.extend([f'R {name}_access {nodes[0]} {name}_plate .055',f'C {name}_storage {name}_plate {nodes[1]} {value:.17g}']);counts['mim']=counts.get('mim',0)+1;continue
        if model=='rppd':
            if len(nodes)!=3 or not set(g)<={'w','l','a','p','rz'}:raise ValueError('unsupported physical resistor')
            bundle=rppd(pdk,g);nodes=[nodes[0],nodes[2],nodes[1],name+'_thermal']
        else:
            if len(nodes)!=4 or not set(g)<={'w','l','ng','as','ad','ps','pd'}:raise ValueError('unsupported MOS geometry')
            family,polarity=model.split('_')[1:];ng=int(g.get('ng',1))
            if ng!=g.get('ng',1) or ng<1:raise ValueError('nonintegral MOS finger count')
            bundle=parameters.psp(pdk,corner,polarity[0],float(f"{g['w']:.12g}"),float(f"{g['l']:.12g}"),family,ng)
            junction={k:g[k] for k in ('as','ad','ps','pd') if k in g}
            if junction:
                if set(junction)!={'as','ad','ps','pd'} or junction['as']<=0:raise ValueError('incomplete/zero extracted source junction; restore from GDS first')
                bundle['instance'].update(junction)
        hashes.update(bundle['input_sha256']);key=json.dumps([bundle['module'],bundle['model'],bundle['instance']],sort_keys=True)
        if key not in bundles:
            path=work/f'model_{len(bundles)}.txt';parameters.write(bundle,path)
            path.write_text('binary '+json.dumps(bundle['binary'])+'\n'+path.read_text());bundles[key]=path
        rows.append('M '+name+' '+' '.join(nodes)+' '+json.dumps(str(bundles[key].resolve())))
        counts[model]=counts.get(model,0)+1
    (work/'circuit.txt').write_text('\n'.join(rows)+'\n')
    receipt=dict(corner=corner,temperature_C=temp,counts=counts,input_netlist_sha256=hashlib.sha256(text.encode()).hexdigest(),process_sha256=hashes,source_sha256={str(Path(__file__).resolve()):digest(__file__),str(Path(parameters.__file__)):digest(parameters.__file__)})
    (work/'lowering.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt
