#pragma once
#include "cpphdl_types.h"

namespace cpphdl
{

template<typename T=u1>
struct reg : public T
{
    T next;
    void strobe()
    {
        *static_cast<T*>(this) = next;
    }

    T operator=(T val)
    {
        return *static_cast<T*>(this) = val;
    }

    void set()
    {
        next = *static_cast<T*>(this);
    }

    void set(const T& t)
    {
        *static_cast<T*>(this) = t;
        next = t;
    }

    void clr()
    {
        memset(this, 0, sizeof(*this));
    }

    reg() = default;
    operator T()
    {
        return *static_cast<T*>(this);
    }
};

}
