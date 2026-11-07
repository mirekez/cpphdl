#include "model.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main(){
 cpphdl_native::Model m;
 for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b){
  m.a[0]=a;m.b[0]=b;
  m.wide_a={uint32_t(a*977),a&128?0x80000000u:0u};
  m.wide_b={uint32_t(b*977),b&128?0x80000000u:0u};
  m.step();int sa=int(int8_t(a)),sb=int(int8_t(b));
  assert(m.signed_less[0]==(sa<sb));assert(m.unsigned_less[0]==(a<b));
  assert(m.signed_extend[0]==uint32_t(sa));assert(m.unsigned_extend[0]==a);
  assert(m.signed_sum[0]==uint16_t(sa+sb));assert(m.shifted[0]==uint8_t(sa>>3));
  bool less=((a^b)&128)?bool(a&128):a<b;
  assert(m.wide_less[0]==less);
  assert(m.extra_signed[0]==uint32_t(sa));
  assert(m.extra_signed[1]==(sa<0?UINT32_MAX:0));
  assert(m.extra_signed[2]==(sa<0?65535u:0u));
  assert(m.extra_unsigned[0]==a && !m.extra_unsigned[1] && !m.extra_unsigned[2]);
 }
 std::puts("PASS native graph signedness casts: comparisons, width extension, sum, shift, 80-bit extension");
}
