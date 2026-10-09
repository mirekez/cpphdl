#!/usr/bin/env python3
"""Compare linked graph partitions against the original C++ hierarchy."""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cpphdl', required=True)
parser.add_argument('--cxx', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='cpphdl-partitions-'))
source = work / 'source'
source.mkdir()
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')

def run(command):
    result = subprocess.run(list(map(str, command)), env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=240)
    if result.returncode: raise RuntimeError(result.stdout)
    return result.stdout

(source / 'cpphdl_support.h').write_text('''#pragma once
#include <cpphdl.h>
namespace firtool_cpphdl {
template<size_t W,size_t N,class I>
cpphdl::logic<W> array_get(const cpphdl::logic<W*N>& data,const I& index) {
 cpphdl::logic<W> result=0;size_t first=static_cast<uint64_t>(index)*W;
 for(size_t bit=0;bit<W;++bit) if(first+bit<W*N) result.set(bit,data.get(first+bit));
 return result;
}
}
''')
(source / 'Leaf.h').write_text('''#pragma once
#include "cpphdl_support.h"
class Leaf : public cpphdl::Module { public:
 _PORT(cpphdl::logic<8>) data; // input
 _PORT(cpphdl::logic<8>) result; // output
 cpphdl::reg<cpphdl::logic<8>> saved{};
 cpphdl::memory<cpphdl::logic<8>,1,4> memory;
 inline static constexpr cpphdl::logic<32> rom=0x1729d30a;
 void _assign(); void _work(bool); void _strobe();
 void initialize() { for(unsigned i=0;i<4;++i) memory[i]=0; memory.apply(); }
};
''')
(source / 'Leaf.cpp').write_text('''#include "Leaf.h"
void Leaf::_assign() { result = _ASSIGN((saved ^ firtool_cpphdl::array_get<8,4>(rom,data().slice<1,0>()))); }
void Leaf::_work(bool reset) {
 auto address=uint64_t(data())%4;
 saved._next = reset ? cpphdl::logic<8>(0) : cpphdl::logic<8>(memory[address]);
 memory[address]=data();
}
void Leaf::_strobe() { saved.strobe(); memory.apply(); }
''')
(source / 'Parent.h').write_text('''#pragma once
#include "cpphdl_support.h"
class Leaf;
class Parent : public cpphdl::Module { public:
 _PORT(cpphdl::logic<8>) data; // input
 _PORT(cpphdl::logic<2>) enable; // input
 _PORT(cpphdl::logic<1>) reset; // input
 _PORT(cpphdl::logic<16>) result; // output
private:
 Leaf* inst_left;
 Leaf* inst_right;
public:
 Parent(); ~Parent();
 void _assign(); void _work(bool); void _strobe(); void initialize();
};
''')
(source / 'Parent.cpp').write_text('''#include "Parent.h"
#include "Leaf.h"
Parent::Parent() { inst_left=new Leaf(); inst_right=new Leaf(); }
Parent::~Parent() { delete inst_left; delete inst_right; }
#ifdef PARTITION_REFERENCE_TEST
void Parent::initialize() { inst_left->initialize(); inst_right->initialize(); }
#endif
void Parent::_assign() {
 inst_left->data = _ASSIGN(data());
 inst_right->data = _ASSIGN(cpphdl::logic<8>(uint64_t(data()) ^ uint64_t(inst_left->result())));
 inst_left->_assign(); inst_right->_assign();
 result = _ASSIGN(cpphdl::cat<8,8>(inst_left->result(),inst_right->result()));
}
void Parent::_work(bool) {
 if(enable().get(0)) inst_left->_work(bool(reset()));
 if(enable().get(1)) inst_right->_work(bool(reset()));
}
void Parent::_strobe() { inst_left->_strobe(); inst_right->_strobe(); }
''')
partitions = work / 'parts'
print(run(['python3', root / 'graph_partitions.py', '--source', source, '--output', partitions,
           '--top', 'Parent', '--cpphdl', args.cpphdl, '--include', root.parent / 'include']))
linker = work / 'link'
print(run([args.cxx, '-std=c++23', '-O1', '-I' + str(root.parent / 'include'),
           root.parent / 'tools/cpphdl-graph-link.cpp', '-o', linker]))
