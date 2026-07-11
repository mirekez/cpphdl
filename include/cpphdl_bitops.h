#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

template<typename T, typename = void>
struct bitops_value_size
{
    static constexpr size_t value = sizeof(T);
};

template<typename T>
struct bitops_value_size<T, std::void_t<decltype(T::SIZE)>>
{
    static constexpr size_t value = T::SIZE;
};

template<typename BASE>
class bitops
{
private:
    static uint8_t* raw_bytes(BASE& value)
    {
        return reinterpret_cast<uint8_t*>(&value);
    }

    static const uint8_t* raw_bytes(const BASE& value)
    {
        return reinterpret_cast<const uint8_t*>(&value);
    }

    static void apply_and(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a & b; });
    }

    static void apply_or(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a | b; });
    }

    static void apply_xor(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a ^ b; });
    }

    static void apply_add(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a + b; });
    }

    static void apply_sub(BASE& dst, const BASE& rhs)
    {
        apply_binary(dst, rhs, [](uint64_t a, uint64_t b){ return a - b; });
    }

    template<typename F64>
    static void apply_binary(BASE& dst, const BASE& rhs, F64 op)
    {
        size_t size = bitops_value_size<BASE>::value;

        uint8_t* dptr = raw_bytes(dst);
        const uint8_t* rptr = raw_bytes(rhs);

        size_t i = 0;
        // 64-bit chunks
        for (; i + sizeof(uint64_t) <= size; i += sizeof(uint64_t)) {
            uint64_t lhs = 0;
            uint64_t rhs = 0;
            uint64_t result = 0;
            std::memcpy(&lhs, dptr + i, sizeof(lhs));
            std::memcpy(&rhs, rptr + i, sizeof(rhs));
            result = op(lhs, rhs);
            std::memcpy(dptr + i, &result, sizeof(result));
        }

        // remaining bytes
        for (; i < size; ++i) {
            dptr[i] = op(dptr[i], rptr[i]);
        }
    }

