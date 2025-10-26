#pragma once

#include <string.h>

#include "cpphdl_logic.h"

namespace cpphdl
{

template<typename T, size_t SIZE>
struct memory_row
{
    array<T,SIZE> data = 0;
    std::vector<memory_row<T,SIZE>>* changes = 0;
    size_t pos = 0;

    template<size_t SIZE1>
    memory_row& operator=(const array<T,SIZE1>& other)
    {
        data = other;
        changes->push_back(*this);
        return *this;
    }

    template<size_t SIZE1>
    memory_row& operator=(const logic<SIZE1>& other)
    {
        data = other;
        changes->push_back(*this);
        return *this;
    }

    operator array<T,SIZE>&()
    {
        return data;
    }

    operator logic<SIZE*sizeof(T)*8>()
    {
        return *(logic<SIZE*sizeof(T)*8>*)&data;
    }

/*    logic<N*sizeof(T)*8> bits(size_t last, size_t first)
    {
        cpphdl_assert(first < N*sizeof(T)*8 && last < N*sizeof(T)*8 && first <= last, "wrong first or last bitnumber");
        return logic<N*sizeof(T)*8>((logic<N*sizeof(T)*8>*)this, first, last);
    }

    operator bitset<N*sizeof(T)> ()
    {
        bitset<N*sizeof(T)> bs;
        memcpy(&bs.data, this->data, sizeof(this->data));
        return bs;
    }

    operator uint64_t()
    {
        return *(uint64_t*)data;
    }

    memory<T,N> operator<<(uint64_t shift)
    {
        uint64_t tmp = 0;
        memory<T,N> arr_tmp = {0};

        for (int i=N-1; i >= 0; --i) {
            tmp = (*this)[i];
            if (i+shift/(sizeof(T)*8) < N) {
                arr_tmp[i+shift/(sizeof(T)*8)] = arr_tmp[i+shift/(sizeof(T)*8)] | tmp<<(shift%(sizeof(T)*8));
            }
            if (i+shift/(sizeof(T)*8)+1 < N) {
                arr_tmp[i+shift/(sizeof(T)*8)+1] = arr_tmp[i+shift/(sizeof(T)*8)+1] | tmp>>((sizeof(T)*8)-shift%(sizeof(T)*8));
            }
        }
        return arr_tmp;
    }

    memory<T,N> operator>>(uint64_t shift)
    {
        uint64_t tmp = 0;
        memory<T,N> arr_tmp = 0;

        for (int i=0; i < (int)N; ++i) {
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

    memory<T,N> operator|(const memory<T,N>& other)
    {
        memory<T,N> arr_tmp = *this;
        for (size_t i=0; i < N; ++i) {
            arr_tmp[i] = arr_tmp[i] | other[i];
        }
        return arr_tmp;
    }
    memory<T,N> operator|=(const memory<T,N>& other)
    {
        *this = *this | other;
        return *this;
    }
*/};

template<typename T, size_t SIZE, size_t DEPTH>
struct memory
{
    array<T,SIZE>* data;
    std::vector<memory_row<T,SIZE>> changes;

    memory()
    {
        data = new array<T,SIZE>[DEPTH];
    }

    ~memory()
    {
        delete[] data;
    }

//    memory(const memory<T,N>& other) = default;

    memory_row<T,SIZE> operator[](std::size_t i)
    {
        return memory_row<T,SIZE>{data[i], &changes, i};
    }

    void apply()
    {
        for (auto entry : changes) {
            memcpy(&data[entry.pos], &entry.data, sizeof(entry.data));
        }
        changes.clear();
    }


};

}
