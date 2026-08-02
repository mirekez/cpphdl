#ifndef CPPHDL_STD_FORMAT_COMPAT_H
#define CPPHDL_STD_FORMAT_COMPAT_H
#if defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L)
#define CPPHDL_HAS_STD_FORMAT 1
#endif
#endif // CPPHDL_STD_FORMAT_COMPAT_H
