#pragma once

#include <string.h>
#include <vector>

#include "cpphdl_checkpoint.h"
#include "cpphdl_logic.h"

namespace cpphdl
{


template<typename T, size_t SIZE>
struct memory_row: public array<SIZE, T>
{
//    array<SIZE, T> data = 0;
    std::vector<memory_row<T,SIZE>>* changes = 0;
    size_t pos = 0;

    memory_row(const array<SIZE, T>& value,
               std::vector<memory_row<T,SIZE>>* pending_changes,
               size_t row_pos)
        : array<SIZE, T>(value), changes(pending_changes), pos(row_pos)
    {
    }

    memory_row(const memory_row&) = default;

    // Generated partial writes merge into a memory_row and assign that row back.
    // Without this overload C++ selects the implicit copy assignment and never queues the write.
    // Keep same-type assignment consistent with every other deferred memory assignment overload.
    memory_row& operator=(const memory_row& other)
    {
        array<SIZE, T>::operator=(static_cast<const array<SIZE, T>&>(other));
        changes->push_back(*this);
        return *this;
    }

    template<size_t SIZE1>
    memory_row& operator=(const array<SIZE1, T>& other)
    {
        array<SIZE, T>::operator=(other);
        changes->push_back(*this);
        return *this;
    }

    template<size_t SIZE1>
    memory_row& operator=(const logic<SIZE1>& other)
    {
        array<SIZE, T>::operator=(other);
        changes->push_back(*this);
        return *this;
    }

    memory_row& operator=(uint64_t other)
    {
        array<SIZE, T>::operator=(other);
        changes->push_back(*this);
        return *this;
    }

    using array<SIZE, T>::operator&;
    using array<SIZE, T>::operator|;
    using array<SIZE, T>::operator^;
    using array<SIZE, T>::operator~;
    using array<SIZE, T>::operator<<;
    using array<SIZE, T>::operator>>;
    using array<SIZE, T>::operator+;
    using array<SIZE, T>::operator-;
    using array<SIZE, T>::operator==;
    using array<SIZE, T>::operator!=;
    using array<SIZE, T>::operator<;
    using array<SIZE, T>::operator<=;
    using array<SIZE, T>::operator>;
    using array<SIZE, T>::operator>=;
    using array<SIZE, T>::to_ullong;
    using array<SIZE, T>::to_hex;
    using array<SIZE, T>::to_string;

    memory_row& operator<<=(size_t shift)
    {
        changes->push_back(*this);
        return (memory_row&)(*this = *this << shift);
    }

    memory_row& operator>>=(size_t shift)
    {
        changes->push_back(*this);
        return (memory_row&)(*this = *this >> shift);
    }

    memory_row& operator|=(const memory_row& other)
    {
        changes->push_back(*this);
        return (memory_row&)(*this = *this | other);
    }

    memory_row& operator&=(const memory_row& other)
    {
        changes->push_back(*this);
        return (memory_row&)(*this = *this & other);
    }

    memory_row& operator^=(const memory_row& other)
    {
        changes->push_back(*this);
        return (memory_row&)(*this = *this ^ other);
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
};

template<typename T, size_t SIZE, size_t DEPTH>
struct memory
{
    array<SIZE, T>* data;
    std::vector<memory_row<T,SIZE>> changes;

    memory()
    {
        data = new array<SIZE, T>[DEPTH];
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

    // Deferred part-select writes need the latest queued row as their merge base.
    // Ordinary operator[] reads remain committed-state reads, matching nonblocking RTL.
    // The returned row still schedules its final whole-row value through operator[].
    memory_row<T,SIZE> pending(std::size_t i)
    {
        cpphdl_assert(i < DEPTH, "index " + std::to_string(i) + "is out of size " + std::to_string(DEPTH));
        memory_row<T,SIZE> row{data[i], &changes, i};
        for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
            if (it->pos == i) {
                static_cast<array<SIZE, T>&>(row) = static_cast<const array<SIZE, T>&>(*it);
                break;
            }
        }
        return row;
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

    void apply(FILE* checkpoint_fd)
    {
        if (!checkpoint_fd) {
            apply();
            return;
        }
        if (checkpoint_reading(checkpoint_fd)) {
            checkpoint_read_exact(checkpoint_fd, data, sizeof(array<SIZE, T>[DEPTH]));
            changes.clear();
        }
        else {
            apply();
            checkpoint_write_exact(checkpoint_fd, data, sizeof(array<SIZE, T>[DEPTH]));
        }
    }

    memory& operator=(memory<T,SIZE,DEPTH>& other)
    {
        memcpy(data, other.data, sizeof(array<SIZE, T>[DEPTH]));
        return *this;
    }

};

template<size_t SIZE,size_t DEPTH>
struct memory<void,SIZE,DEPTH> {};


}
