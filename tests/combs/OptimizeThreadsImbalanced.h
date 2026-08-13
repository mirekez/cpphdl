#pragma once

#include "cpphdl.h"

extern long _system_clock;

class OptimizeThreadsImbalanced : public cpphdl::Module {
public:
  cpphdl::logic<32> input{};

  cpphdl::logic<32> heavy_0_cache{};
  long heavy_0_clock = -1;
  cpphdl::logic<32> &heavy_0() {
    if (heavy_0_clock == _system_clock) return heavy_0_cache;
    heavy_0_clock = _system_clock;
    return heavy_0_cache = input + cpphdl::logic<32>(1u);
  }
  cpphdl::logic<32> heavy_1_cache{};
  long heavy_1_clock = -1;
  cpphdl::logic<32> &heavy_1() {
    if (heavy_1_clock == _system_clock) return heavy_1_cache;
    heavy_1_clock = _system_clock;
    return heavy_1_cache = heavy_0() ^ cpphdl::logic<32>(0x10203u);
  }
  cpphdl::logic<32> heavy_2_cache{};
  long heavy_2_clock = -1;
  cpphdl::logic<32> &heavy_2() {
    if (heavy_2_clock == _system_clock) return heavy_2_cache;
    heavy_2_clock = _system_clock;
    return heavy_2_cache = heavy_1() + cpphdl::logic<32>(3u);
  }
  cpphdl::logic<32> heavy_3_cache{};
  long heavy_3_clock = -1;
  cpphdl::logic<32> &heavy_3() {
    if (heavy_3_clock == _system_clock) return heavy_3_cache;
    heavy_3_clock = _system_clock;
    return heavy_3_cache = (heavy_2() << 3) | (heavy_2() >> 29);
  }
  cpphdl::logic<32> heavy_4_cache{};
  long heavy_4_clock = -1;
  cpphdl::logic<32> &heavy_4() {
    if (heavy_4_clock == _system_clock) return heavy_4_cache;
    heavy_4_clock = _system_clock;
    return heavy_4_cache = heavy_3() ^ cpphdl::logic<32>(0xa5a55a5au);
  }
  cpphdl::logic<32> heavy_5_cache{};
  long heavy_5_clock = -1;
  cpphdl::logic<32> &heavy_5() {
    if (heavy_5_clock == _system_clock) return heavy_5_cache;
    heavy_5_clock = _system_clock;
    return heavy_5_cache = heavy_4() + cpphdl::logic<32>(5u);
  }
  cpphdl::logic<32> heavy_6_cache{};
  long heavy_6_clock = -1;
  cpphdl::logic<32> &heavy_6() {
    if (heavy_6_clock == _system_clock) return heavy_6_cache;
    heavy_6_clock = _system_clock;
    return heavy_6_cache = (heavy_5() << 7) | (heavy_5() >> 25);
  }
  cpphdl::logic<32> heavy_7_cache{};
  long heavy_7_clock = -1;
  cpphdl::logic<32> &heavy_7() {
    if (heavy_7_clock == _system_clock) return heavy_7_cache;
    heavy_7_clock = _system_clock;
    return heavy_7_cache = heavy_6() ^ cpphdl::logic<32>(0x31415926u);
  }
  cpphdl::logic<32> heavy_8_cache{};
  long heavy_8_clock = -1;
  cpphdl::logic<32> &heavy_8() {
    if (heavy_8_clock == _system_clock) return heavy_8_cache;
    heavy_8_clock = _system_clock;
    return heavy_8_cache = heavy_7() + cpphdl::logic<32>(7u);
  }
  cpphdl::logic<32> heavy_9_cache{};
  long heavy_9_clock = -1;
  cpphdl::logic<32> &heavy_9() {
    if (heavy_9_clock == _system_clock) return heavy_9_cache;
    heavy_9_clock = _system_clock;
    return heavy_9_cache = heavy_8() ^ (heavy_8() >> 11);
  }
  cpphdl::logic<32> light_a_cache{};
  long light_a_clock = -1;
  cpphdl::logic<32> &light_a() {
    if (light_a_clock == _system_clock) return light_a_cache;
    light_a_clock = _system_clock;
    return light_a_cache = input + cpphdl::logic<32>(11u);
  }
  cpphdl::logic<32> light_b_cache{};
  long light_b_clock = -1;
  cpphdl::logic<32> &light_b() {
    if (light_b_clock == _system_clock) return light_b_cache;
    light_b_clock = _system_clock;
    return light_b_cache = input ^ cpphdl::logic<32>(13u);
  }
  cpphdl::logic<32> light_c_cache{};
  long light_c_clock = -1;
  cpphdl::logic<32> &light_c() {
    if (light_c_clock == _system_clock) return light_c_cache;
    light_c_clock = _system_clock;
    return light_c_cache = input << 1;
  }

  cpphdl::reg<cpphdl::logic<32>> result_heavy{};
  cpphdl::reg<cpphdl::logic<32>> result_a{};
  cpphdl::reg<cpphdl::logic<32>> result_b{};
  cpphdl::reg<cpphdl::logic<32>> result_c{};

  void _assign() {}
  void _work(bool) {
    result_heavy._next = heavy_9();
    result_a._next = light_a();
    result_b._next = light_b();
    result_c._next = light_c();
  }
  void _strobe() {
    result_heavy.strobe();
    result_a.strobe();
    result_b.strobe();
    result_c.strobe();
  }
};
