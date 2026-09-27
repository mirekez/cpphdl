#pragma once

#include "cpphdl_bitops.h"
#include <cstring>
#include <type_traits>
#include <initializer_list>
#include <utility>

namespace cpphdl
{


template<size_t S, typename T, bool PACKED = false>
struct array;

template<size_t WIDTH>
struct logic;

template<size_t WIDTH>
struct logic_bits;

template<typename T>
struct is_logic_bits : std::false_type {};

template<size_t WIDTH>
struct is_logic_bits<logic_bits<WIDTH>> : std::true_type {};

template<typename T>
inline constexpr bool is_logic_bits_v = is_logic_bits<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template<typename T>
struct is_logic : std::false_type {};

template<size_t WIDTH>
struct is_logic<logic<WIDTH>> : std::true_type {};

template<typename T>
inline constexpr bool is_logic_v = is_logic<std::remove_cv_t<std::remove_reference_t<T>>>::value;

namespace detail
{

// Select the available conversions once per width. Per-use SFINAE on seven
// conversion operators made Clang retain hundreds of megabytes of temporary
// substitution ASTs for a small repeated word-level circuit. Keep the operators
// templated: non-template narrow conversions change built-in overload resolution.
template<size_t Width, typename Derived>
struct logic_conversions {};

template<typename Derived>
struct logic_conversions<8, Derived> {
    template<typename = void>
    constexpr operator unsigned char() const {
        return static_cast<unsigned char>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
    template<typename = void>
    constexpr operator signed char() const {
        return static_cast<signed char>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
};

template<typename Derived>
struct logic_conversions<16, Derived> {
    template<typename = void>
    constexpr operator unsigned short() const {
        return static_cast<unsigned short>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
    template<typename = void>
    constexpr operator signed short() const {
        return static_cast<signed short>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
};

template<typename Derived>
struct logic_conversions<32, Derived> {
    template<typename = void>
    constexpr operator unsigned int() const {
        return static_cast<unsigned int>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
    template<typename = void>
    constexpr operator signed int() const {
        return static_cast<signed int>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
};

template<typename Derived>
struct logic_conversions<64, Derived> {
    template<typename = void>
    constexpr operator signed long() const {
        return static_cast<signed long>(static_cast<const Derived*>(this)->to_uint64_constexpr());
    }
};

template<typename T, typename = void>
struct has_pack_method : std::false_type {};

template<typename T>
struct has_pack_method<T, std::void_t<decltype(std::declval<const T&>().pack())>> : std::true_type {};

template<typename T, typename = void>
struct can_static_cast_uint64 : std::false_type {};

template<typename T>
struct can_static_cast_uint64<T, std::void_t<decltype(static_cast<uint64_t>(std::declval<const T&>()))>> : std::true_type {};

template<typename T, typename V, typename = void>
struct can_assign_from : std::false_type {};

template<typename T, typename V>
struct can_assign_from<T, V, std::void_t<decltype(std::declval<T&>() = std::declval<V>())>> : std::true_type {};

// CppHDL headers retain C++17 compatibility, where the standard query is unavailable.
// GCC and Clang expose the same constant-evaluation predicate as a builtin in that mode.
// Conservatively select the constexpr path on compilers that provide neither facility.
constexpr bool is_constant_evaluated_compat() noexcept
{
#if defined(__cpp_lib_is_constant_evaluated)
    return std::is_constant_evaluated();
#elif defined(__has_builtin)
#if __has_builtin(__builtin_is_constant_evaluated)
    return __builtin_is_constant_evaluated();
#else
    return true;
#endif
#elif defined(__GNUC__)
    return __builtin_is_constant_evaluated();
#else
    return true;
#endif
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// Writeback tails contain at most eight bytes. Fixed-size copies become native
// loads/stores without requiring alignment or reading past the packed object.
__attribute__((always_inline)) inline uint64_t load_writeback_word(const uint8_t* source, size_t bytes)
{
    uint64_t result = 0;
    if (bytes == 8) {
        std::memcpy(&result, source, 8);
        return result;
    }
    size_t shift = 0;
    if (bytes >= 4) {
        uint32_t word;
        std::memcpy(&word, source, 4);
        result = word;
        source += 4;
        bytes -= 4;
        shift = 32;
    }
    if (bytes >= 2) {
        uint16_t word;
        std::memcpy(&word, source, 2);
        result |= uint64_t(word) << shift;
        source += 2;
        bytes -= 2;
        shift += 16;
    }
    if (bytes) result |= uint64_t(*source) << shift;
    return result;
}

__attribute__((always_inline)) inline void store_writeback_word(uint8_t* destination, uint64_t value, size_t bytes)
{
    if (bytes == 8) {
        std::memcpy(destination, &value, 8);
        return;
    }
    if (bytes >= 4) {
        const uint32_t word = static_cast<uint32_t>(value);
        std::memcpy(destination, &word, 4);
        destination += 4;
        bytes -= 4;
        value >>= 32;
    }
    if (bytes >= 2) {
        const uint16_t word = static_cast<uint16_t>(value);
        std::memcpy(destination, &word, 2);
        destination += 2;
        bytes -= 2;
        value >>= 16;
    }
    if (bytes) *destination = static_cast<uint8_t>(value);
}
#endif

}

template<size_t WIDTH>
struct logic : public bitops<logic<WIDTH>>, public detail::logic_conversions<WIDTH, logic<WIDTH>>
{
    constexpr static size_t SIZE = (WIDTH+7)/8;
    uint8_t bytes[SIZE];

    constexpr static size_t _size_bits()
    {
        return WIDTH;
    }

    constexpr logic() = default;
    constexpr logic(const logic& other) = default;

    // Runtime scalar creation and width conversion dominate generated RTL glue.
    // Fixed-size copies let the compiler use native loads/stores while these byte
    // loops remain available for C++17 constant-expression evaluation.
    // Generated narrow values call this hundreds of times per combinational pass.
    // Keep the constexpr fallback in the header, but force the runtime scalar load
    // into its caller so a one-byte assignment cannot become an out-of-line call.
    __attribute__((always_inline)) constexpr void assign_uint64(uint64_t value)
    {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if (!detail::is_constant_evaluated_compat()) {
            constexpr size_t copied = SIZE < sizeof(value) ? SIZE : sizeof(value);
            std::memcpy(bytes, &value, copied);
            if constexpr (SIZE > sizeof(value)) {
                std::memset(bytes + sizeof(value), 0, SIZE - sizeof(value));
            }
            if constexpr ((WIDTH % 8) != 0) {
                bytes[SIZE - 1] &=
                    static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
            }
            return;
        }
#endif
        for (size_t i = 0; i < SIZE; ++i) {
            bytes[i] = i < sizeof(value)
                ? static_cast<uint8_t>((value >> (8 * i)) & 0xffu)
                : 0;
        }
        if constexpr ((WIDTH % 8) != 0) {
            bytes[SIZE - 1] &=
                static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
        }
    }

    template<size_t WIDTH1>
    constexpr logic(const logic<WIDTH1>& other);

    template<size_t WIDTH1>
    constexpr logic(const logic_bits<WIDTH1>& other);

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    __attribute__((always_inline)) constexpr logic(T other) : bytes{}
    {
        assign_uint64(static_cast<uint64_t>(other));
    }

    // A packed value such as cat can be a valid SystemVerilog constant expression.
    // Mark the generic pack-aware constructor constexpr so that its constexpr pack()
    // result can initialize logic without falling back to a runtime-only conversion.
    template<typename T, typename std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> && !is_logic_v<T> && !is_logic_bits_v<T>, int> = 0>
    constexpr logic(const T& other) : bytes{}
    {
        if constexpr (detail::has_pack_method<T>::value) {
            *this = other.pack();
        }
        else {
            bitops<logic<WIDTH>>::operator=(other);
        }
    }

    template<typename T>
    constexpr logic(std::initializer_list<T> values) : bytes{}
    {
        size_t dst = WIDTH;
        for (const auto& value : values) {
            uint64_t bits = static_cast<uint64_t>(value);
            size_t srcWidth = 1;
            if constexpr (!std::is_integral_v<T> && !std::is_enum_v<T>) {
                srcWidth = value.size();
            }
            for (size_t i = 0; i < srcWidth && dst > 0; ++i) {
                --dst;
                set(dst, static_cast<uint8_t>((bits >> (srcWidth - 1 - i)) & 1u));
            }
        }
    }

    constexpr logic& operator=(const logic& other) = default;

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    __attribute__((always_inline)) constexpr logic& operator=(T other)
    {
        assign_uint64(static_cast<uint64_t>(other));
        return *this;
    }

    template<typename T, typename std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> && !is_logic_v<T> && !is_logic_bits_v<T>, int> = 0>
    logic& operator=(const T& other)
    {
        if constexpr (detail::has_pack_method<T>::value) {
            *this = other.pack();
        }
        else {
            bitops<logic<WIDTH>>::operator=(other);
        }
        return *this;
    }

    template<typename T>
    constexpr logic& operator=(std::initializer_list<T> values)
    {
        *this = logic(values);
        return *this;
    }

    template<size_t WIDTH1>
    constexpr logic& operator=(const logic<WIDTH1>& other);

    template<size_t WIDTH1>
    constexpr logic& operator=(const logic_bits<WIDTH1>& other);

    logic_bits<WIDTH> bits(size_t last, size_t first);
    constexpr logic<WIDTH> bits(size_t last, size_t first) const;
    template<size_t LAST, size_t FIRST>
    constexpr logic<LAST - FIRST + 1> slice() const;
    logic_bits<WIDTH> operator[](size_t bitnum);
    constexpr logic<1> operator[](size_t bitnum) const;

    constexpr uint8_t get(size_t bitnum) const
    {
        return (bytes[bitnum/8]>>(bitnum%8))&1;
    }

    constexpr void set(size_t bitnum, uint8_t in)
    {
        in &= 1;
        bytes[bitnum/8] = (bytes[bitnum/8]&~(1<<(bitnum%8)))|(in<<(bitnum%8));
    }

    using bitops<logic<WIDTH>>::operator&;
    using bitops<logic<WIDTH>>::operator|;
    using bitops<logic<WIDTH>>::operator^;
    using bitops<logic<WIDTH>>::operator<<;
    using bitops<logic<WIDTH>>::operator>>;
    using bitops<logic<WIDTH>>::operator+;
    using bitops<logic<WIDTH>>::operator-;
    using bitops<logic<WIDTH>>::operator==;
    using bitops<logic<WIDTH>>::operator!=;
    using bitops<logic<WIDTH>>::operator<;
    using bitops<logic<WIDTH>>::operator<=;
    using bitops<logic<WIDTH>>::operator>;
    using bitops<logic<WIDTH>>::operator>=;
    using bitops<logic<WIDTH>>::to_ullong;
    using bitops<logic<WIDTH>>::to_hex;

    logic& operator<<=(uint64_t shift)
    {
        return *this = *this << shift;
    }

    logic& operator>>=(uint64_t shift)
    {
        return *this = *this >> shift;
    }

    logic& operator++()
    {
        *this = logic(static_cast<uint64_t>(*this) + 1);
        return *this;
    }

    logic operator++(int)
    {
        logic tmp = *this;
        ++(*this);
        return tmp;
    }

    logic& operator--()
    {
        *this = logic(static_cast<uint64_t>(*this) - 1);
        return *this;
    }

    logic operator--(int)
    {
        logic tmp = *this;
        --(*this);
        return tmp;
    }

    template<size_t WIDTH1>
    logic& operator&=(const logic<WIDTH1>& in)
    {
        return *this = *this & in;
    }

    template<size_t WIDTH1>
    logic& operator|=(const logic<WIDTH1>& in)
    {
        return *this = *this | in;
    }

    template<size_t WIDTH1>
    logic& operator^=(const logic<WIDTH1>& in)
    {
        return *this = *this ^ in;
    }

    // Package constants require logic bitwise expressions to remain constant-evaluable.
    // Keep the byte-oriented bitops implementation for runtime performance, and use the
    // declared-width bit loop only while the compiler evaluates a constant expression.
    template<size_t WIDTH1>
    constexpr logic operator&(const logic<WIDTH1>& rhs) const
    {
        if (!detail::is_constant_evaluated_compat()) {
            return bitops<logic<WIDTH>>::operator&(rhs);
        }
        logic result{};
        for (size_t i = 0; i < WIDTH; ++i) {
            result.set(i, get(i) && (i < WIDTH1 ? rhs.get(i) : 0));
        }
        return result;
    }

    template<size_t WIDTH1>
    constexpr logic operator|(const logic<WIDTH1>& rhs) const
    {
        if (!detail::is_constant_evaluated_compat()) {
            return bitops<logic<WIDTH>>::operator|(rhs);
        }
        logic result{};
        for (size_t i = 0; i < WIDTH; ++i) {
            result.set(i, get(i) || (i < WIDTH1 ? rhs.get(i) : 0));
        }
        return result;
    }

    template<size_t WIDTH1>
    constexpr logic operator^(const logic<WIDTH1>& rhs) const
    {
        if (!detail::is_constant_evaluated_compat()) {
            return bitops<logic<WIDTH>>::operator^(rhs);
        }
        logic result{};
        for (size_t i = 0; i < WIDTH; ++i) {
            result.set(i, get(i) != (i < WIDTH1 ? rhs.get(i) : 0));
        }
        return result;
    }

    // The inherited bitops complement was not constexpr for logic constants.
    // Per-bit get/set made runtime complement linear in bits instead of storage bytes.
    // Invert complete bytes, then clear padding above the declared SystemVerilog width.
    constexpr logic operator~() const
    {
        logic result{};
        for (size_t i = 0; i < SIZE; ++i) {
            result.bytes[i] = static_cast<uint8_t>(~bytes[i]);
        }
        if constexpr ((WIDTH % 8) != 0) {
            result.bytes[SIZE - 1] &=
                static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
        }
        return result;
    }

    constexpr uint64_t to_uint64_constexpr() const
    {
        // Runtime scalar conversion is pervasive in generated comparisons and shifts.
        // One fixed-size copy lets the compiler emit a single unaligned load on little-endian
        // targets; retain the byte loop for portable constant-expression evaluation.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if (!detail::is_constant_evaluated_compat()) {
            uint64_t value = 0;
            std::memcpy(&value, bytes,
                        SIZE < sizeof(uint64_t) ? SIZE : sizeof(uint64_t));
            return value;
        }
#endif
        uint64_t value = 0;
        constexpr size_t limit = SIZE < sizeof(uint64_t) ? SIZE : sizeof(uint64_t);
        for (size_t i = 0; i < limit; ++i) {
            value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
        }
        return value;
    }

    constexpr operator uint64_t() const
    {
        return to_uint64_constexpr();
    }

    explicit constexpr operator bool() const
    {
        return to_uint64_constexpr() != 0;
    }

    explicit constexpr operator uint32_t() const
    {
        return static_cast<uint32_t>(to_uint64_constexpr());
    }

    explicit constexpr operator uint16_t() const
    {
        return static_cast<uint16_t>(to_uint64_constexpr());
    }

    explicit constexpr operator uint8_t() const
    {
        return static_cast<uint8_t>(to_uint64_constexpr());
    }

    std::string to_string() const
    {
        std::string hex;
        for (size_t i = 0; i < size(); ++i) {
            hex = std::string(this->get(i)?"1":"0") + hex;
        }
        return hex;
    }

    constexpr size_t size() const
    {
        return WIDTH;
    }
};

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>::logic(const logic<WIDTH1>& other)
{
    *this = other;
}

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>::logic(const logic_bits<WIDTH1>& other)
{
    *this = other;
}

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>& logic<WIDTH>::operator=(const logic<WIDTH1>& other)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!detail::is_constant_evaluated_compat()) {
        constexpr size_t SRC_SIZE = logic<WIDTH1>::SIZE;
        constexpr size_t copied = SIZE < SRC_SIZE ? SIZE : SRC_SIZE;
        std::memcpy(bytes, other.bytes, copied);
        if constexpr (SIZE > SRC_SIZE) {
            std::memset(bytes + SRC_SIZE, 0, SIZE - SRC_SIZE);
        }
        if constexpr ((WIDTH % 8) != 0) {
            bytes[SIZE - 1] &=
                static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
        }
        return *this;
    }
#endif
    constexpr size_t SRC_SIZE = logic<WIDTH1>::SIZE;
    for (size_t i = 0; i < SIZE; ++i) {
        bytes[i] = i < SRC_SIZE ? other.bytes[i] : 0;
    }
    if constexpr ((WIDTH % 8) != 0) {
        bytes[SIZE - 1] &= static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
    }
    return *this;
}

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>& logic<WIDTH>::operator=(const logic_bits<WIDTH1>& other)
{
    constexpr size_t SRC_SIZE = logic<WIDTH1>::SIZE;
    const auto& src = static_cast<const logic<WIDTH1>&>(other);
    for (size_t i = 0; i < SIZE; ++i) {
        bytes[i] = i < SRC_SIZE ? src.bytes[i] : 0;
    }
    if constexpr ((WIDTH % 8) != 0) {
        bytes[SIZE - 1] &= static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
    }
    return *this;
}

template<size_t WIDTH>
template<size_t LAST, size_t FIRST>
constexpr logic<LAST - FIRST + 1> logic<WIDTH>::slice() const
{
    static_assert(FIRST <= LAST, "slice first bit must not exceed last bit");
    static_assert(LAST < WIDTH, "slice exceeds source width");
    constexpr size_t RESULT_WIDTH = LAST - FIRST + 1;
    constexpr size_t SOURCE_BYTE = FIRST / 8;
    constexpr size_t SHIFT = FIRST % 8;
    logic<RESULT_WIDTH> result{};

    for (size_t dst = 0; dst < logic<RESULT_WIDTH>::SIZE; ++dst) {
        const size_t src = SOURCE_BYTE + dst;
        uint16_t pair = src < SIZE ? bytes[src] : 0;
        if constexpr (SHIFT != 0) {
            if (src + 1 < SIZE)
                pair |= static_cast<uint16_t>(bytes[src + 1]) << 8;
        }
        result.bytes[dst] = static_cast<uint8_t>(pair >> SHIFT);
    }
    if constexpr ((RESULT_WIDTH % 8) != 0)
        result.bytes[logic<RESULT_WIDTH>::SIZE - 1] &=
            static_cast<uint8_t>((1u << (RESULT_WIDTH % 8)) - 1u);
    return result;
}

template<size_t WIDTH>
struct logic_bits : public logic<WIDTH>
{
    logic<WIDTH>* parent;
    size_t first;
    size_t last;

    logic_bits() = delete;
    logic_bits(const logic_bits& other) = delete;

    logic_bits(logic<WIDTH>* parent, size_t first, size_t last) : parent(parent), first(first), last(last)
    {
        const size_t bitCount = last + 1 - first;
        const size_t byteCount = (bitCount + 7) / 8;
        const size_t sourceByte = first / 8;
        const size_t shift = first % 8;
        if (shift == 0) {
            memcpy(logic<WIDTH>::bytes, &parent->bytes[sourceByte], byteCount);
        }
        else {
            for (size_t destination = 0; destination < byteCount; ++destination) {
                const size_t source = sourceByte + destination;
                uint16_t pair = parent->bytes[source];
                if (source + 1 < logic<WIDTH>::SIZE) {
                    pair |= static_cast<uint16_t>(parent->bytes[source + 1]) << 8;
                }
                logic<WIDTH>::bytes[destination] = static_cast<uint8_t>(pair >> shift);
            }
        }
        if ((bitCount % 8) != 0) {
            logic<WIDTH>::bytes[byteCount - 1] &=
                static_cast<uint8_t>((1u << (bitCount % 8)) - 1u);
        }
        memset(&logic<WIDTH>::bytes[byteCount], 0,
               sizeof(logic<WIDTH>::bytes) - byteCount);
    }

    void updateParent() const
    {
        if (parent) {  // if parent==0 then it can be pointer to logic, not logic_bits - we can use only logic
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            // Optimize only writeback: the proxy already owns a separate source
            // snapshot. Changing reads or proxy layout regressed the bus replay.
            // Mask against the live parent, preserving other slices and padding.
            if constexpr (WIDTH > 0 && WIDTH <= 64) {
                uint64_t source = 0;
                uint64_t destination = 0;
                std::memcpy(&source, logic<WIDTH>::bytes, logic<WIDTH>::SIZE);
                std::memcpy(&destination, parent->bytes, logic<WIDTH>::SIZE);
                const size_t count = last - first + 1;
                const uint64_t mask = (~uint64_t{0} >> (64 - count)) << first;
                destination = (destination & ~mask) | ((source << first) & mask);
                std::memcpy(parent->bytes, &destination, logic<WIDTH>::SIZE);
                return;
            }
            size_t destinationFirst = first;
            size_t sourceFirst = 0;
            size_t count = last - first + 1;
            while (count != 0) {
                const size_t destinationShift = destinationFirst % 8;
                const size_t sourceShift = sourceFirst % 8;
                const size_t chunk = count < 64 - destinationShift ? count : 64 - destinationShift;
                const size_t sourceBytes = (sourceShift + chunk + 7) / 8;
                const size_t destinationBytes = (destinationShift + chunk + 7) / 8;
                uint64_t incoming = detail::load_writeback_word(
                    logic<WIDTH>::bytes + sourceFirst / 8, sourceBytes < 8 ? sourceBytes : 8);
                incoming >>= sourceShift;
                if (sourceBytes > 8) {
                    incoming |= uint64_t(logic<WIDTH>::bytes[sourceFirst / 8 + 8]) << (64 - sourceShift);
                }
                const uint64_t mask = (~uint64_t{0} >> (64 - chunk)) << destinationShift;
                uint64_t previous = 0;
                if (mask != ~uint64_t{0}) {
                    previous = detail::load_writeback_word(parent->bytes + destinationFirst / 8, destinationBytes);
                }
                const uint64_t result = (previous & ~mask) | ((incoming << destinationShift) & mask);
                detail::store_writeback_word(parent->bytes + destinationFirst / 8, result, destinationBytes);
                destinationFirst += chunk;
                sourceFirst += chunk;
                count -= chunk;
            }
#else
            size_t destination = first;
            size_t source = 0;
            while (destination <= last && (destination % 8) != 0) {
                parent->set(destination++, this->get(source++));
            }
            while (destination + 7 <= last) {
                const size_t sourceByte = source / 8;
                const size_t shift = source % 8;
                uint16_t pair = logic<WIDTH>::bytes[sourceByte];
                if (shift != 0 && sourceByte + 1 < logic<WIDTH>::SIZE) {
                    pair |= static_cast<uint16_t>(logic<WIDTH>::bytes[sourceByte + 1]) << 8;
                }
                parent->bytes[destination / 8] = static_cast<uint8_t>(pair >> shift);
                destination += 8;
                source += 8;
            }
            while (destination <= last) {
                parent->set(destination++, this->get(source++));
            }
#endif
//            parent->updateParent();
        }
    }

    logic_bits& operator=(const logic_bits<WIDTH>& other)
    {
        *this = (logic<WIDTH>)other;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator=(const logic_bits<WIDTH1>& other)
    {
        *this = (logic<WIDTH1>)other;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator=(const logic<WIDTH1>& other)
    {
        *(logic<WIDTH>*)this = other;
        updateParent();
        return *this;
    }

    logic_bits& operator=(uint64_t other)
    {
        *(logic<WIDTH>*)this = other;
        updateParent();
        return *this;
    }

    template<typename T, typename std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> && !is_logic_v<T> && !is_logic_bits_v<T>, int> = 0>
    logic_bits& operator=(const T& other)
    {
        *(logic<WIDTH>*)this = other;
        updateParent();
        return *this;
    }

    using logic<WIDTH>::operator&;
    using logic<WIDTH>::operator|;
    using logic<WIDTH>::operator^;
    using logic<WIDTH>::operator~;
    using logic<WIDTH>::operator<<;
    using logic<WIDTH>::operator>>;
    using logic<WIDTH>::operator+;
    using logic<WIDTH>::operator-;
    using logic<WIDTH>::operator==;
    using logic<WIDTH>::operator!=;
    using logic<WIDTH>::operator<;
    using logic<WIDTH>::operator<=;
    using logic<WIDTH>::operator>;
    using logic<WIDTH>::operator>=;
    using logic<WIDTH>::to_ullong;
    using logic<WIDTH>::to_hex;

    template<size_t WIDTH1>
    logic_bits& operator&=(const logic<WIDTH1>& in)
    {
        *(logic<WIDTH>*)this &= in;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator|=(const logic<WIDTH1>& in)
    {
        *(logic<WIDTH>*)this |= in;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator^=(const logic<WIDTH1>& in)
    {
        *(logic<WIDTH>*)this ^= in;
        updateParent();
        return *this;
    }

    logic_bits& operator&=(uint64_t in)
    {
        *(logic<WIDTH>*)this &= logic<WIDTH>(in);
        updateParent();
        return *this;
    }

    logic_bits& operator|=(uint64_t in)
    {
        *(logic<WIDTH>*)this |= logic<WIDTH>(in);
        updateParent();
        return *this;
    }

    logic_bits& operator^=(uint64_t in)
    {
        *(logic<WIDTH>*)this ^= logic<WIDTH>(in);
        updateParent();
        return *this;
    }
};

template<size_t WIDTH>
logic_bits<WIDTH> logic<WIDTH>::bits(size_t last, size_t first)
{
    cpphdl_assert(first < WIDTH && last < WIDTH && first <= last, "wrong first or last bitnumber");
    return logic_bits(this, first, last);
}

template<size_t WIDTH>
constexpr logic<WIDTH> logic<WIDTH>::bits(size_t last, size_t first) const
{
    cpphdl_assert(first < WIDTH && last < WIDTH && first <= last, "wrong first or last bitnumber");
    logic<WIDTH> ret = 0;
    size_t dst = 0;
    for (size_t src = first; src <= last; ++src) {
        ret.set(dst++, get(src));
    }
    return ret;
}

template<size_t WIDTH>
logic_bits<WIDTH> logic<WIDTH>::operator[](size_t bitnum)
{
    cpphdl_assert(bitnum < WIDTH, "wrong bitnum");
    return logic_bits<WIDTH>(this, bitnum, bitnum);
}

template<size_t WIDTH>
constexpr logic<1> logic<WIDTH>::operator[](size_t bitnum) const
{
    cpphdl_assert(bitnum < WIDTH, "wrong bitnum");
    return logic<1>(get(bitnum));
}


}
