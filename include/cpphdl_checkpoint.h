#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace cpphdl
{

struct checkpoint_io_error
{
    const char* operation;
    size_t expected_size;
    size_t actual_size;
    int error_number;
};

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
    size_t written = std::fwrite(data, 1, size, fd);
    if (written != size) {
        throw checkpoint_io_error{"write", size, written, errno};
    }
}

inline void checkpoint_read_exact(FILE* checkpoint_fd, void* data, size_t size)
{
    FILE* fd = checkpoint_file(checkpoint_fd);
    size_t read = std::fread(data, 1, size, fd);
    if (read != size) {
        throw checkpoint_io_error{"read", size, read, std::ferror(fd) ? errno : 0};
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
