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
                                 const std::string& sv, const std::string& generateParams = "",
                                 const std::string& typeDeclOverrides = "",
                                 const std::string& addressablePackedArrayTypes = "")
{
    auto dir = makeTempDir("hdlcpp_structs_" + section);
    auto input = dir / (section + ".sv");
    auto params = dir / "generate_params.tsv";
    auto overrides = dir / "type_decl_overrides.tsv";
    writeFile(input, sv);
    writeFile(params, generateParams);
    writeFile(overrides, typeDeclOverrides);

    auto oldCwd = fs::current_path();
    fs::current_path(dir);
    std::string command = "HDLCPP_GENERATE_PARAM_VALUES=" + shellQuote(params) + " " +
                          "HDLCPP_TYPE_DECL_OVERRIDES=" + shellQuote(overrides) + " " +
                          "HDLCPP_ADDRESSABLE_PACKED_ARRAY_TYPES='" + addressablePackedArrayTypes + "' " +
                          shellQuote(hdlcppPath(argv0)) + " " + shellQuote(input);
    auto rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    if (rc != 0) {
        std::cerr << "hdlcpp failed for section " << section << " rc=" << rc << "\n";
    }
    assert(rc == 0);
    return readFile(dir / "generated" / (section + ".h"));
}

static void testPackedTypedefStructEmitsCppStruct(const char* argv0)
{
    const std::string sv = R"sv(
module packed_typedef_struct(
    input  logic [7:0] raw_i,
    output logic [7:0] raw_o
);
  typedef struct packed {
    logic [2:0] opcode;
    logic [4:0] rd;
  } instr_t;

  instr_t instr;
  assign instr = raw_i;
  assign raw_o = instr;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_typedef_struct", sv);
    expectContains(h, "struct instr_t");
    expectContains(h, "logic<3> opcode;");
    expectContains(h, "logic<5> rd;");
    expectContains(h, "logic<3 + 5> pack() const");
    expectContains(h, "template<std::size_t W> instr_t& operator=(const logic<W>& v)");
    expectNotContains(h, "struct packed");
}

static void testPackedStructFieldBitsCanDriveLocalparam(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_field_bits(
    output logic [31:0] width_o
);
  typedef struct packed {
    logic [11:0] addr;
    logic [3:0]  mask;
  } csr_buffer_t;

  localparam int ADDR_W = $bits(csr_buffer_t::addr);
  assign width_o = ADDR_W;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_field_bits", sv);
    expectContains(h, "struct csr_buffer_t");
    expectContains(h, "logic<12> addr;");
    expectContains(h, "logic<4> mask;");
    expectContains(h, "static constexpr unsigned ADDR_W =");
    expectNotContains(h, "$bits(");
}

static void testLocalparamTypeStructInParameterList(const char* argv0)
{
    const std::string sv = R"sv(
module localparam_type_struct #(
    localparam type req_t = struct packed {
      logic       valid;
      logic [3:0] id;
    }
) (
    input  req_t req_i,
    output logic valid_o,
    output logic [3:0] id_o
);
  assign valid_o = req_i.valid;
  assign id_o = req_i.id;
endmodule
)sv";

    auto h = convertModule(argv0, "localparam_type_struct", sv);
    expectContains(h, "struct req_t");
    expectContains(h, "logic<1> valid;");
    expectContains(h, "logic<4> id;");
    expectContains(h, "_PORT(req_t) req_i_in;");
    expectNotContains(h, "struct packed");
}

static void testPackedStructWithArrayField(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_array_field(
    input  logic [15:0] raw_i,
    output logic [7:0]  lo_o
);
  typedef struct packed {
    logic [1:0][3:0] lanes;
    logic [7:0]      tag;
  } packet_t;

  packet_t packet;
  assign packet = raw_i;
  assign lo_o = {packet.lanes[0], packet.lanes[1]};
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_array_field", sv);
    expectContains(h, "struct packet_t");
    expectContains(h, "array<logic<4>,2,true> lanes;");
    expectContains(h, "logic<8> tag;");
    expectContains(h, "pack() const");
    expectNotContains(h, "struct packed");
}

