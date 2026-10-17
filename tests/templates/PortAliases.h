#pragma once
#include <cpphdl.h>

template<class T> using Binding = cpphdl::port<T>;

template<unsigned WIDTH> struct PortAliasInterface : cpphdl::Interface {
    _PORT(cpphdl::logic<WIDTH>) data_in;
};

template<unsigned WIDTH = 9>
class PortAliases : public cpphdl::Module {
public:
    Binding<cpphdl::logic<WIDTH>> data_in;
    _PORT(cpphdl::logic<WIDTH>) data_out;
    PortAliasInterface<WIDTH> sink_in;
    _PORT(cpphdl::logic<WIDTH>) interface_out;

    void _assign() {
        data_out = _ASSIGN(data_in());
        interface_out = _ASSIGN(sink_in.data_in());
    }
    void _work(bool) {}
    void _strobe() {}
};

#ifdef PORT_ALIAS_INSTANTIATE
template class PortAliases<9>;
#endif
