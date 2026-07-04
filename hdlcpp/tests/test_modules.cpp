#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string readFile(const fs::path& path)
{
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path);
    out << text;
    out.close();
    if (!out) {
        std::cerr << "failed to write " << path << "\n";
    }
    assert(out);
}

static void expectContains(const std::string& text, const std::string& needle)
{
    if (text.find(needle) == std::string::npos) {
        std::cerr << "missing expected text:\n" << needle << "\n";
    }
    assert(text.find(needle) != std::string::npos);
}

static void expectNotContains(const std::string& text, const std::string& needle)
{
    if (text.find(needle) != std::string::npos) {
        std::cerr << "unexpected text:\n" << needle << "\n";
    }
    assert(text.find(needle) == std::string::npos);
}

static size_t countContains(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

static fs::path hdlcppPath(const char* argv0)
{
    auto self = fs::absolute(argv0);
    return self.parent_path() / "hdlcpp";
}

static fs::path makeTempDir(const std::string& name)
{
    auto base = fs::temp_directory_path() / (name + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);
    return base;
}

static std::string shellQuote(const fs::path& path)
{
    auto s = path.string();
    std::string out = "'";
    for (char ch : s) {
        if (ch == '\'') {
            out += "'\\''";
        }
        else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

static std::string convertModule(const char* argv0, const std::string& section,
                                 const std::string& sv, const std::string& generateParams,
                                 const std::string& typeWidths = "",
                                 const std::string& linePatches = "")
{
    auto dir = makeTempDir("hdlcpp_module_" + section);
    auto input = dir / (section + ".sv");
    auto params = dir / "generate_params.tsv";
    auto widths = dir / "type_widths.tsv";
    auto patches = dir / "line_patches.tsv";
    writeFile(input, sv);
    writeFile(params, generateParams);
    writeFile(widths, typeWidths);
    writeFile(patches, linePatches);

    auto oldCwd = fs::current_path();
    fs::current_path(dir);
    std::string command = "HDLCPP_GENERATE_PARAM_VALUES=" + shellQuote(params) + " " +
                          (typeWidths.empty() ? std::string() : "HDLCPP_TYPE_WIDTHS=" + shellQuote(widths) + " ") +
                          (linePatches.empty() ? std::string() : "HDLCPP_LINE_PATCHES=" + shellQuote(patches) + " ") +
                          shellQuote(hdlcppPath(argv0)) + " " + shellQuote(input);
    auto rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    if (rc != 0) {
        std::cerr << "hdlcpp failed for section " << section << " rc=" << rc << "\n";
    }
    assert(rc == 0);
    return readFile(dir / "generated" / (section + ".h"));
}

static void testSequentialPartialRegUpdateSeedsNextFromCurrent(const char* argv0)
{
    const std::string sv = R"sv(
module partial_reg_seed (
    input  logic       clk_i,
    input  logic       rst_ni,
    input  logic       en_i,
    input  logic [7:0] d_i,
    output logic [7:0] q0_o,
    output logic [7:0] q1_o
);
  logic [7:0] mem_q [2];
  logic [7:0] scalar_q;

  assign q0_o = mem_q[0];
  assign q1_o = scalar_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      mem_q[0] <= 8'h11;
      mem_q[1] <= 8'h22;
      scalar_q <= 8'h33;
    end else begin
      if (en_i) begin
        mem_q[1] <= d_i;
        scalar_q <= d_i;
      end
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "partial_reg_seed", sv, "");
    expectContains(h, "mem_q._next = mem_q;");
    expectContains(h, "scalar_q._next = scalar_q;");
    expectContains(h, "mem_q._next[(unsigned)(uint64_t)(((uint64_t)(1)");
    expectContains(h, "= d_i_in();");
    expectContains(h, "scalar_q._next = d_i_in();");
}

static void testResolvedGenerateOutputBeatsInactiveSequentialBranch(const char* argv0)
{
    const std::string sv = R"sv(
module producer #(parameter int TAG = 0) (output logic [7:0] out_o);
  assign out_o = TAG ? 8'h5a : 8'h00;
endmodule

module consumer(input logic [7:0] in_i, output logic [7:0] out_o);
  assign out_o = in_i;
endmodule

module top #(parameter bit USE_CHILD = 1) (
    input  logic       clk_i,
    input  logic       rst_ni,
    output logic [7:0] out_o
);
  logic [7:0] routed;

  if (USE_CHILD) begin : gen_child
    localparam USE_TAG = 1;
    producer #(
      .TAG(USE_TAG)
    ) p (
      .out_o(routed)
    );
  end else begin : gen_reg
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        routed <= '0;
      end else begin
        routed <= 8'h33;
      end
    end
  end

  consumer c (
    .in_i (routed),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "resolved_generate_output", sv, "top.USE_CHILD\t1\n");
    expectNotContains(h, "reg<logic<8>> routed;");
    expectContains(h, "static constexpr unsigned USE_TAG =");
    expectContains(h, "producer<USE_TAG> p;");
    expectContains(h, "_LAZY_COMB(routed_comb, logic<8>)");
    expectContains(h, "routed_comb_func()");
    expectContains(h, "routed_comb = p.out_o_out();");
    expectContains(h, "c.in_i_in = _ASSIGN_COMB(routed_comb_func());");
    expectNotContains(h, "c.in_i_in = _ASSIGN(cpphdl::pack_value<8>");
}

static void testParameterizedGenerateOutputCanBePassThroughOrRegistered(const char* argv0)
{
    const std::string sv = R"sv(
module shift_like #(parameter type dtype = logic [7:0], parameter int unsigned Depth = 0) (
    input  logic       clk_i,
    input  logic       rst_ni,
    input  dtype       d_i,
    output dtype       d_o
);
  if (Depth == 0) begin : gen_pass_through
    assign d_o = d_i;
  end else if (Depth == 1) begin : gen_register
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        d_o <= '0;
      end else begin
        d_o <= d_i;
      end
    end
  end else if (Depth > 1) begin : gen_shift_reg
    dtype [Depth-1:0] reg_d, reg_q;
    assign d_o = reg_q[Depth-1];
    assign reg_d = {reg_q[Depth-2:0], d_i};
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        reg_q <= '0;
      end else begin
        reg_q <= reg_d;
      end
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "parameterized_generate_output", sv, "");
    expectContains(h, "_PORT(dtype) d_o_out = _ASSIGN_COMB( d_o_comb_func() );");
    expectContains(h, "reg<dtype> d_o;");
    expectContains(h, "reg<array<dtype");
    expectContains(h, "reg_q;");
    expectContains(h, "_LAZY_COMB(d_o_comb, dtype)");
    expectContains(h, "d_o_comb = d_o;");
    expectContains(h, "if constexpr ((((uint64_t)(Depth)");
    expectContains(h, "d_o_comb = d_i_in();");
    expectContains(h, "d_o._next = d_i_in();");
}

static void testInactiveGenerateInstanceLifecycleIsGuarded(const char* argv0)
{
    const std::string sv = R"sv(
module child(
    input  logic a_i,
    output logic b_o
);
  assign b_o = a_i;
endmodule

module top #(parameter int unsigned DEPTH = 2) (
    input  logic in_i,
    output logic out_o
);
  if (DEPTH == 1) begin : gen_one
    child inactive_i (
      .a_i(in_i),
      .b_o(out_o)
    );
  end else if (DEPTH > 1) begin : gen_many
    child active_i (
      .a_i(in_i),
      .b_o(out_o)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "inactive_generate_instance_lifecycle", sv, "");
    expectContains(h, "child inactive_i;");
    expectContains(h, "child active_i;");
    expectContains(h, "if constexpr ((((uint64_t)(DEPTH) & ((1ull << 32) - 1ull)) == ((uint64_t)(1) & ((1ull << 32) - 1ull)))");
    expectContains(h, "inactive_i._work(reset);");
    expectContains(h, "inactive_i._assign();");
    expectContains(h, "inactive_i.a_i_in = _ASSIGN_COMB(in_i_in());");
    expectContains(h, "if constexpr (!((((uint64_t)(DEPTH) & ((1ull << 32) - 1ull)) == ((uint64_t)(1) & ((1ull << 32) - 1ull))))");
    expectContains(h, "active_i._work(reset);");
    expectContains(h, "active_i._assign();");
}

static void testStringGenerateSelectsOneSameNamedInstance(const char* argv0)
{
    const std::string sv = R"sv(
module rr_child(output logic out_o);
  assign out_o = 1'b0;
endmodule

module prio_child(output logic out_o);
  assign out_o = 1'b1;
endmodule

module string_generate_instance #(parameter string ARBITER = "rr") (
    output logic out_o
);
  if (ARBITER == "rr") begin : gen_rr
    rr_child i_arbiter (
      .out_o(out_o)
    );
  end else if (ARBITER == "prio") begin : gen_prio
    prio_child i_arbiter (
      .out_o(out_o)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "string_generate_instance", sv,
                           "string_generate_instance.ARBITER\t\"rr\"\n");
    expectContains(h, "rr_child i_arbiter;");
    expectNotContains(h, "prio_child i_arbiter;");
    assert(countContains(h, "i_arbiter;") == 1);
    assert(countContains(h, "i_arbiter._work(reset);") == 1);
    assert(countContains(h, "i_arbiter._assign();") == 1);
}

static void testGenerateBranchTypedefEnumIsEmitted(const char* argv0)
{
    const std::string sv = R"sv(
module generate_typedef_in_branch #(parameter bit USE_DELAY = 1) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic valid_i,
    output logic ready_o
);
  if (!USE_DELAY) begin : gen_pass
    assign ready_o = valid_i;
  end else begin : gen_delay
    typedef enum logic [1:0] {
      Idle, Valid, Ready
    } state_e;

    state_e state_d, state_q;

    always_comb begin
      state_d = state_q;
      ready_o = 1'b0;
      unique case (state_q)
        Idle: begin
          if (valid_i) state_d = Valid;
        end
        Valid: begin
          state_d = Ready;
        end
        Ready: begin
          ready_o = 1'b1;
          state_d = Idle;
        end
        default: begin
          state_d = Idle;
        end
      endcase
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) state_q <= Idle;
      else state_q <= state_d;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generate_typedef_in_branch", sv,
                           "generate_typedef_in_branch.USE_DELAY\t1\n");
    expectContains(h, "using state_e = logic<2>;");
    expectContains(h, "static constexpr unsigned Idle = 0;");
    expectContains(h, "static constexpr unsigned Valid = 1;");
    expectContains(h, "static constexpr unsigned Ready = 2;");
    expectContains(h, "reg<state_e> state_q;");
    expectContains(h, "state_q._next = Idle;");
}

static void testOrGenerateSelectsOneSameNamedInstance(const char* argv0)
{
    const std::string sv = R"sv(
module wt_child(output logic out_o);
  assign out_o = 1'b0;
endmodule

module hpd_child(output logic out_o);
  assign out_o = 1'b1;
endmodule

module wb_child(output logic out_o);
  assign out_o = 1'b0;
endmodule

module or_generate_instance #(parameter int MODE = 2) (
    output logic out_o
);
  if (MODE == 1) begin : gen_wt
    wt_child i_cache (
      .out_o(out_o)
    );
  end else if (MODE == 2 || MODE == 3 || MODE == 4) begin : gen_hpd
    hpd_child i_cache (
      .out_o(out_o)
    );
  end else begin : gen_wb
    wb_child i_cache (
      .out_o(out_o)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "or_generate_instance", sv,
                           "or_generate_instance.MODE\t2\n");
    expectContains(h, "hpd_child i_cache;");
    expectNotContains(h, "wt_child i_cache;");
    expectNotContains(h, "wb_child i_cache;");
    assert(countContains(h, "i_cache;") == 1);
}

static void testTemplateTernaryDefaultIsParenthesized(const char* argv0)
{
    const std::string sv = R"sv(
module ternary_template_default #(
    parameter  int unsigned N = 0,
    localparam int unsigned W = N > 1 ? $clog2(N) : 1
) (
    output logic [W-1:0] out_o
);
  assign out_o = '0;
endmodule
)sv";

    auto h = convertModule(argv0, "ternary_template_default", sv, "");
    expectContains(h, "unsigned W = (");
    expectNotContains(h, "unsigned W = N >");
}

static void testTypedefIntegerCastIsNamedCast(const char* argv0)
{
    const std::string sv = R"sv(
module typedef_integer_cast (
    input  logic [3:0] sel_i,
    output logic       out_o
);
  typedef int unsigned hpdcache_uint32;
  always_comb begin
    out_o = (3 == hpdcache_uint32'(sel_i));
  end
endmodule
)sv";

    auto h = convertModule(argv0, "typedef_integer_cast", sv, "");
    expectContains(h, "using hpdcache_uint32 = u32;");
    expectContains(h, "hpdcache_uint32(");
    expectNotContains(h, "logic<hpdcache_uint32>");
    expectNotContains(h, "sv_cast<hpdcache_uint32>");
}

static void testPackedTypedefCastUsesNumericSource(const char* argv0)
{
    const std::string sv = R"sv(
package packed_typedef_cast_pkg;
  typedef logic [31:0] word_t;
endpackage

module packed_typedef_cast (
    input  logic [7:0] in_i,
    output packed_typedef_cast_pkg::word_t out_o
);
  import packed_typedef_cast_pkg::*;
  always_comb begin
    out_o = word_t'(in_i);
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_typedef_cast", sv, "");
    expectContains(h, "word_t(((uint64_t)(in_i_in())");
    expectNotContains(h, "cpphdl::sv_cast<word_t>");
    expectNotContains(h, "cpphdl::sv_cast<packed_typedef_cast_pkg::word_t>");
}

static void testRuntimeRangeSelectUsesRuntimeBits(const char* argv0)
{
    const std::string sv = R"sv(
module runtime_range_select #(
    parameter int unsigned N = 4
) (
    input  logic [N-1:0] in_i,
    output logic [N-1:0] out_o
);
  genvar gen_i;
  for (gen_i = 0; gen_i < N; gen_i++) begin : gen_mask
    assign out_o[gen_i] = |in_i[gen_i:0];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "runtime_range_select", sv, "");
    expectContains(h, "cpphdl::sv_bits_runtime");
    expectNotContains(h, "sv_bits<((uint64_t)((uint64_t)(i)))+1>");
    expectNotContains(h, "reduce_and(cpphdl::sv_bits_runtime");
}

static void testSlicedCombDependencyLateBindsMethodCall(const char* argv0)
{
    const std::string sv = R"sv(
module sliced_comb_dependency(
    input  logic [3:0] in_i,
    output logic       out_o
);
  logic [3:0] mid;
  assign mid = in_i;
  always_comb begin
    out_o = mid[1:0] == 2'b11;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "sliced_comb_dependency", sv, "");
    expectContains(h, "mid_comb_func()");
    expectNotContains(h, "logic<4>(mid).bits");
}

static void testDesignatedPatternAssignmentUsesTypedTemporary(const char* argv0)
{
    const std::string sv = R"sv(
module designated_pattern_assignment(
    input  logic [3:0] addr_i,
    output logic       cacheable_o
);
  typedef struct packed {
    logic [3:0] mem_req_addr;
    logic       mem_req_cacheable;
  } req_t;

  req_t req;
  always_comb begin
    req = '{
      mem_req_addr: addr_i,
      mem_req_cacheable: 1'b1
    };
    cacheable_o = req.mem_req_cacheable;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "designated_pattern_assignment", sv, "");
    expectContains(h, "req = req_t{ addr_i_in(), logic<1>(0b1) };");
    expectNotContains(h, "req = { .mem_req_addr");
    expectNotContains(h, "req = req_t{ .mem_req_addr");
}

static void testIndexedDesignatedPatternUsesElementType(const char* argv0)
{
    const std::string sv = R"sv(
module indexed_designated_pattern(
    input  logic       valid_i,
    input  logic [3:0] tag_i,
    output logic       out_o
);
  typedef struct packed {
    logic       valid;
    logic [3:0] tag;
  } entry_t;

  entry_t entries [2];
  always_comb begin
    entries = '0;
    for (int i = 0; i < 2; i++) begin
      entries[i] = '{
        valid: valid_i,
        tag: tag_i
      };
    end
    out_o = entries[0].valid;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_designated_pattern", sv, "");
    expectContains(h, "] = entry_t{ valid_i_in(), tag_i_in() };");
    expectNotContains(h, "] = array<entry_t,2>{ .valid");
    expectNotContains(h, "] = entry_t{ .valid");
}

static void testContinuousDesignatedPatternUsesTargetType(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_designated_pattern(
    input  logic [2:0] line_i,
    output logic       cacheable_o
);
  typedef struct packed {
    logic [3:0] mem_req_addr;
    logic       mem_req_cacheable;
  } req_t;

  req_t req;
  assign req = '{
    mem_req_addr: {line_i, 1'b0},
    mem_req_cacheable: 1'b1
  };
  assign cacheable_o = req.mem_req_cacheable;
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_designated_pattern", sv, "");
    expectContains(h, "req_comb = req_t{ cat{");
    expectNotContains(h, "req_comb = { .mem_req_addr");
    expectNotContains(h, "req_comb = req_t{ .mem_req_addr");
}

static void testEnumPatternListDoesNotBecomeConcat(const char* argv0)
{
    const std::string sv = R"sv(
module enum_pattern_list(output logic out_o);
  typedef enum logic [1:0] {
    DISABLED,
    PARALLEL,
    MERGED
  } unit_t;
  typedef unit_t unit_array_t [2];
  typedef struct packed {
    unit_array_t units;
    logic        valid;
  } cfg_t;

  localparam cfg_t CFG = '{
    units: '{PARALLEL, MERGED},
    valid: 1'b1
  };
  assign out_o = CFG.valid;
endmodule
)sv";

    auto h = convertModule(argv0, "enum_pattern_list", sv, "");
    expectContains(h, ".units = {PARALLEL, MERGED}");
    expectNotContains(h, ".units = cat{");
    expectNotContains(h, "sv_assign_field(v.units, cat{");
}

static void testAggregateInputPortFieldBindingUsesCombMethod(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [3:0] data;
} payload_t;

