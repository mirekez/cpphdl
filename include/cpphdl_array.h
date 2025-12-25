#pragma once

#include <string.h>
#include "cpphdl_logic.h"

namespace cpphdl
{

template<typename T, size_t COUNT>
struct array
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
//    {
//        memcpy(data, other.data, std::min(SIZE,sizeof(other.data)));
//        if (SIZE > sizeof(other.data)) {
//            memset((uint8_t*)data + sizeof(other.data), 0, SIZE - sizeof(other.data));
//        }
//        return *this;
//    }

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

/*    template<size_t COUNT1>
    array& operator=(const array<T,COUNT1>& other)
    {
        constexpr size_t min_size = (COUNT < COUNT1) ? COUNT : COUNT1;
        memcpy(this->data, other.data, sizeof(data[0])*min_size);

        if constexpr (COUNT1 < COUNT) {
            memset(&this->data[COUNT1], 0, sizeof(data[0])*(COUNT-COUNT1));
        }
        return *this;
    }
    logic<SIZE*8> bits(size_t last, size_t first)
    {
        cpphdl_assert(first < SIZE*8 && last < SIZE*8 && first <= last, "wrong first or last bitnumber");
        return logic<SIZE*8>((logic<SIZE*8>*)this, first, last);
    }*/

    operator logic<SIZE*8>() const
    {
        logic<SIZE*8> bs;
        memcpy(&bs, this->data, sizeof(bs));
        return bs;
    }

//    operator uint64_t() const
//    {
//        return *(uint64_t*)data;
//    }

    array<T,COUNT> operator<<(uint64_t shift)
    {
        uint64_t tmp = 0;
        array<T,COUNT> arr_tmp = {0};

        for (int i=COUNT-1; i >= 0; --i) {
            tmp = (*this)[i];
            if (i+shift/(sizeof(T)*8) < COUNT) {
                arr_tmp[i+shift/(sizeof(T)*8)] = arr_tmp[i+shift/(sizeof(T)*8)] | tmp<<(shift%(sizeof(T)*8));
            }
            if (i+shift/(sizeof(T)*8)+1 < COUNT) {
                arr_tmp[i+shift/(sizeof(T)*8)+1] = arr_tmp[i+shift/(sizeof(T)*8)+1] | tmp>>((sizeof(T)*8)-shift%(sizeof(T)*8));
            }
        }
        return arr_tmp;
    }

    array<T,COUNT> operator>>(uint64_t shift)
    {
        uint64_t tmp = 0;
        array<T,COUNT> arr_tmp = 0;

        for (int i=0; i < (int)COUNT; ++i) {
            tmp = (*this)[i];
            if (i-(int)(shift/(sizeof(T)*8)) >= 0) {
                arr_tmp[i-shift/(sizeof(T)*8)] = arr_tmp[i-shift/(sizeof(T)*8)] | tmp>>(shift%(sizeof(T)*8));
            }
            if (i-(int)(shift/(sizeof(T)*8))-1 >= 0) {
                arr_tmp[i-shift/(sizeof(T)*8)-1] = arr_tmp[i-shift/(sizeof(T)*8)-1] | tmp<<((sizeof(T)*8)-shift%(sizeof(T)*8));
            }
        }
        return arr_tmp;
    }

    array<T,COUNT> operator|(const array<T,COUNT>& other)
    {
        array<T,COUNT> arr_tmp = *this;
        for (size_t i=0; i < COUNT; ++i) {
            arr_tmp[i] = arr_tmp[i] | other[i];
        }
        return arr_tmp;
    }
    array<T,COUNT> operator|=(const array<T,COUNT>& other)
    {
        *this = *this | other;
        return *this;
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
