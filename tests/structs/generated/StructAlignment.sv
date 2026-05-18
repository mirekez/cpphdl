`default_nettype none

import Predef_pkg::*;
import TinyBits_pkg::*;
import MixedBits_pkg::*;
import OuterBits_pkg::*;
import InnerUnion_pkg::*;
import StructWithUnion_pkg::*;
import UnionContainingStructContainingUnion_pkg::*;
import UnionStruct_pkg::*;
import UnionWithStruct_pkg::*;
import StructContainingUnionContainingStruct_pkg::*;
import AlignMode_MODES_pkg::*;
import StructWithEnum_pkg::*;


module StructAlignment (
    input wire clk
,   input wire reset
,   input wire[8-1:0] seed_in
,   output OuterBits sample_out
,   output UnionContainingStructContainingUnion union_struct_out
,   output StructContainingUnionContainingStruct struct_union_out
,   output StructWithEnum enum_struct_out
);


    // regs and combs
    OuterBits sample_comb;
    UnionContainingStructContainingUnion union_struct_comb;
    StructContainingUnionContainingStruct struct_union_comb;
    StructWithEnum enum_struct_comb;

    // members

    // tmp variables


    always_comb begin : sample_comb_func  // sample_comb_func
        logic[31:0] seed; seed = seed_in;
        sample_comb = {00};
        sample_comb.head=seed & 'h7;
        sample_comb.tiny.a=((seed >>> 'h1)) & 'h1;
        sample_comb.tiny.b=((seed >>> 'h2)) & 'h3;
        sample_comb.tiny.c=((seed >>> 'h4)) & 'h7;
        sample_comb.mid=((seed + 'h3)) & 'h1F;
        sample_comb.mixed.flag=((seed >>> 'h3)) & 'h1;
        sample_comb.mixed.code=((seed + 'h5)) & 'hF;
        sample_comb.mixed.state = unsigned'(3'(unsigned'(3'(seed + 'h2))));
        sample_comb.mixed.tail=((seed >>> 'h5)) & 'h3;
        sample_comb.nibble = unsigned'(4'(unsigned'(4'(seed ^ 'hA))));
        sample_comb.last=((seed >>> 'h7)) & 'h1;
    end

    always_comb begin : union_struct_comb_func  // union_struct_comb_func
        logic[31:0] seed; seed = seed_in;
        union_struct_comb = {{0}};
        union_struct_comb.wrapped.ua=seed & 'h1;
        union_struct_comb.wrapped.nested.prefix=((seed >>> 'h1)) & 'h7;
        union_struct_comb.wrapped.nested.inner.s.ia=((seed + 'h1)) & 'h3;
        union_struct_comb.wrapped.nested.inner.s.ib = unsigned'(5'(unsigned'(5'(seed + 'h3))));
        union_struct_comb.wrapped.nested.inner.s.ic=((seed >>> 'h2)) & 'h7;
        union_struct_comb.wrapped.ub=((seed + 'h5)) & 'h3F;
    end

    always_comb begin : struct_union_comb_func  // struct_union_comb_func
        logic[31:0] seed; seed = seed_in;
        struct_union_comb = {{0}};
        struct_union_comb.head=((seed >>> 'h2)) & 'h3;
        struct_union_comb.u.branch.us0=seed & 'h3;
        struct_union_comb.u.branch.nested.sa=((seed + 'h7)) & 'hF;
        struct_union_comb.u.branch.nested.sb = unsigned'(6'(unsigned'(6'(seed + 'hB))));
        struct_union_comb.u.branch.nested.sc=((seed >>> 'h6)) & 'h1;
        struct_union_comb.u.branch.us1=((seed >>> 'h3)) & 'h7;
        struct_union_comb.tail=((seed + 'hD)) & 'h7F;
    end

    always_comb begin : enum_struct_comb_func  // enum_struct_comb_func
        logic[31:0] seed; seed = seed_in;
        enum_struct_comb = {0};
        enum_struct_comb.prefix=seed & 'h3;
        enum_struct_comb.mode=((seed & 'h1)) ? (AlignMode_MODES_pkg::MODE_THREE) : (AlignMode_MODES_pkg::MODE_ONE);
        enum_struct_comb.tiny.a=((seed >>> 'h1)) & 'h1;
        enum_struct_comb.tiny.b=((seed >>> 'h2)) & 'h3;
        enum_struct_comb.tiny.c=((seed >>> 'h4)) & 'h7;
        enum_struct_comb.suffix=((seed + 'h3)) & 'h7;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign sample_out = sample_comb;

    assign union_struct_out = union_struct_comb;

    assign struct_union_out = struct_union_comb;

    assign enum_struct_out = enum_struct_comb;


endmodule