typedef struct packed {
  payload_t payload;
} bus_t;

module aggregate_field_child(
    input  payload_t in_i,
    output logic [3:0] out_o
);
  assign out_o = in_i.data;
endmodule

module aggregate_field_parent(
    input  bus_t bus_i,
    output logic [3:0] out_o
);
  aggregate_field_child u_child (
    .in_i(bus_i.payload),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "aggregate_field_parent", sv, "");
    expectContains(h, "__port_bind_u_child_in_i_in_comb_func()");
    expectContains(h, "u_child.in_i_in = _ASSIGN_COMB(__port_bind_u_child_in_i_in_comb_func());");
    expectNotContains(h, "u_child.in_i_in = _ASSIGN(bus_i_in().payload);");
}

static void testIndexedRegPatternThroughArrayAliasUsesElementType(const char* argv0)
{
    const std::string sv = R"sv(
module indexed_reg_pattern_alias(
    input  logic       clk_i,
    input  logic       rst_ni,
    input  logic       valid_i,
    input  logic [3:0] tag_i,
    input  logic       sel_i,
    output logic       out_o
);
  typedef struct packed {
    logic       valid;
    logic [3:0] tag;
  } entry_t;
  typedef entry_t entry_array_t [2];

  entry_array_t entries_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      entries_q <= '0;
    end else if (sel_i) begin
      entries_q[1] <= '{
        valid: valid_i,
        tag: tag_i
      };
    end
  end

  assign out_o = entries_q[1].valid;
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_reg_pattern_alias", sv, "");
    expectContains(h, "entries_q._next[(unsigned)");
    expectContains(h, "] = entry_t{ valid_i_in(), tag_i_in() };");
    expectNotContains(h, "] = entry_array_t{ .valid");
    expectNotContains(h, "] = array<entry_t,2>{ .valid");
    expectNotContains(h, "] = entry_t{ .valid");
}

static void testPackedAggregateBitwiseUpdateUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module packed_aggregate_bitwise_update(
    input  logic clk_i,
    input  logic rst_ni,
    input  logic set_valid_i,
    input  logic rst_valid_i,
    output logic out_o
);
  typedef struct packed {
    logic       valid;
    logic [2:0] code;
  } dep_t;
  typedef dep_t dep_array_t [2];

  dep_array_t deps_q;
  dep_array_t deps_set;
  dep_array_t deps_rst;

  assign deps_set[0] = '{valid: set_valid_i, code: 3'b101};
  assign deps_set[1] = '0;
  assign deps_rst[0] = '{valid: rst_valid_i, code: 3'b111};
  assign deps_rst[1] = '0;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      deps_q <= '0;
    end else begin
      deps_q <= (~deps_q & deps_set) | (deps_q & ~deps_rst);
    end
  end

  assign out_o = deps_q[0].valid;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_aggregate_bitwise_update", sv, "");
    expectContains(h, "cpphdl::pack_value<");
    expectContains(h, "logic<cpphdl::type_width<array<dep_t,2>>()");
    expectNotContains(h, "(uint64_t)(deps_q)");
    expectNotContains(h, "((uint64_t)(deps_q))");
    expectNotContains(h, "(uint64_t)((uint64_t)(deps_q))");
}

static void testScalarTypedefParameterWidthIsNotPacked(const char* argv0)
{
    const std::string sv = R"sv(
module scalar_typedef_parameter_width #(
    parameter uint_t W = 8
) (
    output logic [W-1:0] out_o
);
  typedef logic [31:0] uint_t;
  assign out_o = '0;
endmodule
)sv";

    auto h = convertModule(argv0, "scalar_typedef_parameter_width", sv, "");
    expectContains(h, "logic<(uint64_t)((uint64_t)(W))>");
    expectNotContains(h, "cpphdl::pack_value<32>(W)");
}

static void testSizedCastWidthDoesNotLeakRawSvSyntax(const char* argv0)
{
    const std::string sv = R"sv(
package sized_cast_width_pkg;
  typedef struct packed {
    int unsigned XLEN;
  } cfg_t;
endpackage

module sized_cast_width_expr #(
    parameter int unsigned XLEN = 32,
    parameter int unsigned FLAG = 1,
    parameter sized_cast_width_pkg::cfg_t Cfg = '{XLEN: 32}
) (
    input  logic [XLEN-1:0] q_i,
    output logic [XLEN-1:0] mask_o,
    output logic [Cfg.XLEN-1:0] member_mask_o
);
  localparam logic [63:0] BASE_MASK = 64'hff;
  always_comb begin
    mask_o = XLEN'(FLAG) & q_i;
    member_mask_o = BASE_MASK[Cfg.XLEN-1:0] | Cfg.XLEN'(FLAG) | member_mask_o;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "sized_cast_width_expr", sv, "");
    expectContains(h, "logic<((uint64_t)(XLEN)");
    expectNotContains(h, "XLEN'(");
    expectNotContains(h, "decltype(XLEN'");
    expectNotContains(h, ".XLEN'(");
    expectNotContains(h, "decltype(std::declval<sized_cast_width_pkg::cfg_t>().XLEN'");
    expectNotContains(h, "logic<BASE_MASK.bits");
    expectNotContains(h, ".bits(Cfg.XLEN-1,0) | Cfg.XLEN");
}

static void testBitsOfPortFieldUsesTypeDeclval(const char* argv0)
{
    const std::string sv = R"sv(
module bits_of_port_field (
    input  external_field_bits_t s_i,
    output logic [W-1:0] out_o
);
  localparam int unsigned W = $bits(s_i.a);
  assign out_o = '0;
endmodule
)sv";

    auto h = convertModule(argv0, "bits_of_port_field", sv, "");
    expectContains(h, "std::declval<external_field_bits_t>().a");
    expectNotContains(h, "decltype(s_i_in().a)");
}

static void testFunctionLocalparamAndParameterizedReplication(const char* argv0)
{
    const std::string sv = R"sv(
module function_localparam_replicate #(
    parameter int unsigned W = 32
) (
    input  logic [1:0] word_i,
    output logic [3:0] out_o
);
  function automatic logic [3:0] make_mask(input logic [1:0] word);
    localparam int unsigned OFFW = W > 16 ? 2 : 1;
    typedef logic [3:0] mask_t;
    logic [3:0] ret;
    ret = mask_t'({W/8{1'b1}});
    return ret << word[0 +: OFFW];
  endfunction

  assign out_o = make_mask(word_i);
endmodule
)sv";

    auto h = convertModule(argv0, "function_localparam_replicate", sv, "");
    expectContains(h, "static constexpr unsigned OFFW =");
    expectContains(h, "__cpphdl_rep");
    expectNotContains(h, "W/8{");
    expectNotContains(h, "cpphdl::repeat");
}

static void testConfiguredDottedGenerateSelectsOneBranch(const char* argv0)
{
    const std::string sv = R"sv(
module dotted_on(output logic out_o);
  assign out_o = 1'b1;
endmodule

module dotted_off(output logic out_o);
  assign out_o = 1'b0;
endmodule

module dotted_generate #(
    parameter external_cfg_t HPDcacheCfg = 0
) (
    output logic out_o
);
  if (HPDcacheCfg.u.mshrRamByteEnable) begin : gen_on
    dotted_on i_mem(.out_o(out_o));
  end else begin : gen_off
    dotted_off i_mem(.out_o(out_o));
  end
endmodule
)sv";

    auto h = convertModule(argv0, "dotted_generate", sv,
                           "dotted_generate.HPDcacheCfg.u.mshrRamByteEnable\t1\n");
    expectContains(h, "dotted_on i_mem;");
    expectNotContains(h, "dotted_off i_mem;");
    assert(countContains(h, "i_mem;") == 1);
}

static void testNumericWidthCastIsLogicCast(const char* argv0)
{
    const std::string sv = R"sv(
module numeric_width_cast #(
    parameter int unsigned W = 5
) (
    input  logic [7:0] in_i,
    output logic [W-1:0] out_o
);
  assign out_o = W'(in_i);
endmodule
)sv";

    auto h = convertModule(argv0, "numeric_width_cast", sv, "");
    expectContains(h, "logic<");
    expectNotContains(h, "sv_cast<W>");
}

static void testTypeTemplateCastIsNotWidthCast(const char* argv0)
{
    const std::string sv = R"sv(
module type_template_cast #(
    parameter type data_t = logic [3:0]
) (
    input  logic [3:0] in_i,
    output data_t      out_o
);
  assign out_o = data_t'(in_i);
endmodule
)sv";

    auto h = convertModule(argv0, "type_template_cast", sv, "");
    expectContains(h, "using __cpphdl_cast_t = data_t;");
    expectNotContains(h, "logic<(uint64_t)(data_t)>");
}

static void testEnumNamesAreNotParameterSubstituted(const char* argv0)
{
    const std::string sv = R"sv(
module enum_name_parameter_substitution #(
    parameter int unsigned N = 2
) (
    output logic [1:0] out_o
);
  typedef enum {
    POP_TRY_HEAD,
    POP_TRY_NEXT,
    POP_TRY_NEXT_WAIT
  } state_e;
  assign out_o = POP_TRY_NEXT;
endmodule
)sv";

    auto h = convertModule(argv0, "enum_name_parameter_substitution", sv, "");
    expectContains(h, "POP_TRY_NEXT = 1;");
    expectContains(h, "POP_TRY_NEXT_WAIT = 2;");
    expectNotContains(h, "POP_TRY_(");
}

static void testChildLocalAliasPortTypeIsSpecialized(const char* argv0)
{
    const std::string sv = R"sv(
module alias_child #(
    parameter int unsigned N = 1,
    localparam type in_t = logic [N-1:0]
) (
    input  in_t    val_i,
    output logic   out_o
);
  assign out_o = |val_i;
endmodule

module child_alias_port_type #(
    parameter int unsigned W = 3
) (
    input  logic [7:0] in_i,
    output logic       out_o
);
  logic [W-1:0] routed;
  assign routed = in_i[0 +: W];
  alias_child #(.N(W)) child_i (
    .val_i(routed | routed),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "child_alias_port_type", sv, "");
    expectNotContains(h, "__port_bind_child_i_val_i_in_comb, in_t");
    expectContains(h, "__port_bind_child_i_val_i_in_comb_func()");
    expectContains(h, "_LAZY_COMB(__port_bind_child_i_val_i_in_comb");
}

static void testLogicCombInputUsesDirectCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
module logic_sink (
    input  logic in_i,
    output logic out_o
);
  assign out_o = in_i;
endmodule

module logic_comb_input_direct (
    input  logic a_i,
    output logic out_o
);
  logic routed;
  assign routed = a_i;
  logic_sink child_i (
    .in_i(routed),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "logic_comb_input_direct", sv, "");
    expectContains(h, "child_i.in_i_in = _ASSIGN_COMB(routed_comb_func());");
    expectContains(h, "_LAZY_COMB(routed_comb, logic<1>)");
    expectNotContains(h, "child_i.in_i_in = _ASSIGN(cpphdl::pack_value<1>");
    expectNotContains(h, "__port_bind_child_i_in_i_in_comb_func()");
}

static void testSequentialChildCombInputUsesLazyBinding(const char* argv0)
{
    const std::string sv = R"sv(
module seq_logic_sink (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic in_i,
    output logic out_o
);
  logic q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) q <= 1'b0;
    else q <= in_i;
  end
  assign out_o = q;
endmodule

module seq_child_comb_input_value (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic a_i,
    output logic out_o
);
  logic routed;
  assign routed = a_i;
  seq_logic_sink child_i (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .in_i(routed),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "seq_child_comb_input_value", sv, "");
    expectContains(h, "child_i.in_i_in = _ASSIGN_COMB(routed_comb_func());");
    expectNotContains(h, "child_i.in_i_in = _ASSIGN(std::remove_cvref_t<decltype(child_i.in_i_in())>(routed_comb_func()));");
}

static void testDirectParentPortNarrowingDoesNotUseRegBinding(const char* argv0)
{
    const std::string sv = R"sv(
module narrow_sink (
    input  logic [7:0] in_i,
    output logic [7:0] out_o
);
  assign out_o = in_i;
endmodule

module direct_parent_port_narrowing (
    input  logic [15:0] wide_i,
    output logic [7:0]  out_o
);
  narrow_sink child_i (
    .in_i(wide_i),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "direct_parent_port_narrowing", sv, "");
    expectNotContains(h, "child_i.in_i_in = _ASSIGN_REG(wide_i_in());");
    expectContains(h, "child_i.in_i_in = _ASSIGN(std::remove_cvref_t<decltype(child_i.in_i_in())>(wide_i_in()));");
}

static void testParentPortPartSelectUsesCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
module slice_sink (
    input  logic [7:0] in_i,
    output logic [7:0] out_o
);
  assign out_o = in_i;
endmodule

module parent_port_part_select_binding (
    input  logic [15:0] wide_i,
    output logic [7:0]  out_o
);
  slice_sink child_i (
    .in_i(wide_i[7:0]),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "parent_port_part_select_binding", sv, "");
    expectContains(h, "child_i.in_i_in = _ASSIGN(cpphdl::sv_bits<");
}

static void testIndexedParentPortInputUsesCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       valid;
  logic [7:0] data;
} req_t;

module req_sink (
    input  req_t req_i,
    output logic valid_o
);
  assign valid_o = req_i.valid;
endmodule

module indexed_parent_port_input (
    input  req_t [2:0] reqs_i,
    output logic       valid_o
);
  req_sink child_i (
    .req_i(reqs_i[2]),
    .valid_o(valid_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_parent_port_input", sv, "");
    expectContains(h, "child_i.req_i_in = _ASSIGN_COMB((reqs_i_in())[(unsigned)((uint64_t)(((uint64_t)(2) & ((1ull << 32) - 1ull))))]);");
}

static void testPackedParentPortElementInputUsesValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
module word_sink (
    input logic [31:0] word_i
);
endmodule

module packed_parent_port_element_input (
    input logic [1:0][31:0] words_i
);
  word_sink child_i (
    .word_i(words_i[0])
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_parent_port_element_input", sv, "");
    expectContains(h, "child_i.word_i_in = _ASSIGN(cpphdl::pack_value<cpphdl::type_width<std::remove_cvref_t<decltype(child_i.word_i_in())>>()>((words_i_in())[(unsigned)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))]));");
    expectNotContains(h, "child_i.word_i_in = _ASSIGN_COMB((words_i_in())[(unsigned)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))]);");
    expectNotContains(h, "child_i.word_i_in = _ASSIGN_COMB(cpphdl::pack_value");
}

static void testPackedCombArrayElementInputUsesValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
module word_sink (
    input logic [31:0] word_i
);
endmodule

module packed_comb_array_element_input (
    input logic [1:0][31:0] words_i
);
  logic [1:0][31:0] words;

  always_comb begin
    words = words_i;
  end

  for (genvar i = 0; i < 2; i++) begin : gen_word
    word_sink child_i (
      .word_i(words[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_comb_array_element_input", sv, "");
    expectContains(h, ".word_i_in = _ASSIGN_I(logic<32>(cpphdl::pack_value");
    expectNotContains(h, ".word_i_in = _ASSIGN_COMB_I(");
}

static void testCombVectorBitInputUsesValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
module bit_sink (
    input logic bit_i
);
endmodule

module comb_vector_bit_input (
    input logic [1:0] bits_i
);
  typedef logic [1:0] bit_vec_t;
  bit_vec_t bits;

  always_comb begin
    bits = bits_i;
  end

  for (genvar i = 0; i < 2; i++) begin : gen_bit
    bit_sink child_i (
      .bit_i(bits[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "comb_vector_bit_input", sv, "");
    expectContains(h, ".bit_i_in = _ASSIGN_I(");
    expectNotContains(h, ".bit_i_in = _ASSIGN_COMB_I(");
}

static void testParentPortFieldInputUsesValueBindingAfterAdapter(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       valid;
  logic [7:0] data;
} req_t;

module data_sink (
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  assign data_o = data_i;
endmodule

module parent_port_field_input (
    input  req_t       req_i,
    output logic [7:0] data_o
);
  data_sink child_i (
    .data_i(req_i.data),
    .data_o(data_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "parent_port_field_input", sv, "");
    expectContains(h, "child_i.data_i_in = _ASSIGN(logic<8>(req_i_in().data));");
    expectNotContains(h, "child_i.data_i_in = _ASSIGN_COMB(cpphdl::pack_value");
}

static void testIndexedLocalCombInputUsesValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       valid;
  logic [7:0] data;
} req_t;

module req_sink (
    input  req_t req_i,
    output logic valid_o
);
  assign valid_o = req_i.valid;
endmodule

module indexed_local_comb_input (
    input  req_t [2:0] reqs_i,
    output logic [1:0] valid_o
);
  req_t routed[2];
  for (genvar i = 0; i < 2; i++) begin : gen_route
    assign routed[i] = reqs_i[i];
    req_sink child_i (
      .req_i(routed[i]),
      .valid_o(valid_o[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_local_comb_input", sv, "");
    expectContains(h, ".req_i_in = _ASSIGN_COMB_I( routed_comb_func()[(unsigned)(uint64_t)((uint64_t)(i))] );");
}

static void testIndexedCombArrayInputUsesValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
module word_sink (
    input  logic [7:0] in_i,
    output logic [7:0] out_o
);
  assign out_o = in_i;
endmodule

module indexed_comb_array_input (
    input  logic [7:0] words_i [2],
    output logic [7:0] out_o [2]
);
  logic [7:0] routed [2];
  always_comb begin
    routed[0] = words_i[0];
    routed[1] = words_i[1];
  end

  for (genvar i = 0; i < 2; i++) begin : gen_words
    word_sink child_i (
      .in_i(routed[i]),
      .out_o(out_o[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_comb_array_input", sv, "");
    expectContains(h, ".in_i_in = _ASSIGN_I( routed_comb_func()[(unsigned)(uint64_t)((uint64_t)(i))] );");
    expectNotContains(h, ".in_i_in = _ASSIGN_COMB_I(");
}

static void testScalarParentPortToArrayChildPortUsesAdapter(const char* argv0)
{
    const std::string sv = R"sv(
module addr_array_sink #(
    parameter int unsigned N = 1
) (
    input logic [5:0] addr_i [N]
);
endmodule

module scalar_parent_port_to_array_child (
    input logic [5:0] addr_i
);
  for (genvar i = 0; i < 1; i++) begin : gen_sink
    addr_array_sink #(.N(1)) child_i (
      .addr_i(addr_i)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "scalar_parent_port_to_array_child", sv, "");
    expectContains(h, ".addr_i_in = _ASSIGN_I(array<logic<6>,1>(addr_i_in()));");
}

static void testGeneratedChildArrayKeepsStructuredPortType(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [3:0] mask;
  logic       enable;
} ctrl_t;

module typed_child #(
    parameter type ctrl_param_t = logic
) (
    input ctrl_param_t ctrl_i
);
endmodule

module generated_child_array_struct_port #(
    parameter int unsigned N = 2
) (
    input ctrl_t ctrl_i
);
  for (genvar i = 0; i < N; i++) begin : gen_child
    typed_child #(.ctrl_param_t(ctrl_t)) child_i (
      .ctrl_i(ctrl_i)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generated_child_array_struct_port", sv, "");
    expectContains(h, ".ctrl_i_in = _ASSIGN_COMB_I( ctrl_i_in() );");
    expectNotContains(h, ".ctrl_i_in = _ASSIGN_I(bool(ctrl_i_in()));");
    expectNotContains(h, ".ctrl_i_in = _ASSIGN_COMB_I(bool(ctrl_i_in()));");
}

static void testGeneratedChildArrayScalarPortUsesCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
module scalar_child (
    input logic en_i
);
endmodule

module generated_child_array_scalar_port #(
    parameter int unsigned N = 2
) (
    input logic en_i
);
  for (genvar i = 0; i < N; i++) begin : gen_child
    scalar_child child_i (
      .en_i(en_i)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generated_child_array_scalar_port", sv, "");
    expectContains(h, ".en_i_in = _ASSIGN_COMB_I( en_i_in() );");
    expectNotContains(h, ".en_i_in = _ASSIGN_I( en_i_in() );");
}

static void testWrappedIndexedCombInputUsesCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
module bit_sink (
    input  logic in_i,
    output logic out_o
);
  assign out_o = in_i;
endmodule

module wrapped_indexed_comb_input (
    input  logic [1:0] bits_i,
    output logic [1:0] bits_o
);
  logic [1:0] routed;
  assign routed = bits_i;
  for (genvar i = 0; i < 2; i++) begin : gen_bits
    bit_sink child_i (
      .in_i(logic'(routed[i])),
      .out_o(bits_o[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "wrapped_indexed_comb_input", sv, "");
    expectContains(h, ".in_i_in = _ASSIGN_I(");
    expectContains(h, "routed_comb_func()");
}

static void testArrayInputPortCombBindingIsComplete(const char* argv0)
{
    const std::string sv = R"sv(
module array_child #(
    parameter int unsigned W = 4,
    parameter int unsigned N = 2
) (
    input logic [N-1:0][W-1:0] vals_i,
    output logic [W-1:0] out_o
);
  assign out_o = vals_i[0];
endmodule

module array_parent #(
    parameter int unsigned W = 4,
    parameter int unsigned N = 2
) (
    input  logic [W-1:0] a_i,
    output logic [W-1:0] out_o
);
  typedef logic [W-1:0] item_t;
  typedef logic [N-1:0][W-1:0] arr_t;
  arr_t vals;
  assign vals[0] = a_i;
  assign vals[1] = a_i;
  array_child #(.W(W), .N(N)) child_i (
    .vals_i(vals),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "array_input_port_comb_binding", sv, "");
    expectContains(h, "__port_bind_child_i_vals_i_in_packed_to_array_comb_func()");
    expectContains(h, "std::remove_cvref_t<decltype(child_i.vals_i_in())> __port_bind_child_i_vals_i_in_packed_to_array_comb;");
    expectContains(h, "cpphdl::pack_value<__cpphdl_target_array_t::SIZE_BITS>(__cpphdl_src)");
    expectContains(h, "child_i.vals_i_in = _ASSIGN_COMB(__port_bind_child_i_vals_i_in_packed_to_array_comb_func());");
    expectNotContains(h, "_ASSIGN_COMB(array__port_bind");
}

static void testArrayInputStructPortAdapterUsesChildPortType(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [31:0] lo;
  logic [31:0] hi;
} word_pair_t;

module struct_array_child #(
    parameter int unsigned N = 2
) (
    input word_pair_t vals_i [N]
);
endmodule

