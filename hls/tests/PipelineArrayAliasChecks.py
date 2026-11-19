"""Regression for a local C-array decay alias inside a by-value pipeline helper."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
for key in ('cpphdl', 'cxx', 'source', 'work'):
    parser.add_argument('--' + key, required=True)
parser.add_argument('--verilator')
args = parser.parse_args()
source = Path(args.source).resolve()
root = source.parents[2]
work = Path(args.work).resolve()
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True, exist_ok=True)
flags = ['-std=c++17', '-DSYNTHESIS', '-I' + str(root), '-I' + str(root / 'include')]

def run(command, name, expect_success=True):
    env = dict(os.environ)
    if args.verilator:
        env['PATH'] = str(Path(args.verilator).resolve().parent) + os.pathsep + env.get('PATH', '')
    completed = subprocess.run(list(map(str, command)), env=env, text=True, capture_output=True)
    log = completed.stdout + completed.stderr
    (work / (name + '.log')).write_text(log)
    if (completed.returncode == 0) != expect_success:
        raise RuntimeError(f'{name}: unexpected exit {completed.returncode}: {log[-1200:]}')
    return log

native = work / 'native'
run([args.cxx, '-std=c++17', '-O2', '-I' + str(root / 'include'), source, '-o', native], 'native-build')
if 'PASS native by-value packet array alias and isolation' not in run([native], 'native'):
    raise RuntimeError('native by-value isolation was not checked')
positive = work / 'generated'
run([args.cpphdl, '--generated-dir', positive, source, '--', *flags], 'positive')
modules = list(positive.glob('*ClockedPipeline*.sv'))
if len(modules) != 1 or 'pipelined_logic: II=1' not in modules[0].read_text():
    raise RuntimeError('array alias was not generated as an II=1 pipeline')
mapped = work / 'mapped'
run([args.cpphdl, '--synth', '--top', 'alias_worker', '--module', 'AliasMapped',
     '--output', mapped, '--cxx', args.cxx, '--retiming', 'fit_pipeline_retiming',
     '--clock-period-ns', '4', source, '--', *flags], 'retimed-synth')
report = json.loads((mapped / 'manifest.json').read_text())
regions = report.get('timing', {}).get('streaming_regions', [])
rules = report.get('timing', {}).get('rules', [])
if report.get('status') != 'complete' or not regions or any(r.get('initiation_interval') != 1 for r in regions):
    raise RuntimeError('retimed alias pipeline did not complete at II=1')
if not all(r.get('latency', 0) >= 1 for r in regions):
    raise RuntimeError('retimed alias pipeline has no measured latency')
if not rules or not all(r.get('target_met') is True and r.get('target_ns') == 4 for r in rules):
    raise RuntimeError('retimed alias pipeline did not meet the 4 ns target')
reset_observation_clocks = max(r['latency'] for r in regions) + 2
if args.verilator:
    def verify_rtl(label, module, sources):
        bench = work / (label + '-Test.sv')
        bench.write_text('''module Test;
  logic clk=0;
  always #5 clk=~clk;
  logic reset=1, command_valid_in=0, response_ready_in=0;
  AliasPacket_pkg::AliasPacket packet;
  wire command_ready_out,response_valid_out;
  wire [31:0] fault_out,result_out;
  ''' + module + ''' dut (
    .clk(clk),.reset(reset),.command_valid_in(command_valid_in),
    .command_ready_out(command_ready_out),.response_ready_in(response_ready_in),
    .response_valid_out(response_valid_out),.operation_in(packet),
    .index_in(packet),.value_in(packet),.fault_out(fault_out),.result_out(result_out));
  int unsigned expected, original;
  initial begin
    packet='0;
    repeat(2) @(negedge clk);
    reset=0;
    for (int idx=0; idx<4; idx++) begin
      packet.word[0]=11+idx;packet.word[1]=13+idx;
      packet.word[2]=17+idx;packet.word[3]=19+idx;
      packet.other[0]=23+idx;packet.other[1]=29+idx;packet.tag=idx;
      original=packet.word[0];
      expected=((original+packet.word[idx]+packet.word[3]+7)^idx)+original;
      @(negedge clk);
      if (!command_ready_out) $fatal(1,"worker not ready");
      command_valid_in=1;
      @(negedge clk);command_valid_in=0;
      wait(response_valid_out);
      repeat(3) begin
        @(negedge clk);
        if(!response_valid_out || fault_out!=0 || result_out!=expected)
          $fatal(1,"array alias result/backpressure mismatch index=%d got=%d expected=%d",idx,result_out,expected);
      end
      response_ready_in=1;
      @(negedge clk);response_ready_in=0;
    end
    packet.tag=3;
    @(negedge clk);command_valid_in=1;
    @(negedge clk);command_valid_in=0;reset=1;
    @(negedge clk);reset=0;
    repeat(''' + str(reset_observation_clocks) + ''') begin
      @(negedge clk);if(response_valid_out) $fatal(1,"stale result after reset");
    end
    $display("PASS generated array alias, all four indices, held response and reset");
    $finish;
  end
  initial begin #10000; $fatal(1,"timeout"); end
endmodule
''')
        obj = work / (label + '-obj')
        run([args.verilator, '--binary', '--timing', '-j', '2', '-Wno-fatal',
             '--top-module', 'Test', '--Mdir', obj, positive / 'Predef_pkg.sv',
             positive / 'AliasPacket_pkg.sv', *sources, bench], label + '-verilator-build')
        if 'PASS generated array alias, all four indices, held response and reset' not in run([obj / 'VTest'], label + '-verilator'):
            raise RuntimeError(label + ': generated alias simulation did not check result')
    verify_rtl('ordinary', '\\' + modules[0].stem, [modules[0]])
    verify_rtl('retimed', 'AliasMapped', [mapped / 'gates.v'])
for label, macro in [('rebind', 'ALIAS_NEGATIVE_REBIND'),
                     ('escape', 'ALIAS_NEGATIVE_ESCAPE'),
                     ('volatile', 'ALIAS_NEGATIVE_VOLATILE'),
                     ('reinterpret', 'ALIAS_NEGATIVE_REINTERPRET')]:
    log = run([args.cpphdl, '--generated-dir', work / label, source,
               '--', *flags, '-D' + macro], label, expect_success=False)
    if not any(marker in log for marker in ('memory schedule', 'unsupported', 'volatile/atomic')):
        raise RuntimeError(f'{label}: rejection reason changed: {log[-800:]}')
    if list((work / label).glob('*.sv')):
        raise RuntimeError(f'{label}: emitted RTL after rejected pointer use')
print('PASS native and generated array alias; rebinding, escape and volatile rejected')
