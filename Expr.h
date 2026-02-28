#pragma once

#include <string>
#include <vector>
#include <sstream>

namespace cpphdl
{

struct Expr
{
    std::string value;

    enum {
        EXPR_NONE,
        EXPR_DECL,
        EXPR_TYPE,
        EXPR_NUM,  // can be also "false" and "true"
        EXPR_STRING,
        EXPR_VAR,
        EXPR_PARAM,  // numeric expression for template parameter
        EXPR_PACK,  // std::tuple element substitution
        EXPR_TEMPLATE,
        EXPR_ARRAY,
        EXPR_CALL,
        EXPR_MEMBERCALL,
        EXPR_OPERATORCALL,
        EXPR_MEMBER,
        EXPR_BINARY,
        EXPR_UNARY,
        EXPR_COND,
        EXPR_INDEX,
        EXPR_CAST,
        EXPR_PAREN,
        EXPR_INIT,
        EXPR_TRAIT,
        EXPR_RETURN,
        EXPR_FOR,
        EXPR_WHILE,
        EXPR_IF,
        EXPR_SWITCH,
        EXPR_BODY,
        EXPR_UNKNOWN
    } type = EXPR_NONE;

    std::vector<Expr> sub;

    enum : unsigned {
        FLAG_NONE = 0,
        FLAG_WIRE = 1,
        FLAG_REG = 2,
        FLAG_ASSIGN = 4,  // translating _connect() function into generate assign block
        FLAG_COMB = 8,  // it's comb block and return should be skipped
        FLAG_SPECVAL = 16,  // show number values from specialization if possible
//        FLAG_CALL = 32,  // member inside call
        FLAG_ANON = 32,  // anonymous struct or union
        FLAG_USETHIS = 64,  // methods of structs
        FLAG_NOBASE = 128,  // dont use base of member expr (for constexpr of structs)
        FLAG_NOTREG = 256,  // declare logic instead of reg
        FLAG_BRACKETS = 512
    };
    unsigned flags = FLAG_NONE;

    int indent = 0;
    size_t declSize = 0;

//    bool isMultiline()
//    {
//        return type == EXPR_FOR || type == EXPR_WHILE || type == EXPR_IF || type == EXPR_BODY || type == EXPR_SWITCH;
//    }

    Expr simplify();

    std::string str(std::string prefix = "", std::string size = "");
    std::string typeToSV(std::string name, std::string size = "");

    template <typename Func/*, typename Param*/>
    bool traverseIf(Func&& checker/*, Param& param*/) {
        if (checker(*this/*, param*/)) {
            return true;
        }
        for (auto& expr : sub) {
            if (expr.traverseIf(checker/*, param*/)) {
                return true;
            }
        }
        return false;
    }

    std::string replacePrintFormat(std::vector<Expr>& params, bool fprintf = false);

    std::string debug(int debug_indent = 0)
    {
        std::stringstream str;
        switch(type) {
            case EXPR_NONE: str << "EXPR_NONE"; break;
            case EXPR_DECL: str << "EXPR_DECL"; break;
            case EXPR_TYPE: str << "EXPR_TYPE"; break;
            case EXPR_VAR: str << "EXPR_VAR"; break;
            case EXPR_NUM: str << "EXPR_NUM"; break;  // can also be "true" and "false" - useful for template specs
            case EXPR_STRING: str << "EXPR_STRING"; break;
            case EXPR_PARAM: str << "EXPR_PARAM"; break;
            case EXPR_PACK: str << "EXPR_PACK"; break;
            case EXPR_TEMPLATE: str << "EXPR_TEMPLATE"; break;
            case EXPR_ARRAY: str << "EXPR_ARRAY"; break;
            case EXPR_CALL: str << "EXPR_CALL"; break;
            case EXPR_MEMBERCALL: str << "EXPR_MEMBERCALL"; break;
            case EXPR_OPERATORCALL: str << "EXPR_OPERATORCALL"; break;
            case EXPR_MEMBER: str << "EXPR_MEMBER"; break;
            case EXPR_BINARY: str << "EXPR_BINARY"; break;
            case EXPR_UNARY: str << "EXPR_UNARY"; break;
            case EXPR_COND: str << "EXPR_COND"; break;
            case EXPR_INDEX: str << "EXPR_INDEX"; break;
            case EXPR_CAST: str << "EXPR_CAST"; break;
            case EXPR_PAREN: str << "EXPR_PAREN"; break;
            case EXPR_INIT: str << "EXPR_INIT"; break;
            case EXPR_TRAIT: str << "EXPR_TRAIT"; break;
            case EXPR_RETURN: str << "EXPR_RETURN"; break;
            case EXPR_FOR: str << "EXPR_FOR"; break;
            case EXPR_WHILE: str << "EXPR_WHILE"; break;
            case EXPR_IF: str << "EXPR_IF"; break;
            case EXPR_SWITCH: str << "EXPR_SWITCH"; break;
            case EXPR_BODY: str << "EXPR_BODY"; break;
            case EXPR_UNKNOWN: str << "EXPR_UNKNOWN"; break;
            default: str << "EXPR_???"; break;
        }

        ++debug_indent;

        str << "(" << std::hex << (int)flags << "): " << value << (sub.size()?"(":"");
        bool first = true;
        for (auto& expr : sub) {
            str << (!first ? ", " : " ");
            if (sub.size() > 1) {
                str << "\n" << std::string(debug_indent*4, ' ');
            }
            str << expr.debug(debug_indent);
            first = false;
        }
        str << (sub.size()?")":"");
        return str.str();
    }
};


}

#include <string.h>

inline bool str_ending(const std::string& str, const char* ending)
{
    return str.rfind(ending) == str.length()-strlen(ending) && str.length() >= strlen(ending);
}

inline void str_replace(std::string& str, const char* needle, const char* replace, bool all = true)
{
    size_t pos;
    while ((pos = str.find(needle)) != (size_t)-1) {
        str.replace(pos, strlen(needle), replace);
        if (!all) {
            break;
        }
    }
}

inline std::string genTypeName(std::string name)
{
    if (name.find("decltype(") != (size_t)-1) {
        return name;  // this is for Pack substitution
    }
    str_replace(name, "::", "_");
    str_replace(name, "(", "");
    str_replace(name, ")", "");
    str_replace(name, "[", "");
    str_replace(name, "]", "");
    str_replace(name, "...", "");
    str_replace(name, "struct ", "");
    str_replace(name, "typename ", "");
    str_replace(name, "<", "");
    str_replace(name, ">", "");
    str_replace(name, " ", "");
    str_replace(name, ",", "_");
    str_replace(name, ":", "_");
    str_replace(name, "-", "m");
    str_replace(name, ".", "_");
    return name;
}