module array_logic_to_struct_child (
    input logic [63:0] vals_i [2]
);
  struct_array_child #(.N(2)) child_i (
    .vals_i(vals_i)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "array_logic_to_struct_child", sv, "");
    expectContains(h, "_LAZY_COMB(__port_bind_child_i_vals_i_in_unpacked_array_comb, std::remove_cvref_t<decltype(child_i.vals_i_in())>)");
    expectContains(h, "using __cpphdl_target_elem_t = std::remove_cvref_t<decltype(std::declval<const __cpphdl_target_array_t&>()[0])>;");
    expectContains(h, "cpphdl::unpack_value<__cpphdl_target_elem_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_elem_t>()>(__cpphdl_src[__cpphdl_i]))");
    expectContains(h, "auto __cpphdl_src = vals_i_in();");
    expectNotContains(h, "array<logic<64>,2> __port_bind_child_i_vals_i_in_unpacked_array_comb;");
    expectNotContains(h, "auto __cpphdl_src = std::remove_cvref_t<decltype(child_i.vals_i_in())>(vals_i_in());");
}

static void testChildInputPortUsesActualTypeForAliasNarrowing(const char* argv0)
{
    const std::string sv = R"sv(
module logic_alias_child #(
    parameter int unsigned W = 32,
    localparam type data_t = logic [W-1:0]
) (
    input data_t data_i,
    output logic out_o
);
  assign out_o = |data_i;
endmodule

module alias_narrowing_parent(
    input  logic [31:0] in_i,
    output logic        out_o
);
  typedef logic [0:0][31:0] arr_t;
  arr_t data;
  assign data[0] = in_i;
  logic_alias_child child_i (
    .data_i(data),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "alias_narrowing_parent", sv, "");
    expectContains(h, "__port_bind_child_i_data_i_in_comb_func()");
    expectContains(h, "_LAZY_COMB(__port_bind_child_i_data_i_in_comb");
    expectContains(h, "__port_bind_child_i_data_i_in_comb = cpphdl::pack_value<cpphdl::type_width<std::remove_cvref_t<decltype(child_i.data_i_in())>>()>(data_comb_func());");
    expectContains(h, "child_i.data_i_in = _ASSIGN_COMB(__port_bind_child_i_data_i_in_comb_func());");
    expectNotContains(h, "child_i.data_i_in = _ASSIGN_COMB(data_comb_func());");
}

static void testZeroAggregateInputPortUsesActualType(const char* argv0)
{
    const std::string sv = R"sv(
module aggregate_zero_child #(
    parameter type req_t = logic [4:0]
) (
    input logic clk_i,
    input req_t req_i,
    output logic out_o
);
  assign out_o = req_i.req;
endmodule

module aggregate_zero_parent(
    input  logic clk_i,
    output logic out_o
);
  typedef struct packed {
    logic       req;
    logic [3:0] data;
  } req_t;
  aggregate_zero_child #(.req_t(req_t)) child_i (
    .clk_i(clk_i),
    .req_i('0),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "aggregate_zero_parent", sv, "");
    expectContains(h, "child_i.req_i_in = _ASSIGN(std::remove_cvref_t<decltype(child_i.req_i_in())>{});");
    expectNotContains(h, "child_i.req_i_in = _ASSIGN(0);");
}

static void testArrayOutputPortToScalarUsesElementZero(const char* argv0)
{
    const std::string sv = R"sv(
module array_output_child(
    input  logic [3:0] in_i,
    output logic [0:0][3:0] data_o
);
  assign data_o[0] = in_i;
endmodule

module array_output_scalar_parent(
    input  logic [3:0] in_i,
    output logic [3:0] out_o
);
  typedef struct packed {
    logic [3:0] value;
  } entry_t;
  entry_t entry;
  array_output_child child_i (
    .in_i(in_i),
    .data_o(entry)
  );
  assign out_o = entry.value;
endmodule
)sv";

    auto h = convertModule(argv0, "array_output_scalar_parent", sv, "");
    expectContains(h, "entry_comb = cpphdl::unpack_value<entry_t>(cpphdl::pack_value<cpphdl::type_width<entry_t>()>(child_i.data_o_out()));");
    expectNotContains(h, "child_i.data_o_out()[0]");
    expectNotContains(h, "entry_comb = child_i.data_o_out();");
}

static void testSameTypeStructOutputDoesNotPackUnpack(const char* argv0)
{
    const std::string sv = R"sv(
package same_struct_pkg;
  typedef struct packed {
    logic [31:0] addr;
    logic [3:0]  be;
  } req_t;
endpackage

module same_struct_child(
    input  same_struct_pkg::req_t in_i,
    output same_struct_pkg::req_t data_o
);
  assign data_o = in_i;
endmodule

module same_struct_parent(
    input  same_struct_pkg::req_t in_i,
    output same_struct_pkg::req_t out_o
);
  import same_struct_pkg::*;
  req_t tmp;
  same_struct_child child_i (
    .in_i(in_i),
    .data_o(tmp)
  );
  assign out_o = tmp;
endmodule
)sv";

    auto h = convertModule(argv0, "same_struct_parent", sv, "");
    expectContains(h, "tmp_comb = child_i.data_o_out();");
    expectNotContains(h, "tmp_comb = cpphdl::unpack_value<same_struct_pkg::req_t>(cpphdl::pack_value<cpphdl::type_width<same_struct_pkg::req_t>()>(child_i.data_o_out()));");
}

static void testTemplatedSameTypeStructOutputDoesNotPackUnpack(const char* argv0)
{
    const std::string sv = R"sv(
package templ_same_struct_pkg;
  typedef struct packed {
    logic [31:0] addr;
    logic [3:0]  be;
  } req_t;
endpackage

module templ_same_struct_child #(
    parameter type dtype = logic [7:0]
) (
    input  dtype in_i,
    output dtype data_o
);
  assign data_o = in_i;
endmodule

module templ_same_struct_parent(
    input  templ_same_struct_pkg::req_t in_i,
    output templ_same_struct_pkg::req_t out_o
);
  import templ_same_struct_pkg::*;
  req_t tmp;
  templ_same_struct_child #(
    .dtype(req_t)
  ) child_i (
    .in_i(in_i),
    .data_o(tmp)
  );
  assign out_o = tmp;
endmodule
)sv";

    auto h = convertModule(argv0, "templ_same_struct_parent", sv, "");
    expectContains(h, "std::is_assignable_v<templ_same_struct_pkg::req_t&, std::remove_cvref_t<decltype((child_i.data_o_out()))>>");
    expectNotContains(h, "tmp_comb = cpphdl::unpack_value<templ_same_struct_pkg::req_t>(cpphdl::pack_value<cpphdl::type_width<templ_same_struct_pkg::req_t>()>(child_i.data_o_out()));");
}

static void testReplicationOfPackedAggregateUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module replicate_packed_aggregate #(
    parameter int unsigned N = 2
) (
    input  logic [3:0] in_i,
    output logic [N*4-1:0] out_o
);
  typedef logic [3:0] word_t;
  word_t data [1];
  assign data[0] = in_i;
  assign out_o = {N{data}};
endmodule
)sv";

    auto h = convertModule(argv0, "replicate_packed_aggregate", sv, "");
    expectContains(h, "cpphdl::pack_value<");
    expectNotContains(h, "(uint64_t)(data_comb_func())");
}

static void testBitsOfTypeParameterDefaultUsesTypeWidth(const char* argv0)
{
    const std::string sv = R"sv(
module bits_type_parameter_default #(
    parameter type id_t = logic [3:0],
    localparam int DEPTH = (1 << $bits(id_t))
) (
    output logic [DEPTH-1:0] out_o
);
  assign out_o = '0;
endmodule
)sv";

    auto h = convertModule(argv0, "bits_type_parameter_default", sv, "");
    expectContains(h, "type_width<id_t>()");
    expectNotContains(h, "$bits(");
}

static void testCombOutputPortIsGetter(const char* argv0)
{
    const std::string sv = R"sv(
module comb_output_getter (
    input  logic [7:0] a_i,
    output logic [7:0] y_o
);
  always_comb begin
    y_o = a_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "comb_output_getter", sv, "");
    expectContains(h, "_PORT(logic<8>) y_o_out = _ASSIGN_COMB( y_o_comb_func() );");
    expectNotContains(h, "logic<8>& y_o_out() { return y_o_comb_func(); }");
}

static void testCombOutputExpressionIsMaterializedBeforeGetter(const char* argv0)
{
    const std::string sv = R"sv(
module comb_output_expr_getter (
    input  logic a_i,
    input  logic b_i,
    output logic y_o
);
  logic x;
  assign x = a_i;
  assign y_o = x | b_i;
endmodule
)sv";

    auto h = convertModule(argv0, "comb_output_expr_getter", sv, "");
    expectContains(h, "_LAZY_COMB(y_o_comb, logic<1>)");
    expectContains(h, "_PORT(logic<1>) y_o_out = _ASSIGN_COMB( y_o_comb_func() );");
    expectNotContains(h, "logic<1>& y_o_out() { return");
}

static void testCombOutputExpressionDoesNotUseRegBinding(const char* argv0)
{
    const std::string sv = R"sv(
module comb_output_expr_no_reg (
    input  logic [1:0] a_i,
    output logic       y_o
);
  logic [1:0] x;
  assign x = a_i;
  assign y_o = x[0] | x[1];
endmodule
)sv";

    auto h = convertModule(argv0, "comb_output_expr_no_reg", sv, "");
    expectNotContains(h, "y_o_out = _ASSIGN_REG(");
}

static void testContinuousOutputInputExpressionIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_output_input_expr (
    input  logic a_i,
    input  logic b_i,
    output logic y_o
);
  assign y_o = a_i | b_i;
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_output_input_expr", sv, "");
    expectContains(h, "_PORT(logic<1>) y_o_out = _ASSIGN_COMB( y_o_comb_func() );");
    expectContains(h, "_LAZY_COMB(y_o_comb, logic<1>)");
    expectContains(h, "a_i_in()");
    expectContains(h, "b_i_in()");
    expectNotContains(h, "_PORT(logic<1>) y_o_out = _ASSIGN(");
    expectNotContains(h, "logic<1>& y_o_out() { return");
}

static void testContinuousFeedbackOutputInputExpressionIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_feedback_output (
    input  logic decoded_valid_i,
    input  logic issue_ack_i,
    input  logic full_i,
    output logic issue_valid_o,
    output logic decoded_ack_o
);
  always_comb begin
    issue_valid_o = decoded_valid_i & ~full_i;
    decoded_ack_o = issue_ack_i & ~full_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_feedback_output", sv, "");
    expectContains(h, "_PORT(logic<1>) issue_valid_o_out = _ASSIGN_COMB( issue_valid_o_comb_func() );");
    expectContains(h, "_PORT(logic<1>) decoded_ack_o_out = _ASSIGN_COMB( decoded_ack_o_comb_func() );");
    expectContains(h, "_LAZY_COMB(issue_valid_o_comb, logic<1>)");
    expectContains(h, "_LAZY_COMB(decoded_ack_o_comb, logic<1>)");
    expectContains(h, "decoded_valid_i_in()");
    expectContains(h, "issue_ack_i_in()");
    expectNotContains(h, "logic<1>& issue_valid_o_out() { return");
    expectNotContains(h, "logic<1>& decoded_ack_o_out() { return");
}

static void testContinuousFeedthroughOutputUsesCombGetter(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_feedthrough_output #(
    parameter bit FEEDTHROUGH = 1'b1,
    parameter type data_t = logic [7:0]
) (
    input  logic  w_i,
    input  data_t wdata_i,
    input  logic  r_i,
    output logic  rok_o,
    output data_t rdata_o
);
  data_t buf_q;
  logic valid_q;

  assign rok_o = valid_q | (FEEDTHROUGH & w_i);
  assign rdata_o = FEEDTHROUGH && !valid_q ? wdata_i : buf_q;
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_feedthrough_output", sv, "");
    expectContains(h, "_PORT(logic<1>) rok_o_out = _ASSIGN_COMB( rok_o_comb_func() );");
    expectContains(h, "_PORT(data_t) rdata_o_out = _ASSIGN_COMB( rdata_o_comb_func() );");
    expectContains(h, "_LAZY_COMB(rok_o_comb, logic<1>)");
    expectContains(h, "_LAZY_COMB(rdata_o_comb, data_t)");
    expectNotContains(h, "_PORT(logic<1>) rok_o_out = _ASSIGN(");
    expectNotContains(h, "_PORT(data_t) rdata_o_out = _ASSIGN(");
    expectNotContains(h, "logic<1>& rok_o_out() { return");
    expectNotContains(h, "data_t& rdata_o_out() { return");
}

static void testPowerOperatorPrecedenceInRanges(const char* argv0)
{
    const std::string sv = R"sv(
module power_range_precedence #(
    parameter int unsigned L = 1
) (
    output logic out_o
);
  typedef struct packed {
    int unsigned FIELD;
  } cfg_t;
  localparam cfg_t Cfg = '{FIELD: L};
  logic [2**L-2:0] nodes;
  logic [2**Cfg.FIELD-2:0] dotted_nodes;
  assign out_o = nodes[0] ^ dotted_nodes[0];
endmodule
)sv";

    auto h = convertModule(argv0, "power_range_precedence", sv, "");
    expectContains(h, "1ull << (unsigned)(");
    expectContains(h, "(uint64_t)(L)");
    expectContains(h, "Cfg.FIELD");
    expectNotContains(h, "1ull << L-2");
    expectNotContains(h, "1ull << (unsigned)(L-2)");
    expectNotContains(h, "1ull << (unsigned)(Cfg).FIELD");
}

