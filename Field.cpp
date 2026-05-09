#include "Field.h"
#include "Debug.h"
#include "Project.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace cpphdl;

Field* currField;

namespace
{

std::string dimToString(Expr dim)
{
    dim.flags = Expr::FLAG_SPECVAL;
    return dim.str();
}

size_t dimToSize(Expr dim)
{
    std::string text = dimToString(dim);
    return std::strtoull(text.c_str(), nullptr, 0);
}

struct ArrayShape
{
    Expr base;
    std::vector<Expr> cpphdlDims;
    std::vector<Expr> unpackedDims;
    bool hasArray = false;
};

ArrayShape flattenArrayShape(const Expr& expr)
{
    if (expr.type == Expr::EXPR_ARRAY && expr.sub.size() >= 2) {
        ArrayShape shape = flattenArrayShape(expr.sub.back());
        shape.hasArray = true;
        shape.unpackedDims.insert(shape.unpackedDims.begin(), expr.sub.begin(), expr.sub.end() - 1);
        return shape;
    }

    if (expr.type == Expr::EXPR_TEMPLATE && expr.value == "cpphdl_array" && expr.sub.size() >= 2) {
        ArrayShape shape = flattenArrayShape(expr.sub[0]);
        shape.hasArray = true;
        shape.cpphdlDims.insert(shape.cpphdlDims.begin(), expr.sub[1]);
        return shape;
    }

    return ArrayShape{expr};
}

bool printStructFieldArray(std::ofstream& out, Field& field, const std::string& nameSuffix)
{
    ArrayShape shape = flattenArrayShape(field.expr);
    if (!shape.hasArray) {
        return false;
    }

    shape.base.indent = field.indent;
    shape.base.flags |= field.expr.flags;

    auto appendPackedDims = [](std::string& packedDims, const std::vector<Expr>& dims) {
        for (const auto& dim : dims) {
            packedDims += "[" + dimToString(dim) + "-1:0]";
        }
    };

    std::string packedDims;
    appendPackedDims(packedDims, shape.unpackedDims);
    appendPackedDims(packedDims, shape.cpphdlDims);
    out << shape.base.str("", packedDims) << " " << field.name << nameSuffix << ";\n";

    field.expr.declSize = shape.base.declSize;
    for (const auto& dim : shape.cpphdlDims) {
        field.expr.declSize *= dimToSize(dim);
    }
    for (const auto& dim : shape.unpackedDims) {
        field.expr.declSize *= dimToSize(dim);
    }
    return true;
}

}

bool Field::print(std::ofstream& out, std::string nameSuffix, bool inStruct)
{
    currField = this;

    if (inStruct && printStructFieldArray(out, *this, nameSuffix)) {
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
