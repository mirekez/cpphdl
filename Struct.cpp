#include "Struct.h"
#include "Field.h"
#include "Expr.h"

#include <fstream>

using namespace cpphdl;

Struct* currStruct = nullptr;

bool Struct::print(std::ofstream& out)
{
    currStruct = this;

    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << ( type == STRUCT_STRUCT ? "struct" : "union" ) << " packed {\n";
    for (int i=fields.size()-1; i >= 0; --i) {  // reverse order in SystemVerilog
        if (fields[i].definition.type != STRUCT_EMPTY) {  // inline struct/union
            fields[i].definition.indent = indent + 1;
            fields[i].definition.print(out);
        } else {
            fields[i].indent = indent;
            fields[i].print(out, true);
        }
    }
    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << "} " << (name.length()?name:"anon");

    out << ";\n";
    return true;
}
