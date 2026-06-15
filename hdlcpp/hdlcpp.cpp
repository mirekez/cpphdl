#include "hdlcpp_common.cc"
#include "hdlcpp_comb.h"

struct Converter : SyntaxVisitor<Converter> {
#include "hdlcpp_frontend.cc"
#include "hdlcpp_expr.cc"
#include "hdlcpp_emit.cc"
