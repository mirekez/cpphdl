#include "Field.h"
#include "Debug.h"
#include "Project.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

using namespace cpphdl;

Field* currField;

namespace
{

bool isStructOrUnionType(const Expr& expr)
{
    if (expr.type != Expr::EXPR_TYPE) {
        return false;
    }
    return std::find_if(currProject->structs.begin(), currProject->structs.end(), [&](const auto& st) {
        return st.name == expr.value;
    }) != currProject->structs.end();
}

bool isUnpackedStructArray(const Expr& expr)
{
    return expr.type == Expr::EXPR_TEMPLATE &&
        expr.value == "cpphdl_array" &&
        expr.sub.size() >= 2 &&
        isStructOrUnionType(expr.sub[0]);
}

}

bool Field::print(std::ofstream& out, std::string nameSuffix, bool inStruct)
{
    currField = this;

    if (inStruct && isUnpackedStructArray(expr)) {
        Expr element = expr.sub[0];
        Expr count = expr.sub[1];
        element.indent = indent;
        element.flags |= expr.flags;
        out << element.str() << " " << name << nameSuffix << "[" << count.str() << "];\n";
        expr.declSize = element.declSize * atoi(count.str().c_str());
        return true;
    }

    if (bitwidth.type != Expr::EXPR_NONE) {  // still a special case for struct definition
        expr.indent = indent;
        expr.type = Expr::EXPR_TYPE;
        expr.value = "cpphdl_logic";
        bitwidth.flags = Expr::FLAG_SPECVAL;
        out << expr.str("", std::string("[") + bitwidth.str() + "-1:0]") << " " << name << ";\n";
    } else
    if (initializer.type != Expr::EXPR_NONE) {
        auto tmp = Expr{name, Expr::EXPR_DECL, {std::move(expr), initializer}};
        tmp.indent = indent;
        out << tmp.str("",nameSuffix) + ";\n";
//        out << tmp.debug() + "\n";
        expr = std::move(tmp.sub[0]);  // return expr back
    } else
    if (expr.type == Expr::EXPR_CONST) {
        out << expr.str() + ";\n";
    } else
    if (expr.type == Expr::EXPR_PARAM) {
        out << name << nameSuffix << " = " << expr.str() + ";\n";
//        out << tmp.debug() + "\n";
    } else
    if (array.size()) {
        auto tmp = Expr{name, Expr::EXPR_DECL, {std::move(expr)}};
        tmp.indent = indent;
        out << tmp.str("", nameSuffix) + "[" + array[0].str() + "];\n";
//        out << tmp.debug() + "\n";
        expr = std::move(tmp.sub[0]);  // return expr back
    }
    else {
        auto tmp = Expr{name, Expr::EXPR_DECL, {std::move(expr)}};
        tmp.indent = indent;
        out << tmp.str("", nameSuffix) + ";\n";
//        out << tmp.debug() + "\n";
        expr = std::move(tmp.sub[0]);  // return expr back
    }

    return true;
}

bool Field::printPort(std::ofstream& out)
{
    currField = this;
    expr.flags = Expr::FLAG_WIRE;
    if (array.size()) {
        out << expr.str(!str_ending(name, "_out") ? "input " : "output ") << " " << name << "[" << array[0].str() << "]\n";
        return true;
    }
    out << expr.str(!str_ending(name, "_out") ? "input " : "output ") << " " << name << "\n";
    return true;
}