static void testMacroPackedTypedefStructKeepsFieldPacking(const char* argv0)
{
    const std::string sv = R"sv(
`define DECL_REQ_T(__addr_t, __id_t) \
  struct packed { \
    __addr_t addr; \
    __id_t   id; \
    logic    last; \
  }
`define TYPEDEF_REQ_T(__name__, __addr_t, __id_t) \
  typedef `DECL_REQ_T(__addr_t, __id_t) __name__

module macro_packed_typedef_struct(
    input  logic [12:0] raw_i,
    output logic [3:0]  id_o,
    output logic [12:0] raw_o
);
  typedef logic [7:0] addr_t;
  typedef logic [3:0] id_t;
  `TYPEDEF_REQ_T(req_t, addr_t, id_t);

  req_t req;
  assign req = raw_i;
  assign id_o = req.id;
  assign raw_o = req;
endmodule
)sv";

    auto h = convertModule(argv0, "macro_packed_typedef_struct", sv);
    expectContains(h, "struct req_t");
    expectContains(h, "addr_t addr;");
    expectContains(h, "id_t id;");
    expectContains(h, "logic<1> last;");
    expectContains(h, "logic<8 + 4 + 1> pack() const");
    expectContains(h, "cpphdl::pack_value<4>(this->id)");
    expectNotContains(h, "sizeof(req_t)*8");
}

static void testTypeDeclOverridePackedTokenUsesOverrideFields(const char* argv0)
{
    const std::string sv = R"sv(
module override_packed_token #(
    parameter type req_t = logic
) (
    input  req_t req_i,
    output logic [3:0] id_o
);
  assign id_o = req_i.id;
endmodule
)sv";
    const std::string overrides =
        "override_packed_token.req_t\t"
        "struct req_t {\\n"
        "    logic<8> addr;\\n"
        "    logic<4> id;\\n"
        "    logic<1> last;\\n"
        "@PACKED@};\n";

    auto h = convertModule(argv0, "override_packed_token", sv, "", overrides);
    expectContains(h, "struct req_t");
    expectContains(h, "logic<1 + 4 + 8> pack() const");
    expectContains(h, "cpphdl::pack_value<4>(this->id)");
    expectNotContains(h, "sizeof(req_t)*8");
}

static void testTypeDeclOverridePackedFieldsUseAliasOverrides(const char* argv0)
{
    const std::string sv = R"sv(
module override_alias_fields #(
    parameter type packet_t = logic
) (
    input  packet_t packet_i,
    output logic [3:0] id_o
);
  assign id_o = packet_i.id;
endmodule
)sv";
    const std::string overrides =
        "override_alias_fields.alias_t\tusing alias_t = logic<4>;\n"
        "override_alias_fields.packet_t\t"
        "struct packet_t {\\n"
        "    logic<8> addr;\\n"
        "    alias_t id;\\n"
        "    logic<1> last;\\n"
        "@PACKED@};\n";

    auto h = convertModule(argv0, "override_alias_fields", sv, "", overrides);
    expectContains(h, "using alias_t = logic<4>;");
    expectContains(h, "alias_t id;");
    expectContains(h, "logic<1 + 4 + 8> pack() const");
    expectContains(h, "cpphdl::pack_value<4>(this->id)");
    expectNotContains(h, "cpphdl::type_width<using alias_t");
    expectNotContains(h, "sizeof(packet_t)*8");
}

static void testTypeDeclOverridePackedFieldsAllowExpressionLogicWidths(const char* argv0)
{
    const std::string sv = R"sv(
module override_expr_width_struct #(
    parameter int unsigned ADDR_WIDTH = 16
)();
endmodule
)sv";

    const std::string overrides =
        "override_expr_width_struct.req_t\t"
        "struct req_t {\\n"
        "    logic<(uint64_t)(((uint64_t)(ADDR_WIDTH) & ((1ull << 32) - 1ull)))> addr;\\n"
        "    logic<3> prot;\\n"
        "@PACKED@};\n";

    auto h = convertModule(argv0, "override_expr_width_struct", sv, "", overrides);
    expectContains(h, "struct req_t");
    expectContains(h, "logic<3 + (uint64_t)(((uint64_t)(ADDR_WIDTH)");
    expectContains(h, "cpphdl::pack_value<(uint64_t)(((uint64_t)(ADDR_WIDTH)");
}

