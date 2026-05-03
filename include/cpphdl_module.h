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
    static void _assign() {}

    template<typename ModA, typename ModB, typename A, typename B>
    void assignIf(ModA& modA, ModB& modB, A& a, B& b)
    {
        // Interface direction is bidirectional at C++ level: copy B to A first
        // so modA can assign outputs from its new inputs, copy those outputs
        // back to B so modB can react, then copy modB outputs back to A and
        // assign modA once more. If this module is one side of the connection,
        // skip that _assign() call to avoid recursively entering this helper.
        a = b;
        if constexpr (std::is_base_of_v<Module, ModA>) {
            if (static_cast<Module*>(&modA) != this) {
                modA._assign();
            }
        }
        else {
            modA._assign();
        }
        b = a;
        if constexpr (std::is_base_of_v<Module, ModB>) {
            if (static_cast<Module*>(&modB) != this) {
                modB._assign();
            }
        }
        a = b;
        if constexpr (std::is_base_of_v<Module, ModA>) {
            if (static_cast<Module*>(&modA) != this) {
                modA._assign();
            }
        }
        else {
            modA._assign();
        }
    }
};


}
