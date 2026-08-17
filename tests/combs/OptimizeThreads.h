#pragma once

#include "cpphdl.h"

extern long _system_clock;

class OptimizeThreads : public cpphdl::Module {
public:
  cpphdl::logic<32> input{};

  cpphdl::logic<32> branch_a_cache{};
  long branch_a_clock = -1;
  cpphdl::logic<32> &branch_a() {
    if (branch_a_clock == _system_clock) {
      return branch_a_cache;
    }
    branch_a_clock = _system_clock;
    return branch_a_cache = input + cpphdl::logic<32>(0x10203u);
  }

  cpphdl::logic<32> branch_b_cache{};
  long branch_b_clock = -1;
  cpphdl::logic<32> &branch_b() {
    if (branch_b_clock == _system_clock) {
      return branch_b_cache;
    }
    branch_b_clock = _system_clock;
    return branch_b_cache = input ^ cpphdl::logic<32>(0xa5a55a5au);
  }

  cpphdl::logic<32> branch_c_cache{};
  long branch_c_clock = -1;
  cpphdl::logic<32> &branch_c() {
    if (branch_c_clock == _system_clock) {
      return branch_c_cache;
    }
    branch_c_clock = _system_clock;
    return branch_c_cache = (input << 4) + input;
  }

  cpphdl::logic<32> branch_d_cache{};
  long branch_d_clock = -1;
  cpphdl::logic<32> &branch_d() {
    if (branch_d_clock == _system_clock) {
      return branch_d_cache;
    }
    branch_d_clock = _system_clock;
    return branch_d_cache = (input << 7) | (input >> 25);
  }

  cpphdl::reg<cpphdl::logic<32>> result_a{};
  cpphdl::reg<cpphdl::logic<32>> result_b{};
  cpphdl::reg<cpphdl::logic<32>> result_c{};
  cpphdl::reg<cpphdl::logic<32>> result_d{};

  void _assign() {}
  void _work(bool) {
    result_a._next = branch_a();
    result_b._next = branch_b();
    result_c._next = branch_c();
    result_d._next = branch_d();
  }
  void _strobe() {
    result_a.strobe();
    result_b.strobe();
    result_c.strobe();
    result_d.strobe();
  }
};
