#include "Struct.h"
#include "Field.h"
#include "Expr.h"
#include "Debug.h"
#include "Project.h"
#include "Module.h"
#include "Enum.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>

using namespace cpphdl;

Struct* currStruct = nullptr;

namespace
{

size_t fieldPayloadBitSize(Field& f);
size_t fieldStorageBitSize(Field& f);
void ensureStructStorageSize(Struct& st, size_t bitSize);

size_t alignToByte(size_t bitSize)
{
    return (bitSize + 7) & ~size_t(7);
}

bool isAlignField(const Field& f)
{
    return f.name.find("_align") == 0;
}

bool isPadField(const Field& f)
{
    return f.name.find("_pad") == 0;
}

Field makeAlignField(Struct& st, size_t bits)
{
    Field align{std::string("_align") + std::to_string(st.alignNo), {Expr{"uint8_t", Expr::EXPR_TYPE}}};
    align.bitwidth = Expr{std::to_string(bits), Expr::EXPR_NUM};
    ++st.alignNo;
    return align;
}

Field makePadField(Struct& st, size_t bits)
{
    Field pad{std::string("_pad") + std::to_string(st.alignNo), {Expr{"uint8_t", Expr::EXPR_TYPE}}};
    pad.bitwidth = Expr{std::to_string(bits), Expr::EXPR_NUM};
    ++st.alignNo;
    return pad;
}

void setAlignWidth(Field& f, size_t bits)
{
    f.bitwidth = Expr{std::to_string(bits), Expr::EXPR_NUM};
}

size_t prepareStructLayout(Struct& st)
{
    size_t bitSize = 0;
    size_t unionSize = 0;
    std::vector<size_t> unionFieldSizes;

    if (st.type == Struct::STRUCT_STRUCT && st.fields.empty()) {
        st.fields.emplace_back(makePadField(st, 1));
        st.declSize = 1;
        return st.declSize;
    }

    for (size_t i = 0; i < st.fields.size(); ++i) {
        Field& f = st.fields[i];

        if (isAlignField(f)) {
            size_t alignBits = st.type == Struct::STRUCT_STRUCT ? ((8 - bitSize % 8) % 8) : 0;
            if (alignBits == 0) {
                st.fields.erase(st.fields.begin() + i);
                --i;
                continue;
            }
            setAlignWidth(f, alignBits);
            bitSize += alignBits;
            continue;
        }

        bool bitField = f.bitwidth.type != Expr::EXPR_NONE;
        if (st.type == Struct::STRUCT_STRUCT && !bitField && bitSize % 8) {
            size_t alignBits = 8 - bitSize % 8;
            st.fields.insert(st.fields.begin() + i, makeAlignField(st, alignBits));
            bitSize += alignBits;
            ++i;
        }

        size_t rawSize = fieldPayloadBitSize(st.fields[i]);
        size_t fSize = bitField ? rawSize : fieldStorageBitSize(st.fields[i]);

        if (st.type == Struct::STRUCT_STRUCT) {
            bitSize += rawSize;
            if (!bitField && fSize > rawSize) {
                size_t alignBits = fSize - rawSize;
                if (i + 1 < st.fields.size() && isAlignField(st.fields[i + 1])) {
                    setAlignWidth(st.fields[i + 1], alignBits);
                } else {
                    st.fields.insert(st.fields.begin() + i + 1, makeAlignField(st, alignBits));
                }
                bitSize += fSize - rawSize;
                ++i;
            }
        } else {
            unionSize = std::max(unionSize, fSize);
            unionFieldSizes.push_back(fSize);
        }
    }

    st.declSize = st.type == Struct::STRUCT_STRUCT ? bitSize : unionSize;
    if (st.type == Struct::STRUCT_UNION) {
        size_t unionFieldIndex = 0;
        for (size_t i = 0; i < st.fields.size(); ++i) {
            Field& f = st.fields[i];
            if (isAlignField(f)) {
                continue;
            }
            if (f.definition.type != Struct::STRUCT_EMPTY && unionFieldIndex < unionFieldSizes.size()) {
                ensureStructStorageSize(f.definition, unionSize);
            }
            ++unionFieldIndex;
        }
    }
    return st.declSize;
}

size_t cpphdlScalarWidth(Expr& expr)
{
    if (expr.type == Expr::EXPR_TEMPLATE && expr.value.find("cpphdl_") == 0 && expr.sub.size()) {
        return atoi(expr.sub[0].str().c_str());
    }
    if (expr.type == Expr::EXPR_TYPE) {
        if (expr.value.find("cpphdl_u") == 0) {
            return atoi(expr.value.c_str() + strlen("cpphdl_u"));
        }
        if (expr.value.find("cpphdl_i") == 0) {
            return atoi(expr.value.c_str() + strlen("cpphdl_i"));
        }
        if (expr.value == "cpphdl_logic" || expr.value == "bool" || expr.value == "_Bool") {
            return 1;
        }
    }
    return 0;
}

size_t fieldPayloadBitSize(Field& f)
{
    if (f.definition.type != Struct::STRUCT_EMPTY) {
        return prepareStructLayout(f.definition);
    }

    f.expr.str();
    if (f.bitwidth.type != Expr::EXPR_NONE) {
        return atoi(f.bitwidth.str().c_str());
    }
    size_t scalarWidth = cpphdlScalarWidth(f.expr);
    if (scalarWidth) {
        return scalarWidth;
    }
    return f.expr.declSize;
}

size_t fieldStorageBitSize(Field& f)
{
    if (f.definition.type != Struct::STRUCT_EMPTY) {
        return prepareStructLayout(f.definition);
    }

    f.expr.str();
    return alignToByte(f.expr.declSize);
}

void ensureStructStorageSize(Struct& st, size_t bitSize)
{
    size_t currSize = prepareStructLayout(st);
    if (currSize >= bitSize) {
        return;
    }

    size_t alignBits = bitSize - currSize;
    if (st.fields.size() && isPadField(st.fields.back())) {
        setAlignWidth(st.fields.back(), alignBits);
    } else {
        st.fields.emplace_back(makePadField(st, alignBits));
    }
    st.declSize = bitSize;
}

}

bool Struct::print(std::ofstream& out/*, std::vector<Field>* params*/)
{
    currStruct = this;

//    if (!params) {
//        params = &parameters;
//    }

    // First walk in C++ declaration order. SystemVerilog packed structs are
    // printed in reverse order below, but padding must be calculated from the
    // low-address/low-bit side used by C++ bitfields.
    prepareStructLayout(*this);
    prepareStructLayout(*this);
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
            fields[i].print(out, "", true);
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
        prepareStructLayout(*st);
        return prepareStructLayout(*st);
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
    for (auto& e : currProject->enums) {
        if (e.name == name) {
            return 32;
        }
    }
    if (currModule) {
        for (auto& m : currModule->members) {
            if (m.expr.value == name) {
                return -1;
            }
        }
    }
    std::cerr << "WARNING: can't get structure '" << name << "'\n"; //"', " << std::stacktrace::current() << "\n";
    return 0;
}
