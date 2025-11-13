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
        EXPR_VALUE,
        EXPR_VAR,
        EXPR_STRING,
        EXPR_PARAM,
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
        EXPR_BODY,
        EXPR_UNKNOWN
    } type = EXPR_EMPTY;

    std::vector<Expr> sub;
    bool anonymous = false;
    bool hasInitializer = false;

    // methods
    int indent = 0;

    bool isMultiline()
    {
        return type == EXPR_FOR || type == EXPR_WHILE || type == EXPR_IF || type == EXPR_BODY;
    }

    enum : unsigned {
        FLAG_NONE = 0,
        FLAG_WIRE = 1,
        FLAG_REG = 2,
        FLAG_NORETURN = 4,
        FLAG_RETURN = 8,
        FLAG_STRUCT = 16,
//        FLAG_MEMBER = 32,
    } flags = FLAG_NONE;

    std::string str(std::string prefix = "", std::string size = "");
    std::string typeToSV(std::string name, std::string size = "");

    template <typename Func, typename Param>
    bool traverseIf(Func&& checker, Param& param) {
        if (checker(*this, param)) {
            return true;
        }
        for (auto& expr : sub) {
            if (expr.traverseIf(checker, param)) {
                return true;
            }
        }
        return false;
    }

    void replacePrint(std::string& str);
    std::string debug();
};


}
