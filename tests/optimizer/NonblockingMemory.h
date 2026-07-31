#pragma once

#include "cpphdl.h"

class NonblockingMemory : public cpphdl::Module {
public:
  bool write_enable{};
  bool read_enable{};
  cpphdl::logic<2> address{};
  cpphdl::logic<8> write_data{};
  cpphdl::array<4, cpphdl::logic<8>> memory{};
  cpphdl::reg<cpphdl::logic<8>> read_data{};

  void _work(bool) {
    if (write_enable) {
      const size_t index = static_cast<uint64_t>(address);
      memory[index] = write_data;
    }
    if (read_enable) {
      const size_t index = static_cast<uint64_t>(address);
      read_data._next = memory[index];
    }
  }

  void _strobe() { read_data.strobe(); }
};
