#include "Expr.h"
#include "Method.h"
#include "Module.h"
#include "Field.h"
#include "Debug.h"
#include "Struct.h"

using namespace cpphdl;

std::string Expr::str(std::string prefix, std::string suffix)
{
    std::string indent_str;
    if (indent) {
        for (int i=0; i < indent; ++i) {
            indent_str += "    ";
        }
    }

    for (auto& e : sub) {
        e.flags |= flags;
    }

    switch (type)
    {
        case EXPR_NONE:
            return prefix + suffix;
        case EXPR_DECL:
        {
            ASSERT(sub.size() >= 1);
            if ((flags&FLAG_ASSIGN)) {  // no calls in assigns
                return "";
            }
            if (value.find("__") == 0 || value.find("_____") != (size_t)-1) {  // hidden var
                return "";
            }
            std::string str;
            if (sub[0].type == EXPR_TEMPLATE && sub[0].value == "cpphdl_memory") {
                ASSERT1(sub[0].sub.size() >= 3, std::string("cpphdl_memory subs size = ") + std::to_string(sub.size()) );
                sub[0].sub[0].flags |= Expr::FLAG_REG;
                return indent_str + prefix + sub[0].sub[0].str("", std::string("[") + sub[0].sub[1].str() + "-1:0]") + " " + escapeIdentifier(value) + "[" + sub[0].sub[2].str() + "]";
            } else
            if (sub[0].type == EXPR_NUM) {
                str += indent_str + prefix + escapeIdentifier(value) + suffix + " = " + sub[0].str();  // const initializer
            } else
            if (sub[0].type == EXPR_ARRAY) {
                str += indent_str + prefix + sub[0].str() + " " + escapeIdentifier(value) + suffix;
            }
            else {
                str += indent_str + prefix + sub[0].str() + " " + escapeIdentifier(value) + suffix;
            }
            if (sub.size() == 2 && sub[1].type != EXPR_NONE) {
                str += "; " + value + " = " + sub[1].str();
            }
            return str;
        }
        case EXPR_TYPE:
        {
            if (value.find("_IO_FILE") != (size_t)-1) {
                value = "int";
            }
            std::string str = typeToSV(value, suffix);  // also calc size
            if (!declSize && declSize != (size_t)-1) {  // unknown type
                declSize = getStructSize(value);
            }
            return indent_str + prefix + str;
        }
        case EXPR_NUM:
            if (sub.size()) {  // if we have expression for this number parameter (from template parameters)
                return indent_str + prefix + sub[0].str() + suffix;
            }
            return indent_str + prefix + (value == "true"? "1" : (value == "false"? "0" : value)) + suffix;
        case EXPR_VAR:
            return indent_str + prefix + escapeIdentifier(value) + suffix;
        case EXPR_STRING:
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
                sub[0].flags |= FLAG_REG;
                return indent_str + prefix + sub[0].str("", suffix);
            }
            if (value == "cpphdl_array") {
                ASSERT(sub.size() >= 2);
                std::string str = sub[0].str("", suffix + "[" + sub[1].str() + "-1:0]");  // also calc size
                declSize = sub[0].declSize * atoi(sub[1].str().c_str());
                return indent_str + prefix + str;
            }
            if (value.find("cpphdl_") == 0 && sub.size()) {
                declSize = atoi(sub[0].str().c_str());
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
                return indent_str + prefix + sub[sub.size()-1].sub[0].value + "_tuple_" + typeSpec;   // we have type copy of value in last sub
            }

            return indent_str + prefix + typeToSV(/*sub[sub.size()-1].str(prefix, */value + typeSpec/*)*/, suffix);   // we have type copy of value in last sub
        }
        case EXPR_ARRAY:
            ASSERT(sub.size() >= 2);
            sub[1].str();  // to calc declSize
            declSize = sub[1].declSize * atoi(sub[0].str().c_str());
            return indent_str + prefix + sub[1].str("", suffix + "[" + sub[0].str() + "-1:0]");  // in structures we need to put outer dimension before
        case EXPR_CALL:
        {
            if (str_ending(value, "_comb_func")) {
                std::string str = value;
                return indent_str + str.replace(str.rfind("_comb_func") + 5, 5, "");
            }
            if ((flags&FLAG_ASSIGN)) {  // no calls in assigns
                return "";
            }
            std::string func = value;
            if (func == "clog2") {
                func = "$clog2";
            }
            if (func == "fopen") {
                func = "$fopen";
            }
            if (func == "fclose") {
                func = "$fclose";
            }
            if (sub.size() && (func == "printf" || func == "print" || func == "format" || func == "fprintf")) {
                sub[0].value = replacePrintFormat(sub);
            }
            if (func == "fprintf" && sub.size()) {
                if (sub[0].value == "stderr") {
                    sub[0].value = "2";
                }
                if (sub[0].value == "stdout") {
                    sub[0].value = "1";
                }
                func = "$fwrite";
            }
            if (func == "printf") {
                func = "$write";
            }
            if (func == "print") {
                func = "$write";
            }
            if (func == "format") {
//                return "{0}";
                func = "$sformatf";
            }
            if (func == "exit") {
                return indent_str + "$finish()";
            }
            if (func == "fflush") {
                return "";
            }
//            if (value.find("std::basic_format_string") == 0) {
//                noBrackets = true;
//                func = "";
//            }
//            if (!sub.size()) {  // UnresolvedMemberExpr - just a call to port
//                return indent_str + str;
//            }
            std::string call = escapeIdentifier(func) + suffix;
            call += "(";
            bool first = true;
            for (auto& arg : sub) {
                if (arg.type != EXPR_NONE) {  // normal parameter
                    call += (first?"":", ") + arg.str();
                    first = false;
                }
            }
            call += ")";
            return indent_str + prefix + call;
        }
        case EXPR_OPERATORCALL:
        {
            ASSERT(sub.size());
            if (sub[0].value.find("__") == 0 || sub[0].value.find("_____") != (size_t)-1
                || (sub.size() > 1 && (sub[1].value.find("__") == 0 || sub[1].value.find("_____") != (size_t)-1))) {  // all __ vars are hidden
                return "";
            }
            auto& vars = currModule->vars;
            std::string left = sub[0].str();
            if (value == "=" && std::find_if(vars.begin(), vars.end(), [&](auto& v) {
                    return left.find(v.name + "[") == 0 && v.expr.type == cpphdl::Expr::EXPR_TEMPLATE && v.expr.value == "cpphdl_memory";
                }) != vars.end()) {
                value = "<=";
            }
            if (value == "<=" || value == "=" || value == "+" || value == "-" || value == "*" || value == "/" || value == "==" || value == "!=" || value == ">"
                 || value == ">=" || value == "<" || value == "<=") {
                ASSERT(sub.size() >= 2);
                return indent_str + prefix + sub[0].str() + " " + value + " " + sub[1].str();
            }
            if (value == "[]") {
                auto& check = sub[0];
                while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
                    check = check.sub[0];
                }
                if (check.type == EXPR_PAREN) {  // we dont want parentheses to be on left side of =
                    sub[0] = check.sub[0];  // parentheses remover
                }

                ASSERT(sub.size() >= 2);
                return indent_str + sub[0].str() + "[" + sub[1].str() + "]";
            }
            if (value == "()") {  // it can be only port, skip parenthesis
//                if (sub[0].type == EXPR_MEMBER || sub[0].type == EXPR_PACK) {  // member call - can be only member's port access now, no args
//                    ASSERT(sub[0].sub.size());
////                    if (sub[0].str() != "_this") {
//                        return indent_str + prefix + sub[0].sub[0].str("", std::string(sub[0].sub[0].str().length()?"__":"") + sub[0].value + suffix);
////                    }
//                }
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

            std::string func = value + suffix + "(";

            bool first = true;
            for (auto& arg : sub) {
                func += (first?"":", ") + arg.str();
                first = false;
            }
            func += ")";
            return indent_str + prefix + func;
        }
        // here we have member as first sub element, and it has object as it's sub element
        case EXPR_MEMBERCALL:
        {
            ASSERT(sub.size());
            if (value == "_connect" || value == "_strobe" || str_ending(value, "____connect") || str_ending(value, "____strobe")) {  // never need this functions
                return "";
            }
//            if ((value == "_work" || value == "_work_neg" || str_ending(value, "____work") || str_ending(value, "____work_neg")) && sub[0].value != "_this") {  // never need this functions except third party class work
//                return "";
//            }
            if (value.find("operator") == 0) {
                return indent_str + sub[0].str();
            }
//            if (sub[0].str() == "this") {
//                return indent_str + value + "()";
//            }
            // can we now remove clr and set completely?
            if (sub.size() >= 1 && value == "clr" && sub[0].value != "_this") {
                return indent_str + sub[0].str() + "_tmp = '0";
            }
            if (sub.size() >= 2 && value == "set" && sub[0].value != "_this") {
                return indent_str + sub[0].str() + "_tmp = " + sub[1].str();
            }
            if (sub.size() >= 1 && value == "format" && sub[0].value != "_this") {
                return indent_str + sub[0].str();
            }
            if (sub.size() >= 3 && value == "bits" && sub[0].value != "_this") {
                return indent_str + sub[0].str() + "[" + sub[2].str() + " +:(" + Expr{"-", EXPR_OPERATORCALL, {sub[1],sub[2]}}.simplify().str() + ")+1" + "]";
            }
            if (str_ending(value, "_comb_func")) {
                std::string str = value;
                str_replace(str, "_comb_func", "_comb");
                return indent_str + prefix + str + suffix;
            }
            if (sub[0].type == EXPR_NONE && any_of(currModule->ports.begin(), currModule->ports.end(), [&](auto& m){ return m.name == value; } )) {  // is port? member without base / unknown base
                return indent_str + prefix + escapeIdentifier(value) + suffix;
            }
            std::string base = sub[0].str();
            if (base != "_this" && (sub[0].type == EXPR_MEMBER || sub[0].type == EXPR_PACK)   // for member classe's ports it calls MEMBERCALL, not operator()
                && any_of(currModule->members.begin(), currModule->members.end(), [&](auto& m){ return m.name == base; } )) {
                if (value == "_work") {  // dont call _work() for members
                    return "";
                }
                return indent_str + prefix + base + "__" + escapeIdentifier(value) + suffix;
            }
            if (sub[0].type == EXPR_INDEX && sub[0].sub.size()) {
                base = sub[0].sub[0].str();
                if (base != "_this" && any_of(currModule->members.begin(), currModule->members.end(), [&](auto& m){ return m.name == base; })) {
                    if (value == "_work") {  // dont call _work() for members
                        return "";
                    }
                    return indent_str + prefix + sub[0].str("", "__" + escapeIdentifier(value) + suffix);
                }
            }
            if ((flags&FLAG_ASSIGN)) {  // no calls in assigns
                return "";
            }

            std::string args = "(";
            bool first = true;
            for (auto& arg : sub) {
                if (arg.type != EXPR_NONE) {  // usually, 'this' for Module
                    args += (first?"":", ") + arg.str();
                    first = false;
                }
            }
            args += ")";

            return indent_str + prefix + escapeIdentifier(value) + args + suffix;  // member goes as first parameter, if it has third party this (not Module)
        }
        case EXPR_MEMBER:
        {
            ASSERT1(sub.size()==1, std::string("member sub size zero: ") + debug());

            auto& check = sub[0];
            while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
                check = check.sub[0];
            }
            if (check.type == EXPR_PAREN) {  // we dont want parentheses to be on left side of =
                sub[0] = check.sub[0];  // parentheses remover
            }

            if ((flags&FLAG_ANON)&&value=="") {
                value = "_";
            }

            std::string delim = ".";
            if (value.find("operator") == 0) {
                return indent_str + sub[0].str();
            }
            if ((sub[0].type == EXPR_NONE && !(flags&FLAG_USETHIS)) || (flags&FLAG_NOBASE)) {  // no this inside, no need to "."
                return indent_str + escapeIdentifier(value) + suffix;
            }
            if (value == "_next"/* || (value == "" && !(flags&FLAG_ANON))*/) {
                return indent_str + prefix + sub[0].str("", "_tmp");
            }
            std::string base = sub[0].str();
            if (base != "_this" && any_of(currModule->members.begin(), currModule->members.end(), [&](auto& m){ return m.name == base; } )) {  // we compare full string because Pack can add "tuple_N"
                delim = "__";
            }
            if (sub[0].type == EXPR_INDEX && sub[0].sub.size()) {
                base = sub[0].sub[0].str();
                if (base != "_this" && any_of(currModule->members.begin(), currModule->members.end(), [&](auto& m){ return m.name == base; })) {
                    return indent_str + prefix + sub[0].str("", "__" + escapeIdentifier(value) + suffix);
                }
            }
            return indent_str + prefix + sub[0].str() + delim + escapeIdentifier(value) + suffix;
        }
        case EXPR_BINARY:
        {
            ASSERT(sub.size()==2);
            if ((value == "=" || value == "==") && (sub[0].str().find("__") == 0 || sub[1].str().find("__") == 0
                || sub[0].str().find("_____") != (size_t)-1 || sub[1].str().find("_____") != (size_t)-1)) {  // all __ vars are hidden
                return "";
            }
            auto& vars = currModule->vars;
            std::string left = sub[0].str();
            if (value == "=" && std::find_if(vars.begin(), vars.end(), [&](auto& v) {
                    return left.find(v.name + "[") == 0 && v.expr.type == cpphdl::Expr::EXPR_TEMPLATE && v.expr.value == "cpphdl_memory";
                }) != vars.end()) {
                value = "<=";
            }
            if (value == "=" || value == "<=") {  // parentheses remover
                auto& check = sub[0];
                while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
                    check = check.sub[0];
                }
                if (check.type == EXPR_PAREN) {  // we dont want parentheses to be on left side of =
                    sub[0] = check.sub[0];
                }
            }
            if (value == "<<") {
                value = "<<<";
            }
            if (value == ">>") {
                value = ">>>";
            }
            return indent_str + prefix + sub[0].str() + (value=="*" || value=="/" ? value :" " + value + " ") + sub[1].str();
        }
        case EXPR_UNARY:
            ASSERT(sub.size()==1);
            if (value == "+") {  // unary + not for Verilog
                return indent_str + sub[0].str();
            }
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
            {
                ASSERT(sub.size()==2);
//                if (sub.size()) {  // parentheses remover
//                    auto& check = sub[0];
//                    while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
//                        check = check.sub[0];
//                    }
//                    if (check.type == EXPR_PAREN) {  // we dont want parentheses to be on left side of =
//                        sub[0] = check.sub[0];
//                    }
//                }

                auto& check = sub[0];
                while (check.type == EXPR_UNARY || check.type == EXPR_CAST) {
                    check = check.sub[0];
                }
                if (check.type == EXPR_PAREN) {  // we dont want parentheses to be before []
                    sub[0] = check.sub[0];
                }

                size_t pos, pos1;
                if ((pos = value.find("$bits(")) != (size_t)-1 && (pos1 = value.find(")", pos)) != (size_t)-1) {
                    std::string type = value.substr(pos+strlen("$bits("), pos1-pos-strlen("$bits("));
                    value.replace(pos + strlen("$bits("), type.length(), typeToSV(type));
                }

                return indent_str + prefix + sub[0].str() + suffix + "[(" + sub[1].str() + ")" + value + "]";
            }
        case EXPR_CAST:
            if (sub.size() == 0) return "";
            ASSERT(sub.size()==1);
            sub[0].indent = indent;
            str_replace(value, "cpphdl_logic", "");
            if (value.find("logic") == 0) {
                return indent_str + sub[0].str(prefix, suffix);
            } else
            if (value == "signedchar") {
                return indent_str + "signed'(8'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value.compare(0, 5, "short") == 0) {
                return indent_str + "signed'(16'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value == "int") {
                return indent_str + "signed'(32'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value.compare(0, 4, "long") == 0) {
                return indent_str + "signed'(64'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value == "unsignedchar") {
                return indent_str + "unsigned'(8'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value == "unsignedshort") {
                return indent_str + "unsigned'(16'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value == "unsignedlong" || value == "size_t") {
                return indent_str + "unsigned'(64'(" + sub[0].str(prefix, suffix) + "))";
            } else
            if (value.compare(0, 8, "unsigned") == 0) {
                return indent_str + "unsigned'(32'(" + sub[0].str(prefix, suffix) + "))";
            }
            return indent_str + typeToSV(value) + "'(" + sub[0].str(prefix, suffix) + ")";
        case EXPR_PAREN:
            ASSERT(sub.size()==1);
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
            return indent_str + sub[0].str();
        case EXPR_TRAIT:
            ASSERT(sub.size()==1);
            if (value == "sizeof") {
                return indent_str + "($bits" + "(" + sub[0].str() + ")/8)";
            }
            return indent_str + value + "(" + sub[0].str() + ")";
        case EXPR_RETURN:
            if (sub.size() >= 1) {
                return (flags&FLAG_ASSIGN) ? indent_str + sub[0].str() :
                            ((flags&FLAG_COMB) ? (sub[0].type==EXPR_MEMBER||sub[0].type==EXPR_VAR
                                ||(sub[0].type==EXPR_CAST&&(sub[0].sub[0].type==EXPR_MEMBER||sub[0].sub[0].type==EXPR_VAR))
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
            if (sub[1].str() == "") {  // all __ vars are hidden
                return "";
            }
            std::string str;
            str += indent_str + "for (" + sub[0].str() + ";" + sub[1].str() + ";" + sub[2].str() + ") begin\n";
            sub[3].indent = indent + 1;
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
            if (sub[0].str() == "") {  // all __ vars are hidden
                return "";
            }
            std::string str;
            str += indent_str + "while (" + sub[0].str() + ") begin\n";
            sub[1].indent = indent + 1;
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
            if (sub[0].str() == "") {
                return "";
            }
            std::string str;
            str += indent_str + "if (" + sub[0].str() + ") begin\n";
            if (sub.size() > 1) {
                sub[1].indent = indent + 1;
                str += sub[1].str(prefix);
                if (!str.empty() && str.back() != '\n') {
                    str += ";\n";
                }
            }
            str += indent_str + "end\n";
            if (sub.size() > 2) {
                str += indent_str + "else begin\n";
                sub[2].indent = indent + 1;
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
                for (size_t i=1; i < sub.size(); ++i) {
                    sub[i].indent = indent + 1;
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
            if (value == "lambda") {
                Expr* body = this;
                while (body->sub.size() && body->sub[0].sub.size() && body->sub[0].sub[0].type != EXPR_DECL) {  // DECLs always live in BODY
                    body = &body->sub[0];
                }
                if (body->sub.size() && body->sub[0].sub.size() && body->sub[0].sub[0].type == EXPR_DECL && body->sub[0].sub[0].sub.size() > 1) {  // extract Declare from lambda
                    Expr repl = {"repl", EXPR_RETURN, {body->sub[0].sub[0].sub[1]}};
                    body->sub.clear();
                    body->sub.emplace_back(repl);
                }
            }

            std::string str;
            for (auto& e : sub) {
                e.indent = indent;
                auto s = e.str(prefix);
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
    std::string logic = (flags&FLAG_WIRE) ? "wire" : ((flags&FLAG_REG)&&!(flags&FLAG_NOTREG) ? "reg" : "logic");

    std::string str = type;//genTypeName(type);
    if (type == "cpphdl_logic") {
        str = logic + size;
        declSize = 1;
    } else
    if (type == "cpphdl_u") {
        str = logic + size;
        declSize = 1;
    } else
    if (type == "cpphdl_s") {
        str = logic + " signed" + size;
        declSize = 1;
    } else
    if (type == "cpphdl_u1") {
        str = logic + size;
        declSize = 1;
    } else
    if (type.find("cpphdl_u") == 0) {
        size_t width = atoi(type.c_str() + strlen("cpphdl_u"));
        str = logic + size + "[" + std::to_string(width) + "-1:0]";
        declSize = 8;
    } else
    if (type.find("cpphdl_s") == 0) {
        size_t width = atoi(type.c_str() + strlen("cpphdl_s"));
        str = logic + " signed" + size + "[" + std::to_string(width) + "-1:0]";
        declSize = 8;
    } else
    if (type == "bool") {
        str = logic + size;
        declSize = 1;
    } else
    if (type == "_Bool") {
        str = logic + size;
        declSize = 1;
    } else
    if (type == "int8_t") {
        str = logic + " signed" + size + "[7:0]";
        declSize = 8;
    } else
    if (type == "int16_t") {
        str = logic + " signed" + size + "[15:0]";
        declSize = 16;
    } else
    if (type == "int32_t") {
        str = logic + " signed" + size + "[31:0]";
        declSize = 32;
    } else
    if (type == "int64_t") {
        str = logic + " signed" + size + "[63:0]";
        declSize = 64;
    } else
    if (type == "uint8_t") {
        str = logic + " signed" + size + "[7:0]";
        declSize = 8;
    } else
    if (type == "uint16_t") {
        str = logic + size + "[15:0]";
        declSize = 16;
    } else
    if (type == "uint32_t") {
        str = logic + size + "[31:0]";
        declSize = 32;
    } else
    if (type == "uint64_t") {
        str = logic + size + "[63:0]";
        declSize = 64;
    } else
    if (type == "signedchar") {
        str = "byte" + size;
        declSize = 8;
    } else
    if (type.compare(0, 5, "short") == 0) {
        str = "shortint" + size;
        declSize = 16;
    } else
    if (type == "int") {
        str = "integer" + size;
        declSize = 32;
    } else
    if (type.compare(0, 4, "long") == 0) {
        str = "longint" + size;
        declSize = 64;
    } else
    if (type == "unsignedchar") {
        str = "logic" + size + "[7:0]";
        declSize = 8;
    } else
    if (type == "unsignedshort") {
        str = "logic" + size + "[15:0]";
        declSize = 16;
    } else
    if (type == "unsignedlong" || type == "size_t") {
        str = "logic" + size + "[63:0]";
        declSize = 64;
    } else
    if (type.compare(0, 8, "unsigned") == 0) {
        str = "logic" + size + "[31:0]";
        declSize = 32;
    } else
    if (type.compare(0, 6, "signed") == 0) {
        str = "logic signed" + size + "[31:0]";
        declSize = 32;
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
                if ((e.sub[0].type == EXPR_CAST || e.sub[0].type == EXPR_PAREN) && e.sub[0].sub.size() >= 1) {  // remove casts and parentheses left
                    e.sub[0] = e.sub[0].sub[0];
                }
                if ((e.sub[1].type == EXPR_CAST || e.sub[0].type == EXPR_PAREN) && e.sub[1].sub.size() >= 1) {  // remove casts and parentheses right
                    e.sub[1] = e.sub[1].sub[0];
                }
                // swapping * and +- its places (opening brackets)
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
                if (e.sub[0].type == EXPR_MEMBER || e.sub[0].type == EXPR_VAR) {
                    e = Expr{"0", EXPR_NUM};
                    return true;
                }
                if (e.sub[1].type == EXPR_MEMBER || e.sub[1].type == EXPR_VAR) {
                    e = Expr{"0", EXPR_NUM};
                    return true;
                }
            }
            return false;
        })
    );
    return expr;
}

std::string Expr::replacePrintFormat(std::vector<Expr>& params)
{
    std::string str = params[0].value;
    bool stdPrint = false;
    if (str.find("std::basic_format_string") == 0 && params[0].sub.size()) {
        params[0] = params[0].sub[0];
        str = params[0].value;
        stdPrint = true;
    }
    size_t pos = -1, pos1 = -1;
    for (size_t i=0; i < params.size(); ++i) {
        if (params[i].str() == "__inst_name") {
            bool replaced = false;
            if (!stdPrint && (pos = str.find("%", pos+1)) != (size_t)-1 && pos < str.length()-1) {
                while (str[pos] == str[pos+1] && pos < str.length()-1) { // %%
                    if ((pos = str.find("%", pos+2)) == (size_t)-1) {
                        break;
                    }
                }
                if (pos != (size_t)-1) {
                    str.replace(pos, 2, "%m");  // put %m insted of %s on place of "__inst_name"
                    replaced = true;
                }
            }
            if (stdPrint && (pos = str.find("{", pos+1)) != (size_t)-1 && pos < str.length()-1) {
                while (str[pos] == str[pos+1] && pos < str.length()-1) { // {{
                    if ((pos = str.find("{", pos+2)) == (size_t)-1) {
                        break;
                    }
                }
                if (pos != (size_t)-1 && (pos1 = str.find("}", pos)) != (size_t)-1) {
                    str.replace(pos, pos1-pos+1, "%m");  // put %m insted of {} or {:s} on place of "__inst_name"
                    replaced = true;
                }
            }
            if (!replaced) {
                std::cerr << "WARNING: cant find '%s' in %-position " << i << " of string '" << params[0].value << "'\n";
            }
            params.erase(params.begin() + i);
            --i;
        }
    }

    while ((pos = str.find("{")) != (size_t)-1 && (pos1 = str.find("}", pos)) != (size_t)-1) {
        if (pos1 == pos+1) {
            str.replace(pos, 2, "%x");
        } else
        if (str[pos+1] == ':') {
            str.replace(pos1, 1, "");  // }
            str.replace(pos, 2, "%");  // {:
        }
        else break;
    }
    return str;
}
