#include "Enum.h"
#include "Field.h"
#include "Expr.h"

#include <fstream>

using namespace cpphdl;

Enum* currEnum = nullptr;

bool Enum::print(std::ofstream& out)
{
    currEnum = this;

    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << "enum {\n";

    for (size_t i=0; i < fields.size(); ++i) {
        for (int i=0; i < indent; ++i) {
            out << "    ";
        }
        out << fields[i].name;
        if (fields[i].expr.type != cpphdl::Expr::EXPR_NONE) {
            out << " = ";
            out << fields[i].expr.str();
        }
        if (i != fields.size()-1) {
            out << ",";
        }
        out << "\n";
    }

    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << "} " << (name.length()?name:"_");

    out << ";\n";
    return true;
}