static void testCombMethodDependenciesEmitBeforeUsers(const char* argv0)
{
    const std::string sv = R"sv(
module comb_method_dependency_order (
    input  logic [7:0] a_i,
    output logic [7:0] y_o
);
  logic [7:0] first;
  logic [7:0] second;
  assign first = second;
  assign second = a_i;
  assign y_o = first;
endmodule
)sv";

    auto h = convertModule(argv0, "comb_method_dependency_order", sv, "");
    auto first = h.find("first_comb =");
    auto second = h.find("second_comb =");
    if (first == std::string::npos || second == std::string::npos || second > first) {
        std::cerr << "expected second_comb_func to be emitted before first_comb_func\n";
        std::exit(1);
    }
}

static void testParameterizedReplicationInConcatIsValidCatItem(const char* argv0)
{
    const std::string sv = R"sv(
module replication_in_concat #(
    parameter int unsigned XLEN = 64,
    parameter int unsigned VLEN = 32
) (
    input  logic [VLEN-1:0] pc_i,
    output logic [XLEN-1:0] y_o
);
  assign y_o = {{XLEN-VLEN{pc_i[VLEN-1]}}, pc_i};
endmodule
)sv";

    auto h = convertModule(argv0, "replication_in_concat", sv, "");
    expectContains(h, "cat{([&]() { logic<");
    expectNotContains(h, "}())))), logic<");
}

static void testBracedReplicationCountIsNumeric(const char* argv0)
{
    const std::string sv = R"sv(
module braced_replication_count #(
    parameter int unsigned N = 2
) (
    input  logic [N-1:0] mask_i,
    output logic [2*N-2:0] mask_o
);
  assign mask_o = {{N-1{1'b0}}, {N{1'b1}}} << mask_i[0];
endmodule
)sv";

    auto h = convertModule(argv0, "braced_replication_count", sv, "");
    expectContains(h, "logic<(N-1)*(1)>(0ull)");
    expectContains(h, "? ~0ull : ((1ull <<");
    expectNotContains(h, "(uint64_t)(cat{");
    expectNotContains(h, "std::declval<config_pkg::cva6_cfg_t>");
}

static void testNumericReplicationConstantUsesIntegerMask(const char* argv0)
{
    const std::string sv = R"sv(
module numeric_replication_const #(
    parameter int unsigned W = 6
) (
    output logic [W-1:0] out_o
);
  localparam int unsigned ONES = {W{1'b1}};
  assign out_o = ONES[W-1:0];
endmodule
)sv";

    auto h = convertModule(argv0, "numeric_replication_const", sv, "");
    expectContains(h, "static constexpr unsigned ONES =");
    expectNotContains(h, "__cpphdl_rep{}; for");
    expectNotContains(h, "logic<((uint64_t)(W))");
}

static void testNumericConcatConstantUsesIntegerExpr(const char* argv0)
{
    const std::string sv = R"sv(
module numeric_concat_const (
    output logic [7:0] out_o
);
  localparam int unsigned V = {{4{1'b0}}, 4'h3};
  assign out_o = V[7:0];
endmodule
)sv";

    auto h = convertModule(argv0, "numeric_concat_const", sv, "");
    expectContains(h, "static constexpr unsigned V =");
    expectNotContains(h, "logic<8>(0)");
    expectNotContains(h, "__cpphdl_rep{}; for");
}

static void testParameterizedNumericConcatUsesCat(const char* argv0)
{
    const std::string sv = R"sv(
module parameterized_numeric_concat #(
    parameter int unsigned XLEN = 32
) (
    input  logic [63:0] data_i,
    output logic [XLEN-1:0] data_o
);
  always_comb begin
    data_o[XLEN-1:0] = {data_i[XLEN-9:0], data_i[XLEN-1:XLEN-8]};
  end
endmodule
)sv";

    auto h = convertModule(argv0, "parameterized_numeric_concat", sv, "");
    expectContains(h, "cat{logic<");
    expectContains(h, "((uint64_t)(9)");
    expectContains(h, "((uint64_t)(8)");
    expectNotContains(h, "cat{logic<64>");
    expectNotContains(h, "<< (unsigned)(64)");
    expectNotContains(h, "<< (unsigned)(XLEN)");
}

static void testConcatCaseKeepsOperandWidths(const char* argv0)
{
    const std::string sv = R"sv(
module concat_case_decode (
    input  logic [31:0] instruction_i,
    output logic [1:0]  op_o
);
  always_comb begin
    op_o = 2'b00;
    unique case ({ instruction_i[31:25], instruction_i[14:12] })
      {7'b0000000, 3'b000}: op_o = 2'b01;
      {7'b0100000, 3'b000}: op_o = 2'b10;
      default:              op_o = 2'b00;
    endcase
  end
endmodule
)sv";

    auto h = convertModule(argv0, "concat_case_decode", sv, "");
    expectContains(h, "logic<10>(0)");
    expectContains(h, "logic<7>(instruction_i_in().bits");
    expectContains(h, "logic<3>(instruction_i_in().bits");
    expectNotContains(h, "cat{logic<64>");
    expectNotContains(h, "<< (unsigned)(64)");
}

static void testConcatCaseStructFieldsKeepOperandWidths(const char* argv0)
{
    const std::string sv = R"sv(
module concat_case_struct_decode (
    input  logic [9:0] packed_i,
    output logic [1:0] op_o
);
  typedef struct packed {
    logic [6:0] funct7;
    logic [2:0] funct3;
  } instr_t;

  instr_t instr;
  assign instr = packed_i;

  always_comb begin
    op_o = 2'b00;
    unique case ({ instr.funct7, instr.funct3 })
      {7'b0000000, 3'b000}: op_o = 2'b01;
      {7'b0100000, 3'b000}: op_o = 2'b10;
      default:              op_o = 2'b00;
    endcase
  end
endmodule
)sv";

    auto h = convertModule(argv0, "concat_case_struct_decode", sv, "");
    expectContains(h, "logic<7>");
    expectContains(h, "logic<3>");
    expectNotContains(h, "cat{logic<64>");
    expectNotContains(h, "<< (unsigned)(64)");
}

static void testConcatArrayElementBitSelectUsesOneBitWidth(const char* argv0)
{
    const std::string sv = R"sv(
module concat_array_element_bit_select #(
    parameter int unsigned W = 2,
    parameter int unsigned N = 1
) (
    input  logic [W-1:0] data_i [N+1],
    output logic [W-1:0] out_o [N+1]
);
  logic [W-1:0] idx_ds [N+1];

  always_comb begin
    idx_ds = data_i;
    out_o[0] = idx_ds[0];
    for (int unsigned i = 0; i < N; i++) begin
      out_o[i+1] = {idx_ds[i][W-2:0], idx_ds[i][W-1]};
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "concat_array_element_bit_select", sv, "");
    expectContains(h, ", logic<1>((uint64_t)(logic<1>(");
    expectNotContains(h, "logic<cpphdl::type_width<array");
}

static void testConcatPartSelectKeepsSelectedWidth(const char* argv0)
{
    const std::string sv = R"sv(
module concat_part_select_width #(
    parameter int unsigned VLEN = 32,
    parameter int unsigned TAG_WIDTH = 20,
    parameter int unsigned INDEX_WIDTH = 12,
    parameter int unsigned OFFSET_WIDTH = 4
) (
    input  logic [TAG_WIDTH-1:0] tag_i,
    input  logic [VLEN-1:0]      addr_i,
    output logic [TAG_WIDTH+INDEX_WIDTH-1:0] out_o
);
  assign out_o = {tag_i, addr_i[INDEX_WIDTH-1:OFFSET_WIDTH], {OFFSET_WIDTH{1'b0}}};
endmodule
)sv";

    auto h = convertModule(argv0, "concat_part_select_width", sv, "");
    expectContains(h, "logic<((uint64_t)(((uint64_t)(INDEX_WIDTH)");
    expectContains(h, "((uint64_t)(OFFSET_WIDTH)");
    expectNotContains(h, "logic<(uint64_t)((uint64_t)(VLEN))>((uint64_t)(logic<");
}

static void testIntegerLocalparamConcatIsConstexprNumeric(const char* argv0)
{
    const std::string sv = R"sv(
module integer_localparam_concat #(
    parameter int unsigned W = 7
) (
    output logic [63:0] out_o
);
  function automatic logic [31:0] low(input int unsigned x);
    return x[31:0];
  endfunction
  localparam logic [63:0] C = {32'b0, low(W)};
  assign out_o = C;
endmodule
)sv";

    auto h = convertModule(argv0, "integer_localparam_concat", sv, "");
    expectContains(h, "static constexpr uint64_t C =");
    expectNotContains(h, "static constexpr uint64_t C = cat{");
}

static void testKnownWidthFunctionDoesNotForceStructArgumentNumeric(const char* argv0)
{
    const std::string sv = R"sv(
module numeric_function_struct_arg #(
    parameter type cfg_t = logic [3:0],
    parameter cfg_t Cfg = 4'h1
) (
    output logic [31:0] out_o
);
  function automatic logic [31:0] cfg_value(input cfg_t cfg);
    return 32'h5;
  endfunction

  localparam longint unsigned V = {{32{1'b0}}, cfg_value(Cfg)};
  assign out_o = V[31:0];
endmodule
)sv";

    auto h = convertModule(argv0, "numeric_function_struct_arg", sv,
                           "numeric_function_struct_arg.cfg_value\t32\n");
    expectContains(h, "cfg_value(Cfg)");
    expectNotContains(h, "cfg_value((uint64_t)(Cfg))");
}

static void testParenthesizedWidthCastIsLogicCast(const char* argv0)
{
    const std::string sv = R"sv(
module parenthesized_width_cast #(
    parameter int unsigned W = 3
) (
    input  logic [7:0] in_i,
    output logic [W-1:0] out_o
);
  assign out_o = (W)'(in_i);
endmodule
)sv";

    auto h = convertModule(argv0, "parenthesized_width_cast", sv, "");
    expectContains(h, "logic<(((uint64_t)(W)");
    expectNotContains(h, "sv_cast<(W)>");
}

static void testZeroAssignmentToStructFieldUsesValueInit(const char* argv0)
{
    const std::string sv = R"sv(
module zero_struct_field(
    output logic out_o
);
  typedef struct packed {
    logic valid;
    logic [3:0] id;
  } nested_t;
  typedef struct packed {
    logic    user;
    nested_t inv;
  } resp_t;

  resp_t resp;
  always_comb begin
    resp.user = '0;
    resp.inv = '0;
  end
  assign out_o = resp.user;
endmodule
)sv";

    auto h = convertModule(argv0, "zero_struct_field", sv, "");
    expectContains(h, "resp_comb.inv = std::remove_cvref_t<decltype(resp_comb.inv)>{};");
    expectNotContains(h, "resp_comb.inv = 0;");
}

static void testContinuousZeroAssignmentToStructFieldUsesValueInit(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_zero_struct_field(
    output logic out_o
);
  typedef struct packed {
    logic valid;
    logic [3:0] id;
  } nested_t;
  typedef struct packed {
    logic    user;
    nested_t inv;
  } resp_t;

  resp_t resp;
  assign resp.user = '0;
  assign resp.inv = '0;
  assign out_o = resp.user;
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_zero_struct_field", sv, "");
    expectContains(h, "resp_comb.inv = std::remove_cvref_t<decltype(resp_comb.inv)>{};");
    expectNotContains(h, "resp_comb.inv = 0;");
}

static void testZeroAssignmentToPackedArrayElementUsesPlainZero(const char* argv0)
{
    const std::string sv = R"sv(
module zero_packed_array_element(
    output logic [31:0] out_o
);
  logic [1:0][31:0] instr;

  always_comb begin
    instr = '0;
    instr[1] = '0;
  end

  assign out_o = instr[1];
endmodule
)sv";

    auto h = convertModule(argv0, "zero_packed_array_element", sv, "");
    expectContains(h, "instr_comb[(unsigned)");
    expectContains(h, "] = 0;");
    expectNotContains(h, "std::remove_cvref_t<decltype(instr_comb[(unsigned)");
}

static void testImportedPackageTypeBitsUseConfiguredWidth(const char* argv0)
{
    const std::string sv = R"sv(
package width_pkg;
  typedef struct packed {
    logic       valid;
    logic [2:0] code;
  } packed_t;
endpackage

module imported_package_type_bits(
    output logic [4:0] out_o
);
  import width_pkg::*;
  localparam int W = $bits(packed_t);
  logic [W-1:0] data;
  assign data = '0;
  assign out_o = {1'b0, data};
endmodule
)sv";

    auto h = convertModule(argv0, "imported_package_type_bits", sv, "",
                           "width_pkg.packed_t\t1 + 3\n");
    expectContains(h, "static constexpr unsigned W = 1 + 3;");
    expectNotContains(h, "$bits(");
}

static void testReductionOfPackedAggregatePacksOperand(const char* argv0)
{
    const std::string sv = R"sv(
package reduce_pkg;
  typedef struct packed {
    logic       valid;
    logic [2:0] code;
  } packed_t;
endpackage

module packed_aggregate_reduction(
    output logic out_o
);
  import reduce_pkg::*;
  packed_t deps [2];
  assign out_o = ~(|deps[0]);
endmodule
)sv";

    auto h = convertModule(argv0, "packed_aggregate_reduction", sv, "",
                           "reduce_pkg.packed_t\t1 + 3\n");
    expectContains(h, "cpphdl::pack_value<1 + 3>(deps");
    expectNotContains(h, "(bool)(deps[");
}

static void testReductionOrPortConnectionUsesVectorReduction(const char* argv0)
{
    const std::string sv = R"sv(
module scalar_child(
    input  logic valid_i,
    output logic valid_o
);
  assign valid_o = valid_i;
endmodule

module reduction_or_port_connection #(
    parameter int unsigned N = 2
) (
    input  logic [N-1:0] valid_i,
    output logic         valid_o
);
  scalar_child child_i (
    .valid_i (|valid_i),
    .valid_o (valid_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "reduction_or_port_connection", sv, "");
    expectContains(h, "logic<1>((((uint64_t)(valid_i_in())) &");
    expectContains(h, "child_i.valid_i_in = _ASSIGN(logic<1>(");
    expectNotContains(h, "((bool)(valid_i_in()))");
}

static void testChildInputStateExpressionUsesCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
module state_expr_sink(
    input logic [1:0] in_i
);
endmodule

module state_expr_child_binding(
    input logic clk_i,
    input logic rst_ni,
    input logic d_i
);
  logic [1:0] q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      q <= '0;
    end else begin
      q <= {q[0], d_i};
    end
  end

  state_expr_sink child_i (
    .in_i(~q)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "state_expr_child_binding", sv, "");
    expectContains(h, "child_i.in_i_in = _ASSIGN(logic<2>");
    expectContains(h, "q");
}

static void testGenerateCombOrSequentialSameSignalUsesCombRead(const char* argv0)
{
    const std::string sv = R"sv(
module mixed_generate_signal #(
    parameter bit USE_COMB = 1'b1
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic in_i,
    output logic out_o
);
  logic selected;

  if (USE_COMB) begin : gen_comb
    assign selected = in_i;
  end else begin : gen_seq
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        selected <= 1'b0;
      end else begin
        selected <= in_i;
      end
    end
  end

  assign out_o = selected;
endmodule
)sv";

    auto h = convertModule(argv0, "mixed_generate_signal", sv, "");
    expectContains(h, "selected_comb_func()");
    expectContains(h, "selected_comb = (*this).selected;");
    expectContains(h, "if constexpr (USE_COMB)");
    expectContains(h, "selected_comb = in_i_in();");
    expectContains(h, "_PORT(logic<1>) out_o_out = _ASSIGN_COMB( out_o_comb_func() );");
    expectContains(h, "out_o_comb = selected_comb_func();");
}

static void testMismatchedChildOutputIsMaterializedBeforeGetter(const char* argv0)
{
    const std::string sv = R"sv(
module width_child #(
    parameter int W = 1
) (
    output logic [W-1:0] data_o
);
  assign data_o = '0;
endmodule

module typed_parent_output(
    output pair_t pair_o
);
  typedef struct packed {
    logic       valid;
    logic [2:0] code;
  } pair_t;

  width_child #(
    .W($bits(pair_t))
  ) child_i (
    .data_o(pair_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "typed_parent_output", sv, "");
    expectContains(h, "_PORT(pair_t) pair_o_out = _ASSIGN_COMB( pair_o_comb_func() );");
    expectContains(h, "_LAZY_COMB(pair_o_comb");
    expectContains(h, "std::is_assignable_v<pair_t&, std::remove_cvref_t<decltype((child_i.data_o_out()))>>");
    expectContains(h, "return cpphdl::unpack_value<pair_t>(cpphdl::pack_value<cpphdl::type_width<pair_t>()>(child_i.data_o_out()));");
    expectNotContains(h, "pair_t& pair_o_out() { return child_i.data_o_out(); }");
}

static void testPackedChildOutputFieldProjectionUnpacksBeforeFieldRead(const char* argv0)
{
    const std::string sv = R"sv(
module packed_width_child #(
    parameter int W = 1
) (
    output logic [W-1:0] data_o
);
  assign data_o = '0;
endmodule

module packed_child_output_field_projection(
    output logic [2:0] tag_o
);
  typedef struct packed {
    logic [2:0] tag;
    logic       valid;
  } meta_t;

  meta_t meta;
  packed_width_child #(
    .W($bits(meta_t))
  ) child_i (
    .data_o(meta)
  );
  assign tag_o = meta.tag;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_child_output_field_projection", sv, "");
    expectContains(h, "meta_tag_comb_func()");
    expectContains(h, "cpphdl::unpack_value<meta_t>(cpphdl::pack_value<cpphdl::type_width<meta_t>()>(child_i.data_o_out()))).tag");
    expectNotContains(h, "(child_i.data_o_out()).tag");
}