static void testTypeDeclOverrideOrdersDependentPackedStructs(const char* argv0)
{
    const std::string sv = R"sv(
module override_dependent_structs();
endmodule
)sv";

    const std::string overrides =
        "override_dependent_structs.req_t\t"
        "struct req_t {\\n"
        "    z_chan_t z;\\n"
        "    logic<1> valid;\\n"
        "@PACKED@};\n"
        "override_dependent_structs.z_chan_t\t"
        "struct z_chan_t {\\n"
        "    logic<4> data;\\n"
        "@PACKED@};\n";

    auto h = convertModule(argv0, "override_dependent_structs", sv, "", overrides);
    auto zPos = h.find("struct z_chan_t");
    auto reqPos = h.find("struct req_t");
    assert(zPos != std::string::npos);
    assert(reqPos != std::string::npos);
    assert(zPos < reqPos);
    expectContains(h, "logic<1 + 4> pack() const");
}

static void testTypedefElementMultiUnpackedDimensionsKeepSvOrder(const char* argv0)
{
    const std::string sv = R"sv(
module typedef_element_multi_unpacked_dims(
    input  logic [63:0] in_i,
    output logic [127:0] out_o
);
  typedef logic [63:0] word_t;
  word_t [0:0][1:0] buf_q;

  always_comb begin
    buf_q = '0;
    buf_q[0][0] = in_i;
    buf_q[0][1] = in_i + 64'd1;
  end

  assign out_o = buf_q[0];
endmodule
)sv";

    auto h = convertModule(argv0, "typedef_element_multi_unpacked_dims", sv);
    expectContains(h, "using word_t = logic<64>;");
    expectContains(h, "array<array<word_t,(((uint64_t)(1)");
    expectNotContains(h, "_LAZY_COMB(buf_q_comb, array<array<word_t,(((uint64_t)(0)");
    expectContains(h, "buf_q_comb_func()");
}

static void testNestedPackedArrayElementWriteUsesPackedSlice(const char* argv0)
{
    const std::string sv = R"sv(
module nested_packed_array_element_write(
    input  logic [63:0] a_i,
    input  logic [63:0] b_i,
    output logic [127:0] out_o
);
  typedef logic [63:0] word_t;
  word_t [0:0][1:0] buf_q;

  always_comb begin
    buf_q = '0;
    buf_q[0][0] = a_i;
    buf_q[0][1] = b_i;
  end

  assign out_o = buf_q[0];
endmodule
)sv";

    auto h = convertModule(argv0, "nested_packed_array_element_write", sv);
    expectContains(h, ".data.bits(");
    expectContains(h, " = a_i_in()");
    expectContains(h, " = b_i_in()");
    expectNotContains(h, "][(unsigned)");
}

static void testNestedPackedArrayConditionalWriteUsesLeafType(const char* argv0)
{
    const std::string sv = R"sv(
module nested_packed_array_conditional_write(
    input  logic       sel_i,
    input  logic [7:0] a_i,
    input  logic [7:0] b_i,
    output logic [31:0] out_o
);
  typedef logic [7:0] byte_t;
  byte_t [1:0][1:0] buf_q;

  always_comb begin
    for (int i = 0; i < 2; i++) begin
      for (int j = 0; j < 2; j++) begin
        buf_q[i][j] = sel_i ? a_i : b_i;
      end
    end
  end

  assign out_o = buf_q;
endmodule
)sv";

    auto h = convertModule(argv0, "nested_packed_array_conditional_write", sv);
    expectContains(h, ".data.bits(");
    expectContains(h, "cpphdl::sv_cast<byte_t>(a_i_in())");
    expectContains(h, "cpphdl::sv_cast<byte_t>(b_i_in())");
    expectNotContains(h, "logic<((uint64_t)(((((");
}