public:
    bitops() = default;

    template<typename T>
    bitops(const T& other)
    {
        BASE& dst = static_cast<BASE&>(*this);
        std::memcpy(raw_bytes(dst), &other, std::min(bitops_value_size<BASE>::value, sizeof(other)));
        if (bitops_value_size<BASE>::value > sizeof(other)) {
            std::memset(raw_bytes(dst) + sizeof(other), 0, bitops_value_size<BASE>::value - sizeof(other));
        }
    }

    template<typename T>
    BASE& operator=(const T& other)
    {
        BASE& dst = static_cast<BASE&>(*this);
        std::memcpy(raw_bytes(dst), &other, std::min(bitops_value_size<BASE>::value, sizeof(other)));
        if (bitops_value_size<BASE>::value > sizeof(other)) {
            std::memset(raw_bytes(dst) + sizeof(other), 0, bitops_value_size<BASE>::value - sizeof(other));
        }
        return dst;
    }

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

        size_t size = bitops_value_size<BASE>::value;
        uint8_t* ptr = raw_bytes(result);

        for (size_t i = 0; i < size; ++i)
            ptr[i] = ~ptr[i];

        return result;
    }

    BASE operator<<(size_t shift) const
    {
        BASE result{};

        constexpr size_t size = bitops_value_size<BASE>::value;
        constexpr size_t step = sizeof(uint64_t);
        constexpr size_t total_bits = size * 8;

        if (shift >= total_bits) {
            return result;
        }

        const uint8_t* src8 = raw_bytes(static_cast<const BASE&>(*this));
        uint8_t* dst8 = raw_bytes(result);

        constexpr size_t word_count = size / step;
        constexpr size_t remainder  = size % step;
        constexpr size_t total_words = word_count + (remainder ? 1 : 0);

        size_t word_shift = shift / 64;
        size_t bit_shift  = shift % 64;

        for (size_t i = total_words; i-- > 0;) {

            uint64_t cur  = 0;
            uint64_t prev = 0;

            if (i >= word_shift) {
                size_t src_index = i - word_shift;

                if (src_index < word_count) {
                    cur = ((const uint64_t*)src8)[src_index];
                }
                else if (src_index == word_count && remainder) {
                    std::memcpy(&cur, src8 + word_count * step, remainder);
                }
            }

            if (bit_shift && i > word_shift) {
                size_t prev_index = i - word_shift - 1;

                if (prev_index < word_count) {
                    prev = ((const uint64_t*)src8)[prev_index];
                }
                else if (prev_index == word_count && remainder) {
                    std::memcpy(&prev, src8 + word_count * step, remainder);
                }
            }

            uint64_t value = (cur << bit_shift);

            if (bit_shift) {
                value |= (prev >> (64 - bit_shift));
            }

            if (i < word_count) {
                ((uint64_t*)dst8)[i] = value;
            }
            else if (i == word_count && remainder) {
                std::memcpy(dst8 + word_count * step, &value, remainder);
            }
        }

        return result;
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, size_t>, int> = 0>
    BASE operator<<(T shift) const
    {
        return operator<<(static_cast<size_t>(shift));
    }

    BASE operator>>(size_t shift) const
    {
        BASE result{};

        constexpr size_t size = bitops_value_size<BASE>::value;
        constexpr size_t step = sizeof(uint64_t);
        constexpr size_t total_bits = size * 8;

        if (shift >= total_bits) {
            return result;
        }

        const uint8_t* src8 = raw_bytes(static_cast<const BASE&>(*this));
        uint8_t* dst8 = raw_bytes(result);

        constexpr size_t word_count = size / step;
        constexpr size_t remainder  = size % step;
        constexpr size_t total_words = word_count + (remainder ? 1 : 0);

        size_t word_shift = shift / 64;
        size_t bit_shift  = shift % 64;

        for (size_t i = 0; i < total_words; ++i) {

            uint64_t cur  = 0;
            uint64_t next = 0;

            size_t src_index = i + word_shift;

            if (src_index < word_count) {
                cur = ((const uint64_t*)src8)[src_index];
            }
            else if (src_index == word_count && remainder) {
                std::memcpy(&cur, src8 + word_count * step, remainder);
            }

            if (bit_shift) {
                size_t next_index = src_index + 1;

                if (next_index < word_count) {
                    next = ((const uint64_t*)src8)[next_index];
                }
                else if (next_index == word_count && remainder) {
                    std::memcpy(&next, src8 + word_count * step, remainder);
                }
            }

            uint64_t value = (cur >> bit_shift);

            if (bit_shift) {
                value |= (next << (64 - bit_shift));
            }

            if (i < word_count) {
                ((uint64_t*)dst8)[i] = value;
            }
            else if (i == word_count && remainder) {
                std::memcpy(dst8 + word_count * step, &value, remainder);
            }
        }

        return result;
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, size_t>, int> = 0>
    BASE operator>>(T shift) const
    {
        return operator>>(static_cast<size_t>(shift));
    }

    template<typename T>
    BASE operator+(const T& rhs) const
    {
        BASE result{};
        BASE rhs_base = rhs;

        const uint8_t* ap = raw_bytes(static_cast<const BASE&>(*this));
        const uint8_t* bp = raw_bytes(rhs_base);
        uint8_t* rp = raw_bytes(result);

        constexpr size_t size = bitops_value_size<BASE>::value;
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
        BASE rhs_base = rhs;

        const uint8_t* ap = raw_bytes(static_cast<const BASE&>(*this));
        const uint8_t* bp = raw_bytes(rhs_base);
        uint8_t* rp = raw_bytes(result);

        constexpr size_t size = bitops_value_size<BASE>::value;
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

    bool operator==(const BASE& rhs) const
    {
        return std::memcmp(raw_bytes(static_cast<const BASE&>(*this)), raw_bytes(rhs), bitops_value_size<BASE>::value) == 0;
    }

    bool operator!=(const BASE& rhs) const
    {
        return !(*this == rhs);
    }

    template<typename T, typename std::enable_if_t<
        !std::is_integral_v<T> &&
        !std::is_enum_v<T> &&
        !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, BASE>, int> = 0>
    bool operator==(const T& rhs) const
    {
        constexpr size_t lhs_size = bitops_value_size<BASE>::value;
        constexpr size_t rhs_size = bitops_value_size<T>::value;
        constexpr size_t min_size = lhs_size < rhs_size ? lhs_size : rhs_size;

        const uint8_t* lhs_ptr = raw_bytes(static_cast<const BASE&>(*this));
        const uint8_t* rhs_ptr = reinterpret_cast<const uint8_t*>(&rhs);

        if (std::memcmp(lhs_ptr, rhs_ptr, min_size) != 0) {
            return false;
        }
        if constexpr (lhs_size > rhs_size) {
            for (size_t i = rhs_size; i < lhs_size; ++i) {
                if (lhs_ptr[i] != 0) {
                    return false;
                }
            }
        }
        else if constexpr (rhs_size > lhs_size) {
            for (size_t i = lhs_size; i < rhs_size; ++i) {
                if (rhs_ptr[i] != 0) {
                    return false;
                }
            }
        }
        return true;
    }

    template<typename T, typename std::enable_if_t<
        !std::is_integral_v<T> &&
        !std::is_enum_v<T> &&
        !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, BASE>, int> = 0>
    bool operator!=(const T& rhs) const
    {
        return !(*this == rhs);
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    bool operator==(T rhs) const
    {
        return *this == BASE(rhs);
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    bool operator!=(T rhs) const
    {
        return !(*this == rhs);
    }

    bool operator<(const BASE& rhs) const
    {
        constexpr size_t size = bitops_value_size<BASE>::value;
        constexpr size_t step = sizeof(uint64_t);

        const uint8_t* a = raw_bytes(static_cast<const BASE&>(*this));
        const uint8_t* b = raw_bytes(rhs);

        for (size_t i = size; i >= step; i -= step) {
            uint64_t av, bv;
            std::memcpy(&av, a + i - step, step);
            std::memcpy(&bv, b + i - step, step);

            if (av < bv) return true;
            if (av > bv) return false;
        }

        size_t rem = size % step;
        for (size_t i = rem; i-- > 0;) {
            uint8_t av = a[i];
            uint8_t bv = b[i];

            if (av < bv) return true;
            if (av > bv) return false;
        }

        return false;
    }

    bool operator>(const BASE& rhs) const
    {
        return rhs < static_cast<const BASE&>(*this);
    }

    bool operator<=(const BASE& rhs) const
    {
        return !(rhs < static_cast<const BASE&>(*this));
    }

    bool operator>=(const BASE& rhs) const
    {
        return !(*this < rhs);
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    bool operator<(T rhs) const
    {
        return *this < BASE(rhs);
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    bool operator<=(T rhs) const
    {
        return !(*this > BASE(rhs));
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    bool operator>(T rhs) const
    {
        return BASE(rhs) < static_cast<const BASE&>(*this);
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    bool operator>=(T rhs) const
    {
        return !(*this < BASE(rhs));
    }

    uint64_t to_ullong() const
    {
        uint64_t value = 0;
        std::memcpy(&value, raw_bytes(static_cast<const BASE&>(*this)), std::min(bitops_value_size<BASE>::value, sizeof(uint64_t)));
        return value;
    }

    std::string to_hex(bool trim_leading_zeros = false) const
    {
        constexpr size_t size = bitops_value_size<BASE>::value;
        static const char* digits = "0123456789abcdef";
        const uint8_t* ptr = raw_bytes(static_cast<const BASE&>(*this));

        std::string result;
        result.reserve(size * 2);

        bool started = !trim_leading_zeros;
        for (size_t i = size; i-- > 0;) {
            uint8_t byte = ptr[i];

            if (!started) {
                if (byte == 0) {
                    continue;
                }
                started = true;
            }

            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0F]);
        }

        if (!started) {
            return "0";
        }

        return result;
    }
};
