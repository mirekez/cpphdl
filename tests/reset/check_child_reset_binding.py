#!/usr/bin/env python3
"""Compare scalar child reset bindings through C++ -> SV -> hdlcpp, without Verilator."""
import argparse,hashlib,json,os,re,resource,subprocess,tempfile
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('cpphdl','hdlcpp','cxx','work'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[2];base=a.work.resolve();base.mkdir(parents=True,exist_ok=True)
    # hdlcpp deliberately refuses to overwrite its output. Preserve each run's
    # evidence and permit repeated CTest invocations with the same --work base.
    work=Path(tempfile.mkdtemp(prefix='run-',dir=base))
    include=root/'include';fixture=Path(__file__).with_name('ChildResetBinding.h')
    report={'status':'building','work':str(work),'commands':[],'negative_cases':[],'verilator':False}
    def save():
        data=json.dumps(report,indent=2)+'\n'
        temporary=work/'results.json.tmp';temporary.write_text(data);temporary.replace(work/'results.json')
        # A unique temporary also keeps concurrent invocations from colliding.
        latest=base/(work.name+'.results.tmp');latest.write_text(data);latest.replace(base/'results.json')
    def run(command,label,expected_error=None):
        command=list(map(str,command));report['commands'].append(command);save()
        def limit():resource.setrlimit(resource.RLIMIT_AS,(16*1024**3,16*1024**3))
        result=subprocess.run(command,cwd=work,env=dict(os.environ,TMPDIR=str(work)),capture_output=True,text=True,preexec_fn=limit)
        (work/(label+'.log')).write_text(result.stdout+result.stderr)
        if expected_error:
            if result.returncode==0 or expected_error not in result.stdout+result.stderr:raise RuntimeError('Expected rejection: '+label)
        elif result.returncode:raise RuntimeError('Failed '+label)
        return result
    try:
        flags=['-std=c++17','-DSYNTHESIS','-I'+str(include)]
        generated=work/'generated';run([a.cpphdl,'--generated-dir',generated,fixture,'--',*flags],'lower')
        converted=work/'controller.cc';run([a.hdlcpp,'--native-graph','--top','ChildResetTop','-DSYNTHESIS','--output',converted,
            generated/'Predef_pkg.sv',generated/'ChildResetCounter.sv',generated/'ChildResetTop.sv'],'convert')
        match=re.search(r'graph\.load\(R"(\w+)\((.*?)\)\1"\)',converted.read_text(),re.S)
        if not match:raise RuntimeError('Missing converted graph')
        (work/'controller.graph').write_text(match[2])
        (work/'emit.cpp').write_text('''#include "cpphdl_graph.h"
#include <fstream>
#include <sstream>
int main(int argc,char** argv){std::ifstream f(argv[1]);std::ostringstream s;s<<f.rdbuf();cpphdl::graph::Graph g;g.load(s.str().c_str());g.emit(argv[2],256,1);}
''')
        common=[a.cxx,'-std=c++23','-O1','-g0','-pthread','-I'+str(include)]
        run([*common,work/'emit.cpp','-o',work/'emit'],'compile-emitter')
        run([work/'emit',work/'controller.graph',work/'model.h'],'emit')
        (work/'test.cpp').write_text('''#include "model.h"
#include "FIXTURE"
#include <iostream>
#include <stdexcept>
long _system_clock=0;
int main(){ChildResetTop native;cpphdl_native::Model rtl;bool clear=false;native.clear_in=_ASSIGN(clear);native._assign();
for(unsigned cycle=0;cycle<4096;++cycle){bool reset=cycle<2||cycle%71==0;clear=cycle%23==5||cycle%23==6;
rtl.reset[0]=reset;rtl.clear_in[0]=clear;rtl.clk[0]=0;rtl.step();
native._work(reset);native._strobe();rtl.clk[0]=1;rtl.step();
if(native.shared_out()!=rtl.shared_out[0]||native.cleared_out()!=rtl.cleared_out[0]||native.phased_out()!=rtl.phased_out[0])throw std::runtime_error("child reset mismatch at cycle "+std::to_string(cycle));
rtl.clk[0]=0;rtl.step();++_system_clock;
}std::cout<<"PASS child reset bindings: shared reset, reset OR port, reset OR state, alternate parameter; 4096 cycles\\n";}
'''.replace('FIXTURE',str(fixture)))
        run([*common,work/'test.cpp','-o',work/'test'],'compile-test');result=run([work/'test'],'functional')
        cases={
            'local':('bool temporary=clear_in();child._work(reset||temporary);','task-local'),
            'shadow':('bool clear_in=true;child._work(reset||clear_in);','task-local'),
            'conditional':('if(clear_in())child._work(reset||clear_in());','unconditional scalar'),
            'conflict':('child._work(reset);child._work(reset||clear_in());','conflicting'),
            'side_effect':('child._work(reset||((counter=1)!=0));','side effect'),
            'helper':('helper(reset);','unconditional scalar'),
            'array':('child[0]._work(reset||clear_in());child[1]._work(reset);','unconditional scalar'),
        }
        prelude=fixture.read_text().split('class ChildResetTop:')[0]
        for name,(body,error) in cases.items():
            child='ChildResetCounter child[2];' if name=='array' else 'ChildResetCounter child;'
            helper='void helper(bool local){child._work(local);}' if name=='helper' else ''
            source=work/(name+'.h');source.write_text(prelude+'class InvalidChildReset:public cpphdl::Module{public:'+child+
                '_PORT(bool) clear_in;uint32_t counter=0;'+helper+'void _assign(){}void _strobe(){}void _work(bool reset){'+body+'}};\n')
            run([a.cpphdl,'--generated-dir',work/('generated-'+name),source,'--',*flags],name,error)
            report['negative_cases'].append(name);save()
        report.update(status='passed',output=result.stdout,frontend_sha256=hashlib.sha256(a.cpphdl.read_bytes()).hexdigest(),
                      fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),cycles=4096)
        save();print(result.stdout,end='');print('PASS rejected '+', '.join(report['negative_cases']))
    except Exception as e:report.update(status='failed',error=str(e));save();raise

if __name__=='__main__':main()