print(run([linker, partitions / 'link.plan', work / 'model.h', '2']))
(work / 'check.cpp').write_text('''#include "Parent.cpp"
#include "Leaf.cpp"
#include "model.h"
#include <cstdio>
long _system_clock=0;
int main() {
 Parent reference; reference.initialize(); cpphdl_native::Model graph;
 cpphdl::logic<8> data=0; cpphdl::logic<2> enable=3; cpphdl::logic<1> reset=1;
 reference.data=_ASSIGN(data); reference.enable=_ASSIGN(enable); reference.reset=_ASSIGN(reset); reference._assign();
 for(unsigned cycle=0;cycle<2048;++cycle) {
  data=cycle*37; enable=cycle ? ((cycle*13)>>3)&3 : 3; reset=cycle%17==0;
  graph.r_data[0]=uint64_t(data); graph.r_enable[0]=uint64_t(enable); graph.r_reset[0]=uint64_t(reset);
  ++_system_clock; reference._work(false); reference._strobe(); ++_system_clock; graph.step();
  if(graph.r_result[0] != uint64_t(reference.result())) {
   std::printf("cycle %u: graph=%u cpp=%llu\\n",cycle,graph.r_result[0],(unsigned long long)uint64_t(reference.result())); return 1;
  }
 }
 std::puts("partitioned graph: enables, reset arguments, ROMs and independent RAMs PASS (2048 cycles)");
}
''')
print(run([args.cxx, '-std=c++23', '-O2', '-DPARTITION_REFERENCE_TEST', '-I' + str(root.parent / 'include'), '-I' + str(source),
           '-I' + str(work), work / 'check.cpp', '-o', work / 'check']))
print(run([work / 'check']))

# An external module is a host boundary: no implementation is lowered, and
# all its inputs and lifecycle controls must come from the same snapshot.
(source / 'Host.h').write_text('''#pragma once
#include "cpphdl_support.h"
class Host : public cpphdl::Module { public:
 _PORT(cpphdl::logic<8>) data; // input
 _PORT(cpphdl::logic<1>) enable; // input
 _PORT(cpphdl::logic<8>) result; // output
 void* cpphdlExternalState;
 cpphdl::logic<8> value=0, next=0;
 void _assign() { result=_ASSIGN(value); }
 void _work(bool reset) { next=reset ? cpphdl::logic<8>(0) : data(); }
 void _strobe() { value=next; }
};
''')
(source / 'HostTop.h').write_text('''#pragma once
#include "cpphdl_support.h"
class Host;
class HostTop : public cpphdl::Module { public:
 _PORT(cpphdl::logic<8>) data; // input
 _PORT(cpphdl::logic<1>) enable; // input
 _PORT(cpphdl::logic<1>) reset; // input
 _PORT(cpphdl::logic<16>) result; // output
 Host* inst_host;
 cpphdl::reg<cpphdl::logic<8>> saved{};
 HostTop(); ~HostTop();
 void _assign(); void _work(bool); void _strobe();
};
''')
(source / 'HostTop.cpp').write_text('''#include "HostTop.h"
#include "Host.h"
HostTop::HostTop() { inst_host=new Host(); }
HostTop::~HostTop() { delete inst_host; }
void HostTop::_assign() {
 inst_host->data=_ASSIGN(data()); inst_host->enable=_ASSIGN(enable()); inst_host->_assign();
 result=_ASSIGN(cpphdl::cat<8,8>(saved,inst_host->result()));
}
void HostTop::_work(bool) {
 saved._next=inst_host->result();
 if(enable()) inst_host->_work(bool(reset()));
}
void HostTop::_strobe() { saved.strobe(); inst_host->_strobe(); }
''')
print(run(['python3', root / 'graph_partitions.py', '--source', source, '--output', partitions,
           '--top', 'HostTop', '--cpphdl', args.cpphdl, '--include', root.parent / 'include']))
print(run([linker, partitions / 'link.plan', work / 'host-model.h', '2']))
(work / 'host-check.cpp').write_text('''#include "HostTop.cpp"
#include "host-model.h"
#include <cstdio>
long _system_clock=0;
int main() {
 HostTop reference; Host host; cpphdl_native::Model graph;
 cpphdl::logic<8> data=0, hostData=0; cpphdl::logic<1> enable=0, reset=0;
 reference.data=_ASSIGN(data); reference.enable=_ASSIGN(enable); reference.reset=_ASSIGN(reset);
 reference._assign(); host.data=_ASSIGN(hostData); host._assign();
 for(unsigned cycle=0;cycle<2048;++cycle) {
  data=cycle*37; enable=(cycle>>2)&1; reset=cycle%19==0;
  ++_system_clock;
  graph.r_data[0]=uint64_t(data); graph.r_enable[0]=uint64_t(enable); graph.r_reset[0]=uint64_t(reset);
  graph.h0_result[0]=uint64_t(host.result());
  graph.evaluate<true>();
  if(graph.r_result[0]!=uint64_t(reference.result()) || graph.host_control_0_enable[0]!=uint64_t(enable) ||
     graph.h0_enable[0]!=uint64_t(enable)) return 1;
  hostData=graph.h0_data[0];
  if(graph.host_control_0_enable[0]) host._work(graph.host_control_0_work_reset[0]);
  host._strobe(); reference._work(false); reference._strobe();
 }
 std::puts("partitioned graph: host snapshots and conditional callbacks PASS (2048 cycles)");
}
''')
print(run([args.cxx, '-std=c++23', '-O2', '-I' + str(root.parent / 'include'), '-I' + str(source),
           '-I' + str(work), work / 'host-check.cpp', '-o', work / 'host-check']))
