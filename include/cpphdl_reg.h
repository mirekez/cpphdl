#pragma once
#include "cpphdl_types.h"

namespace cpphdl
{


template<typename T=u1>
struct reg : public T
{
    T _next;
    void strobe()
    {
        *static_cast<T*>(this) = _next;
    }

    T operator=(T val)
    {
        return *static_cast<T*>(this) = val;
    }

    void set()
    {
        _next = *static_cast<T*>(this);
    }

    void set(const T& t)
    {
        *static_cast<T*>(this) = t;
        _next = t;
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
