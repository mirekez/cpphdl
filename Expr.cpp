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
        case EXPR_NONE:
            return "";
        case EXPR_DECL:
        {
            ASSERT(sub.size() >= 1);
            std::string str;
            if (sub[0].type == EXPR_TEMPLATE && sub[0].value == "cpphdl_memory") {
                ASSERT1(sub[0].sub.size() >= 3, std::string("cpphdl_memory subs size = ") + std::to_string(sub.size()) );
                sub[0].sub[0].flags = Expr::FLAG_REG;
                return indent_str + prefix + sub[0].sub[0].str("", std::string("[") + sub[0].sub[1].str() + "-1:0]") + " " + escapeIdentifier(value) + "[" + sub[0].sub[2].str() + "]";
            } else
            if (sub[0].type == EXPR_NUM) {
                str += indent_str + prefix + escapeIdentifier(value) + " = " + sub[0].str();  // const initializer
            }
            else
            if (sub[0].type == EXPR_ARRAY) {
                str += indent_str + prefix + sub[0].str(std::string(" ") + escapeIdentifier(value));  // use name as suffix
            }
            else {
                str += indent_str + prefix + sub[0].str() + " " + escapeIdentifier(value);
            }
            if (sub.size() == 2) {
                str += "; " + value + " = " + sub[1].str();
            }
            return str;
        }
        case EXPR_TYPE:
            if (value.find("_IO_FILE") != (size_t)-1) {
                value = "int";
            }
            return indent_str + prefix + typeToSV(value, suffix);
        case EXPR_NUM:
            if (sub.size()) {  // if we have expression for this number parameter (from template parameters)
                return indent_str + prefix + sub[0].str() + suffix;
            }
            return indent_str + prefix + (value == "true"? "1" : (value == "false"? "0" : value)) + suffix;
        case EXPR_VAR:
            if (value.find("__ONE") == 0) {
                return indent_str + prefix + "'1" + suffix;
            }
            if (value.find("__ZERO") == 0) {
                return indent_str + prefix + "'0" + suffix;
            }
            return indent_str + prefix + escapeIdentifier(value) + suffix;
        case EXPR_STRING:
            replacePrint(value);
            return indent_str + prefix + value;
        case EXPR_PARAM:
            if (sub.size() && !(flags&FLAG_SPECVAL)) {  // if we have expression for this number parameter (from template parameters)
                return indent_str + prefix + sub[0].str() + suffix;
            }
            return indent_str + prefix + (value == "true"? "1" : (value == "false"? "0" : value)) + suffix;
//            return indent_str + prefix + (((flags&FLAG_SPECVAL)&&sub.size())?sub[0].str():value);
        case EXPR_PACK:
            return indent_str + prefix + value + suffix;
        case EXPR_TEMPLATE:
        {
            ASSERT(sub.size());

            if (value == "cpphdl_reg") {
                ASSERT1(sub.size() >= 1, std::string("cpphdl_reg subs size = ") + std::to_string(sub.size()) );
                sub[0].flags = Expr::FLAG_REG;
                return indent_str + prefix + sub[0].str("", suffix);
            }
            if (value == "cpphdl_array") {
                ASSERT(sub.size() >= 2);
                return indent_str + prefix + sub[0].str("", suffix + "[" + sub[1].str() + "-1:0]");
            }
            if (value.find("cpphdl_") == 0 && sub.size()) {
                return indent_str + prefix + typeToSV(value, suffix + "[" + sub[0].str() + "-1:0]");
            }

            std::string typeSpec;
            bool first = true;
            for (size_t i=0; i < sub.size()-1; ++i) {
                if (sub[i].type != EXPR_PARAM) {
                    if (!first) {
                        typeSpec += "_";
                    }
                    typeSpec += sub[i].str();
                    first = false;
                }
            }

            if (value.find("get") == 0 && sub.size() && sub[sub.size()-1].sub.size()) {  // we treat get template as tuple get, probably later to check std:: or this
                return indent_str + prefix + sub[sub.size()-1].sub[0].value + "_tuple_" + typeSpec;
            }

            return indent_str + prefix + typeToSV(sub[sub.size()-1].str(prefix, typeSpec), suffix);
        }
        case EXPR_ARRAY:
            ASSERT(sub.size() >= 2);

