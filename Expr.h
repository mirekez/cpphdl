#pragma once

#include <string>
#include <vector>

namespace cpphdl
{

struct Expr
{
    std::string value;

    enum {
        EXPR_UNKNOWN,
        EXPR_TYPE,
        EXPR_TEMPLATE,
        EXPR_VALUE,
        EXPR_CALL,
        EXPR_BINARY,
        EXPR_DECLARE
    } type;

    std::vector<Expr> sub;

    std::string str()
    {
        std::string ret;
        switch (type)
        {
            case EXPR_UNKNOWN:
                return std::string("(unknown: ") + value + ")";
            case EXPR_TYPE:
                return typeToSV(value);
            case EXPR_TEMPLATE:
                ASSERT(sub.size() == 1);
                return typeToSV(value, sub[0].type == EXPR_VALUE ? sub[0].value : sub[0].str());
            case EXPR_VALUE:
                return typeToSV(value);
            case EXPR_CALL:
                return "unsupported";
            case EXPR_BINARY:
                ASSERT(sub.size()==2);
                return std::string("(binary: ") + sub[0].str() + " " + value + " " + sub[1].str() + ")";
            break;
            case EXPR_DECLARE:
                ASSERT(0);
            break;
        }
        return "missed case";
    }

    std::string typeToSV(std::string type, std::string size = "")
    {
        if (type == "logic") {
            return std::string("logic[" + size + "-1:0]");
        }
        if (type == "u") {
            return std::string("logic[" + size + "-1:0]");
        }
        if (type == "u1") {
            return std::string("logic[0:0]");
        }
        if (type == "u8") {
            return std::string("logic[7:0]");
        }
        if (type == "u16") {
            return std::string("logic[15:0]");
        }
        if (type == "u32") {
            return std::string("logic[31:0]");
        }
        if (type == "u64") {
            return std::string("logic[63:0]");
        }
        if (type == "bool") {
            return std::string("logic[0:0]");
        }
        if (type == "uint8_t") {
            return std::string("logic[7:0]");
        }
        if (type == "uint16_t") {
            return std::string("logic[15:0]");
        }
        if (type == "uint32_t") {
            return std::string("logic[31:0]");
        }
        if (type == "uint64_t") {
            return std::string("logic[63:0]");
        }
        if (type == "int") {
            return std::string("integer");
        }
        return "unknown";
    }

};


}
