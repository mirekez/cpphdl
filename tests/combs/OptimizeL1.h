#pragma once

#include "cpphdl.h"

extern long _system_clock;

class OptimizeL1;
inline OptimizeL1 *_optimized_root_instance = nullptr;
cpphdl::logic<16> optimize_l1_root_input();

class OptimizeL1Leaf : public cpphdl::Module {
public:
  _PORT(cpphdl::logic<16>) value_in;
  _PORT(bool) enable_in;

  cpphdl::logic<16> equal_a_cache{};
  long equal_a_clock = -1;
  cpphdl::logic<16> &equal_a() {
    if (equal_a_clock == _system_clock) {
      return equal_a_cache;
    }
    equal_a_clock = _system_clock;
    return equal_a_cache = value_in() + cpphdl::logic<16>(3);
  }

  cpphdl::logic<16> equal_b_cache{};
  long equal_b_clock = -1;
  cpphdl::logic<16> &equal_b() {
    if (equal_b_clock == _system_clock) {
      return equal_b_cache;
    }
    equal_b_clock = _system_clock;
    return equal_b_cache = value_in() + cpphdl::logic<16>(3);
  }

  bool work_observed{};
  unsigned procedural_invocations{};
  cpphdl::logic<16> procedural_cache{};
  long procedural_clock = -1;
  cpphdl::logic<16> &procedural() {
    ++procedural_invocations;
    if (procedural_clock == _system_clock) {
      return procedural_cache;
    }
    procedural_clock = _system_clock;
    procedural_cache = this->equal_a();
    if (enable_in()) {
      procedural_cache = procedural_cache + equal_b();
    }
    return procedural_cache;
  }

  cpphdl::logic<16> _optimized_value() { return procedural(); }

  void _assign() {}
  void _work(bool) {}
  void _strobe() {}
};

inline cpphdl::logic<16> optimize_l1_leaf_value(OptimizeL1Leaf &leaf) {
  return leaf._optimized_value();
}

inline cpphdl::logic<16> optimize_l1_leaf_value(OptimizeL1Leaf *leaf) {
  return leaf->_optimized_value();
}

class OptimizeL1 : public cpphdl::Module {
public:
  cpphdl::logic<16> input{};
  bool enable{};

  OptimizeL1Leaf leaf_storage{};
  OptimizeL1Leaf *leaf = &leaf_storage;

  cpphdl::logic<16> combined_cache{};
  long combined_clock = -1;
  cpphdl::logic<16> &combined() {
    if (combined_clock == _system_clock) {
      return combined_cache;
    }
    combined_clock = _system_clock;
    return combined_cache =
               leaf->equal_a() + leaf->equal_b() +
               optimize_l1_leaf_value(leaf) + optimize_l1_leaf_value(leaf) +
               optimize_l1_root_input();
  }

  cpphdl::logic<16> _optimized_root_input_in_binding() { return input; }

  cpphdl::reg<cpphdl::logic<16>> result{};

  void _assign() {
    leaf->value_in = _ASSIGN(input);
    leaf->enable_in = _ASSIGN(enable);
    leaf->_assign();
  }

  void _work(bool reset) {
    leaf->_work(reset);
    leaf->work_observed = enable;
    result._next = combined();
  }

  void _strobe() {
    leaf->_strobe();
    result.strobe();
  }
};

inline cpphdl::logic<16> optimize_l1_root_input() {
  return _optimized_root_instance->_optimized_root_input_in_binding();
}
