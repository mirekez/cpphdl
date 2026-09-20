#include "hdlcpp_common.cc"
#include "hdlcpp_comb.h"
#include "hdlcpp_word.h"
#include "hdlcpp_graph.h"

struct Converter : SyntaxVisitor<Converter> {
#include "hdlcpp_frontend.cc"
#include "hdlcpp_expr.cc"
#include "hdlcpp_emit.cc"
