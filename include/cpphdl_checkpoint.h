#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace cpphdl
{

inline bool checkpoint_reading(FILE* checkpoint_fd)
{
    return reinterpret_cast<intptr_t>(checkpoint_fd) < 0;
}

inline FILE* checkpoint_file(FILE* checkpoint_fd)
{
    intptr_t value = reinterpret_cast<intptr_t>(checkpoint_fd);
    if (value < 0) {
        value = -value;
    }
    return reinterpret_cast<FILE*>(value);
}

inline void checkpoint_write_exact(FILE* checkpoint_fd, const void* data, size_t size)
{
    FILE* fd = checkpoint_file(checkpoint_fd);
    if (std::fwrite(data, 1, size, fd) != size) {
        std::abort();
    }
}

inline void checkpoint_read_exact(FILE* checkpoint_fd, void* data, size_t size)
{
    FILE* fd = checkpoint_file(checkpoint_fd);
    if (std::fread(data, 1, size, fd) != size) {
        std::abort();
    }
}

template<typename T>
inline void checkpoint_value(FILE* checkpoint_fd, T& value)
{
    if (!checkpoint_fd) {
        return;
    }
    if (checkpoint_reading(checkpoint_fd)) {
        checkpoint_read_exact(checkpoint_fd, &value, sizeof(value));
    }
    else {
        checkpoint_write_exact(checkpoint_fd, &value, sizeof(value));
    }
}

inline FILE* checkpoint_read_fd(FILE* fd)
{
    return reinterpret_cast<FILE*>(-reinterpret_cast<intptr_t>(fd));
}

}
