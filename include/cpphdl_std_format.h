#ifndef CPPHDL_STD_FORMAT_COMPAT_H
#define CPPHDL_STD_FORMAT_COMPAT_H
#if defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L)
#define CPPHDL_HAS_STD_FORMAT 1
#endif

#ifndef CPPHDL_HAS_STD_FORMAT
#include <string>
#include <string_view>

namespace std {

template <typename... Args>
inline std::string format(std::string_view fmt, Args&&...)
{
    return std::string(fmt);
}

} // namespace std
#endif
#endif // CPPHDL_STD_FORMAT_COMPAT_H

#if defined(CPPHDL_WANT_STD_PRINT_STUBS) && !defined(CPPHDL_HAS_STD_PRINT) && !defined(CPPHDL_STD_PRINT_STUBS_DEFINED)
#define CPPHDL_STD_PRINT_STUBS_DEFINED 1
#include <cstdio>
#include <string_view>

namespace std {

template <typename... Args>
inline void print(std::string_view fmt, Args&&...)
{
    std::fwrite(fmt.data(), 1, fmt.size(), stdout);
}

template <typename... Args>
inline void print(FILE* file, std::string_view fmt, Args&&...)
{
    FILE* out = file ? file : stdout;
    std::fwrite(fmt.data(), 1, fmt.size(), out);
}

template <typename... Args>
inline void println(std::string_view fmt, Args&&...)
{
    std::fwrite(fmt.data(), 1, fmt.size(), stdout);
    std::fputc('\n', stdout);
}

template <typename... Args>
inline void println(FILE* file, std::string_view fmt, Args&&...)
{
    FILE* out = file ? file : stdout;
    std::fwrite(fmt.data(), 1, fmt.size(), out);
    std::fputc('\n', out);
}

} // namespace std
#endif