static void testTypeParameterWidthBeatsConfiguredSuffixWidth(const char* argv0)
{
    const std::string sv = R"sv(
module type_parameter_width_precedence #(
    parameter type item_t = logic [7:0]
) (
    output logic out_o
);
  typedef struct packed {
    logic  valid;
    item_t item;
  } entry_t;

  entry_t entry;
  assign out_o = entry.valid;
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_width_precedence", sv, "",
                           "some_pkg.item_t\t32\nsome_pkg.entry_t\t33\n");
    expectContains(h, "type_width<item_t>()");
    expectNotContains(h, "32>(this->item)");
}

static void testLocalAliasWidthBeatsConfiguredSuffixWidth(const char* argv0)
{
    const std::string sv = R"sv(
module local_alias_width_precedence #(
    parameter int unsigned NINPUT = 2,
    parameter int unsigned DATA_WIDTH = 8,
    parameter bit ONE_HOT_SEL = 1'b1,
    localparam int unsigned NINPUT_LOG2 = $clog2(NINPUT),
    localparam int unsigned SEL_WIDTH = ONE_HOT_SEL ? NINPUT : NINPUT_LOG2,
    localparam type data_t = logic [DATA_WIDTH-1:0],
    localparam type sel_t = logic [SEL_WIDTH-1:0]
) (
    input  data_t [NINPUT-1:0] data_i,
    input  sel_t               sel_i,
    output data_t              data_o
);
  always_comb begin
    data_o = '0;
    for (int unsigned i = 0; i < NINPUT; i++) begin
      data_o |= sel_i[i] ? data_i[i] : '0;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "local_alias_width_precedence", sv, "",
                           "other_pkg.data_t\t(uint64_t)((uint64_t)(DataWidth))\n"
                           "other_pkg.sel_t\t(uint64_t)(clog2(N))\n");
    expectContains(h, "DATA_WIDTH");
    expectContains(h, "logic<1>");
    expectNotContains(h, "DataWidth");
    expectNotContains(h, "clog2(N)");
}

static void testGenerateBlockUnpackedArrayDimensionOrder(const char* argv0)
{
    const std::string sv = R"sv(
module generate_unpacked_array_order #(
    parameter int unsigned ROWS = 4,
    parameter int unsigned COLS = 2
) (
    input  logic clk_i,
    input  logic rst_ni,
    output logic valid_o
);
  typedef struct packed {
    logic valid;
  } entry_t;

  if (ROWS != 0) begin : gen_table
    entry_t table_q[ROWS-1:0][COLS-1:0];

    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        for (int unsigned i = 0; i < ROWS; i++) begin
          table_q[i] <= '{default: 0};
        end
      end else begin
        table_q <= table_q;
      end
    end

    assign valid_o = table_q[ROWS-1][COLS-1].valid;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generate_unpacked_array_order", sv, "");
    expectContains(h, "reg<array<array<entry_t,(uint64_t)(((uint64_t)(COLS)");
    expectContains(h, ">,(uint64_t)(((uint64_t)(ROWS)");
    expectNotContains(h, "reg<array<array<entry_t,(uint64_t)(((uint64_t)(ROWS)");
}

static void testDependentStructTypeParametersStayTemplateParameters(const char* argv0)
{
    const std::string sv = R"sv(
module dependent_struct_type_params #(
    parameter int unsigned WIDTH = 8,
    parameter type b_t = struct packed {
      logic [WIDTH-1:0] id;
    },
    parameter type r_t = struct packed {
      logic [WIDTH-1:0] data;
    },
    parameter type resp_t = struct packed {
      b_t b;
      r_t r;
    },
    localparam type local_t = struct packed {
      logic valid;
    }
) (
    input  resp_t resp_i,
    output logic  valid_o
);
  local_t local_value;
  assign local_value.valid = resp_i.b.id[0] ^ resp_i.r.data[0];
  assign valid_o = local_value.valid;
endmodule
)sv";

    auto h = convertModule(argv0, "dependent_struct_type_params", sv, "");
    expectContains(h, "typename b_t = dependent_struct_type_params_b_t_default_t<WIDTH>");
    expectContains(h, "typename r_t = dependent_struct_type_params_r_t_default_t<WIDTH,b_t>");
    expectContains(h, "typename resp_t = dependent_struct_type_params_resp_t_default_t<WIDTH,b_t,r_t>");
    expectContains(h, "b_t b;");
    expectContains(h, "r_t r;");
    expectContains(h, "struct local_t");
    expectNotContains(h, "struct b_t");
    expectNotContains(h, "struct r_t");
}

static void testUnsignedCastOfGenerateLoopVariableIsIntegerWidth(const char* argv0)
{
    const std::string sv = R"sv(
module unsigned_genvar_cast #(
    parameter int unsigned WIDTH = 2
) (
    output logic [WIDTH-1:0] out_o
);
  for (genvar j = 0; unsigned'(j) < WIDTH; j++) begin : gen_bits
    assign out_o[j] = 1'b1;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "unsigned_genvar_cast", sv, "");
    expectContains(h, "cpphdl::sv_unsigned<(size_t)(32)>((uint64_t)(");
    expectNotContains(h, "cpphdl::sv_unsigned<(size_t)(1)>((uint64_t)(");
}

static void testImplicitPackedArrayOutputPassThroughKeepsArrayShape(const char* argv0)
{
    const std::string sv = R"sv(
package implicit_packed_array_pkg;
  typedef struct packed {
    int unsigned N;
    int unsigned W;
  } cfg_t;
endpackage

module implicit_packed_array_child #(
    parameter implicit_packed_array_pkg::cfg_t Cfg = '{N: 2, W: 8}
) (
    output logic [Cfg.N-1:0][Cfg.W-1:0] data_o
);
  for (genvar i = 0; i < Cfg.N; i++) begin
    assign data_o[i] = '0;
  end
endmodule

module implicit_packed_array_parent #(
    parameter implicit_packed_array_pkg::cfg_t Cfg = '{N: 2, W: 8}
) (
    output [Cfg.N-1:0][Cfg.W-1:0] data_o
);
  implicit_packed_array_child #(
    .Cfg(Cfg)
  ) child_i (
    .data_o(data_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "implicit_packed_array_parent", sv, "");
    expectContains(h, "array<logic<(uint64_t)(((uint64_t)(Cfg.W)");
    expectContains(h, "data_o_out() { return child_i.data_o_out(); }");
    expectNotContains(h, "data_o_comb = child_i.data_o_out()[0];");
    expectNotContains(h, "_PORT(logic<(uint64_t)(((uint64_t)(Cfg.N)");
}

static void testGenerateScalarChildOutputKeepsGenerateGuard(const char* argv0)
{
    const std::string sv = R"sv(
module sync_buffer (
    input  logic       rst_ni,
    input  logic       w_i,
    output logic       wok_o,
    input  logic [7:0] wdata_i,
    input  logic       r_i,
    output logic       rok_o,
    output logic [7:0] rdata_o
);
  logic valid_q;
  logic [7:0] data_q;

  assign wok_o = ~valid_q;
  assign rok_o = valid_q;
  assign rdata_o = data_q;

  always_ff @(posedge w_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_q <= 1'b0;
      data_q <= '0;
    end else begin
      valid_q <= 1'b1;
      data_q <= wdata_i;
    end
  end
endmodule

module guarded_fifo #(
    parameter int unsigned Depth = 1
) (
    input  logic       rst_ni,
    input  logic       w_i,
    output logic       wok_o,
    input  logic [7:0] wdata_i,
    input  logic       r_i,
    output logic       rok_o,
    output logic [7:0] rdata_o
);
  if (Depth == 0) begin : gen_bypass
    assign wok_o = r_i;
    assign rok_o = w_i;
    assign rdata_o = wdata_i;
  end else if (Depth == 1) begin : gen_buffer
    sync_buffer i_sync_buffer (
      .rst_ni,
      .w_i,
      .wok_o,
      .wdata_i,
      .r_i,
      .rok_o,
      .rdata_o
    );
  end else begin : gen_fifo
    assign wok_o = 1'b0;
    assign rok_o = 1'b0;
    assign rdata_o = '0;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "guarded_fifo", sv, "");
    expectContains(h, "sync_buffer i_sync_buffer;");
    expectContains(h, "_LAZY_COMB(wok_o_comb");
    expectContains(h, "if constexpr ((((uint64_t)(Depth) & ((1ull << 32) - 1ull)) == ((uint64_t)(1) & ((1ull << 32) - 1ull)))");
    expectContains(h, "wok_o_comb = i_sync_buffer.wok_o_out();");
    expectContains(h, "rok_o_comb = i_sync_buffer.rok_o_out();");
    expectContains(h, "rdata_o_comb = i_sync_buffer.rdata_o_out();");
    expectNotContains(h, "wok_o_out() { return i_sync_buffer.wok_o_out(); }");
}

static void testUnpackedArrayDynamicIndexUsesArrayIndex(const char* argv0)
{
    const std::string sv = R"sv(
module unpacked_array_dynamic_index #(
    parameter int unsigned N = 2,
    parameter type id_t = logic [$clog2(N)-1:0],
    localparam int RT_DEPTH = N,
    localparam type rt_t = id_t [RT_DEPTH-1:0]
) (
    input  id_t id_i,
    input  rt_t rt_i,
    output id_t sel_o
);
  assign sel_o = rt_i[int'(id_i)];
endmodule
)sv";

    auto h = convertModule(argv0, "unpacked_array_dynamic_index", sv, "");
    expectContains(h, "rt_i_in())[(unsigned)");
    expectNotContains(h, "logic<cpphdl::type_width<array");
}

static void testWideLogicCompoundBitwiseDoesNotTruncateToUint64(const char* argv0)
{
    const std::string sv = R"sv(
module wide_logic_compound_bitwise #(
    parameter int unsigned W = 96
) (
    input  logic [W-1:0] a_i,
    input  logic [W-1:0] b_i,
    input  logic         sel_i,
    output logic [W-1:0] y_o
);
  always_comb begin
    y_o = '0;
    y_o |= sel_i ? a_i : '0;
    y_o |= b_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "wide_logic_compound_bitwise", sv, "");
    expectContains(h, "logic<(uint64_t)(((uint64_t)(W) & ((1ull << 32) - 1ull)))>(y_o_comb) | logic<(uint64_t)(((uint64_t)(W) & ((1ull << 32) - 1ull)))>");
    expectNotContains(h, "(uint64_t)(y_o_comb) |");
    expectNotContains(h, "static_cast<uint64_t>((uint64_t)(a_i_in()))");
    expectNotContains(h, "static_cast<uint64_t>((uint64_t)(b_i_in()))");
}

static void testStructArrayToPackedArrayInputUsesElementPack(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_sink #(
    parameter int unsigned W = 13,
    parameter int unsigned N = 2
) (
    input  logic [N-1:0][W-1:0] data_i,
    output logic [W-1:0]        data_o
);
  assign data_o = data_i[0];
endmodule

module array_struct_input_pack_binding(
    input  logic [12:0] raw_i,
    output logic [12:0] raw_o
);
  typedef struct packed {
    logic [7:0] addr;
    logic [3:0] id;
    logic       last;
  } req_t;

  req_t [1:0] reqs;
  assign reqs[0] = raw_i;
  assign reqs[1] = '0;

  packed_array_sink #(
    .W($bits(req_t)),
    .N(2)
  ) u_sink (
    .data_i(reqs),
    .data_o(raw_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "array_struct_input_pack_binding", sv, "");
    expectContains(h, "__port_bind_u_sink_data_i_in_packed_array_comb_func");
    expectContains(h, "cpphdl::pack_value<__cpphdl_target_t::ELEMENT_BITS>(__cpphdl_src[__cpphdl_i])");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_packed_array_comb_func());");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(reqs);");
}

static void testPackedVectorToUnpackedArrayInputUsesExplicitSlices(const char* argv0)
{
    const std::string sv = R"sv(
module word_mux #(
    parameter int unsigned NINPUT = 2,
    parameter int unsigned DATA_WIDTH = 32
) (
    input  logic [DATA_WIDTH-1:0] data_i [NINPUT-1:0],
    input  logic                  sel_i,
    output logic [DATA_WIDTH-1:0] data_o
);
  assign data_o = data_i[sel_i];
endmodule

module packed_vector_to_array_port(
    input  logic [63:0] refill_data_i,
    input  logic        sel_i,
    output logic [31:0] out_o
);
  word_mux #(
    .NINPUT(2),
    .DATA_WIDTH(32)
  ) u_mux (
    .data_i(refill_data_i),
    .sel_i(sel_i),
    .data_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_vector_to_array_port", sv, "");
    expectContains(h, "__port_bind_u_mux_data_i_in_packed_to_array_comb_func");
    expectContains(h, "cpphdl::pack_value<__cpphdl_target_array_t::SIZE_BITS>(__cpphdl_src)");
    expectContains(h, "cpphdl::unpack_value<__cpphdl_target_elem_t>");
    expectContains(h, "u_mux.data_i_in = _ASSIGN_COMB(__port_bind_u_mux_data_i_in_packed_to_array_comb_func());");
    expectNotContains(h, "array<logic<32>,2>(refill_data_i_in())");
}

static void testSameStructArrayInputPortUsesDirectArrayBinding(const char* argv0)
{
    const std::string sv = R"sv(
package same_struct_array_pkg;
  typedef struct packed {
    logic [31:0] addr;
    logic [3:0]  id;
  } req_t;
endpackage

module struct_array_sink #(
    parameter int unsigned N = 3
) (
    input same_struct_array_pkg::req_t req_i [N-1:0],
    output logic [31:0] addr_o
);
  assign addr_o = req_i[2].addr;
endmodule

module same_struct_array_input_port(
    input  logic [31:0] addr_i,
    output logic [31:0] addr_o
);
  import same_struct_array_pkg::*;

  req_t reqs [2:0];
  assign reqs[0] = '0;
  assign reqs[1] = '0;
  assign reqs[2].addr = addr_i;
  assign reqs[2].id = 4'hf;

  struct_array_sink #(
    .N(3)
  ) u_sink (
    .req_i(reqs),
    .addr_o(addr_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "same_struct_array_input_port", sv, "");
    expectContains(h, "__port_bind_u_sink_req_i_in_packed_to_array_comb_func");
    expectContains(h, "if constexpr (std::is_assignable_v<__cpphdl_target_array_t&, __cpphdl_src_t>)");
    expectContains(h, "__port_bind_u_sink_req_i_in_packed_to_array_comb = __cpphdl_src;");
}

static void testSamePackedArrayCombAssignKeepsArrayShape(const char* argv0)
{
    const std::string sv = R"sv(
module same_packed_array_comb_assign #(
    parameter int unsigned N = 2
) (
    input  logic [N-1:0][31:0] in_i,
    output logic [N-1:0][31:0] out_o
);
  logic [N-1:0][31:0] tmp;

  always_comb begin
    tmp = in_i;
  end

  assign out_o = tmp;
endmodule
)sv";

    auto h = convertModule(argv0, "same_packed_array_comb_assign", sv, "same_packed_array_comb_assign.N\t2\n");
    expectContains(h, "_LAZY_COMB(tmp_comb, array<logic<32>");
    expectContains(h, "tmp_comb = in_i_in();");
    expectNotContains(h, "tmp_comb = cpphdl::pack_value<cpphdl::type_width<array<logic<32>");
    expectContains(h, "out_o_comb = tmp_comb_func();");
    expectNotContains(h, "out_o_comb = cpphdl::pack_value<cpphdl::type_width<array<logic<32>");
}

static void testSameUnpackedStructArrayRegAssignKeepsArrayShape(const char* argv0)
{
    const std::string sv = R"sv(
module same_unpacked_struct_array_reg_assign(
    input logic clk_i,
    input logic [31:0] a_i,
    input logic [31:0] b_i,
    output logic [31:0] out_o
);
  typedef struct packed {
    logic [31:0] instr;
    logic [1:0]  ex;
  } item_t;

  item_t mem_q [2];
  item_t mem_n [2];

  always_comb begin
    mem_n = mem_q;
    mem_n[0].instr = a_i;
    mem_n[1].instr = b_i;
  end

  always_ff @(posedge clk_i) begin
    mem_q <= mem_n;
  end

  assign out_o = mem_q[0].instr;
endmodule
)sv";

    auto h = convertModule(argv0, "same_unpacked_struct_array_reg_assign", sv, "");
    expectContains(h, "mem_n_comb = ([&]() -> array<item_t,2>");
    expectContains(h, "__cpphdl_direct = mem_q;");
    expectContains(h, "mem_q._next = mem_n_comb_func();");
    expectNotContains(h, "mem_q._next = cpphdl::unpack_value<array<item_t,2>>");
    expectNotContains(h, "cpphdl::pack_value<cpphdl::type_width<array<item_t,2>>()(mem_n_comb_func())");
}

static void testPackedByteArrayAssignFromUnpackedWordArrayUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module packed_byte_array_from_unpacked_words(
    input  logic [31:0] lo_i,
    input  logic [31:0] hi_i,
    output logic [63:0] out_o
);
  typedef logic [31:0] word_t;
  typedef word_t words_t [2];

  words_t words;
  logic [7:0][7:0] bytes;

  assign words[0] = lo_i;
  assign words[1] = hi_i;

  always_comb begin
    bytes = words;
  end

  assign out_o = bytes;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_byte_array_from_unpacked_words", sv, "");
    expectContains(h, "bytes_comb = ([&]() ->");
    expectContains(h, "using __cpphdl_target_t = array<logic<8>,8,true>;");
    expectContains(h, "auto&& __cpphdl_src = (words_comb_func());");
    expectContains(h, "__cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val);");
    expectNotContains(h, "bytes_comb = words_comb_func();");
}

static void testContinuousPackedByteArrayAssignFromUnpackedWordArrayUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_packed_byte_array_from_unpacked_words(
    input  logic [31:0] lo_i,
    input  logic [31:0] hi_i,
    output logic [63:0] out_o
);
  typedef logic [31:0] word_t;
  typedef word_t words_t [2];

  words_t words;
  logic [7:0][7:0] bytes;

  assign words[0] = lo_i;
  assign words[1] = hi_i;
  assign bytes = words;
  assign out_o = bytes;
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_packed_byte_array_from_unpacked_words", sv, "");
    expectContains(h, "bytes_comb = ([&]() ->");
    expectContains(h, "using __cpphdl_target_t = array<logic<8>,8,true>;");
    expectContains(h, "auto&& __cpphdl_src = (words_comb_func());");
    expectContains(h, "__cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val);");
    expectNotContains(h, "bytes_comb = words_comb_func();");
}

