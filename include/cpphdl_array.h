#pragma once

#include <string.h>
#include "cpphdl_logic.h"

namespace cpphdl
{


template<typename T, size_t COUNT>
struct array : public bitops<logic<COUNT*sizeof(T)*8>>
{
    constexpr static size_t SIZE = COUNT*sizeof(T);
    T data[COUNT];

    array() {}

    array(const array<T,COUNT>& other) = default;

    template<typename OTHER>
    array(const OTHER& other)
    {
        memcpy(data, &other, std::min(SIZE, sizeof(other)));
        if (SIZE > sizeof(other)) {
            memset((uint8_t*)data + sizeof(other), 0, SIZE - sizeof(other));
        }
    }

    array(uint64_t other)
    {
        memcpy(data, &other, std::min(SIZE,sizeof(other)));
        if (SIZE > sizeof(other)) {
            memset((uint8_t*)data + sizeof(other), 0, SIZE - sizeof(other));
        }
    }

    array& operator=(const array<T,COUNT>& other) = default;

    template<typename OTHER>
    array& operator=(const OTHER& other)
    {
        memcpy(data, &other, std::min(SIZE,sizeof(other)));
        if (SIZE > sizeof(other)) {
            memset((uint8_t*)data + sizeof(other), 0, SIZE - sizeof(other));
        }
        return *this;
    }

    array& operator=(uint64_t other)
    {
        memcpy(data, &other, std::min(SIZE,sizeof(other)));
        if (SIZE > sizeof(other)) {
            memset((uint8_t*)data + sizeof(other), 0, SIZE - sizeof(other));
        }
        return *this;
    }

    T& operator[](std::size_t i) { return data[i]; }
    const T& operator[](std::size_t i) const { return data[i]; }

    operator logic<SIZE*8>&()
    {
        return *(logic<SIZE*8>*)this;
    }

    using bitops<logic<COUNT*sizeof(T)*8>>::operator&;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator|;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator^;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator~;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator<<;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator>>;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator+;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator-;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator==;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator!=;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator<;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator<=;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator>;
    using bitops<logic<COUNT*sizeof(T)*8>>::operator>=;
    using bitops<logic<COUNT*sizeof(T)*8>>::to_ullong;
    using bitops<logic<COUNT*sizeof(T)*8>>::to_hex;

    array& operator<<=(size_t shift)
    {
        return *this = *this << shift;
    }

    array& operator>>=(size_t shift)
    {
        return *this = *this >> shift;
    }

    array& operator|=(const array& other)
    {
        return *this = *this | other;
    }

    array& operator&=(const array& other)
    {
        return *this = *this & other;
    }

    array& operator^=(const array& other)
    {
        return *this = *this ^ other;
    }

    explicit operator uint64_t() const
    {
        return to_ullong();
    }

    explicit operator bool() const
    {
        return to_ullong();
    }

    explicit operator uint32_t() const
    {
        return to_ullong();
    }

    explicit operator uint16_t() const
    {
        return to_ullong();
    }

    explicit operator uint8_t() const
    {
        return to_ullong();
    }

    std::string to_string()
    {
        std::string str;
        for (int i=COUNT-1; i >= 0; --i) {
            char buf[10] = {};
            snprintf(buf, 10, "%.02lx", (uint64_t)data[i]);
            str += buf;
        }
        return str;
    }
};

template<size_t COUNT>
struct array<void,COUNT> {};


}