static void testUnbasedUnsizedOneUsesPackedFieldWidth(const char* argv0)
{
    const std::string sv = R"sv(
module unbased_one_struct_field(
    input  logic [3:0] id_i,
    output logic [3:0] id_o
);
  typedef struct packed {
    logic [3:0] id;
    logic       flag;
  } resp_t;

  function automatic resp_t set_all(input resp_t resp);
    resp.id = '1;
    return resp;
  endfunction

  resp_t tmp;
  always_comb begin
    tmp = '0;
    tmp.id = id_i;
    tmp = set_all(tmp);
  end
  assign id_o = tmp.id;
endmodule
)sv";

    auto h = convertModule(argv0, "unbased_one_struct_field", sv);
    expectContains(h, "resp.id = logic<4>(");
    expectContains(h, "((1ull << 4) - 1ull)");
    expectNotContains(h, "resp.id = 1");
}

static void testUnbasedUnsizedOneUsesDecltypeForUnknownParamField(const char* argv0)
{
    const std::string sv = R"sv(
module unbased_one_param_field #(
    parameter type resp_t = logic
) (
    input  resp_t resp_i,
    output logic [3:0] id_o
);
  function automatic resp_t set_all(input resp_t resp);
    resp.id = '1;
    return resp;
  endfunction

  assign id_o = set_all(resp_i).id;
endmodule
)sv";

    auto h = convertModule(argv0, "unbased_one_param_field", sv);
    expectContains(h, "resp.id = cpphdl::sv_cast<cpphdl::value_type_for_ref_t<decltype(resp.id)>>");
    expectContains(h, "cpphdl::type_width<cpphdl::value_type_for_ref_t<decltype(resp.id)>>()");
    expectNotContains(h, "resp.id = cpphdl::sv_cast<resp_t>");
    expectNotContains(h, "resp.id = 1");
}

static void testPackedStructCastToWideLogicDoesNotTruncate(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_cast_to_wide_logic(
    input  logic [3:0] sid_i,
    output logic [75:0] raw_o
);
  typedef struct packed {
    logic [63:0] low;
    logic [3:0]  sid;
    logic [7:0]  high;
  } entry_t;

  typedef logic [75:0] raw_t;

  entry_t entry;
  always_comb begin
    entry = '0;
    entry.low = 64'h0123_4567_89ab_cdef;
    entry.sid = sid_i;
    entry.high = 8'ha5;
    raw_o = raw_t'(entry);
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_cast_to_wide_logic", sv);
    expectContains(h, "struct entry_t");
    expectContains(h, "using raw_t = logic<76>;");
    expectContains(h, "raw_t(cpphdl::pack_value<76>(");
    expectNotContains(h, "raw_t((uint64_t)(entry");
    expectNotContains(h, "logic<76>((uint64_t)(entry");
}

static void testPackedArrayWideBitwiseMergeDoesNotTruncate(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_wide_bitwise_merge(
    input  logic [127:0] old_i,
    input  logic [127:0] wdata_i,
    input  logic [127:0] wmask_i,
    output logic [127:0] out_o
);
  typedef logic [63:0] word_t;
  word_t [1:0] old_data;
  word_t [1:0] wdata;
  word_t [1:0] wmask;
  word_t [1:0] merged;

  always_comb begin
    old_data = old_i;
    wdata = wdata_i;
    wmask = wmask_i;
    merged = (old_data & ~wmask) | (wdata & wmask);
  end

  assign out_o = merged;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_wide_bitwise_merge", sv);
    expectContains(h, "_LAZY_COMB(merged_comb, array<word_t,");
    expectContains(h, "logic<cpphdl::type_width<array<word_t,");
    expectNotContains(h, "merged_comb = logic<1>");
    expectNotContains(h, "logic<1>((((uint64_t)");
    expectNotContains(h, "~((uint64_t)(cpphdl::pack_value<cpphdl::type_width<array<word_t,2,true>>()");
}