static void testContinuousUnpackedWordArrayAssignFromPackedVectorUsesUnpackValue(const char* argv0)
{
    const std::string sv = R"sv(
module continuous_unpacked_words_from_packed_vector(
    input  logic [63:0] data_i,
    output logic [63:0] out_o
);
  typedef logic [31:0] word_t;
  typedef word_t words_t [2];

  words_t words;

  assign words = data_i;
  assign out_o = words;
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_unpacked_words_from_packed_vector", sv, "");
    expectContains(h, "std::is_assignable_v<words_t&, std::remove_cvref_t<decltype((data_i_in()))>>");
    expectContains(h, "return cpphdl::unpack_value<words_t>(cpphdl::pack_value<cpphdl::type_width<words_t>()>(data_i_in()));");
    expectNotContains(h, "words_comb = data_i_in();");
}

static void testTypeParameterWholeAssignFromPackedVectorUsesUnpackValue(const char* argv0)
{
    const std::string sv = R"sv(
module type_parameter_whole_assign_from_packed #(
    parameter type dtype = logic [63:0]
) (
    input  logic [63:0] data_i,
    output logic [63:0] out_o
);
  dtype data;

  assign data = data_i;
  assign out_o = data;
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_whole_assign_from_packed", sv, "");
    expectContains(h, "std::is_assignable_v<dtype&, std::remove_cvref_t<decltype((data_i_in()))>>");
    expectContains(h, "return cpphdl::unpack_value<dtype>(cpphdl::pack_value<cpphdl::type_width<dtype>()>(data_i_in()));");
    expectNotContains(h, "data_comb = data_i_in();");
}

static void testTypeParameterCastFromPackedByteArrayUsesPackUnpack(const char* argv0)
{
    const std::string sv = R"sv(
module type_parameter_cast_from_packed_byte_array #(
    parameter type dtype = logic [1:0][31:0]
) (
    input  logic [31:0] lo_i,
    input  logic [31:0] hi_i,
    output logic [63:0] out_o
);
  logic [7:0][7:0] bytes;
  dtype data;

  assign bytes = {hi_i, lo_i};
  assign data = dtype'(bytes);
  assign out_o = data;
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_cast_from_packed_byte_array", sv, "");
    expectContains(h, "using __cpphdl_cast_t = dtype;");
    expectContains(h, "cpphdl::sv_cast<__cpphdl_cast_t>(bytes_comb_func())");
}

static void testPackedArrayToStructArrayCombAssignUsesElementUnpack(const char* argv0)
{
    const std::string sv = R"sv(
module array_bit_to_struct_assign(
    input  logic [12:0] raw_i,
    output logic        last_o
);
  typedef struct packed {
    logic [7:0] addr;
    logic [3:0] id;
    logic       last;
  } resp_t;

  logic [1:0][12:0] raw;
  resp_t [1:0] resp;

  assign raw[0] = raw_i;
  assign raw[1] = '0;
  assign resp = raw;
  assign last_o = resp[0].last;
endmodule
)sv";

    auto h = convertModule(argv0, "array_bit_to_struct_assign", sv, "");
    expectContains(h, "using __cpphdl_target_array_t = array<resp_t,");
    expectContains(h, "cpphdl::unpack_value<resp_t>(cpphdl::pack_value<cpphdl::type_width<resp_t>()>(__cpphdl_src[__cpphdl_i]))");
    expectNotContains(h, "resp_comb = raw_comb_func();");
}

static void testPackedArrayOutputToStructArrayUsesElementUnpack(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_source(
    input  logic [12:0] raw_i,
    output logic [1:0][12:0] raw_o
);
  assign raw_o[0] = raw_i;
  assign raw_o[1] = '0;
endmodule

module array_output_struct_unpack(
    input  logic [12:0] raw_i,
    output logic        last_o
);
  typedef struct packed {
    logic [7:0] addr;
    logic [3:0] id;
    logic       last;
  } resp_t;

  resp_t [1:0] resp;
  packed_array_source u_source (
    .raw_i(raw_i),
    .raw_o(resp)
  );
  assign last_o = resp[0].last;
endmodule
)sv";

    auto h = convertModule(argv0, "array_output_struct_unpack", sv, "");
    expectContains(h, "__port_bind_u_source_raw_o_out_unpacked_array_comb_func");
    expectContains(h, "using __cpphdl_target_array_t = array<resp_t,");
    expectContains(h, "if constexpr (std::is_assignable_v<__cpphdl_target_elem_t&, __cpphdl_src_elem_t>)");
    expectContains(h, "__port_bind_u_source_raw_o_out_unpacked_array_comb[__cpphdl_i] = __cpphdl_src[__cpphdl_i];");
    expectNotContains(h, "resp_comb = u_source.raw_o_out();");
}

static void testIndexedUnpackedArrayOutputToPackedVectorUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module indexed_array_to_packed_output #(
    parameter int unsigned WR_WIDTH = 64,
    parameter int unsigned RD_WIDTH = 128,
    parameter int unsigned DEPTH = 2
) (
    input  logic                         sel_i,
    output logic [RD_WIDTH-1:0]          rdata_o
);
  localparam int unsigned WR_WORDS = RD_WIDTH / WR_WIDTH;
  typedef logic [WR_WIDTH-1:0] wdata_t;
  typedef logic [RD_WIDTH-1:0] rdata_t;

  wdata_t [DEPTH-1:0][WR_WORDS-1:0] buf_q;
  assign rdata_o = buf_q[sel_i];
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_array_to_packed_output", sv, "");
    expectContains(h, "_PORT(logic<(uint64_t)(((uint64_t)(RD_WIDTH) & ((1ull << 32) - 1ull)))>) rdata_o_out = _ASSIGN_COMB( rdata_o_comb_func() );");
    expectContains(h, "rdata_o_comb = cpphdl::pack_value<cpphdl::type_width<logic<");
    expectContains(h, ">()>(buf_q[(unsigned)");
    expectNotContains(h, "rdata_o_out = _ASSIGN( buf_q");
    expectNotContains(h, "rdata_o_out = _ASSIGN(buf_q");
}

static void testPackedStructInputToLogicPortUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_logic_sink #(
    parameter int unsigned W = 1
) (
    input  logic [W-1:0] data_i,
    output logic [63:0]  data_o
);
  assign data_o = data_i[64:1];
endmodule

module packed_struct_to_logic_input(
    input  logic [63:0] raw_i,
    output logic [63:0] raw_o
);
  typedef struct packed {
    logic [1:0]  error;
    logic [3:0]  id;
    logic [63:0] data;
    logic        last;
  } resp_t;

  resp_t resp;
  assign resp.error = '0;
  assign resp.id = '0;
  assign resp.data = raw_i;
  assign resp.last = 1'b1;

  packed_struct_logic_sink #(
    .W($bits(resp_t))
  ) u_sink (
    .data_i(resp),
    .data_o(raw_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_to_logic_input", sv, "");
    expectContains(h, "__port_bind_u_sink_data_i_in_comb = cpphdl::pack_value<cpphdl::type_width<std::remove_cvref_t<decltype(u_sink.data_i_in())>>()");
    expectContains(h, "(resp_comb_func());");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(cpphdl::pack_value<");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_comb = logic<");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(resp_comb_func());");
}

static void testPackedStructCastAssignedToVectorUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
package packed_struct_cast_pkg;
  typedef struct packed {
    logic [1:0] a;
    logic [1:0] b;
  } src_t;
  typedef struct packed {
    logic [3:0] raw;
  } dst_t;
endpackage

module packed_struct_cast_to_vector(
    input  packed_struct_cast_pkg::src_t in_i,
    output logic [3:0]                   out_o
);
  assign out_o = packed_struct_cast_pkg::dst_t'(in_i);
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_cast_to_vector", sv, "");
    expectContains(h, "out_o_comb = cpphdl::pack_value<cpphdl::type_width<logic<4>>()>(in_i_in());");
    expectNotContains(h, "out_o_comb = cpphdl::sv_cast<packed_struct_cast_pkg::dst_t>");
}

static void testPackedStructInputPortUsesPackUnpackForDistinctStructTypes(const char* argv0)
{
    const std::string sv = R"sv(
module struct_sink #(parameter type child_t = logic [12:0]) (
    input child_t data_i,
    output logic [3:0] id_o
);
  assign id_o = data_i.id;
endmodule

module packed_struct_input_pack_binding(output logic [3:0] id_o);
  typedef struct packed {
    logic [3:0] id;
    logic [7:0] data;
    logic last;
  } parent_t;

  typedef struct packed {
    logic [3:0] id;
    logic [7:0] data;
    logic last;
  } child_t;

  parent_t resp;
  assign resp.id = 4'h8;
  assign resp.data = 8'ha5;
  assign resp.last = 1'b1;

  struct_sink #(
    .child_t(child_t)
  ) u_sink (
    .data_i(resp),
    .id_o(id_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_input_pack_binding", sv, "");
    expectContains(h, "__port_bind_u_sink_data_i_in_comb = cpphdl::unpack_value<std::remove_cvref_t<decltype(u_sink.data_i_in())>>(cpphdl::pack_value<cpphdl::type_width<std::remove_cvref_t<decltype(u_sink.data_i_in())>>()");
    expectContains(h, ">(resp_comb_func()));");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_comb = std::remove_cvref_t<decltype(u_sink.data_i_in())>(resp);");
}

static void testChildLogicAliasInputFieldBindingDoesNotUseStructUnpack(const char* argv0)
{
    const std::string sv = R"sv(
module logic_alias_sink #(parameter int unsigned WIDTH = 64) (
    input logic clk_i,
    input logic rst_ni,
    input data_t data_i,
    output data_t data_o
);
  typedef logic [WIDTH-1:0] data_t;
  assign data_o = data_i;
endmodule

module logic_alias_input_field_binding(output logic [63:0] data_o);
  typedef struct packed {
    logic [63:0] data;
    logic        last;
  } resp_t;

  resp_t resp;
  assign resp.data = 64'hfc02721301320213;
  assign resp.last = 1'b1;

  logic_alias_sink #(
    .WIDTH(64)
  ) u_sink (
    .clk_i(1'b0),
    .rst_ni(1'b1),
    .data_i(resp.data),
    .data_o(data_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "logic_alias_input_field_binding", sv, "");
    expectContains(h, "__port_bind_u_sink_data_i_in_comb_func()");
    expectContains(h, "_LAZY_COMB(__port_bind_u_sink_data_i_in_comb");
    expectContains(h, "__port_bind_u_sink_data_i_in_comb = std::remove_cvref_t<decltype(u_sink.data_i_in())>(resp_data_comb_func());");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(cpphdl::pack_value<");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_comb = cpphdl::unpack_value<std::remove_cvref_t<decltype(u_sink.data_i_in())>>");
}

static void testArrayOutputPacksIntoPackedField(const char* argv0)
{
    const std::string sv = R"sv(
module array_output_source (
    output logic [1:0][3:0] data_o
);
  assign data_o[0] = 4'ha;
  assign data_o[1] = 4'hb;
endmodule

module array_output_packed_field (
    output logic [7:0] out_o
);
  typedef struct packed {
    logic [7:0] be;
  } req_t;

  req_t req;
  array_output_source u_src (
    .data_o(req.be)
  );
  assign out_o = req.be;
endmodule
)sv";

    auto h = convertModule(argv0, "array_output_packed_field", sv, "");
    expectContains(h, "req_comb.be = cpphdl::unpack_value<logic<8>>(cpphdl::pack_value<cpphdl::type_width<logic<8>>()>(u_src.data_o_out()));");
    expectNotContains(h, "req_comb.be = u_src.data_o_out()[0];");
}

static void testAlwaysCombArrayElementUpdatesMergeWithContinuousOutputComb(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       data_gnt;
  logic [7:0] data;
} rsp_t;

module always_comb_array_merge (
    input  rsp_t       from_i [3:0],
    input  logic       sel_i,
    output rsp_t       ex_o   [2:0],
    output rsp_t       acc_o  [1:0]
);
  assign ex_o[0]  = from_i[0];
  assign ex_o[1]  = from_i[1];
  assign acc_o[0] = from_i[2];

  always_comb begin
    ex_o[2]  = from_i[3];
    acc_o[1] = from_i[3];

    ex_o[2].data_gnt  &= sel_i;
    acc_o[1].data_gnt &= !sel_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "always_comb_array_merge", sv, "");
    expectContains(h, "ex_o_comb[(unsigned)(uint64_t)");
    expectContains(h, "= (from_i_in())[(unsigned)(uint64_t)");
    expectContains(h, "ex_o_comb[2].data_gnt &= sel_i_in();");
    expectContains(h, "acc_o_comb[(unsigned)(uint64_t)");
    expectContains(h, "acc_o_comb[1].data_gnt &= !sel_i_in();");
    expectNotContains(h, "ex_o[2].data_gnt");
    expectNotContains(h, "acc_o[1].data_gnt");
}

static void testAlwaysCombArrayElementUpdatesMergeWithExistingInternalComb(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       data_gnt;
  logic [7:0] data;
} rsp_t;

module array_merge_sink (
    input rsp_t req_i [2:0]
);
endmodule

module always_comb_internal_array_merge (
    input rsp_t from_i [3:0],
    input logic sel_i
);
  rsp_t routed [2:0];

  assign routed[0] = from_i[0];
  assign routed[1] = from_i[1];

  always_comb begin
    routed[2] = from_i[3];
    routed[2].data_gnt &= sel_i;
  end

  array_merge_sink u_sink (
    .req_i(routed)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "always_comb_internal_array_merge", sv, "");
    expectContains(h, "routed_comb[(unsigned)(uint64_t)");
    expectContains(h, "= (from_i_in())[(unsigned)(uint64_t)");
    expectContains(h, "routed_comb[2].data_gnt &= sel_i_in();");
    expectContains(h, "u_sink.req_i_in = _ASSIGN_COMB(routed_comb_func());");
    expectNotContains(h, "routed[2].data_gnt");
}

static void testInputPortDependentCombMethodIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module ready_mux (
    input  logic [1:0] ready_i,
    input  logic       sel_i,
    output logic       ready_o
);
  always_comb begin
    ready_o = ready_i[sel_i];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "ready_mux", sv, "");
    expectContains(h, "ready_i_in()");
    expectContains(h, "sel_i_in()");
    expectNotContains(h, "_LAZY_COMB(ready_o_comb");
}

static void testInputArrayMuxCombMethodIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module array_ready_mux #(
    parameter int unsigned N = 2,
    parameter int unsigned W = 1,
    parameter type data_t = logic [W-1:0]
) (
    input  data_t data_i [N],
    input  logic [$clog2(N)-1:0] sel_i,
    output data_t data_o
);
  always_comb begin
    data_o = '0;
    for (int i = 0; i < N; i++) begin
      if (i == int'(sel_i)) data_o = data_i[i];
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "array_ready_mux", sv, "");
    expectContains(h, "data_o_comb_func()");
    expectContains(h, "data_i_in())[(unsigned");
    expectContains(h, "sel_i_in()");
    expectNotContains(h, "_LAZY_COMB(data_o_comb");
}

static void testInputPortDependentStructCombMethodIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module struct_response_adapter (
    input  logic       valid_i,
    input  logic [7:0] data_i,
    output resp_t      resp_o
);
  typedef struct packed {
    logic       data_rvalid;
    logic [7:0] data_rdata;
  } resp_t;

  always_comb begin
    resp_o.data_rvalid = valid_i;
    resp_o.data_rdata = data_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "struct_response_adapter", sv, "");
    expectContains(h, "valid_i_in()");
    expectContains(h, "data_i_in()");
    expectContains(h, "_LAZY_COMB(resp_o_comb");
}

static void testCombUsedBySequentialWorkIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module comb_used_by_sequential_work(
    input  logic       clk_i,
    input  logic       rst_ni,
    input  logic       req_i,
    input  logic       ready_i,
    input  logic [2:0] gnt_i,
    output logic [2:0] q_o
);
  logic       w;
  logic [2:0] q;

  assign w = req_i & ready_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      q <= '0;
    end else if (w) begin
      q <= gnt_i;
    end
  end

  assign q_o = q;
endmodule
)sv";

    auto h = convertModule(argv0, "comb_used_by_sequential_work", sv, "");
    expectContains(h, "logic<1> w_comb;");
    expectContains(h, "logic<1>& w_comb_func()");
    expectContains(h, "if (w_comb_func())");
    expectNotContains(h, "_LAZY_COMB(w_comb");
}

static void testCombMethodCallingCombMethodIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module dependent_comb_outputs(
    input  logic a_i,
    input  logic b_i,
    output logic y_o
);
  logic x;
  always_comb begin
    x = a_i;
  end
  always_comb begin
    y_o = x | b_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "dependent_comb_outputs", sv, "");
    expectContains(h, "x_comb_func()");
    expectNotContains(h, "_LAZY_COMB(x_comb");
    expectNotContains(h, "_LAZY_COMB(y_o_comb");
}

static void testRecursiveCombChildBindingStaysLazy(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic tag;
  logic unc;
} req_t;

module recursive_comb_sink(
    input req_t req_i
);
endmodule

module recursive_comb_child_binding(
    input logic a_i
);
  req_t req;
  logic unc;

  assign req = '{tag: a_i, unc: unc};
  assign unc = req.tag;

  recursive_comb_sink child_i (
    .req_i(req)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "recursive_comb_child_binding", sv, "");
    expectContains(h, "_LAZY_COMB(req_comb");
    expectContains(h, "__port_bind_child_i_req_i_in_comb_func()");
    expectContains(h, "_LAZY_COMB(__port_bind_child_i_req_i_in_comb");
    expectContains(h, "child_i.req_i_in = _ASSIGN_COMB(__port_bind_child_i_req_i_in_comb_func());");
}

