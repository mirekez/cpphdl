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
        EXPR_VAR,
        EXPR_VALUE,
        EXPR_CONST,
        EXPR_PARAM,
        EXPR_TEMPLATE,
        EXPR_CALL,
        EXPR_MEMBERCALL,
        EXPR_MEMBER,
        EXPR_RETURN,
        EXPR_BINARY,
        EXPR_UNARY,
        EXPR_COND,
        EXPR_INDEX,
        EXPR_CAST,
        EXPR_PAREN,
        EXPR_INIT,
        EXPR_ASSIGN,
        EXPR_DECLARE,
        EXPR_TRAIT,
        EXPR_FOR,
        EXPR_WHILE,
        EXPR_IF,
        EXPR_BODY,
        EXPR_UNKNOWN
    } type = EXPR_EMPTY;

    std::vector<Expr> sub;

    // methods
    int indent = 0;

    std::string str(unsigned flags = 0, std::string prefix = "", std::string suffix = "")
    {
        std::string indent_str;
        if (indent) {
            for (int i=0; i < indent; ++i) {
                indent_str += "    ";
            }
        }
        switch (type)
        {
            case EXPR_EMPTY:
                return indent_str + "";
            case EXPR_TYPE:
                return indent_str + typeToSV(value, flags, prefix, suffix);
            case EXPR_VAR:
                return indent_str + value;
            case EXPR_VALUE:
                return indent_str + value;
            case EXPR_CONST:
                return indent_str + value;
            case EXPR_PARAM:
                return indent_str + value;
            case EXPR_TEMPLATE:
                ASSERT(sub.size() >= 1);
                return indent_str + typeToSV(value, flags, prefix, suffix, sub[0].type == EXPR_VALUE ? sub[0].value : sub[0].str(flags,prefix,suffix));
            case EXPR_CALL:
            {
                 if (value == "=") {
                     ASSERT(sub.size() >= 2);
                     return indent_str + sub[0].str() + " " + "=" + " " + sub[1].str();
                 }
                 if (value == "[]") {
                     ASSERT(sub.size() >= 2);
                     return indent_str + sub[0].str() + "[" + sub[1].str() + "]";
                 }

                 std::string str = value + "(";
                 bool first = true;
                 for (auto& arg : sub) {
                     str += (first?"":", ") + arg.str();
                     first = false;
                 }
                 str += ")";
                 return indent_str + str;
            }
            case EXPR_MEMBERCALL:
                ASSERT(sub.size()==1);
                if (value.find("operator") == 0) {
                    return indent_str + sub[0].str();
                }
                if (sub[0].str() == "this") {
                    return indent_str + value + "()";
                }
                return indent_str + sub[0].str() + "." + value + "()";
            case EXPR_MEMBER:
                ASSERT(sub.size()==1);
                if (value.find("operator") == 0) {
                    return indent_str + sub[0].str();
                }
                if (sub[0].str() == "this") {
                    return indent_str + value;
                }
                return indent_str + sub[0].str() + "." + value;
            case EXPR_RETURN:
                return indent_str + (sub.size()==1 ? "return " + sub[0].str() : "return");
            case EXPR_BINARY:
                ASSERT(sub.size()==2);
                return indent_str + sub[0].str() + " " + value + " " + sub[1].str();
            case EXPR_UNARY:
                ASSERT(sub.size()==1);
                if (value == "*") {
                    return indent_str + sub[0].str();
                }
                if (value == "&") {
                    return indent_str + sub[0].str();
                }
                return indent_str + value + sub[0].str();
            case EXPR_COND:
                ASSERT(sub.size()==3);
                return indent_str + sub[0].str() + " ? " + sub[1].str() + " : " + sub[2].str();
            case EXPR_INDEX:
                ASSERT(sub.size()==2);
                return indent_str + sub[0].str() + "[" + sub[1].str() + "]";
            case EXPR_CAST:
                ASSERT(sub.size()==1);
                return indent_str + sub[0].str();
            case EXPR_PAREN:
                ASSERT(sub.size()==1);
                return indent_str + "(" + sub[0].str() + ")";
            case EXPR_INIT:
                ASSERT(sub.size()==1);
                return indent_str + sub[0].str();
            case EXPR_ASSIGN:
                ASSERT(sub.size()==2);
                return indent_str + std::string("(assign: ") + sub[0].str() + " = " + sub[1].str() + ")";
            case EXPR_DECLARE:
                return indent_str + value;
            case EXPR_TRAIT:
                ASSERT(sub.size()==1);
                return indent_str + value + "(" + sub[0].str() + ")";
            case EXPR_FOR:
            {
                ASSERT(sub.size()==4);
                std::string str;
                str += indent_str + "for (" + sub[0].str() + ";" + sub[1].str() + ";" + sub[2].str() + ") begin\n";
                sub[3].indent = indent + 1;
                str += sub[3].str();
                if (!sub[3].isMultiline()) {
                    str += ";\n";
                }
                str += indent_str + "end\n";
                return str;
            }
            case EXPR_WHILE:
            {
                ASSERT(sub.size()==2);
                std::string str;
                str += indent_str + "while (" + sub[0].str() + ") begin\n";
                sub[1].indent = indent + 1;
                str += sub[1].str();
                if (!sub[1].isMultiline()) {
                    str += ";\n";
                }
                str += indent_str + "end\n";
                return str;
            }
            case EXPR_IF:
            {
                ASSERT(sub.size()>=1);
                std::string str;
                str += indent_str + "if (" + sub[0].str() + ") begin\n";
                if (sub.size() > 1) {
                    sub[1].indent = indent + 1;
                    str += sub[1].str();
                    if (!sub[1].isMultiline()) {
                        str += ";\n";
                    }
                }
                str += indent_str + "end\n";
                if (sub.size() > 2) {
                    str += indent_str + "else begin\n";
                    sub[2].indent = indent + 1;
                    str += sub[2].str();
                    if (!sub[2].isMultiline()) {
                        str += ";\n";
                    }
                    str += indent_str + "end\n";
                }
                return str;
            }
            case EXPR_BODY:
            {
                std::string str;
                for (auto& stmt : sub) {
                    stmt.indent = indent;
                    str += stmt.str();
                    if (!stmt.isMultiline()) {
                        str += ";\n";
                    }
                }
                return str;
            }
            case EXPR_UNKNOWN:
                return std::string("(unknown: ") + value + ")";
            default:
                return "missed case";
        }
        ASSERT(0);
        return "";
    }

    bool isMultiline()
    {
        return type == EXPR_FOR || type == EXPR_WHILE || type == EXPR_IF || type == EXPR_BODY;
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