static void testPackedArrayMemoryRowBitwiseMergeDoesNotTruncate(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_memory_row_bitwise_merge(
    input  logic       clk_i,
    input  logic       we_i,
    input  logic [1:0] addr_i,
    input  logic [127:0] wdata_i,
    input  logic [127:0] wmask_i,
    output logic [127:0] out_o
);
  typedef logic [63:0] word_t;
  word_t [1:0] mem [3:0];
  word_t [1:0] wdata;
  word_t [1:0] wmask;

  assign wdata = wdata_i;
  assign wmask = wmask_i;
  assign out_o = mem[addr_i];

  always_ff @(posedge clk_i) begin
    if (we_i) begin
      mem[addr_i] <= (mem[addr_i] & ~wmask) | (wdata & wmask);
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_memory_row_bitwise_merge", sv);
    expectContains(h, "reg<array<array<word_t,");
    expectContains(h, "logic<cpphdl::type_width<array<word_t,");
    expectNotContains(h, "cpphdl::pack_value<1>(mem");
    expectNotContains(h, "logic<1>((((uint64_t)(mem");
    expectNotContains(h, "~((uint64_t)(cpphdl::pack_value");
}

static void testNamedTypedefPackedArrayKeepsPackedFlag(const char* argv0)
{
    const std::string sv = R"sv(
module named_typedef_packed_array(
    input  logic [7:0][7:0] bytes_i,
    output logic [63:0] out_o
);
  typedef logic [31:0] word_t;
  typedef word_t [1:0] words_t;

  words_t words;

  always_comb begin
    words = words_t'(bytes_i);
  end

  assign out_o = words;
endmodule
)sv";

    auto h = convertModule(argv0, "named_typedef_packed_array", sv);
    expectContains(h, "using words_t = array<word_t,");
    expectContains(h, ",true>;");
    expectContains(h, "words_comb = cpphdl::pack_value<cpphdl::type_width<array<word_t,");
    expectNotContains(h, "using words_t = array<word_t,2>;");
}

static void testNamedStructPackedArrayKeepsAddressableElements(const char* argv0)
{
    const std::string sv = R"sv(
module named_struct_packed_array(
    input  logic [7:0] data_i,
    output logic [3:0] out_o
);
  typedef struct packed {
    logic [3:0] lo;
    logic [3:0] hi;
  } entry_t;
  typedef entry_t [1:0] entries_t;

  entries_t entries;

  always_comb begin
    entries[0].lo = data_i[3:0];
    entries[0].hi = data_i[7:4];
    out_o = entries[0].hi;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "named_struct_packed_array", sv);
    expectContains(h, "using entries_t = array<entry_t,");
    expectNotContains(h, "using entries_t = array<entry_t,2,true>;");
    expectNotContains(h, ",true> entries");
    expectContains(h, "entries_comb[0].hi");
}

static void testConfiguredAggregatePackedArrayKeepsAddressableElements(const char* argv0)
{
    const std::string sv = R"sv(
module configured_aggregate_packed_array #(
    parameter type item_t = logic
) (
    input  item_t [1:0] items_i,
    output logic [3:0] out_o
);
  assign out_o = items_i[0].id;
endmodule
)sv";

    auto h = convertModule(argv0, "configured_aggregate_packed_array", sv, "", "", "item_t");
    expectContains(h, "_PORT(array<item_t,");
    expectNotContains(h, "_PORT(array<item_t,2,true>)");
    expectNotContains(h, "_PORT(array<item_t,(((uint64_t)(1) >= (uint64_t)(0) ? ((uint64_t)(1) - (uint64_t)(0)) : ((uint64_t)(0) - (uint64_t)(1))) + 1),true>)");
    expectContains(h, "(items_i_in())[(unsigned)");
    expectContains(h, ".id");
}

