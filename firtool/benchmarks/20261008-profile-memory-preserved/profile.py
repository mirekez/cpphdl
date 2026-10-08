#!/usr/bin/env python3
import hashlib, json, os, pathlib, subprocess, time
out=pathlib.Path(__file__).resolve().parent
root=pathlib.Path('/home/me/chipyard/chipyard')
elf=root/'tests/build/rocket64-mmul.riscv'
sha=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
results=[]
for mode,rel in [
    ('native-graph','cpphdl-build/RocketConfig/native-graph/runtime/cpphdl-rocket64-graph-sim'),
    ('optimize-combs','cpphdl-build/RocketConfig/runtime/cpphdl-rocket64-optimized-sim')]:
    binary=(root/rel).resolve()
    before=(sha(binary),sha(elf))
    env=dict(os.environ,CPPHDL_MAX_CYCLES='2000000',CPPHDL_PROGRESS_CYCLES='100000')
    print('START',mode,flush=True)
    t=time.monotonic()
    with (out/(mode+'.log')).open('w') as log:
        rc=subprocess.run(['/usr/bin/time','-v','-o',str(out/(mode+'.time')),
            'timeout','1200s','env','LD_PRELOAD='+str(out/'sample.so'),
            'CPU_PROFILE_OUT='+str(out/(mode+'.samples')),str(binary),str(elf)],
            env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT).returncode
    elapsed=time.monotonic()-t
    unchanged=before==(sha(binary),sha(elf))
    passed=rc==0 and unchanged and 'ROCKET RV64 MMUL TEST PASSED: signature=0xe49d58d75696cd28' in (out/(mode+'.log')).read_text()
    results.append(dict(backend=mode,binary=str(binary),binary_sha256=before[0],elf_sha256=before[1],
                        seconds=elapsed,exit_code=rc,passed=passed,unchanged=unchanged))
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('DONE',mode,round(elapsed,3),'PASS' if passed else 'FAIL',flush=True)
