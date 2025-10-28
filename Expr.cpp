#include "Expr.h"
#include "Method.h"
#include "Module.h"
#include "Debug.h"

using namespace cpphdl;

std::string Expr::str(std::string prefix, std::string size)
{
    std::string indent_str;
    if (indent) {
        for (int i=0; i < indent; ++i) {
            indent_str += "    ";
        }
    }
    indent_str += prefix;

    switch (type)
    {
        case EXPR_EMPTY:
            return indent_str + "";
        case EXPR_TYPE:
            return indent_str + typeToSV(value, size);
        case EXPR_VALUE:
            return indent_str + value;
        case EXPR_VAR:
            return indent_str + value;
        case EXPR_STRING:
            size_t pos;
            if ((pos = value.find("%s")) != (size_t)-1) {
                value = value.replace(pos, 2, "%m");
            }
            return indent_str + value;
        case EXPR_PARAM:
            return indent_str + value;
        case EXPR_TEMPLATE:
            if (value == "cpphdl::array") {
                ASSERT(sub.size() >= 2);
                return indent_str + sub[0].str("", size + "[" + sub[1].str() + "-1:0]");
            }
            ASSERT(sub.size() >= 1);
            return indent_str + typeToSV(value, size + "[" + sub[0].str() + "-1:0]");
        case EXPR_ARRAY:
            ASSERT(sub.size() >= 2);
            return indent_str + sub[1].str("", size + "[" + sub[0].str() + "-1:0]");
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

            std::string func = value;
            if (func == "clog2") {
                func = "$clog2";
            }
            if (func == "printf") {
                 func = "$write";
            }
            if (func == "exit") {
                return indent_str + "$finish()";
            }

            std::string str = func + "(";
            bool first = true;
            for (auto& arg : sub) {
                if (arg.value != "clk") { //arg.type != EXPR_MEMBERCALL) {
                    str += (first?"":", ") + arg.str();
                    first = false;
                }
            }
            str += ")";
            return indent_str + str;
        }
        // here we have member as first sub element, and it has object as it's sub element
        case EXPR_MEMBERCALL:
        {
//            ASSERT(sub.size()>=1);
            if (value.find("operator") == 0) {
                return indent_str + sub[0].str();
            }
//            if (sub[0].str() == "this") {
//                return indent_str + value + "()";
//            }

            if (sub.size() != 0 && sub[0].sub.size() != 0 && sub[0].value == "clr") {
                return indent_str + sub[0].sub[0].str() + " = '0";
            }
//            if (value == "set") {
//                return indent_str + sub[0].str() + " = '0";
//            }

            if (sub.size() == 0 || sub[0].sub.size() == 0 || sub[0].sub[0].value != "this") {  // we need only this->calls, no member calls
                return "";
            }

            std::string member = value;
            std::string str = "(";
            bool first = true;
            for (auto& arg : sub) {
                if (arg.type == EXPR_MEMBER) {
                    member = arg.str();
                    continue;
                }

                if (arg.value != "clk") { //arg.type != EXPR_MEMBERCALL) {
                    str += (first?"":", ") + arg.str();
                    first = false;
                }
            }
            str += ")";

            return indent_str + member + str;
        }
        case EXPR_MEMBER:
            ASSERT(sub.size()==1);
            if (value.find("operator") == 0) {
                return indent_str + sub[0].str();
            }
            if (sub[0].str() == "this") {
                return indent_str + value;
            }

            if (value == "next") {
                return indent_str + sub[0].str();
            }

            return indent_str + sub[0].str() + "__" + value;
        case EXPR_RETURN:
            return (flags&FLAG_NORETURN) ? "" : indent_str + "disable " + currMethod->name;//(sub.size()==1 ? "return " + sub[0].str() : "return");
        case EXPR_BINARY:
            ASSERT(sub.size()==2);
            return indent_str + sub[0].str() + (value=="*" || value=="/" ? value :" " + value + " ") + sub[1].str();
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
        case EXPR_TRAIT:
            ASSERT(sub.size()==1);
            return indent_str + value + "(" + sub[0].str() + ")";
        case EXPR_FOR:
        {
            ASSERT(sub.size()==4);
            std::string str;
            str += indent_str + "for (" + sub[0].str() + ";" + sub[1].str() + ";" + sub[2].str() + ") begin\n";
            sub[3].indent = indent + 1;
            sub[3].flags = flags;
            str += sub[3].str(prefix);
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
            sub[1].flags = flags;
            str += sub[1].str(prefix);
            if (!sub[1].isMultiline()) {
                str += ";\n";
            }
            str += indent_str + "end\n";
            return str;
        }
        case EXPR_IF:
        {
            ASSERT(sub.size()>=1);
            if (sub[0].traverseIf( [](Expr& t) { return t.value == "clk";} )) {
                return "";
            }
            std::string str;
            str += indent_str + "if (" + sub[0].str() + ") begin\n";
            if (sub.size() > 1) {
                sub[1].indent = indent + 1;
                sub[1].flags = flags;
                str += sub[1].str(prefix);
                if (!sub[1].isMultiline()) {
                    str += ";\n";
                }
            }
            str += indent_str + "end\n";
            if (sub.size() > 2) {
                str += indent_str + "else begin\n";
                sub[2].indent = indent + 1;
                sub[2].flags = flags;
                str += sub[2].str(prefix);
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
                auto s = stmt.str(prefix);
                if (s.length() && !stmt.isMultiline()) {
                    s += ";\n";
                }
                str += s;
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

std::string Expr::typeToSV(std::string name, std::string size)
{
    std::string logic = (flags&FLAG_WIRE) ? "wire" : ((flags&FLAG_REG) ? "reg" : "logic");

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
        str = logic + size;
    } else
    if (name == "cpphdl::u") {
        str = logic + size;
    } else
    if (name == "cpphdl::u1") {
        str = logic + size;
    } else
    if (name == "cpphdl::u8") {
        str = logic + size + "[7:0]";
    } else
    if (name == "cpphdl::u16") {
        str = logic + size + "[15:0]";
    } else
    if (name == "cpphdl::u32") {
        str = logic + size + "[31:0]";
    } else
    if (name == "cpphdl::u64") {
        str = logic + size + "[63:0]";
    } else
    if (name == "bool") {
        str = logic + size;
    } else
    if (name == "_Bool") {
        str = logic + size;
    } else
    if (name == "uint8_t") {
        str = logic + size + "[7:0]";
    } else
    if (name == "uint16_t") {
        str = logic + size + "[15:0]";
    } else
    if (name == "uint32_t") {
        str = logic + size + "[31:0]";
    } else
    if (name == "uint64_t") {
        str = logic + size + "[63:0]";
    } else
    if (name.compare(0, 4, "short") == 0) {
        str = "shortint" + size;
    } else
    if (name == "int") {
        str = "integer" + size;
    } else
    if (name.compare(0, 4, "long") == 0) {
        str = "longint" + size;
    } else
    if (name == "unsigned short") {
        str = "unsigned shortint" + size;
    } else
    if (name == "unsigned long") {
        str = "unsigned longint" + size;
    } else
    if (name.compare(0, 8, "unsigned") == 0) {
        str = "unsigned int" + size;
    }
    return str;
}
