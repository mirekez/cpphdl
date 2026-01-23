#include "Field.h"
#include "Debug.h"

#include <fstream>

using namespace cpphdl;

Field* currField;

bool Field::print(std::ofstream& out, bool isStruct)
{
    currField = this;

    if (bitwidth.type != Expr::EXPR_NONE) {  // still a special case for struct definition
        expr.indent = indent;
        expr.type = Expr::EXPR_TYPE;
        expr.value = "cpphdl_logic";
        bitwidth.flags = Expr::FLAG_SPECVAL;
        out << expr.str("", std::string("[") + bitwidth.str() + "-1:0]") << " " << name << ";\n";
    }
    else {
        auto tmp = Expr{name, Expr::EXPR_DECL, {std::move(expr)}};
        tmp.indent = indent;
        out << tmp.str() + ";\n";
        expr = std::move(tmp.sub[0]);  // return expr back
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
