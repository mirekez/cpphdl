#!/usr/bin/env python3
"""Exercise reduction class annotations through synthesis, retiming and hdlcpp."""
import argparse
from pathlib import Path
import resource
import subprocess
import sys
import tempfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('cpphdl','hdlcpp','work'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--cxx',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];a.work.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='retimed-reduction-',dir=a.work))
    def limit():resource.setrlimit(resource.RLIMIT_AS,(12*1024**3,12*1024**3))
    with (work/'synthesis.log').open('w') as log:
        subprocess.run([sys.executable,root/'synth/synthesize.py','--cpphdl',a.cpphdl,'--top','reduction_worker',
            '--module','ReductionTest','--output',work/'design','--cxx',a.cxx,'--retiming','fit_pipeline_retiming',
            '--clock-period-ns','4',root/'hls/tests/Fp32ReductionPipeline.cpp','--','-fopenmp','-ffp-contract=off',
            '-DCPPHDL_HLS_REDUCTION_REPLACEMENT','-I'+str(root),'-I'+str(root/'include')],
            check=True,stdout=log,stderr=log,preexec_fn=limit)
    subprocess.run([sys.executable,root/'hls/tests/Fp32ReductionBranchesChecks.py','--hdlcpp',a.hdlcpp,
        '--cxx',a.cxx,'--work',work/'proof','--design',work/'design','--module','ReductionTest',
        '--rows','2','--cols','3','--retimed-reference'],check=True)


if __name__=='__main__':main()
