"""Plot measured first-conversion changes; never substitute a schematic trace."""
import argparse,csv,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from provenance import digest,verify

def run(before,after,output,revision='V12'):
    inputs={};groups=[]
    for paths in (before,after):
        group=[]
        for path in paths:
            c=json.loads((path/'checks.json').read_text())
            if c['status'] not in ('PASS','FAIL') or not c['native']['conversion_complete']:
                raise ValueError('incomplete conversion')
            verify(json.loads((path/'manifest.json').read_text())['artifact_sha256'])
            rows=c['measurements']['samples']
            if rows[0]['input_V']!=.85:raise ValueError('plots compare the same first 0.85 V conversion')
            group.append(rows[0])
            for name in ['checks.json','trace.csv']:
                p=path/name;inputs[str(p.resolve())]=digest(p)
        groups.append(group)
    fig,axes=plt.subplots(1,3,figsize=(15,4.4),layout='constrained')
    colors=['#b3583d','#287fa3'];labels=['V11 baseline',revision+' routed slice']
    for paths,color,label in zip((before,after),colors,labels):
        rows=list(csv.DictReader((paths[0]/'trace.csv').open()))
        rows=[r for r in rows if 27.8e-9<=float(r['time_s'])<=28.6e-9]
        axes[0].plot([float(r['time_s'])*1e9 for r in rows],
                     [float(r['held'])*1e3 for r in rows],color=color,label=label)
    axes[0].axvspan(28.025,28.20,color='#d9a441',alpha=.15,label='Opening transient')
    axes[0].axhline(850,color='#6a737d',lw=.8,ls=':')
    axes[0].set(xlabel='Time (ns)',ylabel='Held voltage (mV)',title='TT sampling switch opens')
    axes[0].legend(fontsize=8)
    for ax,key,scale,limit,title in [(axes[1],'hold_pp_V',1e3,20,'Hold excursion over the conversion'),
                                     (axes[2],'code_error',1,2,'Absolute physical code error')]:
        for j,(rows,color,label) in enumerate(zip(groups,colors,labels)):
            xs=[i+(j-.5)*.34 for i in range(3)];ys=[r[key]*scale for r in rows]
            bars=ax.bar(xs,ys,width=.32,color=color,label=label)
            ax.bar_label(bars,labels=[f'{v:.2f}' if scale>1 else f'{v:.0f}' for v in ys],fontsize=9,padding=3)
        ax.axhline(limit,color='#b22222',ls='--',lw=1,label=f'Limit: {limit}')
        ax.set_xticks(range(3),['TT / 27 C','SS / 125 C','FF / -40 C'])
        ax.set(title=title,ylabel='mV' if scale>1 else 'counts');ax.legend(fontsize=8)
        ax.margins(y=.25)
    fig.suptitle('One extracted SAR slice: first 0.85 V conversion, unchanged limits',fontsize=13)
    output.parent.mkdir(parents=True,exist_ok=True);fig.savefig(output,dpi=180);plt.close(fig)
    verify(inputs)
    output.with_suffix('.json').write_text(json.dumps(dict(scope='first 0.85 V conversion only; not full-range qualification',
        source_sha256=inputs,plot_sha256=digest(output),before=groups[0],after=groups[1]),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before',nargs=3,type=Path,required=True);p.add_argument('--after',nargs=3,type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--revision',default='V12')
    a=p.parse_args();run(a.before,a.after,a.output,a.revision)
