#!/usr/bin/env python3
"""Reject a known incorrect masked-gather lowering before compiling a graph."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def checked_flags(compiler,flags,work,linker=None):
    """Return tested compiler flags and provenance; fail closed on a bad probe."""
    compiler=str(compiler);flags=list(flags);work=Path(work);work.mkdir(parents=True,exist_ok=True)
    source=Path(__file__).resolve().parents[1]/'tests/graph/NativeMaskGather.cc'
    version=subprocess.check_output([compiler,'--version'],text=True)
    target=subprocess.check_output([compiler,'-dumpmachine'],text=True).strip()
    evidence={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
              'requested_flags':flags[:],'compiler_version':version,'target':target,'attempts':[],
              'linker':str(linker or compiler)}
    def check(selected):
        label='probe-'+str(len(evidence['attempts']));binary=work/label
        obj=work/(label+'.o')
        args=[compiler,'-O2',*selected,'-std=c++23',str(source),*(['-c'] if linker else []),'-o',str(obj if linker else binary)]
        compiled=subprocess.run(args,capture_output=True,text=True)
        (work/(label+'.compile.log')).write_text(compiled.stdout+compiled.stderr)
        if compiled.returncode:raise RuntimeError('Native compiler probe did not compile: '+str(work/(label+'.compile.log')))
        if linker:
            subprocess.run([str(linker),str(obj),'-o',str(binary)],check=True,capture_output=True,text=True)
        result=subprocess.run([binary],capture_output=True,text=True)
        (work/(label+'.log')).write_text(result.stdout+result.stderr)
        evidence['attempts'].append({'flags':selected[:],'passed':result.returncode==0,'output':result.stdout.strip()})
        return result.returncode==0
    if not check(flags):
        if 'clang version 21.' not in version or not target.startswith('x86_64'):
            raise RuntimeError('Native compiler miscompiled the masked-gather probe; select another compiler')
        flags += ['-mllvm','-fast-isel=true']
        if not check(flags):raise RuntimeError('Clang instruction-selection workaround failed the native compiler probe')
        evidence['workaround']='clang21_x86_fast_instruction_selection'
    evidence.update(status='passed',effective_flags=flags)
    (work/'results.json').write_text(json.dumps(evidence,indent=2)+'\n')
    return flags,evidence


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cxx',required=True)
    p.add_argument('--cxx-flag',action='append',default=[])
    p.add_argument('--linker',help='Optional separate host linker driver for a compiler-only toolchain')
    p.add_argument('--work',type=Path,required=True)
    a=p.parse_args();_,evidence=checked_flags(a.cxx,a.cxx_flag,a.work,a.linker)
    print(json.dumps(evidence,indent=2))


if __name__=='__main__':main()
