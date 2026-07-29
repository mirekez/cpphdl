#pragma once

#include "cpphdl.h"

extern long _system_clock;

class ProceduralComb : public cpphdl::Module {
public:
  bool input{};
  bool first_cache{};
  long first_clock = -1;
  bool second_cache{};
  long second_clock = -1;
  cpphdl::reg<cpphdl::logic<1>> result{};

  bool &first() {
    if (first_clock == _system_clock)
      return first_cache;
    first_clock = _system_clock;
    first_cache = false;
    if (input) {
      first_cache = true;
    }
    return first_cache;
  }

  bool &second() {
    if (second_clock == _system_clock)
      return second_cache;
    second_clock = _system_clock;
    return second_cache = this->first();
  }

  void _work(bool) { result._next = second(); }
  void _strobe() { result.strobe(); }
};
