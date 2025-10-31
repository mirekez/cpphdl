#include "Field.h"
#include "Debug.h"

#include <fstream>

using namespace cpphdl;

Field* currField;

bool Field::print(std::ofstream& out)
{
    currField = this;

    if (type.value == "cpphdl::memory") {
        ASSERT1(type.sub.size() >= 3, std::string("cpphdl::memory subs size = ") + std::to_string(type.sub.size()) );
        type.flags = Expr::FLAG_REG;
        out << "    " << type.sub[0].str("", std::string("[") + type.sub[1].value + "-1:0]") << " " << name << "[" << type.sub[2].value << "]" << ";\n";
    } else
    if (type.value == "cpphdl::reg") {
        ASSERT1(type.sub.size() >= 1, std::string("cpphdl::reg subs size = ") + std::to_string(type.sub.size()) );
        type.flags = Expr::FLAG_REG;
        out << "    " << type.sub[0].str() << " " << name << ";\n";
    } else
    if (type.type == Expr::EXPR_ARRAY && type.sub[1].type == Expr::EXPR_TYPE && type.sub.size() >= 2) {
        out << "    " << type.sub[1].str() << " " << name << "[" << type.sub[0].str() << "]" << ";\n";
    }
    else {
        out << "    " << type.str() << " " << name << ";\n";
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
