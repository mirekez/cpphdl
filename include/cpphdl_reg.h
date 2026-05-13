#pragma once
#include "cpphdl_checkpoint.h"
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

    void strobe(FILE* checkpoint_fd)
    {
        if (!checkpoint_fd) {
            strobe();
            return;
        }
        if (checkpoint_reading(checkpoint_fd)) {
            checkpoint_value(checkpoint_fd, *static_cast<T*>(this));
            checkpoint_value(checkpoint_fd, _next);
        }
        else {
            strobe();
            checkpoint_value(checkpoint_fd, *static_cast<T*>(this));
            checkpoint_value(checkpoint_fd, _next);
        }
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

    operator T&()
    {
        return *this;
    }
};


}