/*            if (sub[1].type == Expr::EXPR_TYPE || sub[1].type == Expr::EXPR_TEMPLATE) {  // do we need this??
                ASSERT1(sub.size() >= 1, std::string("EXPR_ARRAY subs size = ") + std::to_string(sub.size()) );
                if (sub[1].value == "cpphdl_reg") {
                    ASSERT1(sub[1].sub.size() >= 1, std::string("cpphdl_reg subs size = ") + std::to_string(sub[1].sub.size()) );
                    sub[1].flags = Expr::FLAG_REG;
                    sub[1].sub[0].indent = indent;
                    return indent_str + prefix + sub[1].sub[0].str() + " " + suffix + "[" + sub[0].str() + "]";
//                } else
//                if (isStruct) {
//                    sub[1].indent = indent;
//                    return indent_str + prefix + str() << " " << suffix;
                }
                else {
                    sub[1].indent = indent;
                    return indent_str + prefix + sub[1].str() + " " + suffix + "[" + sub[0].str() + "]";
                }
            }
*/
            return indent_str + prefix + sub[1].str("", suffix + "[" + sub[0].str() + "-1:0]");
        case EXPR_CALL:
        {
            if ((flags&FLAG_NOCALLS)) {
                return "";
            }
            bool noBrackets = false;
            int skipArgs = 0;
            std::string func = value;
            if (func == "clog2") {
                func = "$clog2";
            }
            if (func == "printf") {
                 func = "$write";
            }
            if (func == "fopen") {
                 func = "$fopen";
            }
            if (func == "fclose") {
                 func = "$fclose";
            }
            if (func == "fprintf") {
                 skipArgs = 1;
                 func = "$fwrite";
            }
            if (func == "printf") {
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
            if (str_ending(value, "_comb_func")) {
                std::string str = value;
                return indent_str + str.replace(str.rfind("_comb_func") + 5, 5, "");
            }

            std::string str = escapeIdentifier(func) + suffix;
            if (!noBrackets) {
                str += "(";
            }
            bool first = true;
            for (size_t i=skipArgs; i < sub.size(); ++i) {
                if (sub[i].value != "clk" && sub[i].str().find("__inst_name") == (size_t)-1 && sub[i].type != EXPR_NONE) {
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
            if (value == "=" || value == "+" || value == "-" || value == "*" || value == "/" || value == "==" || value == "!=" || value == ">"
                 || value == ">=" || value == "<" || value == "<=") {
                ASSERT(sub.size() >= 2);
                return indent_str + prefix + sub[0].str() + " " + value + " " + sub[1].str();
            }
            if (value == "[]") {
                ASSERT(sub.size() >= 2);
                return indent_str + sub[0].str() + "[" + sub[1].str() + "]";
            }
            if (value == "()") {
                ASSERT(sub.size() >= 1);
                return indent_str + sub[0].str();
            }
            if (value == "*" && sub.size() == 1) {  // we dont need pointers int Verilog
                ASSERT(sub.size() >= 1);
                return indent_str + sub[0].str();
            }
            if (value == "&" && sub.size() == 1) {  // we dont need pointers int Verilog
                return indent_str + sub[0].str();
            }
            if (value == "&" && sub.size() == 2) {
                return indent_str + sub[0].str() + " & " + sub[1].str();
            }
            if (value == "|" && sub.size() == 2) {
                return indent_str + sub[0].str() + " | " + sub[1].str();
            }
            if (value == "^" && sub.size() == 2) {
                return indent_str + sub[0].str() + " ^ " + sub[1].str();
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

            if (sub.size() >= 1 && value == "clr" && sub[0].type != EXPR_VAR/*this*/) { // this means the call of external struct
                return indent_str + sub[0].str() + " = '0";
            }
            if (sub.size() >= 2 && value == "set" && sub[0].type != EXPR_VAR/*this*/) {
                return indent_str + sub[0].str() + " = " + sub[1].str();
            }
            if (sub.size() >= 1 && value == "format" && sub[0].type != EXPR_VAR/*this*/) {
                return indent_str + sub[0].str();
            }
            if (sub.size() >= 3 && value == "bits" && sub[0].type != EXPR_VAR/*this*/) {
                return indent_str + sub[0].str() + "[" + sub[2].str() + " +:(" + Expr{"-", EXPR_OPERATORCALL, {sub[1],sub[2]}}.simplify().str() + ")+1" + "]";
            }
            if ((value == "_connect" || value == "_strobe" || value == "_work") && sub.size() && sub[0].type != EXPR_VAR/*this*/) {  // never need this functions
                return "";
            }
            if (sub.size() >= 1 && str_ending(value, "_comb_func")) {
                std::string str = value;
                str_replace(str, "_comb_func", "_comb");
                return indent_str + str;
            }
            if ((flags&FLAG_NOCALLS)) {  // except _comb_func() calls
                return "";
            }

            std::string str = "(";

//            bool skipfirst = false;
            bool first = true;
            std::string member = escapeIdentifier(value) + suffix;
            if (sub[0].type == EXPR_MEMBER || sub[0].type == EXPR_PACK) {  // member call - can be only member's port access now, no args
                if (sub[0].str() != "_this") {
                    flags |= FLAG_CALL;  // it will swap places of member and method, and add "("
                    str = "";
                    first = false;
                }
                member = sub[0].str() + "__" + escapeIdentifier(value) + suffix;
                return indent_str + prefix + member;
//                skipfirst = true;
            }

            for (auto& arg : sub) {
//                if (skipfirst) {
//                    skipfirst = false;
//                    continue;
//                }
                if (arg.value != "clk" && arg.type != EXPR_NONE) {
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
            if ((sub[0].type == EXPR_NONE && !(flags&FLAG_USETHIS)) || (flags&FLAG_NOBASE)) {  // no this inside, no need to "."
                return indent_str + value;
            }
            if (value == "next"/* || (value == "" && !(flags&FLAG_ANON))*/) {
                return indent_str + sub[0].str();
            }
            if (sub[0].type == EXPR_INDEX) {  // ?
                ASSERT(sub[0].sub.size() > 1);
                std::string delim = "";
                if (any_of(currModule->members.begin(), currModule->members.end(), [&](auto& elem){ return elem.name == sub[0].sub[0].str(); } )) {
                    delim = "__";
                }
                return indent_str + prefix + sub[0].str("", delim + value);
            }
            if ((flags&FLAG_CALL)) {  // for all we need to extract this as first parameter to function  // ?
                return indent_str + prefix + value + "(" + sub[0].str();
            }
            std::string delim = std::string((flags&FLAG_ANON)?"_":"") + ".";
            if (any_of(currModule->members.begin(), currModule->members.end(), [&](auto& elem){ return elem.name == sub[0].str(); } )) {
                delim = "__";
            }
            return indent_str + prefix + sub[0].str() + delim + value;
        }
        case EXPR_BINARY:
            ASSERT(sub.size()==2);
            if (value == "=") {
                auto& check = sub[0];
                while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
                    check = check.sub[0];
                }
                if (check.type == EXPR_PAREN) {  // we dont want parentheses to be on left side of =
                    sub[0] = check.sub[0];
                }
            }
            sub[0].flags |= flags;
            sub[1].flags |= flags;
            return indent_str + prefix + sub[0].str() + (value=="*" || value=="/" ? value :" " + value + " ") + sub[1].str();
        case EXPR_UNARY:
            ASSERT(sub.size()==1);
            sub[0].flags |= flags;
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
            sub[0].flags |= flags;
            sub[1].flags |= flags;
            sub[2].flags |= flags;
            return indent_str + prefix + sub[0].str() + " ? " + sub[1].str() + " : " + sub[2].str();
        case EXPR_INDEX:
            ASSERT(sub.size()==2);
            {
                auto& check = sub[0];
                while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
                    check = check.sub[0];
                }
                if (check.type == EXPR_PAREN) {  // we dont want parentheses to be before []
                    sub[0] = check.sub[0];
                }
            }
            sub[0].flags |= flags;
            sub[1].flags |= flags;
            return indent_str + prefix + sub[0].str() + suffix + "[" + sub[1].str() + value + "]";
        case EXPR_CAST:
if (sub.size() == 0) return "??????";
            ASSERT(sub.size()==1);
            sub[0].flags |= flags;
            sub[0].indent = indent;
            return /*indent_str + */sub[0].str(prefix, suffix);
        case EXPR_PAREN:
            ASSERT(sub.size()==1);
            sub[0].flags |= flags;
            if (sub[0].type == EXPR_VAR || sub[0].type == EXPR_MEMBER || (sub[0].type == EXPR_UNARY && sub[0].value == "*")) {
                return /*indent_str + */sub[0].str();
            }
            return indent_str + "(" + sub[0].str() + ")";
        case EXPR_INIT:
            ASSERT(sub.size()>=1);
            if (sub[0].type != EXPR_INIT) {  // exclude one initializer case
                bool first = true;
                std::string ret;
                for (size_t i=sub.size(); i > 0; --i) {
                    if (sub[i-1].type != EXPR_NONE) {
                        if (first) {
                            first = false;
                            ret = indent_str + "{";
                        }
                        sub[i-1].flags |= flags;
                        ret += sub[i-1].str();
                    }
                }
                if (!first) {
                    ret += "}";
                }
                if (ret == "") {
                    ret += indent_str + "0";
                }
                return ret;
            }
            sub[0].flags |= flags;
            return indent_str + sub[0].str();
        case EXPR_TRAIT:
            ASSERT(sub.size()==1);
            sub[0].flags |= flags;
            if (value == "sizeof") {
                return indent_str + "($bits" + "(" + sub[0].str() + ")/8)";
            }
            return indent_str + value + "(" + sub[0].str() + ")";
        case EXPR_RETURN:
            if (sub.size() >= 1) {
                return (flags&FLAG_ASSIGN) ? indent_str + sub[0].str() :
                            ((flags&FLAG_COMB) ? (sub[0].type==EXPR_MEMBER||(sub[0].type==EXPR_CAST&&sub[0].sub[0].type==EXPR_MEMBER)
                                    ?"":indent_str + sub[0].str()) : indent_str + "return " + sub[0].str());
            }
            else {
                return indent_str + "disable " + currMethod->name;
            }

/* ((flags&FLAG_RETURN) && sub.size()==1) ?
                       (indent_str + "return " + sub[0].str()) :
                       ((flags&FLAG_NORETURN) ?
                           "" :
                           indent_str + "disable " + currMethod->name);//(sub.size()==1 ? "return " + sub[0].str() : "return");*/
        case EXPR_FOR:
        {
            ASSERT(sub.size()==4);
            std::string str;
            str += indent_str + "for (" + sub[0].str() + ";" + sub[1].str() + ";" + sub[2].str() + ") begin\n";
            sub[3].indent = indent + 1;
            sub[3].flags |= flags;
            str += sub[3].str(prefix);
            if (!str.empty() && str.back() != '\n') {
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
            sub[1].flags |= flags;
            str += sub[1].str(prefix);
            if (!str.empty() && str.back() != '\n') {
                str += ";\n";
            }
            str += indent_str + "end\n";
            return str;
        }
        case EXPR_IF:
        {
            ASSERT(sub.size()>=1);
            std::string param;
            if (sub[0].traverseIf( [&](Expr& e) { return e.value == "clk";} )) {
                return "";
            }
            std::string str;
            str += indent_str + "if (" + sub[0].str() + ") begin\n";
            if (sub.size() > 1) {
                sub[1].indent = indent + 1;
                sub[1].flags |= flags;
                str += sub[1].str(prefix);
                if (!str.empty() && str.back() != '\n') {
                    str += ";\n";
                }
            }
            str += indent_str + "end\n";
            if (sub.size() > 2) {
                str += indent_str + "else begin\n";
                sub[2].indent = indent + 1;
                sub[2].flags |= flags;
                str += sub[2].str(prefix);
                if (!str.empty() && str.back() != '\n') {
                    str += ";\n";
                }
                str += indent_str + "end\n";
            }
            return str;
        }
        case EXPR_SWITCH:
        {
            ASSERT(sub.size()>=1);
            std::string str;
            str += indent_str + "case (" + sub[0].str() + ")\n";
            if (sub.size() > 1) {
                for (size_t i=sub.size()-1; i > 0; --i) {
                    sub[i].indent = indent + 1;
                    sub[i].flags |= flags;
                    str += indent_str + sub[i].value + ":";
                    str += " begin\n";
                    str += sub[i].str(prefix);
                    if (!str.empty() && str.back() != '\n') {
                        str += ";\n";
                    }
                    str += indent_str + "end\n";
                }
            }
            str += indent_str + "endcase\n";
            return str;
        }
        case EXPR_BODY:
        {
            std::string str;
            for (auto& stmt : sub) {
                stmt.indent = indent;
                stmt.flags = flags;
                auto s = stmt.str(prefix);
                if (!s.empty() && s.back() != '\n') {
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

std::string Expr::typeToSV(std::string type, std::string size)
{
    std::string logic = (flags&FLAG_WIRE) ? "wire" : ((flags&FLAG_REG) ? "reg" : "logic");

    std::string str = type;//genTypeName(type);
    if (type == "cpphdl_logic") {
        str = logic + size;
    } else
    if (type == "cpphdl_u") {
        str = logic + size;
    } else
    if (type == "cpphdl_s") {
        str = logic + " signed" + size;
    } else
    if (type == "cpphdl_u1") {
        str = logic + size;
    } else
    if (type == "cpphdl_u8") {
        str = logic + size + "[7:0]";
    } else
    if (type == "cpphdl_s8") {
        str = logic + " signed" + size + "[7:0]";
    } else
    if (type == "cpphdl_u16") {
        str = logic + size + "[15:0]";
    } else
    if (type == "cpphdl_s16") {
        str = logic + " signed" + size + "[15:0]";
    } else
    if (type == "cpphdl_u32") {
        str = logic + size + "[31:0]";
    } else
    if (type == "cpphdl_s32") {
        str = logic + " signed" + size + "[31:0]";
    } else
    if (type == "cpphdl_u64") {
        str = logic + size + "[63:0]";
    } else
    if (type == "cpphdl_s64") {
        str = logic + " signed" + size + "[63:0]";
    } else
    if (type == "bool") {
        str = logic + size;
    } else
    if (type == "_Bool") {
        str = logic + size;
    } else
    if (type == "int8_t") {
        str = logic + " signed" + size + "[7:0]";
    } else
    if (type == "int16_t") {
        str = logic + " signed" + size + "[15:0]";
    } else
    if (type == "int32_t") {
        str = logic + " signed" + size + "[31:0]";
    } else
    if (type == "int64_t") {
        str = logic + " signed" + size + "[63:0]";
    } else
    if (type == "uint8_t") {
        str = logic + " signed" + size + "[7:0]";
    } else
    if (type == "uint16_t") {
        str = logic + size + "[15:0]";
    } else
    if (type == "uint32_t") {
        str = logic + size + "[31:0]";
    } else
    if (type == "uint64_t") {
        str = logic + size + "[63:0]";
    } else
    if (type == "signed char") {
        str = "byte" + size;
    } else
    if (type.compare(0, 4, "short") == 0) {
        str = "shortint" + size;
    } else
    if (type == "int") {
        str = "integer" + size;
    } else
    if (type.compare(0, 4, "long") == 0) {
        str = "longint" + size;
    } else
    if (type == "unsigned char") {
        str = "logic" + size + "[7:0]";
    } else
    if (type == "unsigned short") {
        str = "logic" + size + "[15:0]";
    } else
    if (type == "unsigned long" || type == "size_t") {
        str = "logic" + size + "[63:0]";
    } else
    if (type.compare(0, 8, "unsigned") == 0) {
        str = "logic" + size + "[31:0]";
    } else {
        str += size;
    }
    return str;
}

Expr Expr::simplify()  // open brackets for *(+-)
{
    Expr expr = *this;
    while (
        expr.traverseIf( [&](Expr& e) {
            if ((e.type == EXPR_OPERATORCALL || e.type == EXPR_BINARY) && e.value == "*" && e.sub.size() == 2) {
                if ((e.sub[0].type == EXPR_CAST || e.sub[0].type == EXPR_PAREN) && e.sub[0].sub.size() >= 1) {
                    e.sub[0] = e.sub[0].sub[0];
                }
                if ((e.sub[1].type == EXPR_CAST || e.sub[0].type == EXPR_PAREN) && e.sub[1].sub.size() >= 1) {
                    e.sub[1] = e.sub[1].sub[0];
                }
                if ((e.sub[0].type == EXPR_OPERATORCALL || e.sub[0].type == EXPR_BINARY) && (e.sub[0].value == "+" || e.sub[0].value == "-") && e.sub[0].sub.size() == 2) {
                    e = Expr{e.sub[0].value, EXPR_OPERATORCALL, {Expr{"*", EXPR_BINARY, {e.sub[0].sub[0],e.sub[1]}},Expr{"*", EXPR_OPERATORCALL, {e.sub[0].sub[1],e.sub[1]}}}};
                    return true;
                }
                if ((e.sub[1].type == EXPR_OPERATORCALL || e.sub[1].type == EXPR_BINARY) && (e.sub[1].value == "+" || e.sub[1].value == "-") && e.sub[1].sub.size() == 2) {
                    e = Expr{e.sub[1].value, EXPR_OPERATORCALL, {Expr{"*", EXPR_BINARY, {e.sub[0],e.sub[1].sub[0]}},Expr{"*", EXPR_OPERATORCALL, {e.sub[0],e.sub[1].sub[1]}}}};
                    return true;
                }
            }
            return false;
        })
    );
    while (
        expr.traverseIf( [&](Expr& e) {
            if ((e.type == EXPR_OPERATORCALL || e.type == EXPR_BINARY) && e.value == "*" && e.sub.size() == 2) {
                if (e.sub[0].type == EXPR_MEMBER) {
                    e = Expr{"0", EXPR_NUM};
                    return true;
                }
                if (e.sub[1].type == EXPR_MEMBER) {
                    e = Expr{"0", EXPR_NUM};
                    return true;
                }
            }
            return false;
        })
    );
    return expr;
}

std::string Expr::debug(int debug_indent)
{
    std::string str;
    switch(type) {
        case EXPR_NONE: str += "EXPR_NONE"; break;
        case EXPR_DECL: str += "EXPR_DECL"; break;
        case EXPR_TYPE: str += "EXPR_TYPE"; break;
        case EXPR_VAR: str += "EXPR_VAR"; break;
        case EXPR_NUM: str += "EXPR_NUM"; break;  // can also be "true" and "false" - useful for template specs
        case EXPR_STRING: str += "EXPR_STRING"; break;
        case EXPR_PARAM: str += "EXPR_PARAM"; break;
        case EXPR_PACK: str += "EXPR_PACK"; break;
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
        case EXPR_SWITCH: str += "EXPR_SWITCH"; break;
        case EXPR_BODY: str += "EXPR_BODY"; break;
        case EXPR_UNKNOWN: str += "EXPR_UNKNOWN"; break;
        default: str += "EXPR_???"; break;
    }

    ++debug_indent;

    str += ": " + value + (sub.size()?"(":"");
    bool first = true;
    for (auto& expr : sub) {
        str += (!first ? ", " : " ");
        if (sub.size() > 1) {
            str += "\n" + std::string(debug_indent*4, ' ');
        }
        str += expr.debug(debug_indent);
        first = false;
    }
    str += (sub.size()?")":"");
    return str;
}

void Expr::replacePrint(std::string& str)
{
    size_t pos = 0;
    while (true) {
        if ((pos = str.find("{:s}")) != (size_t)-1) {
            str.replace(pos, 4, "%m");
        } else
        if ((pos = str.find("{}")) != (size_t)-1) {
            str.replace(pos, 2, "%x");
        } else
        if ((pos = str.find("{:x}")) != (size_t)-1) {
            str.replace(pos, 4, "%x");
        } else
        if ((pos = str.find("{:d}")) != (size_t)-1) {
            str.replace(pos, 4, "%d");
        } else
        if ((pos = str.find("%s")) != (size_t)-1) {
            str.replace(pos, 2, "%m");
        }
        else {
            break;
        }
    }
}
