#include "Field.h"
#include "Debug.h"

#include <fstream>

using namespace cpphdl;

Field* currField;

bool Field::print(std::ofstream& out, bool isStruct)
{
    currField = this;

    if (type.value == "cpphdl::memory") {
        ASSERT1(type.sub.size() >= 3, std::string("cpphdl::memory subs size = ") + std::to_string(type.sub.size()) );
        type.sub[0].flags = Expr::FLAG_REG;
        type.sub[0].indent = indent + 1;
        out << type.sub[0].str("", std::string("[") + type.sub[1].str() + "-1:0]") << " " << name << "[" << type.sub[2].str() << "]" << ";\n";
    } else
    if (type.value == "cpphdl::reg") {
        ASSERT1(type.sub.size() >= 1, std::string("cpphdl::reg subs size = ") + std::to_string(type.sub.size()) );
        type.sub[0].flags = Expr::FLAG_REG;
        type.sub[0].indent = indent + 1;
        out << type.sub[0].str() << " " << name << ";\n";
    } else
    if (type.type == Expr::EXPR_ARRAY && (type.sub[1].type == Expr::EXPR_TYPE || type.sub[1].type == Expr::EXPR_TEMPLATE)) {
        ASSERT1(type.sub.size() >= 1, std::string("EXPR_ARRAY subs size = ") + std::to_string(type.sub.size()) );
        if (type.sub[1].value == "cpphdl::reg") {
            ASSERT1(type.sub[1].sub.size() >= 1, std::string("cpphdl::reg subs size = ") + std::to_string(type.sub[1].sub.size()) );
            type.sub[1].flags = Expr::FLAG_REG;
            type.sub[1].sub[0].indent = indent + 1;
            out << type.sub[1].sub[0].str() << " " << name << "[" << type.sub[0].str() << "]" << ";\n";
        } else
        if (isStruct) {
            type.sub[1].indent = indent + 1;
            out << type.str() << " " << name << ";\n";
        }
        else {
            type.sub[1].indent = indent + 1;
            out << type.sub[1].str() << " " << name << "[" << type.sub[0].str() << "]" << ";\n";
        }
    } else
    if (bitwidth.type != Expr::EXPR_EMPTY) {
        type.indent = indent + 1;
        type.type = Expr::EXPR_TYPE;
        type.value = "cpphdl::logic";
        bitwidth.flags = Expr::FLAG_STRUCT;
        out << type.str("", std::string("[") + bitwidth.str() + "-1:0]") << " " << name << ";\n";
    }
    else {
        type.indent = indent + 1;
        out << type.str()  << " " << name << ";\n";
    }
    return true;
}

bool Field::printPort(std::ofstream& out)
{
    currField = this;

    type.flags = Expr::FLAG_WIRE;
    out << type.str(name.find("_out") == (size_t)-1 ? "input " : "output ") << " " << name << "\n";
    return true;
}
