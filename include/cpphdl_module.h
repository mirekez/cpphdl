#pragma once

namespace cpphdl
{


class Module
{
public:
    std::string __inst_name;

    static void _work_neg(bool /*reset*/) {}  // default
    static void _strobe_neg() {}

    static void _strobe() {}
    static void _connect() {}
};


}
