#!/usr/bin/env python3
"""Build and record library regressions; report any explicitly omitted suite."""
import argparse
import bisect
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
sys.path.insert(0, str(ROOT))
from metrics import spectrum, wilson
from characterization import SUITES as CHARACTERIZATION_SUITES


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    paths = set()
    for folder in (ROOT, REPO/'analog/include', REPO/'include', REPO/'analog/tests'):
        for pattern in ('*.h', '*.hpp', '*.cpp', '*.py', 'CMakeLists.txt'):
            paths.update(folder.rglob(pattern))
    return {str(p.relative_to(REPO)): digest(p) for p in sorted(paths)}


def report(work, build, before, code, omitted=()):
    cases = []
    suites = []
    xml = ET.parse(work/'ctest.xml')
    for case in xml.findall('.//testcase'):
        status = 'FAIL' if case.find('failure') is not None else ('SKIP' if case.find('skipped') is not None else 'PASS')
        suites.append(dict(name=case.attrib['name'], seconds=float(case.attrib['time']), status=status))
    expected = {'parameters','acquisition','dac','conversion','routing','comparator','leakage','timing',
                'voltage','range','convergence','sensitivity','dynamic','settling','clock','startup','mismatch','metrics',
                'adaptive_input','history','transition_search','transitions','qualification',
                'ihp_waveform_metrics','ihp_report_evidence','ihp_matched_placement','ihp_loading',
                'ihp_model_element_names',
                'noise_model','noise_rc','noise_metrics','noise_adc','edge_refinement','waveforms','characterization_metrics'} | {'characterize_'+s for s in CHARACTERIZATION_SUITES}
    if {s['name'].removeprefix('analog.lib.adc_sar.') for s in suites} != expected-set(omitted):
        raise RuntimeError('missing or unexpected library regression suites')
    for path in sorted(work.glob('*.csv')):
        rows = list(csv.DictReader(path.open()))
        if rows and 'code_error' in rows[0]:
            for row in rows:
                cases.append(dict(experiment=path.stem, **row))
    spectral = None
    if (work/'dynamic.csv').exists():
        rows = list(csv.DictReader((work/'dynamic.csv').open()))
        if len(rows) == 64:
            spectral = spectrum([float(r['code']) for r in rows], 5)
    convergence = None
    coarse_path, fine_path = work/'coarse_waveform/trace.csv', work/'refined_waveform/trace.csv'
    if coarse_path.exists() and fine_path.exists():
        coarse, fine = [list(csv.DictReader(p.open())) for p in (coarse_path, fine_path)]
        times = [float(r['time_s']) for r in fine]
        nodes = [k for k in coarse[0] if k.startswith('node')]
        if set(coarse[0]) != set(fine[0]) or not nodes:
            raise RuntimeError('incompatible/missing all-node waveforms')
        maxima = {k:0. for k in nodes}
        for row in coarse:
            t = float(row['time_s'])
            i = bisect.bisect_left(times, t)
            if i == len(times):
                if abs(t-times[-1]) > 1e-18: continue
                i = len(times)-1
            if i == 0 and abs(t-times[0]) > 1e-18: continue
            previous = max(0,i-1)
            ratio = (t-times[previous])/(times[i]-times[previous]) if i != previous else 0
            for k in nodes:
                ref = float(fine[previous][k]) + ratio*(float(fine[i][k])-float(fine[previous][k]))
                maxima[k] = max(maxima[k],abs(float(row[k])-ref))
        convergence = dict(nodes=len(nodes), maximum_voltage_difference_V=max(maxima.values()),
                           per_node_maximum_V=maxima, qualification='legacy BE 10/5 ps diagnostic; gated fine reference is recorded in waveform_qualification')
    transitions=None
    transition_path=work/'transitions/checks.json'
    if transition_path.exists():
        transitions=json.loads(transition_path.read_text())
        if transitions.get('smoke') or transitions.get('status')!='PASS':
            raise RuntimeError('incomplete ADC transition characterization')
    if any(s['name']=='analog.lib.adc_sar.transitions' and s['status']=='PASS' for s in suites) and transitions is None:
        raise RuntimeError('passing transition suite has no measured evidence')
    noise=None
    noise_path=work/'noise_adc/checks.json'
    if noise_path.exists():
        noise=json.loads(noise_path.read_text())
        if noise.get('smoke') or noise.get('status')!='PASS':raise RuntimeError('incomplete noise calibration evidence')
        for name in ('noise_statistics','noise_rc','noise_correlation'):
            with (work/'noise'/(name+'.csv')).open() as f:noise[name]=list(csv.DictReader(f))
    if any(s['name']=='analog.lib.adc_sar.noise_adc' and s['status']=='PASS' for s in suites) and noise is None:
        raise RuntimeError('passing noise suite has no measured evidence')
    characterization={}
    for suite in CHARACTERIZATION_SUITES:
        path=work/'characterization'/suite/'checks.json'
        if path.exists():characterization[suite]=json.loads(path.read_text())
        if any(s['name']=='analog.lib.adc_sar.characterize_'+suite and s['status']=='PASS' for s in suites):
            if suite not in characterization or characterization[suite]['status']!='PASS':raise RuntimeError('missing characterization evidence')
    waveform_qualification=None
    if (work/'waveforms/checks.json').exists():waveform_qualification=json.loads((work/'waveforms/checks.json').read_text())
    if any(s['name']=='analog.lib.adc_sar.waveforms' and s['status']=='PASS' for s in suites):
        if not waveform_qualification or waveform_qualification['status']!='PASS':raise RuntimeError('missing all-node convergence evidence')
    after = sources()
    if after != before:
        raise RuntimeError('source changed during campaign; results cannot qualify this source revision')
    result = dict(schema=1, scope='generic Analog++ single-slice library; no process/layout signoff',
                  status='PASS' if code == 0 and all(s['status']=='PASS' for s in suites) else 'FAIL', suites=suites, conversions=cases,
                  spectrum=spectral, convergence=convergence, transitions=transitions,noise=noise,
                  characterization=characterization,waveform_qualification=waveform_qualification,
                  omitted_suites=list(omitted),registered_suites=len(expected),
                  source_sha256=before, binary_sha256=digest(build/'adc_sar_tests'),
                  noise_binary_sha256=digest(build/'adc_sar_noise_tests'),edge_binary_sha256=digest(build/'adc_sar_edge_tests'),
                  export_binary_sha256=digest(build/'adc_sar_ihp_export'),
                  evidence_sha256={str(p.relative_to(work)):digest(p) for p in sorted(work.rglob('*')) if p.is_file()})
    (work/'checks.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    text = ['# ADC SAR library test results', '', result['scope']+'.', '',
            f"Campaign: **{result['status']}**. {sum(s['status']=='PASS' for s in suites)}/{len(suites)} suites pass.", '',
            '| Suite | Result | Seconds |', '|---|---|---|']
    text += [f"| {s['name']} | {s['status']} | {s['seconds']:.2f} |" for s in suites]
    if omitted:
        text += ['', 'Explicitly omitted (not a pass for this source snapshot): '+', '.join(omitted)+'. '
                 'This is a selected-suite campaign; prior exhaustive transition receipts remain separate.']
    text += ['', '| Experiment | Conversions | Failures | Max acquisition mV | Max hold mV | Max code error |', '|---|---|---|---|---|---|']
    for experiment in sorted({r['experiment'] for r in cases}):
        rows = [r for r in cases if r['experiment']==experiment]
        text.append(f"| {experiment} | {len(rows)} | {sum(r['pass']!='1' for r in rows)} | "
                    f"{1000*max(float(r['acquisition_V']) for r in rows):.6f} | "
                    f"{1000*max(float(r['hold_pp_V']) for r in rows):.6f} | {max(int(r['code_error']) for r in rows)} |")
    text += ['', 'Timing stress records include deliberately failing clocks. Those conversions are negative controls, '
             'not part of the rated-clock pass count. Power and spectral measurements have no product acceptance budgets yet.']
    if spectral:
        text += ['', '64-point coherent electrical ADC record: 5 cycles, 50 ns per sample, '
                 '1.5625 MHz sine, 1.45 V center, 0.45 V peak amplitude; rectangular DFT.', '',
                 '| Metric | Measured |', '|---|---|']
        text += [f'| {k} | {v:.6f} |' for k,v in spectral.items()]
    if convergence:
        text += ['', f"Legacy 10/5 ps all-node diagnostic: {convergence['nodes']} nodes, "
                 f"maximum difference {1000*convergence['maximum_voltage_difference_V']:.6f} mV. "
                 'Codes must match and acquisition/hold metrics must agree within 1 mV; '
                 'this coarse diagnostic does not establish fast-node convergence. The separate waveform suite applies explicit gates to the refined reference.']
    if transitions:
        text += ['', 'ADC transition characterization: one persistent nominal slice, 0.85–2.05 V, '
                 f"{transitions['search_conversions']} complete electrical conversions; "
                 f"{transitions['repeat_center_conversions']} of these repeat complete-code interval centers. "
                 'Range endpoints are not treated as ADC transitions. Product DNL/INL limits remain unspecified.', '',
                 '| Search direction | Complete bins | Max abs endpoint DNL (LSB) | Max abs endpoint INL (LSB) | Missing codes |',
                 '|---|---|---|---|---|']
        for direction in ('up','down'):
            m=transitions[direction]
            text.append(f"| {direction} | {m['complete_codes'][0]}–{m['complete_codes'][-1]} | "
                        f"{m['maximum_abs_dnl_lsb']:.6f} | {m['maximum_abs_inl_lsb']:.6f} | {m['missing_complete_codes']} |")
        r=transitions['refinement']
        text += ['', f"Maximum ascending/descending midpoint difference: {1e3*transitions['maximum_direction_difference_V']:.6f} mV. "
                 f"Selected transition 10 ps→5 ps timestep difference: {1e3*r['maximum_midpoint_difference_V']:.6f} mV "
                 f"({r['conversions']} additional conversions). Numerical repeatability tolerance: {1e3*r['acceptance_V']:.6f} mV. "
                 'These numerical checks do not replace a product linearity specification.']
    if noise:
        c=noise['coarse'];f=noise['refined'];fit=c['fit']
        text += ['', 'Sampled input-equivalent noise calibration (not intrinsic PSP noise):', '',
                 '| Check | Measured result |', '|---|---|',
                 f"| Ideal kT/C, 1 pF / 27 C | {1e6*c['expected_sigma_V']:.6f} uV RMS |",
                 f"| Blind electrical ADC fit, {c['noisy_conversions']} noisy conversions | {1e6*fit['sigma_V']:.6f} uV RMS |",
                 f"| 95% profile-likelihood interval | {1e6*fit['sigma_95_interval_V'][0]:.6f}–{1e6*fit['sigma_95_interval_V'][1]:.6f} uV |",
                 f"| Refined 5 ps fit, {f['noisy_conversions']} noisy conversions | {1e6*f['fit']['sigma_V']:.6f} uV RMS |",
                 f"| Paired-seed timestep RMS difference | {100*abs(noise['paired_seed_timestep_sigma_difference_fraction']):.6f}% |",
                 f"| Noise-off reference checks | {c['noise_off_checks']+f['noise_off_checks']} pass |", '',
                 'White/flicker coefficient recovery uses an explicitly synthetic PSD fixture. '
                 'Its fitted coefficients also pass through the C++ slice noise factory. '
                 'Foundry/silicon calibration and product RMS-noise acceptance remain open.']
    if characterization:
        text += ['', 'Connected characterization: each row is a finite sweep. Failed stressed operating points remain visible; '
                 'passing suites require their declared nominal points and measurement guards. Product spectral, PSRR, kickback and power budgets remain unspecified.', '',
                 '| Sweep | Cases | Conversions | Failed operating points |', '|---|---|---|---|']
        for name,c in characterization.items():text.append(f"| {name} | {len(c['cases'])} | {c['conversions']} | {c['failed_operating_points']} |")
        text += ['', '| Sweep / case | Frequency MHz | Amplitude V | Source ohm | Failed / runs | External acquisition mV | Hold mV | External code error | SNDR dB |',
                 '|---|---|---|---|---|---|---|---|---|']
        for name,c in characterization.items():
            for case in c['cases']:
                cfg=case['configuration'];sndr=case.get('spectrum',{}).get('sndr_db')
                text.append(f"| {name}/{cfg['name']} | {cfg['frequency_Hz']/1e6:.6f} | {cfg['amplitude_V']:.3f} | {cfg['source_ohm']:.0f} | "
                    f"{case['failed_operating_points']}/{case['conversions']} | {case['maximum_external_acquisition_V']*1000:.6f} | "
                    f"{case['maximum_hold_variation_V']*1000:.6f} | {case['maximum_external_code_error']:.0f} | "+(f'{sndr:.6f}' if sndr is not None else '—')+' |')
    if waveform_qualification:
        w=waveform_qualification;f=w['comparisons'][-1]
        text += ['', 'Full startup/acquisition/conversion waveform qualification: '+w['status']+'. BDF2, '+str(w['steps_ps'])+' ps regular steps; '+str(w['edge_steps_fs'])+' fs local driver-edge steps.', '',
                 '| Numerical gate | Limit | Measured |', '|---|---|---|']
        for key,limit in w['limits'].items():text.append(f"| {key} | {limit:.9g} | {f[key]:.9g} |")
        text += ['', f"All {f['nodes']} nodes compared without time shifts or skipped switching windows; {f['matched_edges']} matched threshold crossings; "
                 f"unmatched nodes: {f['unmatched_edge_nodes']}. This qualifies the tested generic nominal waveform, not process/layout corners."]
    mismatch = {r['experiment'] for r in cases if r['experiment'].startswith('mismatch_')}
    if mismatch:
        passed=sum(all(r['pass']=='1' for r in cases if r['experiment']==name) for name in mismatch)
        lower, upper=wilson(passed,len(mismatch))
        text += ['', f'Synthetic uniform ±1% R/2R mismatch: {passed}/{len(mismatch)} draws pass both input levels. '
                 f'95% Wilson interval for this synthetic experiment: {lower:.6f}–{upper:.6f}. '
                 'This is not a foundry yield prediction.']
    (work/'results.md').write_text('\n'.join(text)+'\n')
    (build/'latest.json').write_text(json.dumps(dict(work=str(work), checks=str(work/'checks.json')), indent=2)+'\n')
    print(f"Results: {work/'results.md'}")


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,default=REPO/'build/adc-sar-lib')
    parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--exclude-transitions',action='store_true',
                        help='explicitly omit the exhaustive deterministic ADC transition scan; never reuse it as a new pass')
    args=parser.parse_args()
    if args.jobs < 1: parser.error('jobs must be positive')
    build=args.build.resolve(); build.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='campaign-',dir=build)).resolve()
    before=sources()
    subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release',
                    '-DADC_SAR_RESULTS='+str(work)],check=True)
    subprocess.run(['cmake','--build',str(build),'-j',str(args.jobs)],check=True)
    if before != sources(): raise RuntimeError('source changed while building')
    binaries={name:digest(build/name) for name in
              ('adc_sar_tests','adc_sar_noise_tests','adc_sar_edge_tests','adc_sar_ihp_export')}
    omitted=['transitions'] if args.exclude_transitions else []
    command=['ctest','--test-dir',str(build),'-R','^analog.lib.adc_sar.',
             '--output-on-failure','-j',str(args.jobs),'--output-junit',str(work/'ctest.xml')]
    if omitted:command+=['-E','^analog.lib.adc_sar.transitions$']
    completed=subprocess.run(command)
    if any(value!=digest(build/name) for name,value in binaries.items()):raise RuntimeError('binary changed during campaign')
    report(work,build,before,completed.returncode,omitted)
    return completed.returncode


if __name__=='__main__':
    raise SystemExit(main())
