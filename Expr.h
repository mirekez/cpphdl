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
        EXPR_DECLARE,
        EXPR_TYPE,
        EXPR_NUM,  // can be also "false" and "true"
        EXPR_STRING,
        EXPR_VAR,
        EXPR_PARAM,  // numeric expression for template parameter
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
    } type = EXPR_EMPTY;

    std::vector<Expr> sub;

    enum : unsigned {
        FLAG_NONE = 0,
        FLAG_WIRE = 1,
        FLAG_REG = 2,
        FLAG_ASSIGN = 4,  // translating connect() function into generate assign block
        FLAG_COMB = 8,  // it's comb block and return should be skipped
        FLAG_SPECVAL = 16,  // show number values from specialization if possible
        FLAG_CALL = 32,  // member inside call
        FLAG_ANON = 64,  // anonymous struct or union
        FLAG_USETHIS = 128,  // methods of structs
        FLAG_NOCALLS = 256,  // calls forbidden in connect() assign
        FLAG_NOBASE = 512  // dont use base of member expr (for constexpr of structs)
    };
    unsigned flags = FLAG_NONE;

    bool hasInitializer = false;

    // methods
    int indent = 0;

    bool isMultiline()
    {
        return type == EXPR_FOR || type == EXPR_WHILE || type == EXPR_IF || type == EXPR_BODY || type == EXPR_SWITCH;
    }

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

    void replacePrint(std::string& str);
    std::string debug(int debug_indent = 0);
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
    str_replace(name, "::", "_");
    str_replace(name, "(", "");
    str_replace(name, ")", "");
    str_replace(name, "...", "");
    str_replace(name, "struct ", "");
    str_replace(name, "typename ", "");
    str_replace(name, "<", "");
    str_replace(name, ">", "");
    str_replace(name, " ", "");
    str_replace(name, ",", "_");
    str_replace(name, ":", "_");
    str_replace(name, "-", "m");
    return name;
}
