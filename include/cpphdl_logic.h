#pragma once

#include <cstddef>
#include <cassert>
#include <string>
#include <cstring>

namespace cpphdl
{


template<typename T, size_t S>
struct array;

template<size_t WIDTH>
struct logic_bits;

template<size_t WIDTH>
struct logic
{
    constexpr static size_t SIZE = (WIDTH+7)/8;
    uint8_t bytes[SIZE];

    logic() = default;
    logic(const logic& other) = default;

//    template<size_t WIDTH1>
//    logic(const logic<WIDTH1>& other)
//    {
//        for (size_t i=0; i < std::min(SIZE,other.SIZE); ++i) {
//            bytes[i] = other.bytes[i];
//        }
//        memset(&bytes[std::min(SIZE,other.SIZE)], 0, SIZE - std::min(SIZE,other.SIZE));
//    }

    template<typename T>
    logic(const T& other)
    {
        size_t to_copy = std::min(sizeof(*this),sizeof(other));
        memcpy(&bytes, &other, to_copy);
        if (to_copy < sizeof(*this)) {
            memset((uint8_t*)&bytes + to_copy, 0, sizeof(*this) - to_copy);
        }
    }

    logic(uint64_t other)
    {
        for (size_t i=0; i < SIZE; ++i) {
            bytes[i] = other>>(i*8);
        }
    }

    logic& operator=(const logic& other) = default;

    template<size_t WIDTH1>
    logic& operator=(const logic<WIDTH1>& other)
    {
        for (size_t i=0; i < std::min(SIZE,other.SIZE); ++i) {
            bytes[i] = other.bytes[i];
        }
        memset(&bytes[std::min(SIZE,other.SIZE)], 0, SIZE - std::min(SIZE,other.SIZE));
        return *this;
    }

    logic& operator=(uint64_t in)
    {
        for (size_t i=0; i < SIZE; ++i) {  // optimize with ulong
            bytes[i] = in&0xFF;
            in >>= 8;
        }
        return *this;
    }

    logic_bits<WIDTH> bits(size_t last, size_t first);
    logic_bits<WIDTH> operator[](size_t bitnum);

    uint8_t get(size_t bitnum) const
    {
        return (bytes[bitnum/8]>>(bitnum%8))&1;
    }

    void set(size_t bitnum, uint8_t in)
    {
        in &= 1;
        bytes[bitnum/8] = (bytes[bitnum/8]&~(1<<(bitnum%8)))|(in<<(bitnum%8));
    }


/*    template<size_t WIDTH1>
    logic& operator&=(const logic<WIDTH1>& in)
    {
        for (size_t i=0; i < std::min(SIZE,in.SIZE); ++i) {  // optimize with ulong
            bytes[i] &= in.bytes[i];
        }
        return *this;
    }

    template<size_t WIDTH1>
    logic& operator|=(const logic<WIDTH1>& in)
    {
        for (size_t i=0; i < std::min(SIZE,in.SIZE); ++i) {  // optimize with ulong
            bytes[i] |= in.bytes[i];
        }
        return *this;
    }

    template<size_t WIDTH1>
    logic& operator^=(const logic<WIDTH1>& in)
    {
        for (size_t i=0; i < std::min(SIZE,in.SIZE); ++i) {  // optimize with ulong
            bytes[i] ^= in.bytes[i];
        }
        return *this;
    }
*/

    operator uint64_t() const
    {
        return to_ullong();
    }

    logic<WIDTH> operator<<(uint64_t shift) const
    {
        uint64_t tmp = 0;
        logic<WIDTH> logic_tmp = {0};

        for (int i=WIDTH/8-1; i >= 0; --i) {
            tmp = this->bytes[i];
            if (i + shift/8 < WIDTH/8) {
                logic_tmp.bytes[i+shift/8] = logic_tmp.bytes[i+shift/8] | tmp<<(shift%8);
            }
            if (i+shift/8+1 < WIDTH/8) {
                logic_tmp.bytes[i+shift/8+1] = logic_tmp.bytes[i+shift/8+1] | tmp>>(8-shift%8);
            }
        }
        return logic_tmp;
    }

    logic<WIDTH> operator>>(uint64_t shift) const
    {
        uint64_t tmp = 0;
        logic<WIDTH> logic_tmp = 0;

        for (int i=0; i < (int)WIDTH/8; ++i) {
            tmp = this->bytes[i];
            if (i-(int)(shift/8) >= 0) {
                logic_tmp.bytes[i-shift/8] = logic_tmp.bytes[i-shift/8] | tmp>>(shift%8);
            }
            if (i-(int)(shift/8)-1 >= 0) {
                logic_tmp.bytes[i-shift/8-1] = logic_tmp.bytes[i-shift/8-1] | tmp<<(8-shift%8);
            }
        }
        return logic_tmp;
    }

