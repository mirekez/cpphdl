#include "Struct.h"
#include "Field.h"
#include "Expr.h"
#include "Debug.h"
#include "Project.h"
#include "Module.h"

#include <iostream>
#include <fstream>

using namespace cpphdl;

Struct* currStruct = nullptr;

bool Struct::print(std::ofstream& out/*, std::vector<Field>* params*/)
{
    currStruct = this;

//    if (!params) {
//        params = &parameters;
//    }

    // we need to pass structure in straight order first to calculate size and make alignment
    size_t countSize = 0;
    for (size_t i=0; i < fields.size(); ++i) {
        if (fields[i].definition.type != STRUCT_EMPTY) {  // inline struct/union
            countSize += getStructSize(fields[i].name, &fields[i].definition);
            if (fields[i].definition.type == STRUCT_STRUCT) {
                countSize = 8; // dont align structs - they should be aligned
            }
        } else {  // not a struct or union
            if (fields[i].name.find("_align") == 0) {
//                fields[i].name += "_";
//                fields[i].name += std::to_string(countSize);
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
//                fields[i].name += "_";
//                fields[i].name += std::to_string(fields[i].expr.declSize);
                if (fields[i].bitwidth.type != Expr::EXPR_NONE) {  // special case
                    countSize += atoi(fields[i].bitwidth.str().c_str());
                    if (type != STRUCT_STRUCT) {  // union
                        countSize = atoi(fields[i].bitwidth.str().c_str());
                    }
                }
                else {
                    countSize += fields[i].expr.declSize;
                    if (type != STRUCT_STRUCT) {  // union
                        countSize = fields[i].expr.declSize;
                    }
                }
            }
        }
    }
    declSize += countSize;
//    if (type != STRUCT_STRUCT) *declSize = 8;  // hotfix union is always aligned?

    if (!out) {
        return true;
    }

    // now print the structure
    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << ( type == STRUCT_STRUCT ? "struct" : "union" ) << " packed {\n";

    ++indent;
    for (int i=fields.size()-1; i >= 0; --i) {  // reverse order in SystemVerilog
        if (fields[i].definition.type != STRUCT_EMPTY) {  // inline struct/union
            fields[i].definition.indent = indent;
            fields[i].definition.print(out/*, params*/);
        } else {
            fields[i].indent = indent;
            fields[i].print(out);
//            out << fields[i].bitwidth.debug() << "\n";
        }
    }
    --indent;

    for (int i=0; i < indent; ++i) {
        out << "    ";
    }
    out << "} " << (name.length()?name:"_");

    out << ";\n";
    return true;
}

size_t cpphdl::getStructSize(std::string name, Struct* st)
{
    if (st) {
        size_t structSize = 0;
        for (auto& f : st->fields) {
            if (f.name.find("_align") == 0) {
                continue;
            }
            size_t tmp = 0;
            if (f.definition.type != Struct::STRUCT_EMPTY) {  // inline struct/union
                tmp = cpphdl::getStructSize(f.definition.name, &f.definition);
            } else {  // not a struct
                f.expr.str();
                if (f.bitwidth.type != Expr::EXPR_NONE) {  // special case
                    tmp = atoi(f.bitwidth.str().c_str());
                }
                else {
                    tmp = f.expr.declSize;
                }
            }
            if (st->type == Struct::STRUCT_STRUCT) {
                structSize += tmp;
            } else
            if (st->type == Struct::STRUCT_UNION) {
                if (structSize == 0) {
                    structSize = tmp;
                } else
                if (tmp != structSize) {
                    std::cerr << "WARNING: different sizes in union " << st->name << ": " << f.name << " size "  << tmp << " != " << structSize << "(" << f.expr.str()
                              << (f.bitwidth.type != Expr::EXPR_NONE?std::string(":")+f.bitwidth.str():"") << ") for '" << name << "'\n";
                }
            }
        }
        return structSize;
    }

    for (auto& s : currProject->structs) {
        if (s.name == name) {
            if (s.declSize) {
                return s.declSize;
            }
            s.declSize = cpphdl::getStructSize(name, &s);
            if (s.declSize) {
                return s.declSize;
            }
            s.declSize = -1;
            break;
        }
    }
    if (currModule) {
        for (auto& m : currModule->members) {
            if (m.expr.value == name) {
                return -1;
            }
        }
    }
    std::cerr << "WARNING: can't get structure '" << name << "\n"; //"', " << std::stacktrace::current() << "\n";
    return 0;
}
