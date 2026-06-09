#include "hdlcpp_common.cc"

struct Converter : SyntaxVisitor<Converter> {
#include "hdlcpp_frontend.cc"
#include "hdlcpp_expr.cc"
#include "hdlcpp_emit.cc"