    logic operator~() const
    {
        logic bs;
        for (size_t i=0; i < logic<WIDTH>::SIZE; ++i) {  // optimize with ulong
            bs.bytes[i] = ~logic<WIDTH>::bytes[i];
        }
        return bs;
    }

    template<size_t WIDTH1>
    logic operator&(const logic<WIDTH1>& in) const
    {
        auto bs = *this;
        for (size_t i=0; i < std::min(logic<WIDTH>::SIZE,in.SIZE); ++i) {  // optimize with ulong
            bs.bytes[i] &= in.bytes[i];
        }
        return bs;
    }

    template<size_t WIDTH1>
    logic operator|(const logic<WIDTH1>& in) const
    {
        auto bs = *this;
        for (size_t i=0; i < std::min(logic<WIDTH>::SIZE,in.SIZE); ++i) {  // optimize with ulong
            bs.bytes[i] |= in.bytes[i];
        }
        return bs;
    }

    template<size_t WIDTH1>
    logic operator^(const logic<WIDTH1>& in) const
    {
        auto bs = *this;
        for (size_t i=0; i < std::min(logic<WIDTH>::SIZE,in.SIZE); ++i) {  // optimize with ulong
            bs.bytes[i] ^= in.bytes[i];
        }
        return bs;
    }

    logic<WIDTH> operator<<=(uint64_t shift)
    {
        *this = *this << shift;
        return *this;
    }

    logic<WIDTH> operator>>=(uint64_t shift)
    {
        *this = *this >> shift;
        return *this;
    }

    template<size_t WIDTH1>
    logic operator&=(const logic<WIDTH1>& in)
    {
        *this = *this & in;
        return *this;
    }

    template<size_t WIDTH1>
    logic operator|=(const logic<WIDTH1>& in)
    {
        *this = *this | in;
        return *this;
    }

    template<size_t WIDTH1>
    logic operator^=(const logic<WIDTH1>& in)
    {
        *this = *this ^ in;
        return *this;
    }

    uint64_t to_ullong() const
    {
        uint64_t ullong = 0;
        size_t bits = WIDTH;
        for (size_t i=0; i < std::min(sizeof(uint64_t),SIZE); ++i) {
            ullong |= (uint64_t)(bytes[i] & ((1U<<(bits>8?8:bits))-1)) << (i*8);
            bits -= 8;
        }
        return ullong;
    }

    std::string to_hex() const
    {
        std::string hex;
        for (size_t i = 0; i < size(); i+=4) {
            int hex_value = (this->get(i) * 1) + (this->get(i+1) * 2) + (this->get(i+2) * 4) + (this->get(i+3) * 8);
            hex = ((char)(hex_value < 10 ? '0'+hex_value : 'a'+hex_value-10)) + hex;
        }
        return hex;
    }

    std::string to_string() const
    {
        std::string hex;
        for (size_t i = 0; i < size(); ++i) {
            hex = std::string(this->get(i)?"1":"0") + hex;
        }
        return hex;
    }

    size_t size() const
    {
        return WIDTH;
    }
};

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
        if ((first%8)==0 && first != last) {
            memcpy(logic<WIDTH>::bytes, &parent->bytes[first/8], (last+1-first+7)/8);
            memset(&logic<WIDTH>::bytes[(last+1-first+7)/8], 0, sizeof(logic<WIDTH>::bytes)-(last+1-first+7)/8);
            for (size_t i=last+1-first; i%8 != 0; ++i) {
                logic<WIDTH>::set(i, 0);
            }
        }
        else {
            memset(logic<WIDTH>::bytes, 0, sizeof(logic<WIDTH>::bytes));
            size_t j = 0;
            for (size_t i=first; i <= last; ++i) {
                logic<WIDTH>::set(j++, parent->get(i));
            }
        }
    }

    void updateParent() const
    {
        if (parent) {  // if parent==0 then it can be pointer to logic, not logic_bits - we can use only logic
            if ((first%8)==0) {
                memcpy(&parent->bytes[first/8], logic<WIDTH>::bytes, (last+1-first)/8);
                size_t j = (last-first)/8*8;
                for (size_t i=last/8*8; i <= last; ++i) {  // rest bits
                    parent->set(i, this->get(j++));
                }
            }
            else {
                size_t j = 0;
                for (size_t i=first; i <= last; ++i) {  // todo? optimize with ulong
                    parent->set(i, this->get(j++));
                }
            }
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
logic_bits<WIDTH> logic<WIDTH>::operator[](size_t bitnum)
{
    cpphdl_assert(bitnum < WIDTH, "wrong bitnum");
    return logic_bits<WIDTH>(this, bitnum, bitnum);
}


}
