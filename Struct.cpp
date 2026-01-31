#include "Struct.h"
#include "Field.h"
#include "Expr.h"

#include <iostream>
#include <fstream>

using namespace cpphdl;

Struct* currStruct = nullptr;

bool Struct::print(std::ofstream* out, std::vector<Field>* params, size_t* declSize)
{
    currStruct = this;

    size_t countSize = 0;
    if (!declSize) {
        declSize = &countSize;
    }
    if (!params) {
        params = &parameters;
    }

    // we need to pass structure in straight order first to calculate size and make align
    for (size_t i=0; i < fields.size(); ++i) {
        if (fields[i].definition.type != STRUCT_EMPTY) {  // inline struct/union
            fields[i].definition.print(nullptr, params, declSize);  // calculate size
        } else {
            if (fields[i].name.find("_align") == 0) {
                fields[i].bitwidth = Expr{std::to_string(8-countSize%8), Expr::EXPR_NUM};
                if (countSize%8 == 0 && countSize != 0) {
                    fields.erase(fields.begin() + i);  // we neednt alignment here
                    --i;
                    countSize = 0;
                    continue;
                }
                countSize = 0;
            }
            else {  // any other field
                fields[i].expr.str();  // calculate size
                if (fields[i].bitwidth.type != Expr::EXPR_NONE) {
                    countSize += atoi(fields[i].bitwidth.str().c_str());
                }
                else {
                    countSize += fields[i].expr.declSize;
                }
            }
        }
    }
    if (type != STRUCT_STRUCT) *declSize = 8;  // hotfix union is always aligned?

    if (!out) {
        return true;
    }

    // now print the structure
    for (int i=0; i < indent; ++i) {
        *out << "    ";
    }
    *out << ( type == STRUCT_STRUCT ? "struct" : "union" ) << " packed {\n";

    ++indent;
    for (int i=fields.size()-1; i >= 0; --i) {  // reverse order in SystemVerilog
        if (fields[i].definition.type != STRUCT_EMPTY) {  // inline struct/union
            fields[i].definition.indent = indent;
            fields[i].definition.print(out, params, declSize);
        } else {
            fields[i].indent = indent;
            fields[i].print(*out);
//            *out << fields[i].bitwidth.debug() << "\n";
        }
    }
    --indent;

    for (int i=0; i < indent; ++i) {
        *out << "    ";
    }
    *out << "} " << (name.length()?name:"_");

    *out << ";\n";
    return true;
}
