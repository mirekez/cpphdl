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
        fields[i].indent = indent;
        fields[i].print(out, true);
    }

    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << "} " << (name.length()?name:"_");

    out << ";\n";
    return true;
}