static void testPackedStructArrayFieldWriteUsesElementTemporary(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_array_field_write #(
    parameter type item_t = struct packed {
      logic [7:0] addr;
      logic [3:0] id;
    }
) (
    input  item_t [0:0] in_i,
    input  logic [3:0]  id_i,
    output logic [3:0]  out_o
);
  item_t [0:0] out;

  always_comb begin
    out = '0;
    out[0] = in_i[0];
    out[0].id = id_i;
  end

  assign out_o = out[0].id;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_array_field_write", sv);
    expectContains(h, "array<item_t,");
    expectNotContains(h, "array<item_t,(((uint64_t)(0) >= (uint64_t)(0) ? ((uint64_t)(0) - (uint64_t)(0)) : ((uint64_t)(0) - (uint64_t)(0))) + 1),true>");
    expectContains(h, "out_comb[0].id = id_i_in();");
    expectContains(h, "out_comb_func()[(unsigned)");
    expectContains(h, ".id");
}

static void testPackedArrayElementConditionalCastsToValueType(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_element_conditional #(
    parameter type byte_t = logic [7:0]
) (
    input  logic       sel_i,
    input  logic [7:0] a_i,
    output logic [15:0] out_o
);
  byte_t [1:0] data;

  always_comb begin
    data = '0;
	    data[0] = sel_i ? '1 : a_i;
  end

  assign out_o = data;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_element_conditional", sv);
    expectContains(h, "cpphdl::value_type_for_ref_t<decltype(data_comb[");
    expectNotContains(h, "cpphdl::sv_cast<std::remove_cvref_t<decltype(data_comb[");
}

static void testPackedStorageWriteBindsCrossCombSources(const char* argv0)
{
    const std::string sv = R"sv(
module packed_storage_cross_comb (
    input  logic       sel_i,
    input  logic [3:0] data_i,
    output logic [15:0] out_o
);
  typedef logic [3:0] nibble_t;
  logic    choose;
  nibble_t payload;
  nibble_t [1:0][1:0] matrix;

  always_comb begin
    choose = sel_i;
    payload = data_i;
  end

  always_comb begin
    matrix = '0;
    matrix[0][0] = choose ? payload : '0;
  end

  assign out_o = matrix;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_storage_cross_comb", sv);
    expectContains(h, ".data.bits(");
    expectContains(h, "choose_comb_func() ? cpphdl::sv_cast<nibble_t>(payload_comb_func())");
    expectNotContains(h, "= choose ?");
    expectNotContains(h, "sv_cast<nibble_t>(payload)");
}

int main(int argc, char** argv)
{
    assert(argc >= 1);
    testPackedTypedefStructEmitsCppStruct(argv[0]);
    testPackedStructFieldBitsCanDriveLocalparam(argv[0]);
    testLocalparamTypeStructInParameterList(argv[0]);
    testPackedStructWithArrayField(argv[0]);
    testMacroPackedTypedefStructKeepsFieldPacking(argv[0]);
    testTypeDeclOverridePackedTokenUsesOverrideFields(argv[0]);
    testTypeDeclOverridePackedFieldsUseAliasOverrides(argv[0]);
    testTypeDeclOverridePackedFieldsAllowExpressionLogicWidths(argv[0]);
    testTypeDeclOverrideOrdersDependentPackedStructs(argv[0]);
    testTypedefElementMultiUnpackedDimensionsKeepSvOrder(argv[0]);
    testNestedPackedArrayElementWriteUsesPackedSlice(argv[0]);
    testNestedPackedArrayConditionalWriteUsesLeafType(argv[0]);
    testUnbasedUnsizedOneUsesPackedFieldWidth(argv[0]);
    testUnbasedUnsizedOneUsesDecltypeForUnknownParamField(argv[0]);
    testPackedStructCastToWideLogicDoesNotTruncate(argv[0]);
    testPackedArrayWideBitwiseMergeDoesNotTruncate(argv[0]);
    testPackedArrayMemoryRowBitwiseMergeDoesNotTruncate(argv[0]);
    testNamedTypedefPackedArrayKeepsPackedFlag(argv[0]);
    testNamedStructPackedArrayKeepsAddressableElements(argv[0]);
    testConfiguredAggregatePackedArrayKeepsAddressableElements(argv[0]);
    testPackedStructArrayFieldWriteUsesElementTemporary(argv[0]);
    testPackedArrayElementConditionalCastsToValueType(argv[0]);
    testPackedStorageWriteBindsCrossCombSources(argv[0]);
    return 0;
}
