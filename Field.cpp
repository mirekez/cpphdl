#include "Field.h"
#include "Debug.h"

#include <fstream>

using namespace cpphdl;

Field* currField;

bool Field::print(std::ofstream& out, bool isStruct)
{
    currField = this;

    if (expr.value == "cpphdl::memory") {
        ASSERT1(expr.sub.size() >= 3, std::string("cpphdl::memory subs size = ") + std::to_string(expr.sub.size()) );
        expr.sub[0].flags = Expr::FLAG_REG;
        expr.sub[0].indent = indent + 1;
        out << expr.sub[0].str("", std::string("[") + expr.sub[1].str() + "-1:0]") << " " << name << "[" << expr.sub[2].str() << "]" << ";\n";
    } else
    if (expr.value == "cpphdl::reg") {
        ASSERT1(expr.sub.size() >= 1, std::string("cpphdl::reg subs size = ") + std::to_string(expr.sub.size()) );
        expr.sub[0].flags = Expr::FLAG_REG;
        expr.sub[0].indent = indent + 1;
        out << expr.sub[0].str() << " " << name << ";\n";
    } else
    if (expr.type == Expr::EXPR_ARRAY && (expr.sub[1].type == Expr::EXPR_TYPE || expr.sub[1].type == Expr::EXPR_TEMPLATE)) {
        ASSERT1(expr.sub.size() >= 1, std::string("EXPR_ARRAY subs size = ") + std::to_string(expr.sub.size()) );
        if (expr.sub[1].value == "cpphdl::reg") {
            ASSERT1(expr.sub[1].sub.size() >= 1, std::string("cpphdl::reg subs size = ") + std::to_string(expr.sub[1].sub.size()) );
            expr.sub[1].flags = Expr::FLAG_REG;
            expr.sub[1].sub[0].indent = indent + 1;
            out << expr.sub[1].sub[0].str() << " " << name << "[" << expr.sub[0].str() << "]" << ";\n";
        } else
        if (isStruct) {
            expr.sub[1].indent = indent + 1;
            out << expr.str() << " " << name << ";\n";
        }
        else {
            expr.sub[1].indent = indent + 1;
            out << expr.sub[1].str() << " " << name << "[" << expr.sub[0].str() << "]" << ";\n";
        }
    } else
    if (bitwidth.type != Expr::EXPR_EMPTY) {
        expr.indent = indent + 1;
        expr.type = Expr::EXPR_TYPE;
        expr.value = "cpphdl::logic";
        bitwidth.flags = Expr::FLAG_SPECVAL;
        out << expr.str("", std::string("[") + bitwidth.str() + "-1:0]") << " " << name << ";\n";
    }
    else {
        expr.indent = indent + 1;
        out << expr.str()  << " " << name << ";\n";
    }
    return true;
}

bool Field::printPort(std::ofstream& out)
{
    currField = this;

    expr.flags = Expr::FLAG_WIRE;
    out << expr.str(name.find("_out") == (size_t)-1 ? "input " : "output ") << " " << name << "\n";
    return true;
}
