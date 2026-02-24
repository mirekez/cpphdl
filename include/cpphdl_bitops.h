#include <cstdint>
#include <cstddef>
#include <cstring>

template<typename BASE>
class bitops
{
private:
    static constexpr size_t U64 = sizeof(uint64_t);

    static void apply_and(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a & b; }, [](uint8_t  a, uint8_t  b){ return a & b; });
    }

    static void apply_or(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a | b; }, [](uint8_t  a, uint8_t  b){ return a | b; });
    }

    static void apply_xor(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a ^ b; }, [](uint8_t  a, uint8_t  b){ return a ^ b; });
    }

    static void apply_add(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a + b; }, [](uint8_t  a, uint8_t  b){ return a + b; });
    }

    static void apply_sub(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a - b; }, [](uint8_t  a, uint8_t  b){ return a - b; });
    }

    template<typename F64, typename F8>
    static void apply_binary(BASE& dst, const BASE& rhs, F64 op64, F8 op8)
    {
        size_t size = sizeof(BASE);

        uint8_t* dptr = reinterpret_cast<uint8_t*>(&dst);
        const uint8_t* rptr = reinterpret_cast<const uint8_t*>(&rhs);

        size_t i = 0;

        // 64-bit chunks
        for (; i + U64 <= size; i += U64) {
            auto* d64 = reinterpret_cast<uint64_t*>(dptr + i);
            const auto* r64 = reinterpret_cast<const uint64_t*>(rptr + i);
            *d64 = op64(*d64, *r64);
        }

        // remaining bytes
        for (; i < size; ++i) {
            dptr[i] = op8(dptr[i], rptr[i]);
        }
    }

public:
    template<typename T>
    BASE operator&(const T& rhs) const
    {
        BASE result = static_cast<const BASE&>(*this);
        apply_and(result, rhs);
        return result;
    }

    template<typename T>
    BASE operator|(const T& rhs) const
    {
        BASE result = static_cast<const BASE&>(*this);
        apply_or(result, rhs);
        return result;
    }

    template<typename T>
    BASE operator^(const T& rhs) const
    {
        BASE result = static_cast<const BASE&>(*this);
        apply_xor(result, rhs);
        return result;
    }

    BASE operator~() const
    {
        BASE result = static_cast<const BASE&>(*this);

        size_t size = sizeof(BASE);
        uint8_t* ptr = reinterpret_cast<uint8_t*>(&result);

        for (size_t i = 0; i < size; ++i)
            ptr[i] = ~ptr[i];

        return result;
    }

    BASE operator<<(size_t shift) const
    {
        BASE result{};

        constexpr size_t total_bits = sizeof(BASE) * 8;
        if (shift >= total_bits) {
            return result;
        }

        const uint8_t* src = reinterpret_cast<const uint8_t*>(this);
        uint8_t* dst = reinterpret_cast<uint8_t*>(&result);

        constexpr size_t step = sizeof(uint64_t);
        constexpr size_t size = sizeof(BASE);

        size_t word_shift = shift / 64;
        size_t bit_shift  = shift % 64;

        size_t word_count = size / step;

        const uint64_t* s64 = reinterpret_cast<const uint64_t*>(src);
        uint64_t* d64 = reinterpret_cast<uint64_t*>(dst);

        for (size_t i = word_count; i-- > 0;) {
            uint64_t value = 0;

            if (i >= word_shift) {
                value = s64[i - word_shift] << bit_shift;

                if (bit_shift && i > word_shift) {
                    value |= s64[i - word_shift - 1] >> (64 - bit_shift);
                }
            }

            d64[i] = value;
        }

        return result;
    }

    BASE operator>>(size_t shift) const
    {
        BASE result{};

        constexpr size_t total_bits = sizeof(BASE) * 8;
        if (shift >= total_bits) {
            return result;
        }

        const uint8_t* src = reinterpret_cast<const uint8_t*>(this);
        uint8_t* dst = reinterpret_cast<uint8_t*>(&result);

        constexpr size_t step = sizeof(uint64_t);
        constexpr size_t size = sizeof(BASE);

        size_t word_shift = shift / 64;
        size_t bit_shift  = shift % 64;

        size_t word_count = size / step;

        const uint64_t* s64 = reinterpret_cast<const uint64_t*>(src);
        uint64_t* d64 = reinterpret_cast<uint64_t*>(dst);

        for (size_t i = 0; i < word_count; ++i) {
            uint64_t value = 0;

            if (i + word_shift < word_count) {
                value = s64[i + word_shift] >> bit_shift;

                if (bit_shift && i + word_shift + 1 < word_count) {
                    value |= s64[i + word_shift + 1] << (64 - bit_shift);
                }
            }

            d64[i] = value;
        }

        return result;
    }

    template<typename T>
    BASE operator+(const T& rhs) const
    {
        BASE result{};

        const uint8_t* ap = reinterpret_cast<const uint8_t*>(this);
        const uint8_t* bp = reinterpret_cast<const uint8_t*>(&rhs);
        uint8_t* rp = reinterpret_cast<uint8_t*>(&result);

        constexpr size_t size = sizeof(BASE);
        constexpr size_t step = sizeof(uint64_t);

        uint64_t carry = 0;
        size_t i = 0;

        // 64-bit blocks
        for (; i + step <= size; i += step) {
            uint64_t a, b;
            std::memcpy(&a, ap + i, step);
            std::memcpy(&b, bp + i, step);

            uint64_t sum = a + b + carry;

            // Detect carry
            carry = (sum < a) || (carry && sum == a);

            std::memcpy(rp + i, &sum, step);
        }

        // Remaining bytes
        for (; i < size; ++i) {
            uint16_t sum = uint16_t(ap[i]) + uint16_t(bp[i]) + carry;
            rp[i] = uint8_t(sum);
            carry = sum >> 8;
        }

        return result;
    }

    template<typename T>
    BASE operator-(const T& rhs) const
    {
        BASE result{};

        const uint8_t* ap = reinterpret_cast<const uint8_t*>(this);
        const uint8_t* bp = reinterpret_cast<const uint8_t*>(&rhs);
        uint8_t* rp = reinterpret_cast<uint8_t*>(&result);

        constexpr size_t size = sizeof(BASE);
        constexpr size_t step = sizeof(uint64_t);

        uint64_t borrow = 0;
        size_t i = 0;

        for (; i + step <= size; i += step) {
            uint64_t a, b;
            std::memcpy(&a, ap + i, step);
            std::memcpy(&b, bp + i, step);

            uint64_t diff = a - b - borrow;

            borrow = (a < b + borrow);

            std::memcpy(rp + i, &diff, step);
        }

        for (; i < size; ++i) {
            int16_t diff = int16_t(ap[i]) - int16_t(bp[i]) - borrow;
            borrow = diff < 0;
            rp[i] = uint8_t(diff);
        }

        return result;
    }

    bool operator==(const BASE& rhs) const {
        return std::memcmp(this, &rhs, sizeof(BASE)) == 0;
    }

    bool operator!=(const BASE& rhs) const {
        return !(*this == rhs);
    }
};