static void testConditionalUnbasedOneUsesTargetWidth(const char* argv0)
{
    const std::string sv = R"sv(
module conditional_unbased_one_vector(
    input  logic       sel_i,
    output logic [7:0] mask_o
);
  always_comb begin
    mask_o = sel_i ? '1 : '0;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "conditional_unbased_one_vector", sv, "");
    expectContains(h, "((1ull << 8) - 1ull)");
    expectNotContains(h, "logic<8>(1)");
    expectNotContains(h, "? 1 : 0");
}

static void testReplicationOfArrayAliasElementUsesElementWidth(const char* argv0)
{
    const std::string sv = R"sv(
module replicate_array_alias_element #(
    parameter int unsigned WAYS = 2
) (
    input  logic [31:0] data_i [2],
    output logic [31:0] data_o [WAYS]
);
  typedef logic [31:0] word_t;
  typedef word_t access_data_t [2];
  typedef word_t way_data_t [WAYS];

  access_data_t data;
  way_data_t wentry;

  assign data = data_i;

  always_comb begin
    wentry = '0;
    for (int unsigned j = 0; j < 2; j++) begin
      wentry = {WAYS{data[j]}};
    end
  end

  assign data_o = wentry;
endmodule
)sv";

    auto h = convertModule(argv0, "replicate_array_alias_element", sv, "");
    expectContains(h, "* (uint64_t)(32)");
    expectContains(h, "cpphdl::pack_value<32>(data_comb_func()");
    expectNotContains(h, "* (uint64_t)(1))> __cpphdl_rep");
    expectNotContains(h, "cpphdl::pack_value<1>(logic<1>");
}

static void testReplicationOfTypeParameterArrayElementUsesElementAccess(const char* argv0)
{
    const std::string sv = R"sv(
module replicate_type_parameter_array_element #(
    parameter type access_data_t = logic [31:0] [2],
    parameter int unsigned WAYS = 2
) (
    input  access_data_t data_i,
    output logic [31:0]  data_o [WAYS]
);
  typedef logic [31:0] word_t;
  typedef word_t way_data_t [WAYS];

  way_data_t wentry;

  always_comb begin
    wentry = '0;
    for (int unsigned j = 0; j < 2; j++) begin
      wentry = {WAYS{data_i[j]}};
    end
  end

  assign data_o = wentry;
endmodule
)sv";

    auto h = convertModule(argv0, "replicate_type_parameter_array_element", sv, "");
    expectContains(h, "(data_i_in())[(unsigned)");
    expectContains(h, "cpphdl::type_width<std::remove_cvref_t<decltype((data_i_in())[(unsigned)");
    expectContains(h, "* (uint64_t)((cpphdl::type_width<std::remove_cvref_t<decltype((data_i_in())[(unsigned)");
    expectNotContains(h, "logic<64> __cpphdl_rep");
    expectNotContains(h, "logic<type_width<access_data_t>()>(data_i_in())");
    expectNotContains(h, "* (uint64_t)((type_width<access_data_t>()))");
    expectNotContains(h, ">> (unsigned)");
}

static void testTypeParameterArrayElementRangeSelectUsesElementWidth(const char* argv0)
{
    const std::string sv = R"sv(
module type_parameter_array_element_range #(
    parameter type data_t = logic [63:0] [1],
    parameter type be_t = logic [7:0] [1],
    parameter int unsigned WORDS = 1,
    parameter int unsigned WORD_WIDTH = 64
) (
    input  data_t read_i,
    input  data_t write_i,
    input  be_t   be_i,
    output data_t out_o
);
  data_t merged;

  always_comb begin
    merged = write_i;
    for (int unsigned i = 0; i < WORDS; i++) begin
      for (int unsigned j = 0; j < WORD_WIDTH / 8; j++) begin
        merged[i][j*8 +: 8] =
          (read_i[i][j*8 +: 8] & {8{~be_i[i][j]}}) |
          (write_i[i][j*8 +: 8] & {8{be_i[i][j]}});
      end
    end
  end

  assign out_o = merged;
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_array_element_range", sv, "");
    expectContains(h, "(read_i_in())[(unsigned)");
    expectContains(h, "(write_i_in())[(unsigned)");
    expectContains(h, "logic<((uint64_t)(8)");
    expectNotContains(h, "logic<1>(read_i_in()");
    expectNotContains(h, "logic<1>((read_i_in())");
    expectNotContains(h, "logic<1>(write_i_in()");
    expectNotContains(h, "logic<1>((write_i_in())");
    expectNotContains(h, "= logic<1>(((((uint64_t)(logic<((uint64_t)(8)");
}

static void testTypeParameterArrayElementRangeConditionalKeepsByteWidth(const char* argv0)
{
    const std::string sv = R"sv(
module type_parameter_array_element_range_conditional #(
    parameter type data_t = logic [63:0] [1],
    parameter type be_t = logic [7:0] [1],
    parameter int unsigned WORDS = 1,
    parameter int unsigned WORD_WIDTH = 64
) (
    input  data_t old_i,
    input  data_t new_i,
    input  be_t   be_i,
    output data_t out_o
);
  data_t merged;

  always_comb begin
    merged = old_i;
    for (int unsigned w = 0; w < WORDS; w++) begin
      for (int unsigned b = 0; b < WORD_WIDTH / 8; b++) begin
        merged[w][b*8 +: 8] = be_i[w][b] ? new_i[w][b*8 +: 8]
                                         : old_i[w][b*8 +: 8];
      end
    end
  end

  assign out_o = merged;
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_array_element_range_conditional", sv, "");
    expectContains(h, "? logic<((uint64_t)(8)");
    expectContains(h, ": logic<((uint64_t)(8)");
    expectNotContains(h, "static_cast<bool>((uint64_t)(logic<((uint64_t)(8)");
}

static void testOutOfOrderNamedAggregateBecomesPositional(const char* argv0)
{
    const std::string sv = R"sv(
module out_of_order_named_aggregate(
    input  logic [3:0] a_i,
    input  logic [3:0] b_i,
    output logic [8:0] out_o
);
  typedef struct packed {
    logic [3:0] a;
    logic [3:0] b;
    logic       c;
  } req_t;

  req_t req;

  always_comb begin
    req = '{b: b_i, a: a_i, c: 1'b1};
  end

  assign out_o = req;
endmodule
)sv";

    auto h = convertModule(argv0, "out_of_order_named_aggregate", sv, "");
    expectContains(h, "req_comb = req_t{ a_i_in(), b_i_in(), logic<1>(0b1) };");
    expectNotContains(h, "req_t{ .b =");
}

static void testCombArrayElementBitSelectCastOperandIsParenthesized(const char* argv0)
{
    const std::string sv = R"sv(
module comb_array_element_bit_select(
    input  logic [3:0] data_i [2],
    output logic [1:0] out_o
);
  logic [3:0] idx_ds [2];

  always_comb begin
    idx_ds = data_i;
  end

  always_comb begin
    out_o = '0;
    for (int unsigned i = 0; i < 4; i++) begin
      if (idx_ds[0][i]) begin
        out_o[0] = 1'b1;
      end
      if (idx_ds[1][i]) begin
        out_o[1] = 1'b1;
      end
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "comb_array_element_bit_select", sv, "");
    expectContains(h, "static_cast<logic<4>>(idx_ds_comb_func()[(unsigned)");
    expectNotContains(h, "logic<4>(idx_ds_comb_func()[(unsigned)");
}

static void testPackedArrayComparisonEmitsNumericPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_compare #(
    parameter int unsigned N = 1
) (
    input  logic [31:0] tdata2_i,
    input  instr_arr_t  orig_instr_i,
    output logic        matched_o
);
  typedef logic [31:0] instr_arr_t [N];

  always_comb begin
    matched_o = tdata2_i == orig_instr_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_compare", sv, "");
    expectContains(h, "== (uint64_t)(cpphdl::pack_value<cpphdl::type_width<array<logic<32>");
    expectNotContains(h, "== cpphdl::pack_value<cpphdl::type_width<array<logic<32>");
}

static void testTypeParameterStructFieldConditionalConcatUsesFieldType(const char* argv0)
{
    const std::string sv = R"sv(
module type_parameter_struct_field_conditional_concat #(
    parameter bit Swap = 1'b1,
    parameter type resp_t = logic
) (
    input  logic [63:0] a_i,
    input  logic [63:0] b_i,
    input  logic [63:0] c_i,
    input  logic [63:0] d_i,
    output resp_t       resp_o
);
  function automatic logic [63:0] flip64(input logic [63:0] in);
    return in;
  endfunction

  assign resp_o.data = Swap ? {flip64(a_i), flip64(b_i), flip64(c_i), flip64(d_i)}
                            : {a_i, b_i, c_i, d_i};
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_struct_field_conditional_concat", sv, "");
    expectContains(h, "resp_o_comb.data = Swap ? logic<256>");
    expectContains(h, " : logic<256>");
    expectContains(h, "logic<256>((uint64_t)(flip64");
    expectNotContains(h, "sv_cast<logic<4>>");
    expectNotContains(h, "logic<4>(0)");
}

static void testGenerateLocalCombSignalIsPerLoopIndex(const char* argv0)
{
    const std::string sv = R"sv(
module generate_local_comb_signal #(
    parameter int unsigned N = 8,
    parameter int unsigned WORD = 4
) (
    input  logic                 valid_i,
    input  logic [N-1:0]         be_i,
    input  logic [1:0]           word_i,
    input  logic [N-1:0][7:0]    clean_i,
    input  logic [N-1:0][7:0]    dirty_i,
    output logic [N-1:0][7:0]    out_o
);
  for (genvar i = 0; i < N; i++) begin : gen_byte
    logic sel;

    if (WORD > 1) begin : gen_word
      always_comb begin
        automatic int unsigned word;
        word = i / WORD;
        sel = valid_i && be_i[i] && (word == word_i);
      end
    end else begin : gen_byte_only
      assign sel = valid_i && be_i[i];
    end

    assign out_o[i] = sel ? dirty_i[i] : clean_i[i];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generate_local_comb_signal", sv, "");
    expectContains(h, "if constexpr");
    expectContains(h, "([&]() { logic<1> sel = {};");
    expectContains(h, "word =");
    expectContains(h, "word_i_in()");
    expectContains(h, "return sel; }())");
    expectContains(h, "? logic<8>");
    expectNotContains(h, "_LAZY_COMB(sel_comb");
    expectNotContains(h, "sel_comb_func() ? logic<8>");
}

static void testAssignDrivenStructFieldReadUsesFieldProjection(const char* argv0)
{
    const std::string sv = R"sv(
module assign_driven_struct_field_read (
    input  logic       sel_i,
    input  logic [3:0] tag_i,
    output logic       nc_o
);
  typedef struct packed {
    logic [3:0] tag;
    logic       nc;
    logic [7:0] data;
  } req_t;

  req_t req;
  req_t a;
  req_t b;
  logic nc;

  assign nc = |req.tag;
  assign a = '{tag: tag_i, nc: nc, data: 8'h11};
  assign b = '{tag: 4'h0, nc: 1'b0, data: 8'h22};
  assign req = sel_i ? a : b;
  assign nc_o = nc;
endmodule
)sv";

    auto h = convertModule(argv0, "assign_driven_struct_field_read", sv, "");
    expectContains(h, "req_tag_comb_func()");
    expectContains(h, "_LAZY_COMB(a_tag_comb");
    expectContains(h, "_LAZY_COMB(b_tag_comb");
    expectNotContains(h, "req_comb_func().tag");
    expectNotContains(h, "a_comb_func().tag");
    expectNotContains(h, "b_comb_func().tag");
}

static void testDefaultStructFieldProjectionUsesTypedZero(const char* argv0)
{
    const std::string sv = R"sv(
module default_struct_field_projection (
    output logic [3:0] tag_o
);
  typedef struct packed {
    logic [3:0] tag;
    logic       valid;
  } req_t;

  req_t req;
  logic [3:0] tag;

  assign req = '0;
  assign tag = req.tag;
  assign tag_o = tag;
endmodule
)sv";

    auto h = convertModule(argv0, "default_struct_field_projection", sv, "");
    expectContains(h, "req_tag_comb_func()");
    expectNotContains(h, "({}).tag");
    expectNotContains(h, "= ({}).tag");
}

static void testAggregateOutputFieldReadUsesExtractedFieldComb(const char* argv0)
{
    const std::string sv = R"sv(
module aggregate_output_field_read (
    input  logic        req_i,
    input  logic [31:0] vaddr_i,
    output logic [31:0] addr_o
);
  typedef struct packed {
    logic        ready;
    logic [31:0] vaddr;
  } rsp_t;

  rsp_t rsp_o;

  always_comb begin
    rsp_o = '0;
    rsp_o.ready = rsp_o.ready | req_i;
    rsp_o.vaddr = vaddr_i;
  end

  assign addr_o = (rsp_o.ready & req_i) ? rsp_o.vaddr : 32'h0;
endmodule
)sv";

    auto h = convertModule(argv0, "aggregate_output_field_read", sv, "");
    expectContains(h, "rsp_o_ready_comb_func()");
    expectContains(h, "rsp_o_vaddr_comb_func()");
    expectContains(h, "rsp_o_ready_comb = logic<1>");
    expectContains(h, "req_i_in()");
    expectContains(h, "rsp_o_vaddr_comb = vaddr_i_in()");
    expectNotContains(h, "rsp_o_ready_comb.ready");
    expectNotContains(h, "rsp_o_ready_comb = std::remove_cvref_t<decltype(std::declval<rsp_t>().ready)>{};");
    expectNotContains(h, "rsp_o_vaddr_comb = std::remove_cvref_t<decltype(std::declval<rsp_t>().vaddr)>{};");
}

static void testExtractedScalarFieldCombSkipsSiblingStructFields(const char* argv0)
{
    const std::string sv = R"sv(
module extracted_scalar_field_skips_siblings (
    input  logic        kill_i,
    input  logic        data_i,
    input  logic [7:0]  wdata_i,
    output logic        kill_o
);
  typedef struct packed {
    logic        kill_req;
    logic        data_we;
    logic [7:0]  data_wdata;
  } req_t;

  req_t req_o;

  always_comb begin
    req_o = '0;
    req_o.data_we = data_i;
    req_o.data_wdata = wdata_i;
    req_o.kill_req = req_o.kill_req | kill_i;
  end

  assign kill_o = req_o.kill_req;
endmodule
)sv";

    auto h = convertModule(argv0, "extracted_scalar_field_skips_siblings", sv, "");
    expectContains(h, "req_o_kill_req_comb_func()");
    expectContains(h, "req_o_kill_req_comb = logic<1>");
    expectContains(h, "kill_i_in()");
    expectNotContains(h, "req_o_kill_req_comb.data_we");
    expectNotContains(h, "req_o_kill_req_comb.data_wdata");
}

static void testFieldExtractionKeepsWholeAggregateSiblingBranch(const char* argv0)
{
    const std::string sv = R"sv(
module field_extraction_whole_aggregate_sibling (
    input  logic        mode_i,
    input  logic        sel_i,
    input  logic [7:0]  tag_i,
    output logic        any_tag_o
);
  typedef struct packed {
    logic [3:0] off;
    logic [7:0] tag;
  } req_t;

  req_t req;
  req_t store_req;
  req_t flush_req;

  always_comb begin
    if (mode_i) begin
      req.off = 4'h3;
      req.tag = 8'h00;
    end else begin
      store_req = '{off: 4'h8, tag: tag_i};
      flush_req = '{off: 4'h0, tag: 8'h00};
      req = sel_i ? store_req : flush_req;
    end
  end

  assign any_tag_o = |req.tag;
endmodule
)sv";

    auto h = convertModule(argv0, "field_extraction_whole_aggregate_sibling", sv, "");
    expectContains(h, "req_tag_comb_func()");
    expectContains(h, "req_tag_comb = logic<8>(0x00);");
    expectContains(h, "tag_i_in()");
    expectContains(h, "(cpphdl::sv_cast<req_t>(__comb_local_store_req)).tag");
    expectNotContains(h, "req_tag_comb = std::remove_cvref_t<decltype(std::declval<req_t>().tag)>{};\n        if (mode_i_in())");
}

static void testFieldProjectionThroughSelectedAggregateAvoidsWholeCombRecursion(const char* argv0)
{
    const std::string sv = R"sv(
module field_projection_selected_aggregate_no_recursion (
    input  logic       sel_i,
    input  logic [7:0] tag_i,
    output logic       uncached_o
);
  typedef struct packed {
    logic uncacheable;
  } pma_t;

  typedef struct packed {
    logic [7:0] tag;
    pma_t       pma;
  } req_t;

  req_t req;
  req_t store_req;
  req_t flush_req;
  logic is_uncacheable;

  assign is_uncacheable = |req.tag;
  assign store_req = '{tag: tag_i, pma: '{uncacheable: is_uncacheable}};
  assign flush_req = '{tag: 8'h00, pma: '{uncacheable: 1'b0}};
  assign req = sel_i ? store_req : flush_req;
  assign uncached_o = is_uncacheable;
endmodule
)sv";

    auto h = convertModule(argv0, "field_projection_selected_aggregate_no_recursion", sv, "");
    expectContains(h, "req_tag_comb_func()");
    expectContains(h, "store_req_tag_comb_func()");
    expectContains(h, "flush_req_tag_comb_func()");
    expectNotContains(h, "store_req_comb_func()).tag");
    expectNotContains(h, "flush_req_comb_func()).tag");
}

static void testFieldProjectionThroughTypeParamAggregateAvoidsWholeCombRecursion(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic uncacheable;
} field_projection_param_pma_t;

typedef struct packed {
  logic [7:0]                  tag;
  field_projection_param_pma_t pma;
} field_projection_param_req_t;

