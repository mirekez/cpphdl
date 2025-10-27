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
        EXPR_STRING,
        EXPR_PARAM,
        EXPR_TEMPLATE,
        EXPR_ARRAY,
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
        EXPR_DECLARE,
        EXPR_TRAIT,
        EXPR_FOR,
        EXPR_WHILE,
        EXPR_IF,
        EXPR_BODY,
        EXPR_UNKNOWN
    } type = EXPR_EMPTY;

    std::vector<Expr> sub;
    bool has_initializer = false;

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
    } flags = FLAG_NONE;

    std::string str(std::string prefix = "", std::string size = "");
    std::string typeToSV(std::string name, std::string size = "");

};


}
