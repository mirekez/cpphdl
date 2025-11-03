#include "Expr.h"
#include "Method.h"
#include "Module.h"
#include "Field.h"
#include "Debug.h"

using namespace cpphdl;

std::string Expr::str(std::string prefix, std::string suffix)
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
            return indent_str + prefix + "";
        case EXPR_TYPE:
            return indent_str + prefix + typeToSV(value, suffix);
        case EXPR_VALUE:
            return indent_str + prefix + value;
        case EXPR_VAR:
            return indent_str + prefix + value;
        case EXPR_STRING:
            replacePrint(value);
            return indent_str + prefix + value;
        case EXPR_PARAM:
            return indent_str + prefix + value;
        case EXPR_TEMPLATE:
            if (value == "cpphdl::array") {
                ASSERT(sub.size() >= 2);
                return indent_str + prefix + sub[0].str("", suffix + "[" + sub[1].str() + "-1:0]");
            }
            if (sub.size()) {
                return indent_str + prefix + typeToSV(value, suffix + "[" + sub[0].str() + "-1:0]");
            }
            return indent_str + prefix + typeToSV(value);
        case EXPR_ARRAY:
            ASSERT(sub.size() >= 2);
            return indent_str + prefix + sub[1].str("", suffix + "[" + sub[0].str() + "-1:0]");
        case EXPR_CALL:
        {
            bool noBrackets = false;
            int skipArgs = 0;
            std::string func = value;
            if (func == "clog2") {
                func = "$clog2";
            }
            if (func == "printf") {
                 func = "$write";
            }
            if (func == "fprintf") {
                 skipArgs = 1;
                 func = "$write";
            }
            if (func == "print") {
                 func = "$write";
            }
            if (func == "exit") {
                return indent_str + "$finish()";
            }
            if (func == "fflush") {
                return "";
            }
            if (func.find("std::basic_format_string") == 0) {
                ASSERT(sub.size()>0);
                replacePrint(sub[0].value);
                noBrackets = true;
                func = "";
            }

            std::string str = func;
            if (!noBrackets) {
                str += "(";
            }
            bool first = true;
            for (size_t i=skipArgs; i < sub.size(); ++i) {
                if (sub[i].value != "clk") {
                    str += (first?"":", ") + sub[i].str();
                    first = false;
                }
            }
            if (!noBrackets) {
                str += ")";
            }
            return indent_str + prefix + str;
        }
        case EXPR_OPERATORCALL:
        {
            if (value == "=" || value == "+" || value == "-" || value == "*" || value == "/") {
                ASSERT(sub.size() >= 2);
                return indent_str + prefix + sub[0].str() + " " + value + " " + sub[1].str();
            }
            if (value == "[]") {
                ASSERT(sub.size() >= 2);
                return indent_str + sub[0].str() + "[" + sub[1].str() + "]";
            }
            if (value == "*") {  // we dont need pointers int Verilog
                ASSERT(sub.size() >= 1);
                return indent_str + sub[0].str();
            }
            if (value == "&") {  // we dont need pointers int Verilog
                ASSERT(sub.size() >= 1);
                return indent_str + sub[0].str();
            }

            std::string str = value + "(";
            bool first = true;
            for (auto& arg : sub) {
                if (arg.value != "clk") {
                    str += (first?"":", ") + arg.str();
                    first = false;
                }
            }
            str += ")";
            return indent_str + prefix + str;
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

            if (sub.size() >= 1 && sub[0].sub.size() != 0 && sub[0].value == "clr") {
                return indent_str + sub[0].sub[0].str() + " = '0";
            }
            if (sub.size() >= 2 && sub[0].sub.size() != 0 && sub[0].value == "set") {
                return indent_str + sub[0].sub[0].str() + " = " + sub[1].str();
            }
            if (sub.size() >= 1 && sub[0].sub.size() != 0 && sub[0].value == "format") {
                return indent_str + sub[0].sub[0].str();
            }
            if (sub.size() == 0 || sub[0].sub.size() == 0 || sub[0].sub[0].value != "this") {  // we need only this->calls, no member calls like work(), connect()
                return "";
            }
            if (sub.size() >= 1 && sub[0].sub.size() != 0 && sub[0].value.size() > 10 && sub[0].value.find("_comb_func") == sub[0].value.size()-10) {
                std::string str = sub[0].value;
                return indent_str + str.replace(str.find("_comb_func") + 5, 5, "");
            }

            std::string member = value;
            std::string str = "(";
            bool first = true;
            for (auto& arg : sub) {
                if (first && arg.type == EXPR_MEMBER) {
                    member = arg.str();
                    continue;
                }

                if (arg.value != "clk") { //arg.type != EXPR_MEMBERCALL) {
                    str += (first?"":", ") + arg.str();
                    first = false;
                }
            }
            str += ")";

            return indent_str + prefix + member + str;
        }
        case EXPR_MEMBER:
        {
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
            if (sub[0].type == EXPR_INDEX) {
                ASSERT(sub[0].sub.size() > 1);
                std::string delim = "";
                if (any_of(currModule->members.begin(), currModule->members.end(), [&](auto& elem){ return elem.name == sub[0].sub[0].str(); } )) {
                    delim = "__";
                }
                return indent_str + prefix + sub[0].str("", delim + value);
            }
            std::string delim = std::string(anonymous?".anon":"") + ".";
            if (any_of(currModule->members.begin(), currModule->members.end(), [&](auto& elem){ return elem.name == sub[0].value; } )) {
                delim = "__";
            }
            return indent_str + prefix + sub[0].str() + delim + value;
        }
        case EXPR_BINARY:
            ASSERT(sub.size()==2);
            return indent_str + prefix + sub[0].str() + (value=="*" || value=="/" ? value :" " + value + " ") + sub[1].str();
        case EXPR_UNARY:
            ASSERT(sub.size()==1);
            if (value == "*") {  // we dont need pointers int Verilog
                return indent_str + sub[0].str();
            }
            if (value == "&") {  // we dont need pointers int Verilog
                return indent_str + sub[0].str();
            }
            if (value == "++") {
                return indent_str + sub[0].str() + "=" + sub[0].str() + "+1";
            }
            return indent_str + prefix + value + sub[0].str();
        case EXPR_COND:
            ASSERT(sub.size()==3);
            return indent_str + prefix + sub[0].str() + " ? " + sub[1].str() + " : " + sub[2].str();
        case EXPR_INDEX:
            ASSERT(sub.size()==2);
            return indent_str + prefix + sub[0].str() + suffix + "[" + sub[1].str() + "]";
        case EXPR_CAST:
            ASSERT(sub.size()==1);
            return indent_str + sub[0].str();
        case EXPR_PAREN:
            ASSERT(sub.size()==1);
            if (sub[0].type == EXPR_VAR || sub[0].type == EXPR_MEMBER || (sub[0].type == EXPR_UNARY && sub[0].value == "*")) {
                return indent_str + sub[0].str();
            }
            return indent_str + "(" + sub[0].str() + ")";
        case EXPR_INIT:
            ASSERT(sub.size()==1);
            return indent_str + sub[0].str();
        case EXPR_TRAIT:
            ASSERT(sub.size()==1);
            if (value == "sizeof") {
                return indent_str + "($bits" + "(" + sub[0].str() + ")/8)";
            }
            return indent_str + value + "(" + sub[0].str() + ")";
        case EXPR_RETURN:
            return ((flags&FLAG_RETURN) && sub.size()==1) ? (indent_str + "return " + sub[0].str()) : ((flags&FLAG_NORETURN) ? "" : indent_str + "disable " + currMethod->name);//(sub.size()==1 ? "return " + sub[0].str() : "return");
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
            std::string param;
            if (sub[0].traverseIf( [](Expr& e, std::string& param) { return e.value == "clk";}, param )) {
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
    if (name == "signed char") {
        str = "byte" + size;
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
    if (name == "unsigned char") {
        str = "logic" + size + "[7:0]";
    } else
    if (name == "unsigned short") {
        str = "logic" + size + "[15:0]";
    } else
    if (name == "unsigned long") {
        str = "logic" + size + "[63:0]";
    } else
    if (name.compare(0, 8, "unsigned") == 0) {
        str = "logic" + size + "[31:0]";
    } else {
        str += size;
    }

    size_t pos;
    while ((pos = str.find("::")) != (size_t)-1) {
        str.replace(pos, 2, "__");
    }
    if ((pos = str.find("struct ")) != (size_t)-1) {
        str.replace(pos, 7, "");
    }
    return str;
}

std::string Expr::debug()
{
    std::string str;
    switch(type) {
        case EXPR_EMPTY: str += "EXPR_EMPTY"; break;
        case EXPR_TYPE: str += "EXPR_TYPE"; break;
        case EXPR_VALUE: str += "EXPR_VALUE"; break;
        case EXPR_VAR: str += "EXPR_VAR"; break;
        case EXPR_STRING: str += "EXPR_STRING"; break;
        case EXPR_PARAM: str += "EXPR_PARAM"; break;
        case EXPR_TEMPLATE: str += "EXPR_TEMPLATE"; break;
        case EXPR_ARRAY: str += "EXPR_ARRAY"; break;
        case EXPR_CALL: str += "EXPR_CALL"; break;
        case EXPR_MEMBERCALL: str += "EXPR_MEMBERCALL"; break;
        case EXPR_OPERATORCALL: str += "EXPR_OPERATORCALL"; break;
        case EXPR_MEMBER: str += "EXPR_MEMBER"; break;
        case EXPR_BINARY: str += "EXPR_BINARY"; break;
        case EXPR_UNARY: str += "EXPR_UNARY"; break;
        case EXPR_COND: str += "EXPR_COND"; break;
        case EXPR_INDEX: str += "EXPR_INDEX"; break;
        case EXPR_CAST: str += "EXPR_CAST"; break;
        case EXPR_PAREN: str += "EXPR_PAREN"; break;
        case EXPR_INIT: str += "EXPR_INIT"; break;
        case EXPR_TRAIT: str += "EXPR_TRAIT"; break;
        case EXPR_RETURN: str += "EXPR_RETURN"; break;
        case EXPR_FOR: str += "EXPR_FOR"; break;
        case EXPR_WHILE: str += "EXPR_WHILE"; break;
        case EXPR_IF: str += "EXPR_IF"; break;
        case EXPR_BODY: str += "EXPR_BODY"; break;
        case EXPR_UNKNOWN: str += "EXPR_UNKNOWN"; break;
        default: str += "EXPR_???"; break;
    }

    str += " " + value + (sub.size()?"(":"");
    bool first = true;
    for (auto& expr : sub) {
        str += (!first ? ", " : " ");
        str += expr.debug();
        first = false;
    }
    str += (sub.size()?")":"");
    return str;
}

void Expr::replacePrint(std::string& str)
{
    size_t pos = 0;
    while (true) {
        if ((pos = str.find("{}", pos)) != (size_t)-1) {
            str.replace(pos, 2, "%x");
            pos += 2;
        } else
        if ((pos = str.find("{:x}", pos)) != (size_t)-1) {
            str.replace(pos, 4, "%x");
            pos += 2;
        } else
        if ((pos = str.find("{:d}", pos)) != (size_t)-1) {
            str.replace(pos, 4, "%d");
            pos += 2;
        } else
        if ((pos = str.find("%s")) != (size_t)-1) {
            str.replace(pos, 2, "%m");
        } else
        if ((pos = str.find("%s")) != (size_t)-1) {
            str.replace(pos, 2, "%m");
        }
        else {
            break;
        }
    }
}