print(run([work / 'host-check']))

# A combinational external result must be available to the graph on the same
# edge. A counter fed through the host detects an accidental extra cycle.
(source / 'CombHost.h').write_text('''#pragma once
#include "cpphdl_support.h"
#include <stdexcept>
inline unsigned host_ticks=0, host_strobes=0;
class CombHost : public cpphdl::Module { public:
 _PORT(cpphdl::logic<8>) data; // input
 _PORT(cpphdl::logic<8>) result; // output
 void* cpphdlExternalState;
 unsigned expected=0, unstable=0;
 void _assign() { result=_ASSIGN(cpphdl::logic<8>((uint64_t(data())+1)^unstable)); }
 void _work(bool reset) {
  if(!reset && uint64_t(data())!=expected)
   throw std::runtime_error("combinational host introduced a clock of latency");
  expected=reset ? 0 : (expected+1)&255; ++host_ticks;
 }
 void _strobe() { ++host_strobes; }
};
''')
(source / 'TestHarness.h').write_text('''#pragma once
#include "cpphdl_support.h"
class CombHost;
class TestHarness : public cpphdl::Module { public:
 _PORT(cpphdl::logic<1>) clock; // input
 _PORT(cpphdl::logic<1>) reset; // input
 CombHost* inst_host;
 cpphdl::reg<cpphdl::logic<8>> saved{};
 TestHarness(); ~TestHarness();
 void _assign(); void _work(bool); void _strobe();
};
''')
(source / 'TestHarness.cpp').write_text('''#include "TestHarness.h"
#include "CombHost.h"
TestHarness::TestHarness() { inst_host=new CombHost(); }
TestHarness::~TestHarness() { delete inst_host; }
void TestHarness::_assign() { inst_host->data=_ASSIGN(saved); inst_host->_assign(); }
void TestHarness::_work(bool) {
 saved._next=reset() ? cpphdl::logic<8>(0) : inst_host->result();
 inst_host->_work(bool(reset()));
}
void TestHarness::_strobe() { saved.strobe(); inst_host->_strobe(); }
''')
(source / 'cpphdl_external_models.h').write_text('''#pragma once
#include "CombHost.h"
#include <cstdlib>
namespace firtool_cpphdl_external {
inline void eval(CombHost& host) {
 if(std::getenv("TEST_UNSTABLE_HOST")) host.unstable^=1;
}
}
''')
(source / 'cpphdl_runtime.h').write_text('''#pragma once
#include "CombHost.h"
namespace firtool_cpphdl_runtime {
inline void configure(int,char**) {}
inline uint32_t exitCode() {
 if(host_ticks!=host_strobes) throw std::runtime_error("work/strobe count mismatch");
 if(host_ticks!=unsigned(_system_clock)) throw std::runtime_error("extra work during settling");
 return host_ticks==64 ? 1 : 0;
}
}
''')
print(run(['python3', root / 'graph_partitions.py', '--source', source, '--output', partitions,
           '--top', 'TestHarness', '--combinational-host', 'CombHost',
           '--cpphdl', args.cpphdl, '--include', root.parent / 'include']))
print(run([linker, partitions / 'link.plan', work / 'model.h', '2']))
print(run([args.cxx, '-std=c++23', '-O2', '-I' + str(root.parent / 'include'),
           '-I' + str(source), '-I' + str(work), partitions / 'runner.cpp',
           '-o', work / 'comb-check']))
print(run([work / 'comb-check', 'unused.riscv']))
print('partitioned graph: same-edge combinational host settling PASS (64 cycles)')

result = subprocess.run([str(work / 'comb-check'), 'unused.riscv'],
                        env={**env, 'TEST_UNSTABLE_HOST': '1'}, text=True,
                        capture_output=True, timeout=30)
if result.returncode != 2 or 'combinational host boundary did not settle' not in result.stderr:
    raise RuntimeError('unstable host was not rejected: ' + result.stdout + result.stderr)
print('partitioned graph: non-converging combinational host rejection PASS')

result = subprocess.run(['python3', str(root / 'graph_partitions.py'),
                         '--source', str(source), '--output', str(partitions),
                         '--combinational-host', 'MissingHost', '--cpphdl', args.cpphdl,
                         '--include', str(root.parent / 'include')],
                        env=env, text=True, capture_output=True, timeout=240)
if result.returncode == 0 or 'unknown combinational host type(s): MissingHost' not in result.stderr:
    raise RuntimeError('unknown host was not rejected: ' + result.stdout + result.stderr)
print('partitioned graph: unknown combinational host rejection PASS')
