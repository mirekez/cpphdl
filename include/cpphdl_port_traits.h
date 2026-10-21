#pragma once
#include <type_traits>

namespace cpphdl {

template <class T> class function_ref;

// Value ports keep lazy C++HDL bindings. Domains may select a connection port.
template <class T, class = void> struct port_traits {
    using type = function_ref<T>;
};

template <class T> struct port_traits<T, std::void_t<typename T::cpphdl_connection_tag>> {
    using type = typename T::template cpphdl_port_type<T>;
};

template <class T> using port = typename port_traits<T>::type;

} // namespace cpphdl

#ifndef _PORT
#ifdef CPPHDL_STATIC
#define _PORT(...) inline static ::cpphdl::port<__VA_ARGS__>
#else
#define _PORT(...) ::cpphdl::port<__VA_ARGS__>
#endif
#endif
