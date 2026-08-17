#pragma once

#include "cpphdl.h"

class OptimizeMath : public cpphdl::Module {
public:
  cpphdl::logic<32> input{};
  bool sign{};

  cpphdl::logic<32> reverse_cache{};
  cpphdl::logic<32> &reverse() {
    reverse_cache[0] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 31) & 1ull);
    reverse_cache[1] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 30) & 1ull);
    reverse_cache[2] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 29) & 1ull);
    reverse_cache[3] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 28) & 1ull);
    reverse_cache[4] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 27) & 1ull);
    reverse_cache[5] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 26) & 1ull);
    reverse_cache[6] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 25) & 1ull);
    reverse_cache[7] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 24) & 1ull);
    reverse_cache[8] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 23) & 1ull);
    reverse_cache[9] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 22) & 1ull);
    reverse_cache[10] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 21) & 1ull);
    reverse_cache[11] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 20) & 1ull);
    reverse_cache[12] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 19) & 1ull);
    reverse_cache[13] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 18) & 1ull);
    reverse_cache[14] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 17) & 1ull);
    reverse_cache[15] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 16) & 1ull);
    reverse_cache[16] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 15) & 1ull);
    reverse_cache[17] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 14) & 1ull);
    reverse_cache[18] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 13) & 1ull);
    reverse_cache[19] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 12) & 1ull);
    reverse_cache[20] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 11) & 1ull);
    reverse_cache[21] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 10) & 1ull);
    reverse_cache[22] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 9) & 1ull);
    reverse_cache[23] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 8) & 1ull);
    reverse_cache[24] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 7) & 1ull);
    reverse_cache[25] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 6) & 1ull);
    reverse_cache[26] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 5) & 1ull);
    reverse_cache[27] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 4) & 1ull);
    reverse_cache[28] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 3) & 1ull);
    reverse_cache[29] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 2) & 1ull);
    reverse_cache[30] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 1) & 1ull);
    reverse_cache[31] = cpphdl::logic<1>(((uint64_t)(static_cast<cpphdl::logic<32>>(input)) >> 0) & 1ull);
    return reverse_cache;
  }

  cpphdl::logic<20> replicate_cache{};
  cpphdl::logic<20> &replicate() {
    replicate_cache[19] = sign;
    replicate_cache[18] = sign;
    replicate_cache[17] = sign;
    replicate_cache[16] = sign;
    replicate_cache[15] = sign;
    replicate_cache[14] = sign;
    replicate_cache[13] = sign;
    replicate_cache[12] = sign;
    replicate_cache[11] = sign;
    replicate_cache[10] = sign;
    replicate_cache[9] = sign;
    replicate_cache[8] = sign;
    replicate_cache[7] = sign;
    replicate_cache[6] = sign;
    replicate_cache[5] = sign;
    replicate_cache[4] = sign;
    replicate_cache[3] = sign;
    replicate_cache[2] = sign;
    replicate_cache[1] = sign;
    replicate_cache[0] = sign;
    return replicate_cache;
  }

  cpphdl::logic<32> sign_extend_cache{};
  cpphdl::logic<32> &sign_extend() {
    sign_extend_cache[31] = sign;
    sign_extend_cache[30] = sign;
    sign_extend_cache[29] = sign;
    sign_extend_cache[28] = sign;
    sign_extend_cache[27] = sign;
    sign_extend_cache[26] = sign;
    sign_extend_cache[25] = sign;
    sign_extend_cache[24] = sign;
    sign_extend_cache[23] = sign;
    sign_extend_cache[22] = sign;
    sign_extend_cache[21] = sign;
    sign_extend_cache[20] = sign;
    sign_extend_cache[19] = sign;
    sign_extend_cache[18] = sign;
    sign_extend_cache[17] = sign;
    sign_extend_cache[16] = sign;
    sign_extend_cache[15] = sign;
    sign_extend_cache[14] = sign;
    sign_extend_cache[13] = sign;
    sign_extend_cache[12] = sign;
    sign_extend_cache[11] = sign;
    sign_extend_cache[10] = sign;
    sign_extend_cache[9] = sign;
    sign_extend_cache[8] = sign;
    sign_extend_cache.bits(7, 0) = cpphdl::logic<8>(input);
    return sign_extend_cache;
  }

  cpphdl::logic<8> partial_cache{};
  cpphdl::logic<8> &partial() {
    partial_cache[3] = sign;
    partial_cache[2] = sign;
    partial_cache[1] = sign;
    partial_cache[0] = sign;
    return partial_cache;
  }

  cpphdl::reg<cpphdl::logic<32>> reverse_result{};
  cpphdl::reg<cpphdl::logic<20>> replicate_result{};
  cpphdl::reg<cpphdl::logic<32>> sign_extend_result{};
  cpphdl::reg<cpphdl::logic<8>> partial_result{};

  void _assign() {}
  void _work(bool) {
    reverse_result._next = reverse();
    replicate_result._next = replicate();
    sign_extend_result._next = sign_extend();
    partial_result._next = partial();
  }
  void _strobe() {
    reverse_result.strobe();
    replicate_result.strobe();
    sign_extend_result.strobe();
    partial_result.strobe();
  }
};