module field_projection_type_param_no_recursion #(
    parameter type req_t = field_projection_param_req_t
) (
    input  logic       sel_i,
    input  logic [7:0] tag_i,
    output logic       uncached_o
);
  req_t req;
  req_t store_req;
  req_t flush_req;
  logic is_uncacheable;

  assign is_uncacheable = |req.tag;
  assign store_req = '{tag: tag_i, pma: '{uncacheable: is_uncacheable}};
  assign flush_req = '{tag: 8'h00, pma: '{uncacheable: 1'b0}};
  assign req = sel_i ? store_req : flush_req;
  assign uncached_o = is_uncacheable;
endmodule
)sv";

    auto h = convertModule(argv0, "field_projection_type_param_no_recursion", sv, "");
    expectContains(h, "store_req_tag_comb_func()");
    expectContains(h, "flush_req_tag_comb_func()");
    expectContains(h, "req_tag_comb = (sel_i_in() ? store_req_tag_comb_func() : flush_req_tag_comb_func())");
    expectNotContains(h, "store_req_comb_func()).tag");
    expectNotContains(h, "flush_req_comb_func()).tag");
}

static void testFieldCombDoesNotPropagateThroughPlainWholeValueCall(const char* argv0)
{
    const std::string sv = R"sv(
module field_comb_plain_whole_value_no_propagation #(
    parameter int unsigned N = 2
) (
    input  logic [N-1:0] be_i,
    output logic [N-1:0] be_o
);
  typedef struct packed {
    logic [N-1:0] mem_req_w_be;
    logic         last;
  } mem_req_w_t;

  logic [N-1:0] send_be;
  mem_req_w_t   mem_req_write_data_o;

  assign send_be = be_i;
  assign mem_req_write_data_o.mem_req_w_be = send_be;
  assign mem_req_write_data_o.last = 1'b1;
  assign be_o = mem_req_write_data_o.mem_req_w_be;
endmodule
)sv";

    auto h = convertModule(argv0, "field_comb_plain_whole_value_no_propagation", sv, "");
    expectContains(h, "mem_req_write_data_o_mem_req_w_be_comb_func()");
    expectContains(h, "mem_req_write_data_o_mem_req_w_be_comb = send_be_comb_func()");
    expectNotContains(h, "send_be_mem_req_w_be_comb");
    expectNotContains(h, "std::declval<logic<(uint64_t)(N)>>().mem_req_w_be");
}

static void testExtractedFieldConditionalDefaultBranchUsesFieldType(const char* argv0)
{
    const std::string sv = R"sv(
module extracted_field_conditional_default_branch (
    input  logic       a_i,
    input  logic [7:0] tag_a_i,
    output logic [7:0] tag_o
);
  typedef struct packed {
    logic [7:0] tag;
  } req_t;

  req_t req;
  req_t req_a;

  assign req_a = '{tag: tag_a_i};
  assign req = a_i ? req_a : '0;
  assign tag_o = req.tag;
endmodule
)sv";

    auto h = convertModule(argv0, "extracted_field_conditional_default_branch", sv, "");
    expectContains(h, "req_tag_comb_func()");
    expectContains(h, "req_a_tag_comb_func()");
    expectContains(h, ": logic<8>{}");
    expectNotContains(h, ": 0)");
}

static void testSequentialChildInputPortBindingStaysLazy(const char* argv0)
{
    const std::string sv = R"sv(
module seq_child (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic en_i,
    output logic out_o
);
  logic q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) q <= 1'b0;
    else if (en_i) q <= 1'b1;
  end
  assign out_o = q;
endmodule

module sequential_child_input_port_binding (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic en_i,
    output logic out_o
);
  seq_child u_child (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .en_i(en_i),
    .out_o(out_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "sequential_child_input_port_binding", sv, "");
    expectContains(h, "u_child.rst_ni_in = _ASSIGN_COMB(rst_ni_in());");
    expectContains(h, "u_child.en_i_in = _ASSIGN_COMB(en_i_in());");
    expectNotContains(h, "u_child.rst_ni_in = _ASSIGN(std::remove_cvref_t");
    expectNotContains(h, "u_child.en_i_in = _ASSIGN(std::remove_cvref_t");
}

static void testBinaryPrecedencePreservesNestedEqualityUnderBitwise(const char* argv0)
{
    const std::string sv = R"sv(
module binary_precedence_nested_equality(
    input  logic       en_i,
    input  logic [2:0] op_i,
    output logic       hit_o
);
  assign hit_o = en_i & (op_i == 3'b101);
endmodule
)sv";

    auto h = convertModule(argv0, "binary_precedence_nested_equality", sv, "");
    expectContains(h, "en_i_in() & (");
    expectContains(h, " == ");
    expectNotContains(h, "en_i_in() & (uint64_t)(op_i_in()) ==");
}

static void testGenerateBoundDivisionExpressionIsBalanced(const char* argv0)
{
    const std::string sv = R"sv(
module leaf(output logic out_o);
  assign out_o = 1'b0;
endmodule

module generate_bound_division_expression #(
    parameter int unsigned DATA_WIDTH = 64
) (
    output logic [((DATA_WIDTH+63)/64)-1:0] out_o
);
  for (genvar k = 0; k < (DATA_WIDTH+63)/64; k++) begin : gen_leaf
    leaf u_leaf(.out_o(out_o[k]));
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generate_bound_division_expression", sv, "");
    expectContains(h, "array<leaf,");
    expectContains(h, "for (unsigned i = 0;");
    expectNotContains(h, "))));k++)");
    expectNotContains(h, "))));i++)");
}

static void testOneBitUnaryNotInConcatAvoidsLogicOperatorNot(const char* argv0)
{
    const std::string sv = R"sv(
module one_bit_unary_not_concat(
    input  logic a_i,
    output logic [1:0] y_o
);
  assign y_o = {a_i, ~a_i};
endmodule
)sv";

    auto h = convertModule(argv0, "one_bit_unary_not_concat", sv, "");
    expectContains(h, "logic<1>(");
    expectNotContains(h, "~a_i_in()");
    expectNotContains(h, "~(a_i_in())");
}

static void testWideConditionalLogicBranchDoesNotNarrowToUint64(const char* argv0)
{
    const std::string sv = R"sv(
module wide_conditional_logic_branch #(
    parameter int unsigned NOUTPUT = 2,
    parameter int unsigned DATA_WIDTH = 96,
    localparam type data_t = logic [DATA_WIDTH-1:0]
) (
    input  data_t data_i,
    input  logic  sel_i,
    output data_t data_o [NOUTPUT-1:0]
);
  always_comb begin
    for (int unsigned i = 0; i < NOUTPUT; i++) begin
      data_o[i] = sel_i ? data_i : '0;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "wide_conditional_logic_branch", sv,
                           "wide_conditional_logic_branch.DATA_WIDTH\t96\n"
                           "wide_conditional_logic_branch.NOUTPUT\t2\n");
    expectContains(h, "data_o_comb[");
    expectContains(h, "? logic<(uint64_t)(((uint64_t)(DATA_WIDTH)");
    expectContains(h, "(data_i_in())");
    expectNotContains(h, "(uint64_t)(data_i_in())");
}

int main(int argc, char** argv)
{
    assert(argc >= 1);
    testSequentialPartialRegUpdateSeedsNextFromCurrent(argv[0]);
    testResolvedGenerateOutputBeatsInactiveSequentialBranch(argv[0]);
    testParameterizedGenerateOutputCanBePassThroughOrRegistered(argv[0]);
    testInactiveGenerateInstanceLifecycleIsGuarded(argv[0]);
    testStringGenerateSelectsOneSameNamedInstance(argv[0]);
    testGenerateBranchTypedefEnumIsEmitted(argv[0]);
    testOrGenerateSelectsOneSameNamedInstance(argv[0]);
    testTemplateTernaryDefaultIsParenthesized(argv[0]);
    testTypedefIntegerCastIsNamedCast(argv[0]);
    testPackedTypedefCastUsesNumericSource(argv[0]);
    testRuntimeRangeSelectUsesRuntimeBits(argv[0]);
    testSlicedCombDependencyLateBindsMethodCall(argv[0]);
    testDesignatedPatternAssignmentUsesTypedTemporary(argv[0]);
    testIndexedDesignatedPatternUsesElementType(argv[0]);
    testContinuousDesignatedPatternUsesTargetType(argv[0]);
    testEnumPatternListDoesNotBecomeConcat(argv[0]);
    testAggregateInputPortFieldBindingUsesCombMethod(argv[0]);
    testIndexedRegPatternThroughArrayAliasUsesElementType(argv[0]);
    testPackedAggregateBitwiseUpdateUsesPackValue(argv[0]);
    testScalarTypedefParameterWidthIsNotPacked(argv[0]);
    testSizedCastWidthDoesNotLeakRawSvSyntax(argv[0]);
    testBitsOfPortFieldUsesTypeDeclval(argv[0]);
    testFunctionLocalparamAndParameterizedReplication(argv[0]);
    testConfiguredDottedGenerateSelectsOneBranch(argv[0]);
    testNumericWidthCastIsLogicCast(argv[0]);
    testTypeTemplateCastIsNotWidthCast(argv[0]);
    testEnumNamesAreNotParameterSubstituted(argv[0]);
    testChildLocalAliasPortTypeIsSpecialized(argv[0]);
    testLogicCombInputUsesDirectCombBinding(argv[0]);
    testSequentialChildCombInputUsesLazyBinding(argv[0]);
    testDirectParentPortNarrowingDoesNotUseRegBinding(argv[0]);
    testParentPortPartSelectUsesCombBinding(argv[0]);
    testIndexedParentPortInputUsesCombBinding(argv[0]);
    testPackedParentPortElementInputUsesValueBinding(argv[0]);
    testPackedCombArrayElementInputUsesValueBinding(argv[0]);
    testCombVectorBitInputUsesValueBinding(argv[0]);
    testParentPortFieldInputUsesValueBindingAfterAdapter(argv[0]);
    testIndexedLocalCombInputUsesValueBinding(argv[0]);
    testIndexedCombArrayInputUsesValueBinding(argv[0]);
    testScalarParentPortToArrayChildPortUsesAdapter(argv[0]);
    testGeneratedChildArrayKeepsStructuredPortType(argv[0]);
    testGeneratedChildArrayScalarPortUsesCombBinding(argv[0]);
    testWrappedIndexedCombInputUsesCombBinding(argv[0]);
    testArrayInputPortCombBindingIsComplete(argv[0]);
    testArrayInputStructPortAdapterUsesChildPortType(argv[0]);
    testChildInputPortUsesActualTypeForAliasNarrowing(argv[0]);
    testZeroAggregateInputPortUsesActualType(argv[0]);
    testArrayOutputPortToScalarUsesElementZero(argv[0]);
    testSameTypeStructOutputDoesNotPackUnpack(argv[0]);
    testTemplatedSameTypeStructOutputDoesNotPackUnpack(argv[0]);
    testReplicationOfPackedAggregateUsesPackValue(argv[0]);
    testBitsOfTypeParameterDefaultUsesTypeWidth(argv[0]);
    testCombOutputPortIsGetter(argv[0]);
    testCombOutputExpressionIsMaterializedBeforeGetter(argv[0]);
    testCombOutputExpressionDoesNotUseRegBinding(argv[0]);
    testContinuousOutputInputExpressionIsNoCache(argv[0]);
    testContinuousFeedbackOutputInputExpressionIsNoCache(argv[0]);
    testContinuousFeedthroughOutputUsesCombGetter(argv[0]);
    testPowerOperatorPrecedenceInRanges(argv[0]);
    testCombMethodDependenciesEmitBeforeUsers(argv[0]);
    testParameterizedReplicationInConcatIsValidCatItem(argv[0]);
    testBracedReplicationCountIsNumeric(argv[0]);
    testNumericReplicationConstantUsesIntegerMask(argv[0]);
    testNumericConcatConstantUsesIntegerExpr(argv[0]);
    testParameterizedNumericConcatUsesCat(argv[0]);
    testConcatCaseKeepsOperandWidths(argv[0]);
    testConcatCaseStructFieldsKeepOperandWidths(argv[0]);
    testConcatArrayElementBitSelectUsesOneBitWidth(argv[0]);
    testConcatPartSelectKeepsSelectedWidth(argv[0]);
    testIntegerLocalparamConcatIsConstexprNumeric(argv[0]);
    testKnownWidthFunctionDoesNotForceStructArgumentNumeric(argv[0]);
    testParenthesizedWidthCastIsLogicCast(argv[0]);
    testZeroAssignmentToStructFieldUsesValueInit(argv[0]);
    testContinuousZeroAssignmentToStructFieldUsesValueInit(argv[0]);
    testZeroAssignmentToPackedArrayElementUsesPlainZero(argv[0]);
    testImportedPackageTypeBitsUseConfiguredWidth(argv[0]);
    testReductionOfPackedAggregatePacksOperand(argv[0]);
    testReductionOrPortConnectionUsesVectorReduction(argv[0]);
    testChildInputStateExpressionUsesCombBinding(argv[0]);
    testGenerateCombOrSequentialSameSignalUsesCombRead(argv[0]);
    testMismatchedChildOutputIsMaterializedBeforeGetter(argv[0]);
    testPackedChildOutputFieldProjectionUnpacksBeforeFieldRead(argv[0]);
    testTypeParameterWidthBeatsConfiguredSuffixWidth(argv[0]);
    testLocalAliasWidthBeatsConfiguredSuffixWidth(argv[0]);
    testGenerateBlockUnpackedArrayDimensionOrder(argv[0]);
    testDependentStructTypeParametersStayTemplateParameters(argv[0]);
    testUnsignedCastOfGenerateLoopVariableIsIntegerWidth(argv[0]);
    testImplicitPackedArrayOutputPassThroughKeepsArrayShape(argv[0]);
    testGenerateScalarChildOutputKeepsGenerateGuard(argv[0]);
    testUnpackedArrayDynamicIndexUsesArrayIndex(argv[0]);
    testWideLogicCompoundBitwiseDoesNotTruncateToUint64(argv[0]);
    testStructArrayToPackedArrayInputUsesElementPack(argv[0]);
    testPackedVectorToUnpackedArrayInputUsesExplicitSlices(argv[0]);
    testSameStructArrayInputPortUsesDirectArrayBinding(argv[0]);
    testSamePackedArrayCombAssignKeepsArrayShape(argv[0]);
    testSameUnpackedStructArrayRegAssignKeepsArrayShape(argv[0]);
    testPackedByteArrayAssignFromUnpackedWordArrayUsesPackValue(argv[0]);
    testContinuousPackedByteArrayAssignFromUnpackedWordArrayUsesPackValue(argv[0]);
    testContinuousUnpackedWordArrayAssignFromPackedVectorUsesUnpackValue(argv[0]);
    testTypeParameterWholeAssignFromPackedVectorUsesUnpackValue(argv[0]);
    testTypeParameterCastFromPackedByteArrayUsesPackUnpack(argv[0]);
    testPackedArrayToStructArrayCombAssignUsesElementUnpack(argv[0]);
    testPackedArrayOutputToStructArrayUsesElementUnpack(argv[0]);
    testIndexedUnpackedArrayOutputToPackedVectorUsesPackValue(argv[0]);
    testPackedStructInputToLogicPortUsesPackValue(argv[0]);
    testPackedStructCastAssignedToVectorUsesPackValue(argv[0]);
    testPackedStructInputPortUsesPackUnpackForDistinctStructTypes(argv[0]);
    testChildLogicAliasInputFieldBindingDoesNotUseStructUnpack(argv[0]);
    testArrayOutputPacksIntoPackedField(argv[0]);
    testAlwaysCombArrayElementUpdatesMergeWithContinuousOutputComb(argv[0]);
    testAlwaysCombArrayElementUpdatesMergeWithExistingInternalComb(argv[0]);
    testInputPortDependentCombMethodIsNoCache(argv[0]);
    testInputArrayMuxCombMethodIsNoCache(argv[0]);
    testInputPortDependentStructCombMethodIsNoCache(argv[0]);
    testCombUsedBySequentialWorkIsNoCache(argv[0]);
    testCombMethodCallingCombMethodIsNoCache(argv[0]);
    testRecursiveCombChildBindingStaysLazy(argv[0]);
    testConditionalUnbasedOneUsesTargetWidth(argv[0]);
    testReplicationOfArrayAliasElementUsesElementWidth(argv[0]);
    testReplicationOfTypeParameterArrayElementUsesElementAccess(argv[0]);
    testTypeParameterArrayElementRangeSelectUsesElementWidth(argv[0]);
    testTypeParameterArrayElementRangeConditionalKeepsByteWidth(argv[0]);
    testOutOfOrderNamedAggregateBecomesPositional(argv[0]);
    testCombArrayElementBitSelectCastOperandIsParenthesized(argv[0]);
    testPackedArrayComparisonEmitsNumericPackValue(argv[0]);
    testTypeParameterStructFieldConditionalConcatUsesFieldType(argv[0]);
    testGenerateLocalCombSignalIsPerLoopIndex(argv[0]);
    testAssignDrivenStructFieldReadUsesFieldProjection(argv[0]);
    testDefaultStructFieldProjectionUsesTypedZero(argv[0]);
    testAggregateOutputFieldReadUsesExtractedFieldComb(argv[0]);
    testExtractedScalarFieldCombSkipsSiblingStructFields(argv[0]);
    testFieldExtractionKeepsWholeAggregateSiblingBranch(argv[0]);
    testFieldProjectionThroughSelectedAggregateAvoidsWholeCombRecursion(argv[0]);
    testFieldProjectionThroughTypeParamAggregateAvoidsWholeCombRecursion(argv[0]);
    testFieldCombDoesNotPropagateThroughPlainWholeValueCall(argv[0]);
    testExtractedFieldConditionalDefaultBranchUsesFieldType(argv[0]);
    testSequentialChildInputPortBindingStaysLazy(argv[0]);
    testBinaryPrecedencePreservesNestedEqualityUnderBitwise(argv[0]);
    testGenerateBoundDivisionExpressionIsBalanced(argv[0]);
    testOneBitUnaryNotInConcatAvoidsLogicOperatorNot(argv[0]);
    testWideConditionalLogicBranchDoesNotNarrowToUint64(argv[0]);
    return 0;
}
