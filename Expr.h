#pragma once

#include <string>
#include <vector>

namespace cpphdl
{

struct Expr
{
    std::string value;

    enum {
        EXPR_EMPTY,
        EXPR_TYPE,
        EXPR_TEMPLATE,
        EXPR_VALUE,
        EXPR_CALL,
        EXPR_BINARY,
        EXPR_DECLARE,
        EXPR_FOR,
        EXPR_IF,
        EXPR_UNKNOWN
    } type = EXPR_EMPTY;

    std::vector<Expr> sub;

    // methods

    std::string str(unsigned flags = 0, std::string prefix = "", std::string suffix = "")
    {
        std::string ret;
        switch (type)
        {
            case EXPR_EMPTY:
                return "";
            case EXPR_TYPE:
                return typeToSV(value, flags, prefix, suffix);
            case EXPR_TEMPLATE:
                ASSERT(sub.size() >= 1);
                return typeToSV(value, flags, prefix, suffix, sub[0].type == EXPR_VALUE ? sub[0].value : sub[0].str(flags,prefix,suffix));
            case EXPR_VALUE:
                return value;//, flags, prefix, suffix);
            case EXPR_CALL:
                return "unsupported";
            case EXPR_BINARY:
                ASSERT(sub.size()==2);
                return std::string("(binary: ") + sub[0].str(flags,prefix,suffix) + " " + value + " " + sub[1].str(flags,prefix,suffix) + ")";
            case EXPR_DECLARE:
                ASSERT(0);
                return "";
            case EXPR_UNKNOWN:
                return std::string("(unknown: ") + value + ")";
            default:
                return "missed case";
        }
        ASSERT(0);
        return "";
    }

    enum : unsigned {
        FLAG_PORT = 1,
        FLAG_REG = 2,
    };

    std::string typeToSV(std::string name, unsigned flags, const std::string& prefix, const std::string& suffix, std::string size = "")
    {
        std::string logic = (flags&FLAG_PORT) ? "wire" : ((flags&FLAG_REG) ? "reg" : "logic");

        std::string str = name;
        if (type == EXPR_TEMPLATE) {
            str += " #(";
            bool first = true;
            for (auto& param : sub) {
                if (!first) {
                    str += ",";
                }
                str += param.str();
                first = false;
            }
            str += ")";
        }
        if (name == "cpphdl::logic") {
            str = logic + suffix + "[" + size + "-1:0]";
        } else
        if (name == "cpphdl::u") {
            str = logic + suffix + "[" + size + "-1:0]";
        } else
        if (name == "cpphdl::u1") {
            str = logic + suffix;
        } else
        if (name == "cpphdl::u8") {
            str = logic + suffix + "[7:0]";
        } else
        if (name == "cpphdl::u16") {
            str = logic + suffix + "[15:0]";
        } else
        if (name == "cpphdl::u32") {
            str = logic + suffix + "[31:0]";
        } else
        if (name == "cpphdl::u64") {
            str = logic + suffix + "[63:0]";
        } else
        if (name == "bool") {
            str = logic;
        } else
        if (name == "uint8_t") {
            str = logic + "[7:0]";
        } else
        if (name == "uint16_t") {
            str = logic + "[15:0]";
        } else
        if (name == "uint32_t") {
            str = logic + "[31:0]";
        } else
        if (name == "uint64_t") {
            str = logic + "[63:0]";
        } else
        if (name.compare(0, 4, "short") == 0) {
            str = "shortint";
        } else
        if (name == "int") {
            str = "integer";
        } else
        if (name.compare(0, 4, "long") == 0) {
            str = "longint";
        } else
        if (name == "unsigned short") {
            str = "unsigned shortint";
        } else
        if (name == "unsigned long") {
            str = "unsigned longint";
        } else
        if (name.compare(0, 8, "unsigned") == 0) {
            str = "unsigned int";
        }
        return std::string() + prefix + str;
    }

};


}
