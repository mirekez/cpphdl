#include <cstdint>
extern "C" [[gnu::noinline]] uint64_t native_mask_gather(const uint64_t* data){
const uint64_t value1129130 = (((((uint64_t(data[0])) & 1ull) * 32767ull) | ((((data[1] >> 13) & 1ull) * 67108863ull) << 15) | ((((uint64_t(data[0])) & 1ull) * 8388607ull) << 41)) ^ (18446744073709551615ull)) & 18446744073709551615ull;
const uint64_t value1113371 = ((((data[2] & 1ull) * 18446744073709551615ull)) & (((data[3] >> 37) & 134217727ull) | ((data[4] & 137438953471ull) << 27))) & 18446744073709551615ull;
const uint64_t value1113370 = ((data[5]) & (((data[4] >> 37) & 134217727ull) | ((data[6] & 137438953471ull) << 27))) & 18446744073709551615ull;
const uint64_t value1113372 = ((value1113371) | (value1113370)) & 18446744073709551615ull;
const uint64_t value1165389 = ((((data[2] & 1ull) * 16383ull) | (((data[7] >> 41) & 1ull) << 14) | ((((data[1] >> 8) & 1ull) * 67108863ull) << 15) | (((data[2] & 1ull) * 8388607ull) << 41)) & (((data[4] >> 37) & 16383ull) | (((data[8] >> 30) & 134217727ull) << 14) | (((data[9] >> 61) & 1ull) << 41) | (((data[6] >> 51) & 15ull) << 42))) & 18446744073709551615ull;
const uint64_t value1113373 = ((((data[2] & 1ull) * 16383ull) | (((data[7] >> 41) & 1ull) << 14) | ((((data[1] >> 8) & 1ull) * 67108863ull) << 15) | (((data[2] & 1ull) * 8388607ull) << 41)) ^ (18446744073709551615ull)) & 18446744073709551615ull;
const uint64_t value1113374 = ((value1113373) & (((data[6] >> 37) & 16383ull) | (((data[6] >> 51) & 8191ull) << 41) | ((data[10] & 1023ull) << 54))) & 18446744073709551615ull;
const uint64_t value1113376 = (((value1165389 & 70368744177663ull)) | ((value1113374 & 16383ull) | (((value1113374 >> 41) & 8388607ull) << 41))) & 18446744073709551615ull;
return ((value1129130) & (((value1113372 >> 63) & 1ull) | ((value1113376 & 16383ull) << 1) | (((value1113376 >> 41) & 31ull) << 41) | (((value1113374 >> 46) & 262143ull) << 46))) & 18446744073709551615ull;
}

#include <cstdio>
int main(){
 const uint64_t data[]={0x3c58382fcfeb4280ull,0x7e5e5f1a429c5725ull,0x31a60e62b8a7e927ull,
 0xfedf94bc894129a7ull,0x9880cd90f179e5cfull,0x7f4c51548f5e11bdull,0x7738e4168b6eb381ull,
 0x4e6c91482f70115aull,0xb4d7c542f27bc3ffull,0x30c6346b4bed1b71ull,0xc72d928e5bfc52e7ull};
 auto actual=native_mask_gather(data);const uint64_t expected=0x1e0000000cd9ull;
 std::printf("masked gather actual=%llx expected=%llx\n",(unsigned long long)actual,(unsigned long long)expected);
 return actual!=expected;
}
