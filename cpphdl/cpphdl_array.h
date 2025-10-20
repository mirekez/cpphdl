#pragma once

#include <string.h>
#include "cpphdl_logic.h"

namespace cpphdl
{

template<typename T, size_t SIZE>
struct array
{
    T data[SIZE];

    array() {}

    array(const array<T,SIZE>& other) = default;
//    {
//        *this = other;
//    }

    template<size_t SIZE1>
    array(const array<T,SIZE1>& other)
    {
        memcpy(data, other.data, std::min(sizeof(data),sizeof(other.data)));
        memset((uint8_t*)data + std::min(sizeof(data),sizeof(other.data)), 0, sizeof(data) - std::min(sizeof(data),sizeof(other.data)));
    }

    array(uint64_t other)
    {
        memcpy(data, &other, std::min(sizeof(data),sizeof(other)));
        memset((uint8_t*)data + std::min(sizeof(data),sizeof(other)), 0, sizeof(data) - std::min(sizeof(data),sizeof(other)));
    }

    array& operator=(const array<T,SIZE>& other)
    {
        memcpy(data, other.data, std::min(sizeof(data),sizeof(other.data)));
        memset((uint8_t*)data + std::min(sizeof(data),sizeof(other.data)), 0, sizeof(data) - std::min(sizeof(data),sizeof(other.data)));
        return *this;
    }

    template<size_t SIZE1>
    array& operator=(uint64_t other)
    {
        memcpy(data, &other, std::min(sizeof(data),sizeof(other)));
        memset((uint8_t*)data + std::min(sizeof(data),sizeof(other)), 0, sizeof(data) - std::min(sizeof(data),sizeof(other)));
    }

    T& operator[](std::size_t i) { return data[i]; }
    const T& operator[](std::size_t i) const { return data[i]; }

/*    template<size_t SIZE1>
    array& operator=(const array<T,SIZE1>& other)
    {
        constexpr size_t min_size = (SIZE < SIZE1) ? SIZE : SIZE1;
        memcpy(this->data, other.data, sizeof(data[0])*min_size);

        if constexpr (SIZE1 < SIZE) {
            memset(&this->data[SIZE1], 0, sizeof(data[0])*(SIZE-SIZE1));
        }
        return *this;
    }*/

    template<typename OTHER>
    array& operator=(const OTHER& other)
    {
        memcpy(data, &other, std::min(sizeof(data),sizeof(other)));
        memset((uint8_t*)data + std::min(sizeof(data),sizeof(other)), 0, sizeof(data) - std::min(sizeof(data),sizeof(other)));
        return *this;
    }

/*    logic<SIZE*sizeof(T)*8> bits(size_t last, size_t first)
    {
        cpphdl_assert(first < SIZE*sizeof(T)*8 && last < SIZE*sizeof(T)*8 && first <= last, "wrong first or last bitnumber");
        return logic<SIZE*sizeof(T)*8>((logic<SIZE*sizeof(T)*8>*)this, first, last);
    }*/

    operator logic<SIZE*sizeof(T)> ()
    {
        logic<SIZE*sizeof(T)> bs;
        memcpy(&bs.data, this->data, sizeof(this->data));
        return bs;
    }

    operator uint64_t()
    {
        return *(uint64_t*)data;
    }

    array<T,SIZE> operator<<(uint64_t shift)
    {
        uint64_t tmp = 0;
        array<T,SIZE> arr_tmp = {0};

        for (int i=SIZE-1; i >= 0; --i) {
            tmp = (*this)[i];
            if (i+shift/(sizeof(T)*8) < SIZE) {
                arr_tmp[i+shift/(sizeof(T)*8)] = arr_tmp[i+shift/(sizeof(T)*8)] | tmp<<(shift%(sizeof(T)*8));
            }
            if (i+shift/(sizeof(T)*8)+1 < SIZE) {
                arr_tmp[i+shift/(sizeof(T)*8)+1] = arr_tmp[i+shift/(sizeof(T)*8)+1] | tmp>>((sizeof(T)*8)-shift%(sizeof(T)*8));
            }
        }
        return arr_tmp;
    }

    array<T,SIZE> operator>>(uint64_t shift)
    {
        uint64_t tmp = 0;
        array<T,SIZE> arr_tmp = 0;

        for (int i=0; i < (int)SIZE; ++i) {
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

    array<T,SIZE> operator|(const array<T,SIZE>& other)
    {
        array<T,SIZE> arr_tmp = *this;
        for (size_t i=0; i < SIZE; ++i) {
            arr_tmp[i] = arr_tmp[i] | other[i];
        }
        return arr_tmp;
    }
    array<T,SIZE> operator|=(const array<T,SIZE>& other)
    {
        *this = *this | other;
        return *this;
    }

    std::string to_string()
    {
        std::string str;
        for (int i=SIZE-1; i >= 0; --i) {
            char buf[10] = {};
            snprintf(buf, 10, "%.02lx", (uint64_t)data[i]);
            str += buf;
        }
        return str;
    }
};

}
