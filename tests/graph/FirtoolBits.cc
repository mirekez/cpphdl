#include "cpphdl.h"
template <size_t Width> constexpr cpphdl::logic<Width> firtoolConstant(std::initializer_list<uint64_t> words) {
    cpphdl::logic<Width> result = 0;
    size_t word = 0;
    for (uint64_t value : words) {
        for (size_t bit = 0; bit < 64 && word * 64 + bit < Width; ++bit)
            result.set(word * 64 + bit, (value >> bit) & 1);
        ++word;
    }
    return result;
}
template <typename T> constexpr const T& firtoolIdentity(const T& value) { return value; }
class FirtoolBits : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<32>) data_in;
    _PORT(cpphdl::logic<4>) index_in;
    _PORT(cpphdl::logic<9>) result_out;
    _PORT(cpphdl::logic<65>) masked_out;
    _PORT(cpphdl::logic<168>) chunks_out;
    _PORT(cpphdl::logic<8>) reference_out;
    _PORT(cpphdl::logic<1>) wide_bit_out, stored_bit_out;
    inline static constexpr cpphdl::logic<8> reference_value = 19;
    _PORT(cpphdl::logic<1>) bit_out, cat_bit_out;
    cpphdl::logic<9> narrow() {
        cpphdl::logic<9> result(data_in());
        for (size_t bit = 9; bit < cpphdl::logic<9>::SIZE * 8; ++bit) result.set(bit, 0);
        result.set(0, uint64_t(data_in()) ^ 1);
        return result;
    }
    bool storedBit() {
        cpphdl::logic<128> stored(cpphdl::cat<64, 64>(cpphdl::logic<64>(0x123456789abcdef0ULL),
            cpphdl::logic<64>(data_in())));
        return stored.get(uint64_t(index_in()) * 8 + 3);
    }
    cpphdl::logic<168> chunks() {
        cpphdl::logic<168> result(cpphdl::repeat<21>(cpphdl::logic<8>(0x5a)));
        for(size_t chunk=0;chunk<8;++chunk) if(data_in().get(chunk))
            result.bits((chunk+1)*21-1,chunk*21) = cpphdl::cat<64,64,40>(
                cpphdl::logic<64>(data_in()), cpphdl::logic<64>(0x123456789abcdef0ULL),
                cpphdl::logic<40>(uint64_t(data_in())*123)).bits((chunk+1)*21-1,chunk*21);
        return result;
    }
    void _assign() {
        result_out = _ASSIGN(narrow());
        reference_out = _ASSIGN(firtoolIdentity(reference_value));
        masked_out = _ASSIGN(cpphdl::logic<65>(data_in()) ^ firtoolConstant<65>({0x0123456789abcdefULL, 1ULL}));
        bit_out = _ASSIGN(narrow().get(uint64_t(index_in())));
        wide_bit_out = _ASSIGN(cpphdl::cat<64, 64>(cpphdl::logic<64>(0x123456789abcdef0ULL),
            cpphdl::logic<64>(data_in())).get(uint64_t(index_in()) * 8 + 3));
        stored_bit_out = _ASSIGN(storedBit());
        chunks_out = _ASSIGN(chunks());
        cat_bit_out = _ASSIGN(cpphdl::cat<3, 6>(narrow().slice<8, 6>(), narrow().slice<5, 0>()).get(uint64_t(index_in())));
    }
};
extern FirtoolBits cpphdl_top;
#ifdef CHECK_FIRTOOL_BITS
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    FirtoolBits top;
    cpphdl_native::Model model;
    cpphdl::logic<32> data;
    cpphdl::logic<4> index;
    top.data_in = _ASSIGN(data); top.index_in = _ASSIGN(index); top._assign();
    for (unsigned i = 0; i < 4096; ++i) {
        data = i * 123457; index = i % 16;
        auto expected = (uint64_t(data) & 511) ^ 1;
        model.data[0] = uint64_t(data); model.index[0] = uint64_t(index);
        ++_system_clock; model.eval();
        if (model.reference[0] != 19 || uint64_t(top.reference_out()) != 19) return 3;
        unsigned bit = uint64_t(index) * 8 + 3;
        uint64_t selectedWord = bit < 64 ? uint64_t(data) : 0x123456789abcdef0ULL;
        auto selectedBit = (selectedWord >> (bit % 64)) & 1;
        if (model.wide_bit[0] != selectedBit || uint64_t(top.wide_bit_out()) != selectedBit) return 4;
        if (model.stored_bit[0] != selectedBit || uint64_t(top.stored_bit_out()) != selectedBit) return 5;
        auto chunks = top.chunks_out();
        for(unsigned b=0;b<168;++b) {
            auto word = b<40 ? uint64_t(data)*123 : b<104 ? 0x123456789abcdef0ULL : uint64_t(data);
            auto offset = b<40 ? b : b<104 ? b-40 : b-104;
            auto expectedBit = ((uint64_t(data)>>(b/21))&1) ? (word>>offset)&1 : (0x5a>>(b%8))&1;
            if(chunks.get(b)!=expectedBit || ((model.chunks[b/32]>>(b%32))&1)!=expectedBit) return 6;
        }
        auto masked = top.masked_out();
        auto expectedMask = uint64_t(data) ^ 0x0123456789abcdefULL;
        if (uint64_t(masked) != expectedMask || masked.get(64) != 1 ||
            model.masked[0] != uint32_t(expectedMask) || model.masked[1] != uint32_t(expectedMask >> 32) ||
            model.masked[2] != 1) return 2;
        if (model.result[0] != expected || uint64_t(top.result_out()) != expected ||
            model.bit[0] != ((expected >> uint64_t(index)) & 1) || uint64_t(top.bit_out()) != model.bit[0] ||
            model.cat_bit[0] != model.bit[0] || uint64_t(top.cat_bit_out()) != model.bit[0]) return 1;
    }
    std::puts("firtool bit helpers: 4096 samples PASS");
}
#endif
