#pragma once

#include <string.h>
#include <vector>

#include "cpphdl_logic.h"

namespace cpphdl
{

template<typename T, size_t SIZE>
struct memory_row: public array<T,SIZE>
{
//    array<T,SIZE> data = 0;
    std::vector<memory_row<T,SIZE>>* changes = 0;
    size_t pos = 0;

    template<size_t SIZE1>
    memory_row& operator=(const array<T,SIZE1>& other)
    {
        array<T,SIZE>::operator=(other);
        changes->push_back(*this);
        return *this;
    }

    template<size_t SIZE1>
    memory_row& operator=(const logic<SIZE1>& other)
    {
        array<T,SIZE>::operator=(other);
        changes->push_back(*this);
        return *this;
    }

    memory_row& operator=(uint64_t other)
    {
        array<T,SIZE>::operator=(other);
        changes->push_back(*this);
        return *this;
    }

//    operator array<T,SIZE>() const
//    {
//        return data;
//    }

//    operator logic<SIZE*sizeof(T)*8>() const
//    {
//        return *this;
//    }

    logic<SIZE*sizeof(T)*8> operator<<(uint64_t shift)
    {
        return (logic<SIZE*sizeof(T)*8>)*this << shift;
    }

    logic<SIZE*sizeof(T)*8> operator>>(uint64_t shift)
    {
        return (logic<SIZE*sizeof(T)*8>)*this >> shift;
    }

    logic<SIZE*sizeof(T)*8> operator|(const logic<SIZE*sizeof(T)*8>& other)
    {
        return (logic<SIZE*sizeof(T)*8>)*this | other;
    }

    logic<SIZE*sizeof(T)*8> operator&(const logic<SIZE*sizeof(T)*8>& other)
    {
        return (logic<SIZE*sizeof(T)*8>)*this & other;
    }

    logic<SIZE*sizeof(T)*8> operator|=(const logic<SIZE*sizeof(T)*8>& other)
    {
        *(logic<SIZE*sizeof(T)*8>*)*this |= other;
        return *(logic<SIZE*sizeof(T)*8>*)this;
    }

    logic<SIZE*sizeof(T)*8> operator&=(const logic<SIZE*sizeof(T)*8>& other)
    {
        *(logic<SIZE*sizeof(T)*8>*)*this &= other;
        return *(logic<SIZE*sizeof(T)*8>*)this;
    }
};

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
        cpphdl_assert(i < DEPTH, "index " + std::to_string(i) + "is out of size " + std::to_string(DEPTH));
        return memory_row<T,SIZE>{data[i], &changes, i};
    }

    void apply()
    {
        for (auto entry : changes) {
//std::print("!!! {} !!!\n", entry.data);
//            memcpy(&data[entry.pos], &entry.data, sizeof(entry.data));
            data[entry.pos] = entry.data;
        }
        changes.clear();
    }


};

template<size_t SIZE,size_t DEPTH>
struct memory<void,SIZE,DEPTH> {};

}
