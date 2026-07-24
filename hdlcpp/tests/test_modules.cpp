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

static void expectBefore(const std::string& text, const std::string& first,
                         const std::string& second)
{
    auto firstPos = text.find(first);
    auto secondPos = text.find(second);
    if (firstPos == std::string::npos || secondPos == std::string::npos || firstPos >= secondPos) {
        std::cerr << "expected generated text in this order:\n" << first << "\n" << second << "\n";
    }
    assert(firstPos != std::string::npos);
    assert(secondPos != std::string::npos);
    assert(firstPos < secondPos);
}

static void expectLineParenthesesBalanced(const std::string& text, const std::string& needle)
{
    auto pos = text.find(needle);
    if (pos == std::string::npos) {
        std::cerr << "missing line containing:\n" << needle << "\n";
        assert(false);
    }
    auto begin = text.rfind('\n', pos);
    begin = begin == std::string::npos ? 0 : begin + 1;
    auto end = text.find('\n', pos);
    end = end == std::string::npos ? text.size() : end;
    int balance = 0;
    for (auto i = begin; i < end; ++i) {
        if (text[i] == '(') {
            ++balance;
        }
        else if (text[i] == ')') {
            --balance;
        }
        if (balance < 0) {
            break;
        }
    }
    if (balance != 0) {
        std::cerr << "unbalanced generated line:\n" << text.substr(begin, end - begin) << "\n";
    }
    assert(balance == 0);
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
                                 const std::string& linePatches = "",
                                 const std::string& packageCallables = "",
                                 const std::string& moduleParams = "",
                                 const std::string& portTypes = "",
                                 const std::string& includeText = "",
                                 const std::string& moduleTraits = "",
                                 const std::string& typeAliasOverrides = "")
{
    auto dir = makeTempDir("hdlcpp_module_" + section);
    auto input = dir / (section + ".sv");
    auto params = dir / "generate_params.tsv";
    auto widths = dir / "type_widths.tsv";
    auto patches = dir / "line_patches.tsv";
    auto callables = dir / "package_callables.tsv";
    auto moduleParamsPath = dir / "module_params.tsv";
    auto portTypesPath = dir / "port_types.tsv";
    auto includePath = dir / "hdlcpp_test_defs.svh";
    auto moduleTraitsPath = dir / "module_traits.tsv";
    auto typeAliasOverridesPath = dir / "type_alias_overrides.tsv";
    writeFile(input, sv);
    writeFile(params, generateParams);
    writeFile(widths, typeWidths);
    writeFile(patches, linePatches);
    writeFile(callables, packageCallables);
    writeFile(moduleParamsPath, moduleParams);
    writeFile(portTypesPath, portTypes);
    if (!includeText.empty()) {
        writeFile(includePath, includeText);
    }
    writeFile(moduleTraitsPath, moduleTraits);
    writeFile(typeAliasOverridesPath, typeAliasOverrides);

    auto executable = hdlcppPath(argv0);
    auto oldCwd = fs::current_path();
    fs::current_path(dir);
    std::string command = "HDLCPP_GENERATE_PARAM_VALUES=" + shellQuote(params) + " " +
                          (includeText.empty() ? std::string() : "HDLCPP_INCLUDE_DIRS=" + shellQuote(dir) + " HDLCPP_DEFINES=HDLCPP_TEST_DEFINE ") +
                          (typeWidths.empty() ? std::string() : "HDLCPP_TYPE_WIDTHS=" + shellQuote(widths) + " ") +
                          (linePatches.empty() ? std::string() : "HDLCPP_LINE_PATCHES=" + shellQuote(patches) + " ") +
                          (packageCallables.empty() ? std::string() : "HDLCPP_PACKAGE_CALLABLES=" + shellQuote(callables) + " ") +
                          (moduleParams.empty() ? std::string() : "HDLCPP_MODULE_PARAMS=" + shellQuote(moduleParamsPath) + " ") +
                          (portTypes.empty() ? std::string() : "HDLCPP_PORT_TYPES=" + shellQuote(portTypesPath) + " ") +
                          (moduleTraits.empty() ? std::string() : "HDLCPP_MODULE_TRAITS=" + shellQuote(moduleTraitsPath) + " ") +
                          (typeAliasOverrides.empty() ? std::string() : "HDLCPP_TYPE_ALIAS_OVERRIDES=" + shellQuote(typeAliasOverridesPath) + " ") +
                          shellQuote(executable) + " " + shellQuote(input);
    auto rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    if (rc != 0) {
        std::cerr << "hdlcpp failed for section " << section << " rc=" << rc << "\n";
    }
    assert(rc == 0);
    return readFile(dir / "generated" / (section + ".h"));
}

static void testModuleDependencyMetadataUsesParsedInstances(const char* argv0)
{
    auto dir = makeTempDir("hdlcpp_module_dependencies");
    auto input = dir / "dependencies.sv";
    auto dependencies = dir / "dependencies.tsv";
    writeFile(input, R"(
module dependency_leaf(input logic value_i);
endmodule

module dependency_parent(input logic value_i);
    dependency_leaf child_i(.value_i(value_i));
endmodule
)");

    auto executable = hdlcppPath(argv0);
    std::string command = "cd " + shellQuote(dir) + " && " +
                          "HDLCPP_WRITE_MODULE_DEPENDENCIES=" + shellQuote(dependencies) + " " +
                          "HDLCPP_METADATA_ONLY=1 " + shellQuote(executable) + " " + shellQuote(input);
    auto rc = std::system(command.c_str());
    assert(rc == 0);
    auto metadata = readFile(dependencies);
    expectContains(metadata, "dependency_parent\tdependency_leaf\n");
    expectNotContains(metadata, "dependency_leaf\tdependency_parent");
}

static void testConfiguredGenerateSelectsZeroDelayPassThrough(const char* argv0)
{
    const std::string sv = R"(
module configured_zero_delay #(
    parameter bit StallRandom = 0,
    parameter int FixedDelay = 1
) (
    input  logic value_i,
    output logic value_o
);
    if (FixedDelay == 0 && !StallRandom) begin : gen_passthrough
        assign value_o = value_i;
    end else begin : gen_invert
        assign value_o = ~value_i;
    end
endmodule
)";
    auto h = convertModule(argv0, "configured_zero_delay", sv,
                           "configured_zero_delay.StallRandom\t0\n"
                           "configured_zero_delay.FixedDelay\t0\n");
    expectContains(h, "value_i_in()");
    expectNotContains(h, "~(uint64_t)");
}

static void testDelayedNetDeclarationInitializerIsContinuousComb(const char* argv0)
{
    const std::string sv = R"(
module delayed_net_initializer(
    input  logic source_i,
    output logic sink_o
);
    wire #0.1 delayed = source_i;
    assign sink_o = delayed;
endmodule
)";
    auto h = convertModule(argv0, "delayed_net_initializer", sv, "");
    expectContains(h, "delayed_comb_func()");
    expectContains(h, "delayed_comb = source_i_in();");
    expectContains(h, "sink_o_comb = delayed_comb_func();");
}

static void testExternalChildOutputDemandIsPublished(const char* argv0)
{
    auto dir = makeTempDir("hdlcpp_external_output_demand");
    auto input = dir / "external_output_demand.sv";
    auto portTypes = dir / "port_types.tsv";
    auto finalTraits = dir / "final_traits.tsv";
    writeFile(input, R"(
typedef struct packed {
    logic [3:0] code;
    logic [7:0] detail;
} demand_status_t;
typedef struct packed {
    logic           valid;
    demand_status_t status;
} demand_result_t;

module external_output_demand(output logic [3:0] code_o);
    demand_result_t result;
    external_demand_source child_i(.result_o(result));
    assign code_o = result.status.code;
endmodule
)" );
    writeFile(portTypes, "external_demand_source.result_o\toutput:demand_result_t\n");
    writeFile(finalTraits, "");

    auto executable = hdlcppPath(argv0);
    auto oldCwd = fs::current_path();
    fs::current_path(dir);
    std::string command = "HDLCPP_PORT_TYPES=" + shellQuote(portTypes) + " " +
                          "HDLCPP_APPEND_FINAL_MODULE_TRAITS=" + shellQuote(finalTraits) + " " +
                          shellQuote(executable) + " " + shellQuote(input);
    auto rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    assert(rc == 0);
    expectContains(readFile(finalTraits),
                   "external_demand_source\toutput_field.result_o.status.code");
}

static void testConfiguredOutputFieldDemandMaterializesDependentPort(const char* argv0)
{
    const std::string sv = R"(
module configured_output_field_child #(
    parameter type result_t = logic [7:0]
) (
    input  result_t value_i,
    output result_t result_o
);
    assign result_o = value_i;
endmodule
)";
    const std::string moduleTraits =
        "configured_output_field_child\toutput_field.result_o.status\n";
    auto h = convertModule(argv0, "configured_output_field_child", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "result_o_out__field_status");
    expectContains(h, "std::declval<__cpphdl_projected_t>().status");
    expectContains(h, "requires(__cpphdl_projected_t __cpphdl_projected_value)");
    expectContains(h, "if constexpr (requires(result_t __cpphdl_projected_value)");
    expectContains(h, "result_o_status_comb;");
}

static void testTypeParameterizedChildOutputDemandUsesFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [7:0] status;
    logic [7:0] payload;
} parameterized_result_t;

module parameterized_output_child #(
    parameter type result_t = logic [7:0]
) (
    input  result_t value_i,
    output result_t result_o
);
    assign result_o = value_i;
endmodule

module parameterized_output_parent (
    input  parameterized_result_t value_i,
    output logic [7:0] status_o
);
    parameterized_result_t result;
    parameterized_output_child #(.result_t(parameterized_result_t)) child_i (
        .value_i(value_i),
        .result_o(result)
    );
    assign status_o = result.status;
endmodule
)";
    const std::string moduleParams =
        "parameterized_output_child\ttypename result_t = logic<8>\n";
    const std::string portTypes =
        "parameterized_output_child.value_i\tinput:result_t\n"
        "parameterized_output_child.result_o\toutput:result_t\n";
    const std::string moduleTraits =
        "parameterized_output_child\toutput_field.result_o.status\n";
    auto h = convertModule(argv0, "parameterized_output_parent", sv,
                           "", "", "", "", moduleParams, portTypes, "", moduleTraits);
    expectContains(h, "child_i.result_o_out__field_status()");
    expectNotContains(h, "child_i.result_o_out()).status");
}

static void testMaterializedTypeParameterizedIndexedChildOutputUsesFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic valid;
    logic ready;
} materialized_parameterized_req_t;

module materialized_parameterized_child #(
    parameter type req_t = logic [1:0]
) (
    input  req_t req_i,
    output req_t req_o
);
    always_comb begin
        req_o = '0;
        req_o.valid = req_i.valid;
        req_o.ready = req_i.ready;
    end
endmodule

module materialized_parameterized_parent (
    input  materialized_parameterized_req_t req_i [2],
    output logic [1:0] valid_o
);
    materialized_parameterized_req_t child_req [2];
    for (genvar i = 0; i < 2; ++i) begin
        materialized_parameterized_child #(
            .req_t(materialized_parameterized_req_t)
        ) child_i (
            .req_i(req_i[i]),
            .req_o(child_req[i])
        );
        assign valid_o[i] = child_req[i].valid;
    end
endmodule
)";
    const std::string moduleParams =
        "materialized_parameterized_child\ttypename req_t = logic<2>\n";
    auto h = convertModule(argv0, "materialized_parameterized_parent", sv,
                           "", "", "", "", moduleParams);
    expectContains(h, "].req_o_out__field_valid()");
    expectContains(h, "requires(__cpphdl_projected_t __cpphdl_projected_value)");
    expectContains(h, "if constexpr (requires(req_t __cpphdl_projected_value)");
    expectNotContains(h, "].req_o_out()).valid");
    expectNotContains(h, "type_width<materialized_parameterized_req_t>()>(child_i[");
}

static void testGenericChildProjectionRejectsMissingConcreteSourceField(const char* argv0)
{
    const std::string sv = R"(
module concrete_projection_parent(
    input concrete_projection_pkg::concrete_projection_entry_t entry_i
);
    generic_projection_sink child_i(.data_i(entry_i));
endmodule
)";
    const std::string portTypes =
        "generic_projection_sink.data_i\tinput:dtype\n";
    const std::string moduleTraits =
        "concrete_projection_pkg\ttype_field.concrete_projection_entry_t.header=logic<8>\n"
        "concrete_projection_pkg\ttype_field.concrete_projection_entry_t.payload=logic<8>\n"
        "generic_projection_sink\tinput_field.data_i.header\n"
        "generic_projection_sink\tinput_field.data_i.id\n";
    auto h = convertModule(argv0, "concrete_projection_parent", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectContains(h, "child_i.data_i_in__field_header");
    expectContains(h, "entry_i_in__field_header");
    expectNotContains(h, "child_i.data_i_in__field_id");
    expectNotContains(h, "entry_i_in__field_id");
}

static void testConfiguredNestedInputProjectionDeclaresChildPort(const char* argv0)
{
    const std::string sv = R"(
module configured_nested_input_projection #(
    parameter type resp_t = logic,
    parameter int unsigned Count = 2
) (
    input resp_t responses_i [Count]
);
endmodule
)";
    const std::string moduleParams =
        "configured_nested_input_projection\ttypename resp_t = logic<1>\tunsigned Count = 2\n";
    const std::string portTypes =
        "configured_nested_input_projection.responses_i\tinput:array<resp_t,Count>\n";
    const std::string moduleTraits =
        "configured_nested_input_projection\tinput_field.responses_i.channel.id\n";

    auto h = convertModule(argv0, "configured_nested_input_projection", sv,
                           "", "", "", "", moduleParams, portTypes, "", moduleTraits);
    expectContains(h, "_PORT(array<Count,std::remove_cvref_t<decltype(");
    expectContains(h, "responses_i_in__field_channel_id;");
    expectContains(h, "__cpphdl_projected_value.channel.id");
}

static void testCrossFileScalarAliasRejectsNestedProjectedField(const char* argv0)
{
    const std::string sv = R"(
module concrete_alias_projection_parent(
    input concrete_alias_projection_pkg::request_t request_i
);
    generic_projection_sink child_i(.data_i(request_i.id));
endmodule
)";
    const std::string portTypes =
        "generic_projection_sink.data_i\tinput:dtype\n";
    const std::string moduleTraits =
        "concrete_alias_projection_pkg\ttype_field.request_t.id=concrete_alias_projection_pkg::id_t\n"
        "concrete_alias_projection_pkg\ttype_alias.id_t=logic<5>\n"
        "generic_projection_sink\tinput_field.data_i.header\n";
    auto h = convertModule(argv0, "concrete_alias_projection_parent", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectNotContains(h, "request_i_in__field_id_header");
    expectNotContains(h, "child_i.data_i_in__field_header");
}

static void testConfiguredOutputProjectionRejectsMissingConcreteField(const char* argv0)
{
    const std::string sv = R"(
package concrete_output_projection_pkg;
    typedef struct packed {
        logic [7:0] header;
        logic [7:0] payload;
    } entry_t;
endpackage

module concrete_output_projection(
    input  concrete_output_projection_pkg::entry_t entry_i,
    output concrete_output_projection_pkg::entry_t entry_o
);
    assign entry_o = entry_i;
endmodule
)";
    const std::string moduleTraits =
        "concrete_output_projection\toutput_field.entry_o.header\n"
        "concrete_output_projection\toutput_field.entry_o.id\n";
    auto h = convertModule(argv0, "concrete_output_projection", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "entry_o_out__field_header");
    expectNotContains(h, "entry_o_out__field_id");
    expectNotContains(h, "entry_o_id_comb_func");
}

static void testScalarAliasChildOutputRejectsConfiguredFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [7:0] tag;
    logic [7:0] payload;
} scalar_alias_payload_t;

module scalar_alias_output_child #(
    parameter int WIDTH = 16,
    localparam type data_t = logic [WIDTH-1:0]
) (
    input  data_t data_i,
    output data_t data_o
);
    assign data_o = data_i;
endmodule

module scalar_alias_output_parent (
    input  scalar_alias_payload_t value_i,
    output logic [7:0] tag_o
);
    scalar_alias_payload_t result;
    scalar_alias_output_child #(.WIDTH(16)) child_i (
        .data_i(value_i),
        .data_o(result)
    );
    assign tag_o = result.tag;
endmodule
)";
    const std::string moduleTraits =
        "scalar_alias_output_child\toutput_field.data_o.tag\n";
    auto h = convertModule(argv0, "scalar_alias_output_parent", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "child_i.data_o_out()");
    expectNotContains(h, "child_i.data_o_out__field_tag()");
}

static void testExternalScalarAliasChildOutputRejectsConfiguredFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [7:0] tag;
    logic [7:0] payload;
} external_scalar_payload_t;

module external_scalar_alias_parent (
    input  external_scalar_payload_t value_i,
    output logic [7:0] tag_o
);
    external_scalar_payload_t result;
    external_scalar_alias_child child_i (
        .data_i(value_i),
        .data_o(result)
    );
    assign tag_o = result.tag;
endmodule
)";
    const std::string portTypes =
        "external_scalar_alias_child.data_i\tinput:data_t\n"
        "external_scalar_alias_child.data_o\toutput:data_t\n";
    const std::string moduleTraits =
        "external_scalar_alias_child\toutput_field.data_o.tag\n";
    const std::string typeAliasOverrides =
        "external_scalar_alias_child.data_t\tlogic<16>\n";
    auto h = convertModule(argv0, "external_scalar_alias_parent", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits,
                           typeAliasOverrides);
    expectContains(h, "child_i.data_o_out()");
    expectNotContains(h, "child_i.data_o_out__field_tag()");
}

static void testTypeParameterizedExternalChildInputBindsDemandedField(const char* argv0)
{
    const std::string sv = R"sv(
module type_param_external_parent #(
    parameter type req_t = logic
) (
    input req_t source_i [2],
    output logic [1:0] result_o
);
    for (genvar i = 0; i < 2; i++) begin : gen_child
        external_type_param_child #(
            .req_t(req_t)
        ) child_i (
            .req_i(source_i[i]),
            .result_o(result_o[i])
        );
    end
endmodule
)sv";
    const std::string portTypes =
        "external_type_param_child.req_i\tinput:req_t\n"
        "external_type_param_child.result_o\toutput:logic<1>\n";
    const std::string moduleTraits =
        "external_type_param_child\tinput_field.req_i.valid\n";
    auto h = convertModule(argv0, "type_param_external_parent", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectContains(h,
        "child_i[(unsigned)(uint64_t)((uint64_t)(i))].req_i_in__field_valid = "
        "_ASSIGN_I(source_i_in__field_valid()[");
}

static void testImplicitNamedExternalChildInputBindsDemandedFields(const char* argv0)
{
    const std::string sv = R"sv(
module implicit_named_external_parent #(
    parameter type ctrl_t = logic
) (
    input ctrl_t irq_ctrl_i
);
    for (genvar i = 0; i < 2; ++i) begin
        external_decoder #(
            .ctrl_t(ctrl_t)
        ) child_i (
            .irq_ctrl_i
        );
    end
endmodule
)sv";
    const std::string portTypes =
        "external_decoder.irq_ctrl_i\tinput:ctrl_t\n";
    const std::string moduleTraits =
        "external_decoder\tinput_field.irq_ctrl_i.mip\n"
        "external_decoder\tinput_field.irq_ctrl_i.mie\n";
    auto h = convertModule(argv0, "implicit_named_external_parent", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectContains(h,
        "child_i[(unsigned)(uint64_t)((uint64_t)(i))].irq_ctrl_i_in__field_mip = "
        "_ASSIGN_COMB(irq_ctrl_i_in__field_mip());");
    expectContains(h,
        "child_i[(unsigned)(uint64_t)((uint64_t)(i))].irq_ctrl_i_in__field_mie = "
        "_ASSIGN_COMB(irq_ctrl_i_in__field_mie());");
}

static void testOptimizeRunnerNameFollowsMainFile(const char* argv0)
{
    auto dir = makeTempDir("hdlcpp_optimize_runner_name");
    auto executable = hdlcppPath(argv0);
    fs::create_directories(dir / "generated");
    writeFile(dir / "generated" / "worker.h", R"cpp(
#pragma once
#include "cpphdl.h"

using namespace cpphdl;

template <uint64_t Width = 1>
class worker : public Module
{
public:
    worker() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
)cpp");
    writeFile(dir / "generated" / "parent.h", R"cpp(
#pragma once
#include "worker.h"

template <unsigned First = (1ull << 4), uint64_t Late = 7>
class parent : public Module
{
    worker<First> own;
    worker<Late> second;
public:
    parent() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template <unsigned Other = 3>
class sibling : public Module
{
    worker<Other> unrelated;
public:
    sibling() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template <unsigned Value = 1>
class z_implicit_ctor : public Module
{
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
)cpp");
    writeFile(dir / "custom_runner.cpp", R"cpp(
#include "generated/parent.h"
using CpphdlOptimizationTop = parent<>;
CpphdlOptimizationTop* dut = nullptr;
z_implicit_ctor<> support;
)cpp");
    writeFile(dir / "all_generated.h", R"cpp(
#pragma once
#include "generated/worker.h"
#include "generated/parent.h"
)cpp");

    auto oldCwd = fs::current_path();
    fs::current_path(dir);
    auto command = shellQuote(executable) + " --optimize custom_runner.cpp";
    auto rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    assert(rc == 0);
    expectContains(readFile(dir / "Makefile.optimize"), "RUNNER := custom_runner_opt\n");
    expectContains(readFile(dir / "Makefile.optimize"), "DEPFLAGS ?= -MMD -MP\n");
    expectContains(readFile(dir / "Makefile.optimize"), "-include $(DEPS)\n");
    auto externs = readFile(dir / "cpphdl_optimized_externs.h");
    expectContains(externs, "using cpphdl_opt_t0 = parent<>;");
    expectContains(externs, "using cpphdl_opt_t1 = z_implicit_ctor<>;");
    expectContains(externs, "using cpphdl_opt_t2 = worker<(1ull<< 4)>;");
    expectContains(externs, "using cpphdl_opt_t3 = worker<7>;");
    expectNotContains(externs, "worker<Other>");
    expectNotContains(externs, "uint64_t Late");
    expectContains(externs, "extern template cpphdl_opt_t0::parent();");
    expectNotContains(externs, "extern template cpphdl_opt_t1::z_implicit_ctor();");
    auto instantiations = readFile(dir / "cpphdl_optimized_inst_0.cpp");
    expectContains(instantiations, "template cpphdl_opt_t0::parent();");
    expectContains(instantiations, "template void cpphdl_opt_t0::_work(bool);");
    expectContains(instantiations, "template void cpphdl_opt_t2::_work(bool);");
    expectNotContains(instantiations, "template void cpphdl_opt_t3::_work(bool);");
    expectContains(readFile(dir / "cpphdl_optimized_inst_1.cpp"),
                   "template void cpphdl_opt_t3::_work(bool);");

    auto inst0WriteTime = fs::last_write_time(dir / "cpphdl_optimized_inst_0.cpp");
    fs::current_path(dir);
    rc = std::system((shellQuote(executable) + " --optimize custom_runner.cpp").c_str());
    fs::current_path(oldCwd);
    assert(rc == 0);
    assert(fs::last_write_time(dir / "cpphdl_optimized_inst_0.cpp") == inst0WriteTime);

    fs::current_path(dir);
    command = "make -f Makefile.optimize -j1 "
              "build/opt/cpphdl_optimized_main.o "
              "build/opt/cpphdl_optimized_inst_0.o "
              "build/opt/cpphdl_optimized_inst_1.o";
    rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    assert(rc == 0);
}

static void testOptimizeSeparatesUnitsByGeneratedDefinitionWeight(const char* argv0)
{
    auto dir = makeTempDir("hdlcpp_optimize_definition_weight");
    auto executable = hdlcppPath(argv0);
    fs::create_directories(dir / "generated");
    const std::string moduleHeader = R"cpp(
#pragma once
#include "cpphdl.h"

using namespace cpphdl;

template <uint64_t Value = 0>
class MODULE_NAME : public Module
{
public:
    MODULE_NAME() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
)cpp";
    for (const auto& name : {"large", "small_a", "small_b"}) {
        auto header = moduleHeader;
        if (std::string(name) == "large") {
            header.insert(header.find("public:"), std::string(2000, ' '));
        }
        for (auto marker = header.find("MODULE_NAME"); marker != std::string::npos;
             marker = header.find("MODULE_NAME", marker + std::string(name).size())) {
            header.replace(marker, std::string("MODULE_NAME").size(), name);
        }
        writeFile(dir / "generated" / (std::string(name) + ".h"), header);
    }
    writeFile(dir / "all_generated.h", R"cpp(
#pragma once
#include "generated/large.h"
#include "generated/small_a.h"
#include "generated/small_b.h"
)cpp");
    writeFile(dir / "weighted_runner.cpp", R"cpp(
#include "all_generated.h"
large<> first;
small_a<> second;
small_b<> third;
)cpp");

    auto oldCwd = fs::current_path();
    fs::current_path(dir);
    auto command = "HDLCPP_OPTIMIZE_INSTANTIATIONS_PER_FILE=32 "
                   "HDLCPP_OPTIMIZE_MAX_DEFINITION_BYTES_PER_FILE=1000 " +
                   shellQuote(executable) + " --optimize weighted_runner.cpp";
    auto rc = std::system(command.c_str());
    fs::current_path(oldCwd);
    assert(rc == 0);

    bool foundLargeWork = false;
    bool foundLargeAssign = false;
    bool foundSmallA = false;
    bool foundSmallB = false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        auto name = entry.path().filename().string();
        if (name.rfind("cpphdl_optimized_inst_", 0) != 0 || entry.path().extension() != ".cpp") {
            continue;
        }
        auto text = readFile(entry.path());
        if (text.find("template void cpphdl_opt_t0::_work(bool);") != std::string::npos) {
            foundLargeWork = true;
            expectNotContains(text, "template void cpphdl_opt_t0::_assign();");
        }
        if (text.find("template void cpphdl_opt_t0::_assign();") != std::string::npos) {
            foundLargeAssign = true;
            expectNotContains(text, "template void cpphdl_opt_t0::_work(bool);");
        }
        foundSmallA = foundSmallA ||
            text.find("template void cpphdl_opt_t1::_work(bool);") != std::string::npos;
        foundSmallB = foundSmallB ||
            text.find("template void cpphdl_opt_t2::_work(bool);") != std::string::npos;
    }
    assert(foundLargeWork);
    assert(foundLargeAssign);
    assert(foundSmallA);
    assert(foundSmallB);
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
    expectContains(h, "memory<logic<8>,1,2> mem_q;");
    expectContains(h, "mem_q.apply();");
    expectContains(h, "scalar_q._next = scalar_q;");
    expectContains(h, "mem_q[(unsigned)((uint64_t)(((uint64_t)(1)");
    expectNotContains(h, "mem_q._next");
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
    expectContains(h, "static constexpr uint64_t USE_TAG =");
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
    expectContains(h, "reg<array<");
    expectContains(h, ",dtype,true>> reg_q;");
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
    output logic [N-1:0] out_o,
    output logic [N-1:0] upper_o
);
  genvar gen_i;
  for (gen_i = 0; gen_i < N; gen_i++) begin : gen_mask
    assign out_o[gen_i] = |in_i[gen_i:0];
    assign upper_o[gen_i] = &in_i[N-1:gen_i];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "runtime_range_select", sv, "");
    expectContains(h, "cpphdl::sv_bits_runtime");
    expectNotContains(h, ",(uint64_t))(");
    expectNotContains(h, "sv_bits_runtime(in_i_in(),(uint64_t))");
    expectNotContains(h, "sv_bits_runtime(in_i_in(),(uint64_t)(N) -");
    expectNotContains(h, "sv_bits<((uint64_t)((uint64_t)(i)))+1>");
    expectNotContains(h, "reduce_and(cpphdl::sv_bits_runtime");
}

static void testRuntimeRangeConcatUsesRuntimeOperandWidths(const char* argv0)
{
    const std::string sv = R"sv(
module runtime_range_concat #(
    parameter int unsigned N = 8,
    localparam int unsigned Levels = $clog2(N)
) (
    input  logic [Levels-1:0] in_i,
    output logic [Levels:0]   out_o
);
  logic [Levels:0] values [Levels-2:0];
  for (genvar level = 0; level < Levels-1; level++) begin
    assign values[level] = {1'b1, in_i[Levels-level-2:0]};
  end
  assign out_o = values[0];
endmodule
)sv";

    auto h = convertModule(argv0, "runtime_range_concat", sv, "");
    expectContains(h, "cpphdl::sv_bits_runtime");
    expectContains(h, "__cpphdl_cat_append");
    expectNotContains(h, "cpphdl::sv_bits<((uint64_t)(Levels)");
    expectNotContains(h, "cpphdl::pack_value<((uint64_t)(Levels)");
}

static void testPackedArrayElementCastMaterializesStoredValue(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_element_cast (
    input  logic       select_i,
    input  logic [3:0] value_i,
    output logic [3:0] value_o
);
  typedef struct packed {
    logic [3:0] value;
  } entry_t;

  entry_t [1:0] entries;
  always_comb begin
    entries = '0;
    entries[select_i] = entry_t'(value_i);
    value_o = entries[0].value;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_element_cast", sv, "");
    expectContains(h, "cpphdl::value_type_for_ref_t<decltype(entries_comb[");
    expectNotContains(h, "std::remove_cvref_t<decltype(entries_comb[");
}

static void testPackedStructArrayFieldConditionalUsesDeclaredElementType(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_array_field_conditional #(
    parameter type entry_t = logic
) (
    input  logic        clk_i,
    input  logic        select_i,
    input  logic [31:0] left_i,
    input  logic [31:0] right_i,
    output entry_t [1:0] entries_o
);
  always_ff @(posedge clk_i) begin
    for (int i = 0; i < 2; ++i) begin
      entries_o[i].value <= select_i ? left_i : right_i;
    end
  end
endmodule
)sv";

    const std::string moduleParams =
        "packed_struct_array_field_conditional\ttypename entry_t = logic<1>\n";
    const std::string portTypes =
        "packed_struct_array_field_conditional.entries_o\toutput:array<entry_t,2>\n";
    const std::string moduleTraits =
        "packed_struct_array_field_conditional\toutput_field.entries_o.value\n";
    auto h = convertModule(argv0, "packed_struct_array_field_conditional", sv,
                           "", "", "", "", moduleParams, portTypes, "", moduleTraits);
    expectContains(h, "auto __cpphdl_elem = cpphdl::unpack_value<entry_t>");
    expectContains(h, "__cpphdl_elem.value = select_i_in() ? logic<32>(left_i_in()) : logic<32>(right_i_in())");
    expectNotContains(h, "decltype(entries_o[i].value)");
    expectNotContains(h, "decltype(entries_o._next[i].value)");
}

static void testStaticParameterizedRangeSelectKeepsAllBitsArguments(const char* argv0)
{
    const std::string sv = R"sv(
module static_parameterized_range_select #(
    parameter int unsigned XLEN = 32,
    parameter int unsigned FLEN = 16
) (
    input  logic [XLEN-1:0] in_i,
    output logic [FLEN-1:0] out_o
);
  assign out_o = in_i[FLEN-1:0];
endmodule
)sv";

    auto h = convertModule(argv0, "static_parameterized_range_select", sv, "");
    expectContains(h, "cpphdl::sv_bits<");
    expectNotContains(h, ">)(in_i_in()");
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
    expectNotContains(h, "] = array<2,entry_t>{ .valid");
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
    expectContains(h, "u_child.in_i_in = _ASSIGN(__port_bind_u_child_in_i_in_comb_func());");
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
    expectNotContains(h, "] = array<2,entry_t>{ .valid");
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
    expectContains(h, "logic<cpphdl::type_width<array<2,dep_t>>()");
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
    expectContains(h, "cpphdl::repeat");
    expectNotContains(h, "W/8{");
    expectNotContains(h, "__cpphdl_rep{}; for");
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

static void testTypeTemplateCastShiftKeepsTargetWidth(const char* argv0)
{
    const std::string sv = R"sv(
module type_template_cast_shift #(
    parameter int unsigned OFF = 3,
    parameter type addr_t = logic [31:0]
) (
    input  logic [15:0] tag_i,
    output addr_t       addr_o
);
  addr_t tmp;

  always_comb begin
    tmp = addr_t'(tag_i) << OFF;
  end

  assign addr_o = tmp;
endmodule
)sv";

    auto h = convertModule(argv0, "type_template_cast_shift", sv, "");
    expectContains(h, "type_width<addr_t>()");
    expectNotContains(h, "tmp_comb = logic<1>(");
    expectNotContains(h, "logic<1>(((uint64_t)(cpphdl::sv_cast<addr_t>");
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
    expectContains(h, "_LAZY_COMB(__port_bind_child_i_val_i_in_comb, std::remove_cvref_t<decltype(child_i.val_i_in())>)");
    expectContains(h, "child_i.val_i_in = _ASSIGN_COMB(__port_bind_child_i_val_i_in_comb_func());");
    expectNotContains(h, "__port_bind_child_i_val_i_in_packed_to_array_comb_func()");
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
    expectContains(h, "logic<1>& routed_comb_func()");
    expectNotContains(h, "_LAZY_COMB(routed_comb");
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
    expectContains(h, "child_i.in_i_in = _ASSIGN_COMB(__port_bind_child_i_in_i_in_comb_func());");
    expectNotContains(h, "child_i.in_i_in = _ASSIGN(std::remove_cvref_t<decltype(child_i.in_i_in())>(wide_i_in()));");
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
    expectContains(h, "child_i.in_i_in = _ASSIGN_COMB(__port_bind_child_i_in_i_in_comb_func());");
    expectNotContains(h, "child_i.in_i_in = _ASSIGN(cpphdl::sv_bits<");
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
    expectContains(h, "child_i.req_i_in = _ASSIGN(cpphdl::sv_cast<std::remove_cvref_t<decltype(child_i.req_i_in())>>((reqs_i_in())[(unsigned)((uint64_t)(((uint64_t)(2) & ((1ull << 32) - 1ull))))]));");
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
    expectContains(h, "child_i.word_i_in = _ASSIGN_COMB(__port_bind_child_i_word_i_in_comb_func());");
    expectNotContains(h, "child_i.word_i_in = _ASSIGN(cpphdl::pack_value");
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
    expectContains(h, ".word_i_in = _ASSIGN_I( words_comb_func()[");
    expectNotContains(h, ".word_i_in = _ASSIGN_I(logic<32>(cpphdl::pack_value");
    expectNotContains(h, ".word_i_in = _ASSIGN_COMB_I(logic<32>(cpphdl::pack_value");
}

static void testCombVectorBitInputUsesCombBinding(const char* argv0)
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
    expectContains(h, "child_i.data_i_in = _ASSIGN_COMB(__port_bind_child_i_data_i_in_comb_func());");
    expectNotContains(h, "child_i.data_i_in = _ASSIGN(logic<8>(req_i_in().data));");
    expectNotContains(h, "child_i.data_i_in = _ASSIGN_COMB(cpphdl::pack_value");
}

static void testDynamicStructInputBindingsStayComb(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       valid;
  logic [7:0] data;
} resp_t;

module field_sink (
    input  logic       valid_i,
    input  logic [7:0] data_i,
    output logic       pass_o
);
  assign pass_o = valid_i & data_i[0];
endmodule

module whole_sink (
    input  resp_t resp_i,
    output logic  pass_o
);
  assign pass_o = resp_i.valid & resp_i.data[0];
endmodule

module dynamic_struct_input_bindings (
    input  resp_t resp_i,
    output logic  field_pass_o,
    output logic  whole_pass_o
);
  field_sink u_field (
    .valid_i(resp_i.valid),
    .data_i(resp_i.data),
    .pass_o(field_pass_o)
  );

  whole_sink u_whole (
    .resp_i(resp_i),
    .pass_o(whole_pass_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "dynamic_struct_input_bindings", sv, "");
    expectContains(h, "u_field.valid_i_in = _ASSIGN_COMB(__port_bind_u_field_valid_i_in_comb_func());");
    expectContains(h, "u_field.data_i_in = _ASSIGN_COMB(__port_bind_u_field_data_i_in_comb_func());");
    expectContains(h, "u_whole.resp_i_in = _ASSIGN(resp_i_in());");
    expectNotContains(h, "u_field.valid_i_in = _ASSIGN(logic<1>(resp_i_in().valid));");
    expectNotContains(h, "u_field.data_i_in = _ASSIGN(logic<8>(resp_i_in().data));");
    expectNotContains(h, "u_whole.resp_i_in = _ASSIGN_COMB(resp_i_in());");
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
    expectContains(h, ".req_i_in = _ASSIGN_I( routed_comb_func()[(unsigned)(uint64_t)((uint64_t)(i))] );");
}

static void testIndexedCombArrayInputUsesCombBinding(const char* argv0)
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
    expectContains(h, ".addr_i_in = _ASSIGN_I( std::remove_cvref_t<decltype(child_i[");
    expectContains(h, "].addr_i_in())>(addr_i_in()) );");
    expectNotContains(h, ".addr_i_in = _ASSIGN_COMB_I( addr_i_in() );");
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
    expectContains(h, ".ctrl_i_in = _ASSIGN_I( ctrl_i_in() );");
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
    expectNotContains(h, ".in_i_in = _ASSIGN_COMB_I(");
    expectContains(h, "routed_comb_func()");
}

static void testGeneratedIndexedBitExtractUsesValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
module bit_sink (
    input logic bit_i
);
endmodule

module generated_indexed_bit_extract (
    input logic [1:0] src_i
);
  logic [1:0] routed;
  assign routed = src_i;

  for (genvar i = 0; i < 2; i++) begin : gen_bits
    bit_sink child_i (
      .bit_i(routed[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generated_indexed_bit_extract", sv, "");
    expectContains(h, ".bit_i_in = _ASSIGN_I(");
    expectNotContains(h, ".bit_i_in = _ASSIGN_COMB_I( logic<1>(");
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
    expectContains(h, "child_i.vals_i_in = _ASSIGN(vals_comb_func());");
    expectNotContains(h, "child_i.vals_i_in = _ASSIGN_COMB(vals_comb_func());");
    expectNotContains(h, "__port_bind_child_i_vals_i_in_packed_to_array_comb_func()");
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
    expectContains(h, "using __cpphdl_target_array_t = std::remove_cvref_t<decltype(child_i.vals_i_in())>;");
    expectContains(h, "__port_bind_child_i_vals_i_in_unpacked_array_comb[__cpphdl_i] = cpphdl::unpack_value<__cpphdl_target_elem_t>");
    expectContains(h, "auto __cpphdl_src = vals_i_in();");
    expectNotContains(h, "array<2,logic<64>> __port_bind_child_i_vals_i_in_unpacked_array_comb;");
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

static void testUnbasedOneInputPortUsesDestinationWidth(const char* argv0)
{
    const std::string sv = R"sv(
module mask_child(
    input logic [2:0] be_i
);
endmodule

module unbased_one_port_parent;
  mask_child child_i (
    .be_i('1)
  );

  for (genvar i = 0; i < 2; i++) begin : gen_child
    mask_child indexed_child_i (
      .be_i('1)
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "unbased_one_port_parent", sv, "");
    expectContains(h, "child_i.be_i_in = _ASSIGN(logic<3>(((1ull << 3) - 1ull)));");
    expectContains(h, ".be_i_in = _ASSIGN_I( logic<3>(((1ull << 3) - 1ull)) )");
    expectNotContains(h, ".be_i_in = _ASSIGN(1);");
    expectNotContains(h, ".be_i_in = _ASSIGN_I( 1 )");
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
    expectContains(h, "std::is_assignable_v<templ_same_struct_pkg::req_t&, std::remove_cvref_t<decltype((tmp_comb_func()))>>");
    expectNotContains(h, "tmp_comb = cpphdl::unpack_value<templ_same_struct_pkg::req_t>(cpphdl::pack_value<cpphdl::type_width<templ_same_struct_pkg::req_t>()>(child_i.data_o_out()));");
}

static void testParameterizedInterfacePortInfersChildTemplateAndBindsByRef(const char* argv0)
{
    const std::string sv = R"sv(
interface IFACE #(
  parameter int AW = -1,
  parameter int DW = -1
)(
  input logic clk_i
);
  logic [AW-1:0] addr;
  logic [DW-1:0] data;
  modport out (output addr, data);
endinterface

module iface_child (
  input logic rst_ni,
  IFACE.out bus
);
  always_comb begin
    bus.addr = '0;
    bus.data = '1;
  end
endmodule

module iface_parent (
  input logic clk_i,
  input logic rst_ni
);
  IFACE #(
    .AW(32),
    .DW(64)
  ) bus(clk_i);

  iface_child child_i (
    .rst_ni(rst_ni),
    .bus(bus)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "iface_parent", sv, "");
    expectContains(h, "template<int AW = -1, int DW = -1>");
    expectContains(h, "_PORT(::IFACE<AW,DW>) bus;");
    expectContains(h, "::IFACE<32,64> bus;");
    expectContains(h, "::iface_child<32,64> child_i;");
    expectContains(h, "child_i.bus = _ASSIGN_REG(bus);");
    expectNotContains(h, "::iface_child child_i;");
    expectNotContains(h, "child_i.bus = _ASSIGN(bus);");
}

static void testParameterizedInterfaceArrayInfersNestedTemplateArguments(const char* argv0)
{
    const std::string sv = R"sv(
interface IFACE_ARRAY #(
  parameter int AW = -1,
  parameter int DW = -1
)();
  logic [AW-1:0] addr;
  logic [DW-1:0] data;
  modport out (output addr, data);
endinterface

module iface_array_child #(
  parameter int MODE = 1
) (
  IFACE_ARRAY.out first [1:0],
                  second [1:0]
);
endmodule

module iface_array_parent;
  IFACE_ARRAY #(.AW(32), .DW(64)) first[1:0]();
  IFACE_ARRAY #(.AW(40), .DW(72)) second[1:0]();
  iface_array_child #(.MODE(7)) child_i (
    .first(first),
    .second(second)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "iface_array_parent", sv, "");
    expectContains(h, "::IFACE_ARRAY<32,64>");
    expectContains(h, "::IFACE_ARRAY<40,72>");
    expectContains(h, "::iface_array_child<7,32,64,40,72> child_i;");
    expectContains(h, "child_i.first = _ASSIGN_REG(first);");
    expectContains(h, "child_i.second = _ASSIGN_REG(second);");
    expectNotContains(h, "__port_bind_child_i_first_packed_to_array_comb");
    expectNotContains(h, "__port_bind_child_i_second_packed_to_array_comb");
    expectNotContains(h, "::iface_array_child<7,-1,-1");
}

static void testIndexedInterfaceConnectionBindsObjectByReference(const char* argv0)
{
    const std::string sv = R"sv(
interface INDEXED_IF;
  logic request;
  modport sink(input request);
endinterface

module indexed_interface_child(INDEXED_IF.sink bus);
  logic observed;
  always_comb observed = bus.request;
endmodule

module indexed_interface_connection;
  INDEXED_IF buses[1:0]();
  indexed_interface_child child_i(.bus(buses[1]));
endmodule
)sv";

    auto h = convertModule(argv0, "indexed_interface_connection", sv, "");
    expectContains(h, "child_i.bus = _ASSIGN_REG(buses[");
    expectNotContains(h, "child_i.bus = _ASSIGN(cpphdl::sv_cast<");
}

static void testIncludedMacroExpandsInterfaceArrayBindings(const char* argv0)
{
    const std::string includeText = R"sv(
`define CONNECT_REQ(dst, src) assign dst.req = src.req;
`define CONNECT_RSP(dst, src) assign dst.rsp = src.rsp;
)sv";
    const std::string sv = R"sv(
`include "hdlcpp_test_defs.svh"

interface BIND_IF;
  logic req;
  logic rsp;
  modport Master(output req, input rsp);
  modport Slave(input req, output rsp);
endinterface

module interface_array_bindings(
  BIND_IF.Master mst [1:0],
  BIND_IF.Slave  slv [1:0]
);
  for (genvar i = 0; i < 2; i++) begin
    `CONNECT_REQ(mst[i], slv[i])
    `CONNECT_RSP(slv[i], mst[i])
  end
`ifdef HDLCPP_TEST_DEFINE
`else
  invalid_missing_define should_not_parse();
`endif
endmodule
)sv";

    auto h = convertModule(argv0, "interface_array_bindings", sv, "", "", "", "", "", "", includeText);
    expectContains(h, "_PORT(logic<1>) req;");
    expectContains(h, "_PORT(logic<1>) rsp;");
    expectContains(h, "for (unsigned i = 0;");
    expectContains(h, "mst()[i].req = _ASSIGN");
    expectContains(h, "slv()[i].rsp = _ASSIGN");
    expectContains(h, ".req()");
    expectContains(h, ".rsp()");
    expectNotContains(h, "should_not_parse");
}

static void testCrossFileInterfaceTraitEnablesSignalBinding(const char* argv0)
{
    const std::string sv = R"sv(
module remote_interface_binding(
  input logic in_i,
  REMOTE_IF.Master bus
);
  assign bus.sig = in_i;
endmodule
)sv";
    const std::string moduleParams = "REMOTE_IF\tint W = -1\n";
    const std::string moduleTraits = "REMOTE_IF\tinterface=1\n";

    auto h = convertModule(argv0, "remote_interface_binding", sv, "", "", "", "",
                           moduleParams, "", "", moduleTraits);
    expectContains(h, "_PORT(::REMOTE_IF<W>) bus;");
    expectContains(h, "bus().sig = _ASSIGN");
    expectContains(h, "in_i_in()");
}

static void testConditionalGenerateInterfaceBindingDoesNotCaptureLoopIndex(const char* argv0)
{
    const std::string sv = R"sv(
interface CONDITIONAL_IF;
  logic sig;
endinterface

module conditional_interface_binding #(
  parameter bit ENABLE = 1'b0
)(
  CONDITIONAL_IF bus
);
  if (ENABLE) begin
    assign bus.sig = 1'b1;
  end else begin
    assign bus.sig = 1'b0;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "conditional_interface_binding", sv, "");
    expectContains(h, "bus().sig = _ASSIGN(");
    expectNotContains(h, "bus().sig = _ASSIGN_I(");
    expectNotContains(h, "bus().sig = _ASSIGN_COMB_I(");
}

static void testProceduralInterfaceMemberGetsIndependentCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
interface PROCEDURAL_IF;
  logic request;
  logic ready;
  logic payload;
  modport target(input request, output ready, payload);
endinterface

module procedural_interface_member(
  input logic enable_i,
  PROCEDURAL_IF.target bus
);
  logic temporary;
  always_comb begin
    temporary = enable_i & bus.request;
    bus.ready = 1'b0;
    bus.payload = 1'b0;
    if (temporary) begin
      bus.ready = 1'b1;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "procedural_interface_member", sv, "");
    expectContains(h, "bus_ready_comb_func()");
    expectContains(h, "bus_payload_comb_func()");
    expectContains(h, "bus().ready = _ASSIGN_COMB(bus_ready_comb_func());");
    expectContains(h, "bus().payload = _ASSIGN_COMB(bus_payload_comb_func());");
    expectContains(h, "__comb_local_temporary");
    expectContains(h, "bus().request()");
    expectNotContains(h, "bus_ready_comb().request()");
    expectNotContains(h, "void continuous_comb_func");
}

static void testInstanceOutputBindsInterfaceMemberDirectly(const char* argv0)
{
    const std::string sv = R"sv(
interface INSTANCE_OUTPUT_IF;
  logic request;
  logic ready;
  logic unused;
  modport target(input request, output ready, unused);
endinterface

module instance_output_child(
  input  logic request_i,
  output logic ready_o
);
  assign ready_o = request_i;
endmodule

module instance_output_interface(INSTANCE_OUTPUT_IF.target bus);
  instance_output_child child_i(
    .request_i(bus.request),
    .ready_o(bus.ready)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "instance_output_interface", sv, "");
    expectContains(h, "bus().ready = _ASSIGN_COMB(child_i.ready_o_out());");
    expectContains(h, "bus().unused = _ASSIGN(std::remove_cvref_t<decltype(bus().unused())>{});");
    expectNotContains(h, "bus().request = _ASSIGN(");
    expectNotContains(h, "bus_comb");
}

static void testUnderscoreSignalIsNotInferredAsStructField(const char* argv0)
{
    const std::string sv = R"sv(
module underscore_signal_not_field(
  input  logic data_i,
  input  logic valid_i,
  output logic valid_o
);
  typedef struct packed {
    logic payload;
  } item_t;
  item_t item;
  logic item_valid;

  always_comb begin
    item.payload = data_i;
    item_valid = valid_i;
  end
  assign valid_o = item_valid;
endmodule
)sv";

    auto h = convertModule(argv0, "underscore_signal_not_field", sv, "");
    expectContains(h, "item_valid_comb_func()");
    expectNotContains(h, "std::declval<item_t>().valid");
    expectNotContains(h, "item_valid_comb = (cpphdl::unpack_value<item_t>");
}

static void testStructFieldCombNameDoesNotCollideWithScalarSignal(const char* argv0)
{
    const std::string sv = R"sv(
module field_name_collision_child(
  input  logic data_i,
  input  logic valid_i,
  output item_t item_o,
  output logic item_valid_o
);
  typedef struct packed {
    logic data;
    logic valid;
  } item_t;
  assign item_o = '{data: data_i, valid: valid_i};
  assign item_valid_o = valid_i;
endmodule

module field_name_collision_parent(
  input  logic data_i,
  input  logic valid_i,
  output logic scalar_valid_o,
  output logic aggregate_valid_o
);
  typedef struct packed {
    logic data;
    logic valid;
  } item_t;
  item_t item;
  logic item_valid;

  field_name_collision_child child_i(
    .data_i(data_i),
    .valid_i(valid_i),
    .item_o(item),
    .item_valid_o(item_valid)
  );
  assign scalar_valid_o = item_valid;
  assign aggregate_valid_o = item.valid;
endmodule
)sv";

    auto h = convertModule(argv0, "field_name_collision_parent", sv, "");
    expectContains(h, "item_valid_comb = child_i.item_valid_o_out();");
    expectContains(h, "item__field_valid_comb");
    expectNotContains(h, "item_valid_comb = (cpphdl::unpack_value<item_t>");
    expectNotContains(h, "std::declval<item_t>().valid)> item_valid_comb");
}

static void testFieldDemandMethodCallRequiresIdentifierBoundary(const char* argv0)
{
    const std::string sv = R"sv(
module field_demand_identifier_boundary(
  input  logic data_i,
  input  logic valid_i,
  output logic valid_o
);
  typedef struct packed {
    logic data;
  } item_t;
  typedef struct packed {
    logic valid;
  } exception_item_t;
  item_t item;
  exception_item_t ex_item;

  always_comb begin
    item.data = data_i;
    ex_item.valid = valid_i;
  end
  assign valid_o = ex_item.valid;
endmodule
)sv";

    auto h = convertModule(argv0, "field_demand_identifier_boundary", sv, "");
    expectContains(h, "ex_item_valid_comb_func()");
    expectNotContains(h, "std::declval<item_t>().valid");
    expectNotContains(h, "item__field_valid_comb");
}

static void testTypeParameterFieldDoesNotUseOtherModuleLocalType(const char* argv0)
{
    const std::string sv = R"sv(
module unrelated_local_type;
  typedef logic [3:0] unrelated_chan_t;
  typedef struct packed {
    unrelated_chan_t channel;
  } req_t;
  req_t unused;
endmodule

module opaque_field_sink(input logic channel_i);
endmodule

module opaque_type_parameter_field #(
  parameter type req_t = logic
)(
  input  req_t req_i,
  output req_t req_o,
  output logic channel_o
);
  logic channel_copy;
  always_comb begin
    req_o = '0;
  end
  assign req_o.channel = req_i.channel;
  always_comb begin
    channel_copy = 1'b0;
    if (req_o.channel)
      channel_copy = 1'b1;
  end
  assign channel_o = channel_copy;
  opaque_field_sink sink_i(.channel_i(req_o.channel));
endmodule
)sv";

    auto h = convertModule(argv0, "opaque_type_parameter_field", sv, "");
    auto targetClass = h.find("class opaque_type_parameter_field");
    assert(targetClass != std::string::npos);
    auto targetText = h.substr(targetClass);
    expectContains(targetText, "req_o_channel_comb_func()");
    expectNotContains(targetText, "unrelated_chan_t");
}

static void testConfiguredInterfaceArrayInfersNestedTemplateArguments(const char* argv0)
{
    const std::string sv = R"sv(
interface IFACE_META #(
  parameter int AW = -1,
  parameter int DW = -1
)();
endinterface

module configured_iface_parent;
  IFACE_META #(.AW(32), .DW(64)) first[1:0]();
  IFACE_META #(.AW(40), .DW(72)) second[1:0]();
  metadata_child #(.MODE(7)) child_i (
    .first(first),
    .second(second)
  );
endmodule
)sv";
    const std::string moduleParams =
        "IFACE_META\tint AW = -1\tint DW = -1\n"
        "metadata_child\tint MODE = 1\tint AW = -1\tint DW = -1\t"
        "int second_AW = -1\tint second_DW = -1\n";
    const std::string portTypes =
        "metadata_child.first\t:array<::IFACE_META<AW,DW>,2>\n"
        "metadata_child.second\t:array<::IFACE_META<second_AW,second_DW>,2>\n";

    auto h = convertModule(argv0, "configured_iface_parent", sv, "", "", "", "",
                           moduleParams, portTypes);
    expectContains(h, "::metadata_child<7,32,64,40,72> child_i;");
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

static void testConstantZeroFeedthroughMaskDoesNotEagerlyReadInput(const char* argv0)
{
    const std::string sv = R"sv(
module constant_zero_feedthrough_mask #(
    parameter bit FEEDTHROUGH = 1'b0
) (
    input  logic w_i,
    input  logic valid_q,
    output logic rok_o
);
  assign rok_o = valid_q | (FEEDTHROUGH & w_i);
endmodule
)sv";

    auto h = convertModule(argv0, "constant_zero_feedthrough_mask", sv,
                           "constant_zero_feedthrough_mask.FEEDTHROUGH\t0\n");
    expectContains(h, "_PORT(logic<1>) rok_o_out = _ASSIGN_COMB( rok_o_comb_func() );");
    expectContains(h, "_LAZY_COMB(rok_o_comb, logic<1>)");
    expectContains(h, "((uint64_t)(FEEDTHROUGH)) == 0) ? logic<64>(0) : logic<64>(logic<64>(FEEDTHROUGH) & logic<64>(w_i_in()))");
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
    expectContains(h, "() { logic<");
    expectContains(h, "std::size_t __cpphdl_i");
    expectNotContains(h, "logic<64> __cpphdl_rep");
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

static void testPackageArrayReplicationConstantUsesCppLambda(const char* argv0)
{
    const std::string sv = R"sv(
package package_array_replication_const;
  localparam NB = 3;
  localparam NR = 1;
  localparam logic [NR-1:0][NB-1:0] VALID = {{NR * NB}{1'b1}};
endpackage
)sv";

    auto h = convertModule(argv0, "package_array_replication_const", sv, "");
    expectContains(h, "inline constexpr std::array<uint64_t");
    expectContains(h, "cpphdl::repeat");
    expectNotContains(h, "{{{NR * NB}{");
}

static void testImportedPackageValueIsQualified(const char* argv0)
{
    const std::string sv = R"sv(
package value_pkg_a;
  localparam int unsigned TOKEN = 3;
endpackage

package value_pkg_b;
  localparam int unsigned TOKEN = 7;
endpackage

module imported_value_qualify (
    output logic [3:0] out_o
);
  import value_pkg_a::*;

  function automatic logic [3:0] pick();
    pick = TOKEN;
  endfunction

  assign out_o = pick();
endmodule
)sv";

    auto h = convertModule(argv0, "imported_value_qualify", sv, "");
    expectContains(h, "value_pkg_a::TOKEN");
    expectNotContains(h, "pick = TOKEN;");
}

static void testImportedPackageCallableIsQualified(const char* argv0)
{
    const std::string sv = R"SV(
module imported_package_callable
  import selected_functions::*;
(
  input logic value_i,
  output logic value_o
);
  assign value_o = accepts(value_i);
endmodule
)SV";

    auto h = convertModule(
        argv0, "imported_package_callable", sv, "", "", "",
        "selected_functions.accepts\tselected_functions::accepts\n"
        "unrelated_functions.accepts\tunrelated_functions::accepts\n");
    expectContains(h, "selected_functions::accepts(value_i_in())");
    expectNotContains(h, "unrelated_functions::accepts(value_i_in())");
}

static void testLocalEnumValueBeatsSingleImportedPackageFallback(const char* argv0)
{
    const std::string sv = R"sv(
package fallback_pkg;
  localparam int unsigned TOKEN = 7;
endpackage

module local_enum_value_beats_import (
    output logic [1:0] out_o
);
  import fallback_pkg::*;

  typedef enum logic [1:0] {
    TOKEN_IDLE,
    TOKEN_BUSY
  } state_t;

  state_t state_q;
  assign out_o = TOKEN_BUSY;
endmodule
)sv";

    auto h = convertModule(argv0, "local_enum_value_beats_import", sv, "");
    expectContains(h, "TOKEN_BUSY");
    expectNotContains(h, "fallback_pkg::TOKEN_BUSY");
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
    expectContains(h, "cat{logic<7>");
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

static void testInterfaceMembersInConcatUseSignalWidths(const char* argv0)
{
    const std::string sv = R"sv(
interface CONCAT_BUS #(parameter int unsigned ID_WIDTH = 5);
  logic [ID_WIDTH-1:0] id;
  logic [63:0] addr;
  logic [7:0] len;
  logic [2:0] size;
  logic [1:0] burst;
  modport Slave(input id, addr, len, size, burst);
endinterface

module interface_concat_capture(CONCAT_BUS.Slave bus, output logic [81:0] packed_o);
  typedef struct packed {
    logic [4:0] id;
    logic [63:0] addr;
    logic [7:0] len;
    logic [2:0] size;
    logic [1:0] burst;
  } req_t;
  req_t req;
  always_comb req = {bus.id, bus.addr, bus.len, bus.size, bus.burst};
  assign packed_o = req;
endmodule
)sv";

    auto h = convertModule(argv0, "interface_concat_capture", sv, "");
    expectContains(h, ">().id())");
    expectContains(h, ">().addr())");
    expectNotContains(h, ">().id)");
    expectNotContains(h, ">().addr)");
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

static void testConcatGeneratedLocalArrayBitSelectUsesOneBitWidth(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    int unsigned width;
    int unsigned count;
} concat_local_cfg_t;

module concat_generated_local_array_bit_select #(
    parameter concat_local_cfg_t CFG = '{width: 2, count: 1}
) (
    input  logic [CFG.width-1:0] seed_i,
    output logic [CFG.width-1:0] out_o
);
  logic [CFG.width-1:0] idx_ds [CFG.count+1];

  assign idx_ds[0] = seed_i;
  for (genvar i = 0; i < CFG.count; i++) begin
    assign idx_ds[i+1] = {idx_ds[i][CFG.width-2:0], idx_ds[i][CFG.width-1]};
  end
  assign out_o = idx_ds[CFG.count];
endmodule
)sv";

    auto h = convertModule(argv0, "concat_generated_local_array_bit_select", sv, "");
    expectContains(h, "cat{");
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

static void testConcatPackedArraySliceUsesElementWidth(const char* argv0)
{
    const std::string sv = R"sv(
module concat_packed_array_slice #(
    parameter int unsigned W = 64,
    parameter int unsigned N = 2
) (
    input  logic [W-1:0] in_i,
    output logic [W-1:0] out_o [N]
);
  logic [W-1:0] shift_q [N];
  logic [W-1:0] shift_d [N];

  always_comb begin
    shift_d = shift_q;
    shift_d = {in_i, shift_q[N-1:1]};
    out_o = shift_d;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "concat_packed_array_slice", sv, "concat_packed_array_slice.W\t64\nconcat_packed_array_slice.N\t2\n");
    expectContains(h, "__cpphdl_slice_out");
    expectContains(h, "logic<((uint64_t)(((uint64_t)(W) & ((1ull << 32) - 1ull)))) *");
    expectNotContains(h, "logic<1>((uint64_t)(logic<((uint64_t)(((uint64_t)(W) & ((1ull << 32) - 1ull)))) *");
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
    expectContains(h, "cpphdl::sv_assign_field(resp_comb.inv, 0);");
    expectNotContains(h, "using __cpphdl_cast_t = nested_t;");
    expectNotContains(h, "resp_comb.inv = 0;");
}

static void testZeroAssignmentToTypeParameterizedPackedArrayKeepsPackedWidth(const char* argv0)
{
    const std::string sv = R"sv(
module zero_type_array #(
    parameter int unsigned PORTS = 4,
    parameter type req_t = logic [7:0]
) (
    input  req_t seed_i,
    output req_t first_o
);
  req_t [PORTS-1:0] reqs;

  always_comb begin
    reqs = '0;
    reqs[0] = seed_i;
  end

  assign first_o = reqs[0];
endmodule
)sv";

    auto h = convertModule(argv0, "zero_type_array", sv, "");
    expectContains(h, "_LAZY_COMB(reqs_comb, array<PORTS,req_t,true>)");
    expectContains(h, "cpphdl::pack_value<cpphdl::type_width<array<PORTS,req_t,true>>()>(0)");
    expectNotContains(h, "array<PORTS,req_t>>(0)");
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
    expectContains(h, "] = logic<32>(0);");
    expectNotContains(h, "std::remove_cvref_t<decltype(instr_comb[(unsigned)");
}

static void testZeroAggregateCastToIndexedPackedElementUsesStoredType(const char* argv0)
{
    const std::string sv = R"sv(
module zero_aggregate_cast_to_indexed_packed_element #(
    parameter int unsigned COUNT = 4,
    parameter int unsigned INDEX_WIDTH = $clog2(COUNT),
    parameter type index_t = logic [INDEX_WIDTH-1:0]
) (
    output logic out_o
);
  index_t [COUNT-1:0] indices;

  always_comb begin
    for (int unsigned i = 0; i < COUNT; i++) begin
      indices[i] = index_t'('0);
    end
  end

  assign out_o = indices[0][0];
endmodule
)sv";

    auto h = convertModule(argv0, "zero_aggregate_cast_to_indexed_packed_element", sv, "");
    expectContains(h, " = cpphdl::value_type_for_ref_t<decltype(indices_comb[");
    expectNotContains(h, "cpphdl::sv_assign_field(indices_comb[");
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
    expectContains(h, "child_i.valid_i_in = _ASSIGN_COMB(__port_bind_child_i_valid_i_in_comb_func());");
    expectNotContains(h, "child_i.valid_i_in = _ASSIGN(logic<1>(");
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
    expectContains(h, "child_i.in_i_in = _ASSIGN_COMB(__port_bind_child_i_in_i_in_comb_func());");
    expectNotContains(h, "child_i.in_i_in = _ASSIGN(logic<2>");
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

static void testMatchingChildOutputUsesDeferredAssignPort(const char* argv0)
{
    const std::string sv = R"sv(
module typed_forward_child #(
    parameter type T = logic [7:0]
) (
    output T data_o
);
  assign data_o = '0;
endmodule

module typed_forward_parent #(
    parameter type T = logic [7:0]
) (
    output T dst_data_o
);
  typed_forward_child #(
    .T(T)
  ) i_dst (
    .data_o(dst_data_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "typed_forward_parent", sv, "");
    expectContains(h, "_PORT(T) dst_data_o_out;");
    expectContains(h, "dst_data_o_out = _ASSIGN( i_dst.data_o_out() );");
    expectNotContains(h, "T& dst_data_o_out() { return i_dst.data_o_out(); }");
    expectNotContains(h, "dst_data_o_out = _ASSIGN_COMB( i_dst.data_o_out() );");
}

static void testForwardedAggregateOutputProjectsChildField(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [3:0] code;
    logic       valid;
} forwarded_status_t;

module forwarded_status_child(output forwarded_status_t status_o);
  assign status_o = '0;
endmodule

module forwarded_aggregate_output(output forwarded_status_t status_o);
  forwarded_status_child i_status(.status_o(status_o));
endmodule
)sv";

    const std::string traits =
        "forwarded_status_child\toutput_field.status_o.code\n"
        "forwarded_aggregate_output\toutput_field.status_o.code\n";
    auto h = convertModule(argv0, "forwarded_aggregate_output", sv, "", "", "", "", "", "", "", traits);
    expectContains(h, "_PORT(forwarded_status_t) status_o_out;");
    expectContains(h, "status_o_out = _ASSIGN( i_status.status_o_out() );");
    expectContains(h, "status_o_out__field_code");
    expectContains(h, "i_status.status_o_out__field_code()");
    expectNotContains(h, "forwarded_status_t& status_o_out() { return i_status.status_o_out(); }");
}

static void testRegisteredAggregateOutputProjectsCurrentField(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [3:0] code;
    logic       valid;
} registered_status_t;

module registered_aggregate_output(
    input  logic               clk_i,
    input  logic [3:0]         code_i,
    output registered_status_t status_o
);
  always_ff @(posedge clk_i) begin
    status_o.code <= code_i;
    status_o.valid <= 1'b1;
  end
endmodule
)sv";

    const std::string traits =
        "registered_aggregate_output\toutput_field.status_o.code\n"
        "registered_aggregate_output\toutput_field.status_o.valid\n";
    auto h = convertModule(argv0, "registered_aggregate_output", sv, "", "", "", "", "", "", "", traits);
    expectContains(h, "status_o_out__field_code");
    expectContains(h, "status_o_out__field_valid");
    expectContains(h, "status_o_code_comb = ((*this).status_o).code;");
    expectContains(h, "status_o_valid_comb = ((*this).status_o).valid;");
}

static void testRegisteredPackedStructArrayOutputProjectsEachField(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [3:0] code;
    logic       valid;
} registered_item_t;

module registered_array_output(
    input  logic                         clk_i,
    input  logic [3:0]                   code_i,
    output registered_item_t [1:0]       items_o
);
  always_ff @(posedge clk_i) begin
    for (int i = 0; i < 2; i++) begin
      items_o[i].code <= code_i + i;
      items_o[i].valid <= 1'b1;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "registered_array_output", sv, "", "", "", "", "", "", "",
                           "registered_array_output\toutput_field.items_o.code\n");
    expectContains(h, "items_o_out__field_code");
    expectContains(h, "items_o_code_comb");
    expectContains(h, "__cpphdl_projected_element_");
}

static void testStructArrayFieldOutputProjectionKeepsElementAssignments(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [7:0] value;
} projected_entry_t;

typedef struct packed {
    projected_entry_t [1:0] entries;
} projected_bundle_t;

module struct_array_field_output_projection(
    input  projected_entry_t [1:0] entries_i,
    output projected_bundle_t       bundle_o
);
  for (genvar i = 0; i < 2; i++) begin
    assign bundle_o.entries[i] = entries_i[i];
  end
endmodule
)sv";

    auto h = convertModule(
        argv0, "struct_array_field_output_projection", sv, "", "", "", "", "", "", "",
        "struct_array_field_output_projection\toutput_field.bundle_o.entries\n");
    expectContains(h, "bundle_o_out__field_entries");
    expectContains(h, "bundle_o_entries_comb[i] =");
}

static void testMalformedConfiguredOutputFieldPathIsIgnored(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [3:0] code;
} clean_status_t;

module malformed_output_field_path(output clean_status_t status_o);
  assign status_o = '0;
endmodule
)sv";

    const std::string traits =
        "malformed_output_field_path\toutput_field.status_o.code\n"
        "malformed_output_field_path\toutput_field.status_o.code, generated_text); return v; }.bad\n";
    auto h = convertModule(argv0, "malformed_output_field_path", sv, "", "", "", "", "", "", "", traits);
    expectContains(h, "status_o_out__field_code");
    expectNotContains(h, "generated_text");
    expectNotContains(h, "__field_bad");
}

static void testAggregateLambdaIsNotParsedAsChildOutputCall(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [7:0] rdata;
    logic       valid;
} lambda_rsp_t;

module aggregate_lambda_projection #(
    parameter type RSP_T = lambda_rsp_t
) (
    input  logic [7:0] data_i,
    output RSP_T       rsp_o
);
  always_comb begin
    rsp_o = '{rdata: data_i, valid: 1'b0};
  end
endmodule
)sv";

    auto h = convertModule(argv0, "aggregate_lambda_projection", sv, "", "", "", "", "", "", "",
                           "aggregate_lambda_projection\toutput_field.rsp_o.data_ptr\n");
    expectContains(h, "rsp_o_out__field_data_ptr");
    expectNotContains(h, "return v; }__field_data_ptr()");
    expectNotContains(h, "valid, __cpphdl_field_value); }(); return v; }__field_data_ptr()");
}

static void testUnusedAggregateOutputDoesNotAdvertiseEveryField(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [7:0] data;
    logic [3:0] tag;
    logic       valid;
} unused_output_t;

module unused_aggregate_output(output unused_output_t result_o);
  assign result_o = '0;
endmodule
)sv";

    auto h = convertModule(argv0, "unused_aggregate_output", sv, "");
    expectNotContains(h, "result_o_out__field_data");
    expectNotContains(h, "result_o_out__field_tag");
    expectNotContains(h, "result_o_out__field_valid");
}

static void testAggregatePartSelectZeroUsesWritableProxyAssignment(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [31:0] addr;
} part_select_req_t;

module aggregate_part_select_zero(
    input  logic [31:0] addr_i,
    output part_select_req_t req_o
);
  always_comb begin
    req_o = '0;
    req_o.addr = addr_i;
    req_o.addr[7:0] = '0;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "aggregate_part_select_zero", sv, "", "", "", "", "", "", "",
                           "aggregate_part_select_zero\toutput_field.req_o.addr\n");
    expectContains(h, "req_o_addr_comb.bits(");
    expectContains(h, ") = 0;");
    expectNotContains(h, "sv_assign_field(req_o_addr_comb.bits(");
    expectNotContains(h, "sv_assign_field(req_o_comb.addr.bits(");
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
    expectContains(h, "reg<array<(uint64_t)(((uint64_t)(ROWS)");
    expectContains(h, ",array<(uint64_t)(((uint64_t)(COLS)");
    expectNotContains(h, "reg<array<(uint64_t)(((uint64_t)(COLS)");
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
    expectContains(h, "cpphdl::sv_unsigned<(std::size_t)(32)>((uint64_t)(");
    expectNotContains(h, "cpphdl::sv_unsigned<(std::size_t)(1)>((uint64_t)(");
}

static void testUnsignedCastOfClog2ResultPreservesIntegerWidth(const char* argv0)
{
    const std::string sv = R"sv(
module unsigned_clog2_cast #(
    parameter int unsigned N = 10
) (
    output logic out_o
);
  function automatic integer unsigned idx_width(input integer unsigned num_idx);
    return (num_idx > 32'd1) ? unsigned'($clog2(num_idx)) : 32'd1;
  endfunction

  assign out_o = '0;
endmodule
)sv";

    auto h = convertModule(argv0, "unsigned_clog2_cast", sv, "");
    expectContains(h, "cpphdl::sv_unsigned<(std::size_t)(32)>");
    expectNotContains(h, "cpphdl::sv_unsigned<(std::size_t)(1)>");
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
    expectContains(h, "Cfg.W");
    expectContains(h, "data_o_out = _ASSIGN( child_i.data_o_out() );");
    expectNotContains(h, "data_o_out() { return child_i.data_o_out(); }");
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

static void testGenerateMetadataOnlyChildKeepsNamedPortBindings(const char* argv0)
{
    const std::string sv = R"sv(
module generate_metadata_child_parent #(
    parameter int unsigned NumIn = 11,
    parameter bit FairArb = 1'b1
) (
    input  logic [NumIn-1:0] req_i,
    output logic             empty_o
);
  logic [NumIn-1:0] upper_mask;
  logic [$clog2(NumIn+1)-1:0] upper_idx;
  logic upper_empty;

  assign upper_mask = req_i;
  if (NumIn != 1) begin : gen_multi
    if (FairArb) begin : gen_fair
      external_lzc #(
        .WIDTH(NumIn),
        .MODE(1'b0)
      ) i_lzc_upper (
        .in_i(upper_mask),
        .cnt_o(upper_idx),
        .empty_o(upper_empty)
      );
      assign empty_o = upper_empty;
    end
  end
endmodule
)sv";
    const std::string moduleParams =
        "external_lzc\tunsigned WIDTH = 2\tuint64_t MODE = 0\n";
    const std::string portTypes =
        "external_lzc.in_i\tinput:logic<WIDTH>\n"
        "external_lzc.cnt_o\toutput:logic<clog2(WIDTH + 1)>\n"
        "external_lzc.empty_o\toutput:logic<1>\n";

    auto h = convertModule(argv0, "generate_metadata_child_parent", sv, "", "", "", "",
                           moduleParams, portTypes);
    expectContains(h, "i_lzc_upper.in_i_in = _ASSIGN_COMB(");
    expectContains(h, "upper_mask_comb_func()");
    expectContains(h, "i_lzc_upper.cnt_o_out()");
    expectContains(h, "i_lzc_upper.empty_o_out()");
}

static void testGenerateLoopLocalparamsAreSubstitutedIntoCombMethods(const char* argv0)
{
    const std::string sv = R"sv(
module generate_loop_localparam_index #(
    parameter int unsigned N = 4,
    localparam int unsigned Levels = $clog2(N)
) (
    input  logic [N-2:0] source_i,
    output logic [N-2:0] nodes_o
);
  for (genvar level = 0; level < Levels; level++) begin : gen_levels
    for (genvar lane = 0; lane < 2**level; lane++) begin : gen_lanes
      localparam int unsigned Idx0 = 2**level - 1 + lane;
      localparam int unsigned Idx1 = 2**(level + 1) - 1 + lane*2;
      assign nodes_o[Idx0] = source_i[Idx0] | (Idx1 == 0);
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generate_loop_localparam_index", sv, "");
    expectContains(h, "for (unsigned i =");
    expectContains(h, "for (unsigned k =");
    expectNotContains(h, "Idx0");
    expectNotContains(h, "Idx1");
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
    expectContains(h, "u_sink.data_i_in = _ASSIGN(__port_bind_u_sink_data_i_in_");
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
    expectContains(h, "constexpr std::size_t __cpphdl_target_elem_bits = cpphdl::type_width<__cpphdl_target_elem_t>();");
    expectContains(h, "cpphdl::pack_value<__cpphdl_target_count * __cpphdl_target_elem_bits>(__cpphdl_src)");
    expectContains(h, "logic<__cpphdl_target_elem_bits>(__cpphdl_packed.bits((__cpphdl_i + 1) * __cpphdl_target_elem_bits - 1, __cpphdl_i * __cpphdl_target_elem_bits))");
    expectContains(h, "cpphdl::unpack_value<__cpphdl_target_elem_t>");
    expectContains(h, "u_mux.data_i_in = _ASSIGN(__port_bind_u_mux_data_i_in_");
    expectNotContains(h, "array<2,logic<32>>(refill_data_i_in())");
}

static void testPackedConcatToUnpackedStructArrayDoesNotUseDirectAssignment(const char* argv0)
{
    const std::string sv = R"sv(
package concat_array_source_pkg;
  typedef struct packed {
    logic [7:0] upper;
    logic [7:0] lower;
  } source_t;
  localparam source_t Source = '{upper: 8'h12, lower: 8'h34};
endpackage

package concat_array_target_pkg;
  typedef struct packed {
    logic [7:0] upper;
    logic [7:0] lower;
  } target_t;
endpackage

module concat_array_sink(
    input concat_array_target_pkg::target_t value_i [0:0]
);
endmodule

module packed_concat_to_unpacked_struct_array;
  concat_array_sink u_sink (
    .value_i({concat_array_source_pkg::Source})
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_concat_to_unpacked_struct_array", sv, "");
    expectContains(h, "__port_bind_u_sink_value_i_in_packed_to_array_comb_func");
    expectContains(h, "if constexpr (!cpphdl::is_logic_v<__cpphdl_src_t> && requires { __cpphdl_src_t::COUNT_VALUE; __cpphdl_src_t::ELEMENT_BITS; __cpphdl_src[0]; }");
    expectContains(h, "cpphdl::pack_value<__cpphdl_target_count * __cpphdl_target_elem_bits>(__cpphdl_src)");
    expectContains(h, "cpphdl::unpack_value<__cpphdl_target_elem_t>");
    expectNotContains(h, "if constexpr (!cpphdl::is_logic_v<__cpphdl_src_t> && __cpphdl_same_array_packing");
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
    expectContains(h, "u_sink.req_i_in = _ASSIGN(");
    expectContains(h, "reqs_comb_func()");
    expectNotContains(h, "__port_bind_u_sink_req_i_in_packed_array_comb_func");
}

static void testTypeParameterizedArrayPortForwardingDoesNotPack(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [3:0]  idx;
  logic [63:0] start_addr;
  logic [63:0] end_addr;
} rule_t;

module rule_array_sink #(
    parameter int unsigned N = 2,
    parameter type rule_type = rule_t
) (
    input rule_type [N-1:0] addr_map_i
);
endmodule

module rule_array_wrapper #(
    parameter int unsigned N = 2,
    parameter type rule_type = rule_t
) (
    input rule_type [N-1:0] addr_map_i
);
  rule_array_sink #(
    .N(N),
    .rule_type(rule_type)
  ) u_sink (
    .addr_map_i
  );
endmodule
)sv";

    auto h = convertModule(argv0, "rule_array_wrapper", sv, "");
    expectContains(h, "u_sink.addr_map_i_in = _ASSIGN(");
    expectContains(h, "addr_map_i_in()");
    expectNotContains(h, "__port_bind_u_sink_addr_map_i_in_comb = cpphdl::pack_value");
}

static void testPackedStructArrayToChildArrayChecksRepresentation(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [3:0]  idx;
  logic [63:0] start_addr;
  logic [63:0] end_addr;
} rule_t;

module packed_rule_array_sink #(
    parameter int unsigned N = 2
) (
    input  rule_t [N-1:0] addr_map_i,
    output logic [63:0]    start_o
);
  assign start_o = addr_map_i[0].start_addr;
endmodule

module packed_struct_array_to_child(
    output logic [63:0] start_o
);
  rule_t [1:0] addr_map;

  assign addr_map = '{
    '{idx: 4'd1, start_addr: 64'h1000, end_addr: 64'h2000},
    '{idx: 4'd0, start_addr: 64'h8000, end_addr: 64'h9000}
  };

  packed_rule_array_sink #(.N(2)) u_sink (
    .addr_map_i(addr_map),
    .start_o(start_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_array_to_child", sv, "");
    expectContains(h, "auto __cpphdl_assign = [&]<typename __cpphdl_src_arg_t>");
    expectContains(h, "__cpphdl_target_array_t::PACKED == __cpphdl_src_t::PACKED");
    expectContains(h, "requires { __cpphdl_src_t::COUNT_VALUE; __cpphdl_src_t::ELEMENT_BITS; __cpphdl_src[0]; }");
    expectContains(h, "rule_t,true> __cpphdl_projected_source_0 = {");
    expectContains(h, "cpphdl::unpack_value<rule_t>(cpphdl::pack_value<cpphdl::type_width<rule_t>()>");
    expectNotContains(h, "({ { .idx");
}

static void testSequentialStorageMemberIsNotProjectedAsHdlField(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       rdata;
  logic [3:0] wdata;
} trace_t;

module sequential_storage_member_not_field(
    input  logic   clk_i,
    input  logic   sample_i,
    input  logic [3:0] data_i,
    output trace_t trace_o
);
  always_ff @(posedge clk_i) begin
    if (sample_i) trace_o.rdata <= data_i[0];
  end
  assign trace_o.wdata = data_i;
endmodule
)sv";

    auto h = convertModule(argv0, "sequential_storage_member_not_field", sv, "");
    expectContains(h, "trace_o._next.rdata");
    expectNotContains(h, "std::declval<trace_t>()._next");
    expectNotContains(h, "trace_o__next_");
}

static void testProjectedStructArrayWholeAssignmentMapsElements(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic       valid;
  logic [3:0] data;
} item_t;

module projected_array_sink(
    input  item_t      items_i [1:0],
    output logic [1:0] valid_o
);
  assign valid_o[0] = items_i[0].valid;
  assign valid_o[1] = items_i[1].valid;
endmodule

module projected_array_whole_assignment(
    input  logic       clk_i,
    input  logic       valid_i,
    output logic [1:0] valid_o
);
  item_t items_q [1:0];
  item_t items_d [1:0];

  always_comb begin
    items_d = items_q;
    items_d[0].valid = valid_i;
  end
  always_ff @(posedge clk_i) items_q <= items_d;

  projected_array_sink u_sink (
    .items_i(items_d),
    .valid_o(valid_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "projected_array_whole_assignment", sv, "");
    expectContains(h, "__cpphdl_projected_source_0 = cpphdl::unpack_value<array<2,item_t>>");
    expectContains(h, ">(items_q));");
    expectContains(h, "items_d_valid_comb[__cpphdl_i_0_0] = __cpphdl_projected_element_0_0.valid;");
    expectNotContains(h, "(items_q).valid");
}

static void testProjectedStructArrayPositionalPatternSelectsMember(const char* argv0)
{
    const std::string sv = R"sv(
package positional_pattern_pkg;
  typedef struct packed {
    logic       valid;
    logic [3:0] data;
  } item_t;
endpackage

module positional_pattern_sink(
    input  positional_pattern_pkg::item_t items_i [1:0],
    output logic [1:0] valid_o
);
  assign valid_o[0] = items_i[0].valid;
  assign valid_o[1] = items_i[1].valid;
endmodule

module projected_array_positional_pattern(
    input logic       valid_i,
    input logic [3:0] data_i,
    output logic [1:0] valid_o
);
  positional_pattern_pkg::item_t items_d [1:0];

  always_comb begin
    items_d = '0;
    items_d[0] = '{valid_i, data_i};
  end

  positional_pattern_sink u_sink (
    .items_i(items_d),
    .valid_o(valid_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "projected_array_positional_pattern", sv, "");
    expectContains(h, "items_d_valid_comb");
    expectContains(h, "= {valid_i_in(), data_i_in()};");
    expectContains(h, "] = __cpphdl_projected_source_1.valid;");
    expectNotContains(h, "}).valid");
}

static void testDemandedUndrivenStructLeafGetsDefaultComb(const char* argv0)
{
    const std::string sv = R"sv(
package demanded_leaf_pkg;
  typedef struct packed {
    logic valid;
    logic way;
  } inv_t;
  typedef struct packed {
    inv_t       inv;
    logic [7:0] data;
    logic [3:0] user;
  } response_t;
endpackage

module demanded_leaf_sink(
    input demanded_leaf_pkg::response_t response_i,
    output logic way_o,
    output logic [3:0] user_o
);
  assign way_o = response_i.inv.way;
  assign user_o = response_i.user;
endmodule

module demanded_undriven_struct_leaf(
    input logic [7:0] data_i,
    output logic way_o,
    output logic [3:0] user_o
);
  demanded_leaf_pkg::response_t response;
  always_comb begin
    response.data = data_i;
    response.inv = '0;
  end
  demanded_leaf_sink u_sink (
    .response_i(response),
    .way_o(way_o),
    .user_o(user_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "demanded_undriven_struct_leaf", sv, "");
    expectContains(h, "response_inv_way_comb_func()");
    expectContains(h, "response_user_comb_func()");
    expectContains(h, "response_inv_way_comb = std::remove_cvref_t<decltype(std::declval<demanded_leaf_pkg::response_t>().inv.way)>{};");
    expectContains(h, "response_user_comb = logic<4>{};");
}

static void testStructInputFieldUsedOnlyByChildBindingIsProjected(const char* argv0)
{
    const std::string sv = R"sv(
package structural_field_pkg;
  typedef struct packed {
    logic [7:0] addr;
  } channel_t;
  typedef struct packed {
    channel_t aw;
  } request_t;
endpackage

module structural_field_leaf(
    input logic [7:0] addr_i
);
endmodule

module structural_field_child(
    input structural_field_pkg::request_t request_i [1:0]
);
  structural_field_leaf u_leaf (
    .addr_i(request_i[0].aw.addr)
  );
endmodule

module structural_field_parent(
    input structural_field_pkg::request_t request_i [1:0]
);
  structural_field_child u_child (
    .request_i(request_i)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "struct_input_field_child_binding", sv, "");
    expectContains(h, "request_i_in__field_aw_addr");
    expectContains(h, "__port_bind_u_leaf_addr_i_in_comb = logic<8>(request_i_in__field_aw_addr()[");
    expectContains(h, "u_leaf.addr_i_in = _ASSIGN_COMB(__port_bind_u_leaf_addr_i_in_comb_func());");
    expectContains(h, "u_child.request_i_in__field_aw_addr = _ASSIGN(request_i_in__field_aw_addr());");
    expectNotContains(h, "request_i_in())[");
}

static void testRegisteredAggregateChildInputBindsDemandedFields(const char* argv0)
{
    const std::string sv = R"sv(
package registered_projection_pkg;
  typedef struct packed {
    logic enable;
    logic mode;
  } options_t;
endpackage

module registered_projection_child(
    input  registered_projection_pkg::options_t options_i,
    output logic                                enabled_o
);
  assign enabled_o = options_i.enable && options_i.mode;
endmodule

module registered_projection_parent(
    input  logic                                clk_i,
    input  registered_projection_pkg::options_t options_i,
    output logic                                enabled_o
);
  registered_projection_pkg::options_t options_q;
  always_ff @(posedge clk_i)
    options_q <= options_i;

  registered_projection_child child_i (
    .options_i(options_q),
    .enabled_o(enabled_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "registered_aggregate_child_projection", sv, "");
    expectContains(h, "child_i.options_i_in = _ASSIGN(options_q);");
    expectContains(h, "child_i.options_i_in__field_enable = _ASSIGN_REG(options_q.enable);");
    expectContains(h, "child_i.options_i_in__field_mode = _ASSIGN_REG(options_q.mode);");
}

static void testPackedLogicArrayChildOutputUnpacksIntoStructElement(const char* argv0)
{
    const std::string sv = R"sv(
package packed_output_pkg;
  typedef struct packed {
    logic       valid;
    logic [6:0] tag;
  } entry_t;
endpackage

module packed_logic_array_source(
    output logic [0:0][7:0] data_o
);
  assign data_o = 8'hd5;
endmodule

module packed_logic_array_to_struct_element(
    output logic       valid_o,
    output logic [6:0] tag_o
);
  packed_output_pkg::entry_t entries [1:0];

  for (genvar i = 0; i < 2; i++) begin
    packed_logic_array_source u_source (
      .data_o(entries[i])
    );
  end

  assign valid_o = entries[0].valid;
  assign tag_o = entries[0].tag;
endmodule
)sv";

    auto h = convertModule(argv0, "packed_logic_array_to_struct_element", sv, "");
    expectContains(h, "cpphdl::unpack_value<packed_output_pkg::entry_t>");
    expectNotContains(h, "data_o_out()[0]).valid");
    expectNotContains(h, "data_o_out()[0]).tag");
}

static void testGeneratedChildStructArrayOutputProjectsNestedParentField(const char* argv0)
{
    const std::string sv = R"sv(
package nested_output_pkg;
  typedef struct packed {
    logic [3:0] id;
  } aw_t;
  typedef struct packed {
    aw_t  aw;
    logic valid;
  } req_t;
endpackage

module nested_output_row_source(
    output nested_output_pkg::req_t data_o [2]
);
  assign data_o = '0;
endmodule

module generated_child_nested_output(
    output logic [3:0] id_o
);
  nested_output_pkg::req_t req [2][2];

  for (genvar i = 0; i < 2; ++i) begin
    nested_output_row_source u_source (
      .data_o(req[i])
    );
  end

  assign id_o = req[0][0].aw.id;
endmodule
)sv";

    auto h = convertModule(argv0, "generated_child_nested_output", sv, "");
    expectContains(h, "cpphdl::unpack_value<nested_output_pkg::req_t>");
    expectContains(h, "__cpphdl_projected_element_");
    expectContains(h, ".aw.id;");
    expectNotContains(h, "data_o_out()).aw");
    expectNotContains(h, "data_o_out()).valid");
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
    expectContains(h, "_LAZY_COMB(tmp_comb, array<");
    expectContains(h, ",logic<32>,true>)");
    expectContains(h, "tmp_comb = in_i_in();");
    expectNotContains(h, "tmp_comb = cpphdl::pack_value<cpphdl::type_width<array<");
    expectContains(h, "out_o_comb = tmp_comb_func();");
    expectNotContains(h, "out_o_comb = cpphdl::pack_value<cpphdl::type_width<array<");
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
    expectContains(h, "mem_n_comb = ([&]() -> array<2,item_t>");
    expectContains(h, "__cpphdl_direct = mem_q;");
    expectContains(h, "mem_q._next = mem_n_comb_func();");
    expectNotContains(h, "mem_q._next = cpphdl::unpack_value<array<2,item_t>>");
    expectNotContains(h, "cpphdl::pack_value<cpphdl::type_width<array<2,item_t>>()(mem_n_comb_func())");
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
    expectContains(h, "using __cpphdl_target_t = array<8,logic<8>,true>;");
    expectContains(h, "auto&& __cpphdl_src = (words_comb_func());");
    expectContains(h, "__cpphdl_out = cpphdl::unpack_value<__cpphdl_target_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val));");
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
    expectContains(h, "using __cpphdl_target_t = array<8,logic<8>,true>;");
    expectContains(h, "auto&& __cpphdl_src = (words_comb_func());");
    expectContains(h, "__cpphdl_out = cpphdl::unpack_value<__cpphdl_target_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val));");
    expectNotContains(h, "bytes_comb = words_comb_func();");
}

static void testPackedArrayAssignFromUnpackedArrayChecksRepresentation(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_from_unpacked_array(
    input  logic [4:0] rd_i [2],
    output logic [1:0][4:0] rd_o
);
  always_comb begin
    rd_o = rd_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_from_unpacked_array", sv, "");
    expectContains(h, "using __cpphdl_target_t = array<2,logic<5>,true>;");
    expectContains(h, "if constexpr (requires { __cpphdl_src_t::PACKED; })");
    expectContains(h, "return __cpphdl_target_t::PACKED == __cpphdl_src_t::PACKED;");
    expectContains(h, "if constexpr (__cpphdl_same_array_packing && std::is_assignable_v<__cpphdl_target_t&, __cpphdl_src_t>)");
    expectContains(h, "__cpphdl_out = cpphdl::unpack_value<__cpphdl_target_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val));");
    expectNotContains(h, "rd_o_out = rd_i_in();");
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
    expectContains(h, "using __cpphdl_target_array_t = array<");
    expectContains(h, ",resp_t>;");
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
    expectContains(h, "using __cpphdl_target_array_t = array<");
    expectContains(h, ",resp_t>;");
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
    expectContains(h, ">(resp_comb_func());");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(cpphdl::pack_value<");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_comb = logic<");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(resp_comb_func());");
}

static void testPackedStructInputToLogicAliasPortUsesPackValue(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_logic_alias_sink #(
    parameter int unsigned DATA_WIDTH = 1
) (
    input  data_t       data_i,
    output logic [63:0] data_o
);
  typedef logic [DATA_WIDTH-1:0] data_t;
  assign data_o = data_i[64:1];
endmodule

module packed_struct_to_logic_alias_input(
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

  packed_struct_logic_alias_sink #(
    .DATA_WIDTH($bits(resp_t))
  ) u_sink (
    .data_i(resp),
    .data_o(raw_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_to_logic_alias_input", sv, "");
    expectContains(h, "__port_bind_u_sink_data_i_in_comb = cpphdl::pack_value<cpphdl::type_width<std::remove_cvref_t<decltype(u_sink.data_i_in())>>()");
    expectContains(h, ">(resp_comb_func());");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_comb = logic<");
    expectNotContains(h, "std::remove_cvref_t<decltype(u_sink.data_i_in())>(resp_comb_func())");
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
    expectContains(h, "u_sink.data_i_in = _ASSIGN(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_comb = std::remove_cvref_t<decltype(u_sink.data_i_in())>(resp);");
}

static void testProjectedDistinctStructInputUsesTargetTypedValueBinding(const char* argv0)
{
    const std::string sv = R"sv(
package projected_distinct_struct_pkg;
  typedef struct packed {
    logic [3:0] id;
    logic [1:0] resp;
  } source_b_t;
  typedef struct packed {
    logic [3:0] id;
    logic [1:0] resp;
  } target_b_t;
  typedef struct packed {
    source_b_t b;
    logic      b_valid;
  } response_t;
endpackage

module projected_distinct_struct_sink #(
    parameter type b_t = logic [5:0]
) (
    input  b_t         b_i,
    output logic [3:0] id_o
);
  assign id_o = b_i.id;
endmodule

module projected_distinct_struct_parent(
    input  projected_distinct_struct_pkg::response_t response_i,
    output logic [3:0] id_o
);
  projected_distinct_struct_sink #(
    .b_t(projected_distinct_struct_pkg::target_b_t)
  ) child_i (
    .b_i(response_i.b),
    .id_o(id_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "projected_distinct_struct_parent", sv, "");
    expectContains(h, "__port_bind_child_i_b_i_in_comb = cpphdl::unpack_value<std::remove_cvref_t<decltype(child_i.b_i_in())>>(cpphdl::pack_value<cpphdl::type_width<std::remove_cvref_t<decltype(child_i.b_i_in())>>()>(response_i_in__field_b()));");
    expectContains(h, "child_i.b_i_in = _ASSIGN(__port_bind_child_i_b_i_in_comb_func());");
    expectNotContains(h, "child_i.b_i_in = _ASSIGN(response_i_in__field_b());");
    expectNotContains(h, "child_i.b_i_in = _ASSIGN_COMB(response_i_in__field_b());");
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
    expectContains(h, "__port_bind_u_sink_data_i_in_comb = std::remove_cvref_t<decltype(u_sink.data_i_in())>(resp_data_comb_func());");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_COMB(__port_bind_u_sink_data_i_in_comb_func());");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(cpphdl::pack_value<");
    expectNotContains(h, "__port_bind_u_sink_data_i_in_packed_to_array_comb");
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
    expectContains(h, "u_sink.req_i_in = _ASSIGN(");
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
    expectContains(h, "child_i.req_i_in = _ASSIGN(__port_bind_child_i_req_i_in_comb_func());");
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

static void testWideInputAssignedToPartSelectDoesNotNarrowToUint64(const char* argv0)
{
    const std::string sv = R"sv(
module wide_input_part_select #(
    parameter int unsigned W = 128
) (
    input  logic [W-1:0]    data_i,
    output logic [W+63:0]   aligned_o
);
  always_comb begin
    aligned_o = '0;
    aligned_o[W-1:0] = data_i;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "wide_input_part_select", sv, "");
    expectContains(h, "logic<(uint64_t)(((uint64_t)(W) & ((1ull << 32) - 1ull)))>(data_i_in())");
    expectNotContains(h, "(uint64_t)(data_i_in())");
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
    expectContains(h, "auto __cpphdl_repeated_value = data_comb_func()");
    expectContains(h, "wentry_comb[__cpphdl_i] = __cpphdl_repeated_value;");
    expectNotContains(h, "__cpphdl_rep{}");
}

static void testPackedStructArrayReplicationKeepsPackedElementStride(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_array_replication #(
    parameter int unsigned WAYS = 4
) (
    input logic valid_i,
    input logic [21:0] tag_i,
    output logic [WAYS-1:0] valid_o
);
  typedef struct packed {
    logic valid;
    logic wback;
    logic dirty;
    logic fetch;
    logic [21:0] tag;
  } entry_t;

  entry_t entry;
  entry_t [WAYS-1:0] entries;

  always_comb begin
    entry = '{valid: valid_i, wback: 1'b0, dirty: 1'b0, fetch: 1'b0, tag: tag_i};
    entries = {WAYS{entry}};
    for (int unsigned i = 0; i < WAYS; i++) begin
      valid_o[i] = entries[i].valid;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_array_replication", sv, "");
    expectContains(h, "auto __cpphdl_repeated_value = __comb_local_entry;");
    expectContains(h, "[__cpphdl_i] = __cpphdl_repeated_value;");
    expectNotContains(h, "cpphdl::unpack_value<array<WAYS,entry_t>>");
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
    expectContains(h, "auto __cpphdl_repeated_value = (data_i_in())[(unsigned)");
    expectContains(h, "wentry_comb[__cpphdl_i] = __cpphdl_repeated_value;");
    expectNotContains(h, "logic<64> __cpphdl_rep");
    expectNotContains(h, "logic<type_width<access_data_t>()>(data_i_in())");
    expectNotContains(h, "* (uint64_t)((type_width<access_data_t>()))");
    expectNotContains(h, ">> (unsigned)");
}

static void testNestedPackedArrayReplicationUsesTargetElementWidth(const char* argv0)
{
    const std::string sv = R"sv(
module nested_packed_array_replication #(
    parameter int unsigned WAYS = 2,
    parameter int unsigned WORDS = 2,
    parameter type access_data_t = logic [63:0] [WORDS]
) (
    input access_data_t data_i,
    output logic [63:0] data_o
);
  typedef logic [63:0] word_t;
  typedef word_t [WAYS-1:0] ram_data_t;
  typedef ram_data_t [0:0] [WORDS-1:0] entry_t;
  entry_t wentry;

  always_comb begin
    wentry = '0;
    for (int unsigned j = 0; j < WORDS; j++) begin
      wentry[0][j] = {WAYS{data_i[j]}};
    end
    data_o = wentry[0][0][0];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "nested_packed_array_replication", sv, "");
    expectContains(h, "cpphdl::type_width<ram_data_t>() / (WAYS)");
    expectContains(h, "cpphdl::pack_value<(cpphdl::type_width<ram_data_t>() / (WAYS))>");
    expectNotContains(h, "pack_value<1>(data_i_in()");
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
    expectContains(h, "== (uint64_t)(cpphdl::pack_value<cpphdl::type_width<array<");
    expectNotContains(h, "== cpphdl::pack_value<cpphdl::type_width<array<");
}

static void testPackedStructArrayFieldReadsMaterializeElement(const char* argv0)
{
    const std::string sv = R"sv(
module packed_struct_array_field_read(
    input  logic [1:0] index_i,
    output logic       locked_o,
    output logic       mode_o
);
  external_pkg::cfg_t [3:0] cfg_q;

  always_comb begin
    locked_o = cfg_q[index_i].locked;
    mode_o = cfg_q[index_i].mode[1];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_struct_array_field_read", sv, "",
                           "external_pkg::cfg_t\t3\n");
    expectContains(h, "cpphdl::unpack_value<external_pkg::cfg_t>");
    expectNotContains(h, "].locked");
    expectNotContains(h, "].mode");
}

static void testUnpackedStructArrayFieldReadStaysAddressable(const char* argv0)
{
    const std::string sv = R"sv(
module unpacked_struct_array_field_read(
    input  logic [1:0] index_i,
    output logic       locked_o
);
  typedef struct packed {
    logic locked;
    logic [1:0] mode;
  } cfg_t;

  cfg_t [3:0] cfg_q;
  assign locked_o = cfg_q[index_i].locked;
endmodule
)sv";

    auto h = convertModule(argv0, "unpacked_struct_array_field_read", sv, "");
    expectContains(h, "].locked");
    expectNotContains(h, "cpphdl::unpack_value<cfg_t>");
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
    expectContains(h, "cat{logic<64>((uint64_t)(flip64");
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

static void testGenerateLocalChildOutputKeepsProducerIndex(const char* argv0)
{
    const std::string sv = R"sv(
module generated_value_source (
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  assign data_o = data_i;
endmodule

module generated_value_sink (
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  assign data_o = data_i;
endmodule

module generate_local_child_output #(
    parameter int unsigned N = 3
) (
    input  logic [N-1:0][7:0] data_i,
    output logic [N-1:0][7:0] data_o
);
  for (genvar i = 0; i < N; i++) begin : gen_path
    logic [7:0] local_data;

    generated_value_source u_source (
      .data_i(data_i[i]),
      .data_o(local_data)
    );
    generated_value_sink u_sink (
      .data_i(local_data),
      .data_o(data_o[i])
    );
  end
endmodule
)sv";

    auto h = convertModule(argv0, "generate_local_child_output", sv, "");
    expectContains(h, "u_sink[(unsigned)(uint64_t)((uint64_t)(i))].data_i_in = _ASSIGN_I(");
    expectContains(h, "u_source[(unsigned)(uint64_t)((uint64_t)(i))].data_o_out()");
    expectNotContains(h, "_LAZY_COMB(local_data_comb");
    expectNotContains(h, "local_data_comb_func()");
}

static void testNestedGenerateLocalCombKeepsInnerLoop(const char* argv0)
{
    const std::string sv = R"sv(
module nested_generate_local_comb #(
    parameter int unsigned N = 4,
    parameter int unsigned W = 8
) (
    input  logic [N-1:0][W-1:0] data_i,
    output logic [N-1:0]        match_o
);
  for (genvar i = 0; i < N; i++) begin : gen_outer
    logic [W-1:0] match_bits;
    for (genvar j = 0; j < W; j++) begin : gen_inner
      always_comb begin
        match_bits[j] = data_i[i][j];
      end
    end
    assign match_o[i] = &match_bits;
  end
endmodule
)sv";

    auto h = convertModule(argv0, "nested_generate_local_comb", sv, "");
    expectContains(h, "match_bits = {}; for (unsigned k =");
    expectContains(h, "match_bits[(unsigned)(uint64_t)((uint64_t)((uint64_t)(k)))]");
    expectContains(h, "} return match_bits; }())");
}

static void testNestedGenerateModuleArrayKeepsEveryDimension(const char* argv0)
{
    const std::string sv = R"sv(
module generated_grid_leaf (
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  assign data_o = data_i;
endmodule

module nested_generate_module_array #(
    parameter int unsigned ROWS = 1,
    parameter int unsigned COLS = 2
) (
    input  logic [ROWS-1:0][COLS-1:0][7:0] data_i,
    output logic [ROWS-1:0][COLS-1:0][7:0] data_o
);
  for (genvar row = 0; row < ROWS; ++row) begin : gen_row
    for (genvar col = 0; col < COLS; ++col) begin : gen_col
      generated_grid_leaf leaf_i (
        .data_i(data_i[row][col]),
        .data_o(data_o[row][col])
      );
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "nested_generate_module_array", sv, "");
    expectContains(h, "array<((uint64_t)(ROWS)");
    expectContains(h, "array<((uint64_t)(COLS)");
    expectContains(h, ",::generated_grid_leaf>> leaf_i;");
    expectContains(h, "leaf_i[(unsigned)(uint64_t)((uint64_t)(i))][(unsigned)(uint64_t)((uint64_t)(k))].data_i_in");
    expectContains(h, "leaf_i[(unsigned)(uint64_t)((uint64_t)(i))][(unsigned)(uint64_t)((uint64_t)(k))]._work(reset);");
    expectContains(h, "leaf_i[(unsigned)(uint64_t)((uint64_t)(i))][(unsigned)(uint64_t)((uint64_t)(k))]._assign();");
    expectNotContains(h, "leaf_i[(unsigned)(uint64_t)((uint64_t)(i))].data_i_in");
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

static void testNestedDefaultStructFieldProjectionUsesLeafZero(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [3:0] cause;
  logic       valid;
} nested_default_exception_t;
typedef struct packed {
  logic                      fetch_valid;
  nested_default_exception_t fetch_exception;
} nested_default_request_t;

module nested_default_struct_field_projection #(
    parameter type request_t = nested_default_request_t
) (
    input  logic set_i,
    output logic valid_o
);
  request_t req_o;

  always_comb begin
    req_o = '0;
    req_o.fetch_exception = '0;
    if (set_i)
      req_o.fetch_exception.valid = 1'b1;
  end

  assign valid_o = req_o.fetch_exception.valid;
endmodule
)sv";

    auto h = convertModule(argv0, "nested_default_struct_field_projection", sv, "");
    expectContains(h, "_LAZY_COMB(req_o_fetch_exception_valid_comb");
    expectNotContains(h, ">(0)).valid");
    expectNotContains(h, "({}).valid");
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

static void testExtractedStructFieldCombKeepsNestedUpdates(const char* argv0)
{
    const std::string sv = R"sv(
module nested_field_update_projection (
    input  logic        from_rtab_i,
    input  logic [7:0]  saved_tag_i,
    input  logic [7:0]  core_tag_i,
    input  logic [7:0]  off_i,
    output logic [15:0] addr_o
);
  typedef struct packed {
    logic [7:0] tag;
    logic [7:0] off;
  } req_t;

  typedef struct packed {
    logic from_rtab;
    req_t req;
  } req_x_t;

  req_x_t st1_req_q;
  req_x_t st1_req;
  logic [15:0] addr;

  assign st1_req_q = '{from_rtab: from_rtab_i, req: '{tag: saved_tag_i, off: off_i}};

  always_comb begin
    st1_req = st1_req_q;
    if (!st1_req_q.from_rtab) begin
      st1_req.req.tag = core_tag_i;
    end
  end

  assign addr = {st1_req.req.tag, st1_req.req.off};
  assign addr_o = addr;
endmodule
)sv";

    auto h = convertModule(argv0, "nested_field_update_projection", sv, "");
    expectContains(h, "st1_req_req_comb_func()");
    expectContains(h, "st1_req_req_comb = (");
    expectContains(h, ").req;");
    expectContains(h, "st1_req_req_comb.tag = core_tag_i_in();");
    expectContains(h, "st1_req_req_comb_func().tag");
    expectContains(h, "st1_req_req_comb_func().off");
    expectNotContains(h, "cat{logic<cpphdl::type_width<std::remove_cvref_t<decltype(st1_req_q.req.tag)>>()>((uint64_t)(st1_req_q.req.tag))");
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

static void testExtractedNestedLeafReadsSiblingThroughFieldComb(const char* argv0)
{
    const std::string sv = R"sv(
module extracted_nested_leaf_reads_sibling (
    input  logic [31:0] value_i,
    output logic [31:0] mask_o
);
  typedef struct packed {
    logic [31:0] rdata;
    logic [31:0] wmask;
  } csr_entry_t;

  typedef struct packed {
    csr_entry_t cycle;
  } csr_state_t;

  csr_state_t csr_o;

  always_comb begin
    csr_o = '0;
    csr_o.cycle.rdata = value_i;
    csr_o.cycle.wmask = csr_o.cycle.rdata != 32'h0 ? '1 : '0;
  end

  assign mask_o = csr_o.cycle.wmask;
endmodule
)sv";

    auto h = convertModule(argv0, "extracted_nested_leaf_reads_sibling", sv, "");
    expectContains(h, "_LAZY_COMB(csr_o_cycle_wmask_comb");
    expectContains(h, "csr_o_cycle_rdata_comb_func()");
    expectContains(h, "csr_o_cycle_wmask_comb =");
    expectNotContains(h, "csr_o_cycle_wmask_comb.cycle.rdata");
}

static void testExtractedFieldConditionCallsSiblingFieldComb(const char* argv0)
{
    const std::string sv = R"sv(
module extracted_field_condition_calls_sibling (
    input  logic       select_i,
    output logic [7:0] result_o
);
  typedef struct packed {
    logic [2:0] op;
    logic [7:0] result;
  } decoded_t;

  decoded_t decoded_o;

  always_comb begin
    decoded_o = '0;
    decoded_o.op = select_i ? 3'd4 : 3'd1;
    if (decoded_o.op == 3'd4) begin
      decoded_o.result = 8'h5a;
    end
  end

  assign result_o = decoded_o.result;
endmodule
)sv";

    auto h = convertModule(argv0, "extracted_field_condition_calls_sibling", sv, "");
    expectContains(h, "decoded_o_result_comb_func()");
    expectContains(h, "decoded_o_op_comb_func()");
    expectContains(h, "if (((uint64_t)(decoded_o_op_comb_func())");
    expectNotContains(h, "__hdlcpp_field_value");
    expectNotContains(h, "std::as_const(decoded_o_comb_func()).op");
}

static void testExtractedPackedArrayFieldKeepsElementIndex(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
  logic [31:0] rdata;
  logic [31:0] wdata;
  logic [31:0] wmask;
} indexed_csr_entry_t;

typedef struct packed {
  indexed_csr_entry_t [1:0] entries;
} indexed_csr_state_t;

module extracted_packed_array_field_keeps_index (
    input logic [31:0] value_i,
    output indexed_csr_state_t csr_o
);
  always_comb begin
    csr_o = '0;
    for (int i = 0; i < 2; i++) begin
      csr_o.entries[i].wdata = value_i + i;
      csr_o.entries[i].wmask = csr_o.entries[i].rdata != value_i ? '1 : '0;
    end
  end
endmodule
)sv";
    const std::string moduleTraits =
        "extracted_packed_array_field_keeps_index\toutput_field.csr_o.entries\n";

    auto h = convertModule(argv0, "extracted_packed_array_field_keeps_index", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "csr_o_entries_comb_func()");
    expectContains(h, "csr_o_entries_comb[i].wdata");
    expectContains(h, "csr_o_entries_comb[i].wmask");
    expectNotContains(h, "csr_o_entries_comb.wdata");
    expectNotContains(h, "csr_o_entries_comb.wmask");
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
    expectContains(h, "cpphdl::pack_value<cpphdl::type_width<req_t>()>(__comb_local_store_req))).tag");
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
    expectContains(h, "req_tag_comb = (sel_i_in() ? cpphdl::sv_cast<");
    expectContains(h, ">(store_req_tag_comb_func()) : cpphdl::sv_cast<");
    expectContains(h, ">(flush_req_tag_comb_func()))");
    expectNotContains(h, "store_req_comb_func()).tag");
    expectNotContains(h, "flush_req_comb_func()).tag");
}

static void testFieldProjectionFromIndexedZeroElementIsNotDefault(const char* argv0)
{
    const std::string sv = R"sv(
module field_projection_index_zero_not_default (
    input  logic       sel_i,
    input  logic       sel2_i,
    input  logic [1:0] trans0_i,
    input  logic [1:0] trans1_i,
    output logic [1:0] trans_o
);
  typedef struct packed {
    logic [1:0] trans_id;
    logic [7:0] data;
  } fu_t;

  fu_t data_i [2];
  fu_t one_cycle_data;

  assign data_i[0] = '{trans_id: trans0_i, data: 8'h11};
  assign data_i[1] = '{trans_id: trans1_i, data: 8'h22};

  always_comb begin
    one_cycle_data = sel_i ? data_i[0] : '0;
    if (sel2_i) begin
      one_cycle_data = data_i[1];
    end
  end

  assign trans_o = one_cycle_data.trans_id;
endmodule
)sv";

    auto h = convertModule(argv0, "field_projection_index_zero_not_default", sv, "");
    expectContains(h, "one_cycle_data_trans_id_comb_func()");
    expectContains(h, "data_i_comb_func()");
    expectContains(h, ".trans_id");
    expectContains(h, "if (sel2_i_in())");
    expectContains(h, "data_i_comb_func()[(unsigned)(uint64_t)(((uint64_t)(1)");
    expectNotContains(h, "? logic<2>{} : logic<2>{}");
    expectNotContains(h, "? std::remove_cvref_t<decltype(std::declval<fu_t>().trans_id)>{} : std::remove_cvref_t<decltype(std::declval<fu_t>().trans_id)>{}");
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

static void testExtractedFieldConditionalCastsWideConstantBranch(const char* argv0)
{
    const std::string sv = R"sv(
package conditional_constant_pkg;
  localparam logic [31:0] READ_VALUE = 32'h0000_2000;
endpackage

module extracted_field_conditional_constant (
    input  logic        select_i,
    output logic [31:0] value_o
);
  typedef struct packed {
    logic [31:0] value;
  } result_t;

  result_t result;
  always_comb begin
    result = '0;
    result.value = select_i ? conditional_constant_pkg::READ_VALUE : '0;
  end
  assign value_o = result.value;
endmodule
)sv";

    auto h = convertModule(argv0, "extracted_field_conditional_constant", sv, "");
    expectContains(h, "result_value_comb_func()");
    expectContains(h, "select_i_in() ? logic<32>(conditional_constant_pkg::READ_VALUE) : logic<32>(0)");
    expectContains(h, "conditional_constant_pkg::READ_VALUE");
    expectNotContains(h, "conditional_constant_pkg::READ_VALUE : logic<32>{}");
}

static void testAggregateCastDoesNotHideNestedFieldConditional(const char* argv0)
{
    const std::string sv = R"sv(
module aggregate_cast_nested_field_conditional (
    input  logic       choose_a_i,
    input  logic       choose_b_i,
    input  logic [7:0] tag_a_i,
    input  logic [7:0] tag_b_i,
    input  logic [7:0] tag_c_i,
    output logic [7:0] tag_o
);
  typedef struct packed {
    logic [7:0] tag;
  } req_t;

  logic choose_b;
  req_t req_a, req_b, req_c, req;

  assign choose_b = choose_b_i;
  assign req_a = '{tag: tag_a_i};
  assign req_b = '{tag: tag_b_i};
  assign req_c = '{tag: tag_c_i};
  assign req = choose_a_i ? req_a : choose_b ? req_b : req_c;
  assign tag_o = req.tag;
endmodule
)sv";

    auto h = convertModule(argv0, "aggregate_cast_nested_field_conditional", sv, "");
    expectContains(h, "choose_b_comb_func() ? cpphdl::sv_cast<logic<8>>(req_b_tag_comb_func())");
    expectContains(h, "cpphdl::sv_cast<logic<8>>(req_c_tag_comb_func())");
    expectNotContains(h, "choose_b_comb_func().tag");
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

static void testChildInputConnectedToParentRegUsesRegBinding(const char* argv0)
{
    const std::string sv = R"sv(
module reg_sink (
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  assign data_o = data_i;
endmodule

module child_input_parent_reg_binding (
    input  logic       clk_i,
    input  logic       rst_ni,
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  logic [7:0] q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) q <= '0;
    else q <= data_i;
  end

  reg_sink u_sink (
    .data_i(q),
    .data_o(data_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "child_input_parent_reg_binding", sv, "");
    expectContains(h, "u_sink.data_i_in = _ASSIGN_REG(q);");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN(q);");
    expectNotContains(h, "u_sink.data_i_in = _ASSIGN_COMB(q);");
}

static void testCombVectorChildInputUsesDirectCombBinding(const char* argv0)
{
    const std::string sv = R"sv(
module vector_sink #(parameter int unsigned N = 1) (
    input logic [N-1:0] valid_i
);
endmodule

module comb_vector_child_input_binding (
    input logic src_i
);
  logic [2:0] valid;

  assign valid[0] = src_i;
  assign valid[1] = 1'b0;
  assign valid[2] = 1'b0;

  vector_sink #(
    .N(3)
  ) u_sink (
    .valid_i(valid)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "comb_vector_child_input_binding", sv, "");
    expectContains(h, "u_sink.valid_i_in = _ASSIGN_COMB(valid_comb_func());");
    expectNotContains(h, "__port_bind_u_sink_valid_i_in_packed_to_array_comb_func()");
    expectNotContains(h, "__port_bind_u_sink_valid_i_in_comb_func()");
    expectNotContains(h, "_LAZY_COMB(__port_bind_u_sink_valid_i_in_comb");
}

static void testCombOutputStorageChildInputUsesCombGetter(const char* argv0)
{
    const std::string sv = R"sv(
module comb_output_sink (
    input  logic hit_i,
    output logic seen_o
);
  assign seen_o = hit_i;
endmodule

module comb_output_storage_child_input (
    input  logic source_i,
    output logic hit_o,
    output logic seen_o
);
  always_comb begin
    hit_o = source_i;
  end

  comb_output_sink u_sink (
    .hit_i(hit_o),
    .seen_o(seen_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "comb_output_storage_child_input", sv, "");
    expectContains(h, "u_sink.hit_i_in = _ASSIGN_COMB(hit_o_comb_func());");
    expectNotContains(h, "u_sink.hit_i_in = _ASSIGN(hit_o_comb);");
}

static void testChildOutputCombFeedingSequentialInputIsNoCache(const char* argv0)
{
    const std::string sv = R"sv(
module grant_source (
    input  logic [1:0] req_i,
    output logic [1:0] gnt_o
);
  assign gnt_o = req_i & 2'b01;
endmodule

module sample_sink (
    input logic clk_i,
    input logic rst_ni,
    input logic w_i,
    input logic [1:0] data_i
);
  logic [1:0] q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) q <= '0;
    else if (w_i) q <= data_i;
  end
endmodule

module child_output_feeds_seq_input (
    input logic clk_i,
    input logic rst_ni,
    input logic [1:0] req_i,
    input logic ready_i
);
  logic [1:0] gnt;
  logic       write;

  grant_source u_grant (
    .req_i(req_i),
    .gnt_o(gnt)
  );

  assign write = |(gnt & req_i) & ready_i;

  sample_sink u_sink (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .w_i(write),
    .data_i(gnt)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "child_output_feeds_seq_input", sv, "");
    expectContains(h, "u_sink.w_i_in = _ASSIGN_COMB(write_comb_func());");
    expectContains(h, "logic<1>& write_comb_func()");
    expectContains(h, "logic<2>& gnt_o_comb_func()");
    expectContains(h, "logic<2>& gnt_comb_func()");
    expectNotContains(h, "_LAZY_COMB(write_comb");
    expectNotContains(h, "_LAZY_COMB(gnt_o_comb");
    expectNotContains(h, "_LAZY_COMB(gnt_comb");
}

static void testCppKeywordLocalSignalIsEscaped(const char* argv0)
{
    const std::string sv = R"sv(
module cpp_keyword_local_signal (
    input  logic a_i,
    output logic y_o
);
  logic register;
  assign register = a_i;
  assign y_o = register;
endmodule
)sv";

    auto h = convertModule(argv0, "cpp_keyword_local_signal", sv, "");
    expectContains(h, "logic<1> register_;");
    expectContains(h, "register_ = _ASSIGN_COMB( a_i_in() );");
    expectContains(h, "y_o_out = _ASSIGN_COMB( y_o_comb_func() );");
    expectNotContains(h, "logic<1> register;");
    expectNotContains(h, "register()");
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
    expectContains(h, ",::leaf> u_leaf;");
    expectContains(h, "for (unsigned i = 0;");
    expectNotContains(h, "))));k++)");
    expectNotContains(h, "))));i++)");
}

static void testOneBitUnaryNotInConcatAvoidsLogicOperatorNot(const char* argv0)
{
    const std::string sv = R"sv(
module one_bit_unary_not_concat(
    input  logic a_i,
    input  logic [31:0] word_i,
    output logic [2:0] y_o
);
  assign y_o = {a_i, ~word_i[15], ~a_i};
endmodule
)sv";

    auto h = convertModule(argv0, "one_bit_unary_not_concat", sv, "");
    expectContains(h, "logic<1>(");
    expectNotContains(h, "~a_i_in()");
    expectNotContains(h, "~(a_i_in())");
    expectNotContains(h, "logic<32>(~");
    expectNotContains(h, "cat{logic<34>");
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

static void testFunctionOutputArgumentIsReference(const char* argv0)
{
    const std::string sv = R"sv(
module function_output_ref (
    input  logic       sel_i,
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
  logic [7:0] data_q [2];
  logic [7:0] data_d [2];

  function automatic void merge_byte(
      output logic [7:0] ret_data,
      input  logic [7:0] old_data,
      input  logic [7:0] new_data);
    ret_data = sel_i ? new_data : old_data;
  endfunction

  always_comb begin
    data_d = data_q;
    merge_byte(data_d[1], data_q[1], data_i);
  end

  assign data_o = data_d[1];
endmodule
)sv";

    auto h = convertModule(argv0, "function_output_ref", sv, "");
    expectContains(h, "void merge_byte(logic<8>& ret_data, logic<8> old_data, logic<8> new_data)");
    expectContains(h, "merge_byte(data_d_comb[");
    expectNotContains(h, "void merge_byte(logic<8> ret_data");
}

static void testSequentialDpiOutputArgumentUsesBlockingStorage(const char* argv0)
{
    const std::string sv = R"sv(
import "DPI-C" function void update_state(output bit state);

module sequential_dpi_output (
    input  logic clk_i,
    output logic state_o
);
  bit state_q;

  always_ff @(posedge clk_i)
    update_state(state_q);

  assign state_o = state_q;
endmodule
)sv";

    auto h = convertModule(argv0, "sequential_dpi_output", sv, "");
    expectContains(h, "logic<1> state_q;");
    expectContains(h, "update_state(state_q);");
    expectNotContains(h, "update_state(state_q._next);");
    expectNotContains(h, "state_q.strobe();");
}

static void testScopedPackedConstantConcatKeepsAggregateWidth(const char* argv0)
{
    const std::string sv = R"sv(
package scoped_packed_constant_pkg;
  typedef struct packed {
    logic [7:0] upper;
    logic [23:0] lower;
  } info_t;
  localparam info_t Info = '{upper: 8'h5a, lower: 24'h123456};
endpackage

module scoped_packed_constant_sink (
    input scoped_packed_constant_pkg::info_t info_i [1]
);
endmodule

module scoped_packed_constant_concat;
  scoped_packed_constant_sink i_sink (
      .info_i({scoped_packed_constant_pkg::Info})
  );
endmodule
)sv";

    auto h = convertModule(argv0, "scoped_packed_constant_concat", sv, "");
    expectContains(h, "cpphdl::pack_value<8 + 24>(scoped_packed_constant_pkg::Info)");
    expectContains(h, "requires { __cpphdl_src_t::COUNT_VALUE; __cpphdl_src_t::ELEMENT_BITS; __cpphdl_src[0]; }");
    expectNotContains(h, "requires { __cpphdl_src[0]; }");
    expectNotContains(h, "logic<1>((uint64_t)(scoped_packed_constant_pkg::Info))");
}

static void testExternalScopedConstantConcatUsesCppValueTypeWidth(const char* argv0)
{
    const std::string sv = R"sv(
module external_scoped_constant_concat (
    output logic [31:0] data_o
);
  assign data_o = {external_constant_pkg::Info};
endmodule
)sv";

    auto h = convertModule(argv0, "external_scoped_constant_concat", sv, "");
    expectContains(h, "cpphdl::type_width<std::remove_cvref_t<decltype(external_constant_pkg::Info)>>()");
    expectNotContains(h, "logic<1>((uint64_t)(external_constant_pkg::Info))");
}

static void testSingleOperandConcatsKeepCatTypeInArithmetic(const char* argv0)
{
    const std::string sv = R"sv(
package scoped_enum_arithmetic_pkg;
  typedef enum logic [7:0] {
    BaseAddress = 8'h04
  } address_e;
endpackage

module single_concat_enum_arithmetic (
    input  logic [7:0] address_i,
    output logic [3:0] index_o
);
  assign index_o = 4'({address_i} - {scoped_enum_arithmetic_pkg::BaseAddress});
endmodule
)sv";

    auto h = convertModule(argv0, "single_concat_enum_arithmetic", sv, "");
    expectContains(h, "cat{logic<8>");
    expectContains(h, "} - cat{logic<");
    expectNotContains(h, "} - logic<");
}

static void testFunctionOutputStructFieldsUpdateAggregateComb(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic [1:0][7:0] data;
    logic [1:0][1:0] be;
} update_entry_t;

module function_output_struct_fields (
    input  logic                    write_i,
    input  logic                    index_i,
    input  logic [1:0][7:0]         data_i,
    input  logic [1:0][1:0]         be_i,
    output update_entry_t           entries_d [1:0]
);
  update_entry_t entries_q [1:0];

  function automatic void update_entry(
      output logic [1:0][7:0] ret_data,
      output logic [1:0][1:0] ret_be,
      input  logic [1:0][7:0] old_data,
      input  logic [1:0][1:0] old_be,
      input  logic [1:0][7:0] new_data,
      input  logic [1:0][1:0] new_be);
    for (int unsigned word = 0; word < 2; word++) begin
      ret_data[word] = new_be[word][0] ? new_data[word] : old_data[word];
      ret_be[word] = old_be[word] | new_be[word];
    end
  endfunction

  always_comb begin
    automatic logic [1:0][1:0] old_be;
    entries_d = entries_q;
    old_be = entries_q[index_i].be;
    if (write_i) begin
      update_entry(entries_d[index_i].data, entries_d[index_i].be,
                   entries_q[index_i].data, old_be,
                   data_i, be_i);
    end
  end
endmodule
)sv";

    auto h = convertModule(
        argv0, "function_output_struct_fields", sv, "", "", "", "", "", "", "",
        "function_output_struct_fields\toutput_field.entries_d.data\n"
        "function_output_struct_fields\toutput_field.entries_d.be\n");
    expectContains(h, "update_entry(entries_d_comb[");
    expectContains(h, "].data, entries_d_comb[");
    expectContains(h, "].be,");
    expectNotContains(h, "update_entry(entries_d_data_comb_func()");
    expectNotContains(h, "entries_d_be_comb_func()[");
}

static void testTypedefArrayIndexedPartSelectKeepsSelectedWidth(const char* argv0)
{
    const std::string sv = R"sv(
module typedef_array_indexed_part_select (
    input  logic [1:0][3:0] byte_enable_i,
    input  logic [31:0]      new_data_i [1:0],
    input  logic [31:0]      old_data_i [1:0],
    output logic [31:0]      data_o [1:0]
);
  typedef logic [31:0] word_buf_t [1:0];

  function automatic void merge_data(
      output word_buf_t ret_data,
      input  word_buf_t new_data,
      input  word_buf_t old_data,
      input  logic [1:0][3:0] byte_enable);
    for (int unsigned word = 0; word < 2; word++) begin
      for (int unsigned byte_index = 0; byte_index < 4; byte_index++) begin
        ret_data[word][byte_index * 8 +: 8] = byte_enable[word][byte_index] ?
            new_data[word][byte_index * 8 +: 8] :
            old_data[word][byte_index * 8 +: 8];
      end
    end
  endfunction

  always_comb begin
    merge_data(data_o, new_data_i, old_data_i, byte_enable_i);
  end
endmodule
)sv";

    auto h = convertModule(argv0, "typedef_array_indexed_part_select", sv, "");
    expectContains(h, "void merge_data(word_buf_t& ret_data");
    expectContains(h, "cpphdl::sv_bits_runtime(new_data[");
    expectContains(h, "cpphdl::sv_bits_runtime(old_data[");
    expectNotContains(h, "logic<8>(logic<1>(cpphdl::sv_bits_runtime");
    expectNotContains(h, "logic<((uint64_t)(8) & ((1ull << 32) - 1ull))>(logic<1>(");
}

static void testParameterizedInterfaceArrayUsesUnpackedDimension(const char* argv0)
{
    const std::string sv = R"sv(
package dim_pkg;
  parameter int unsigned N = 4;
endpackage

interface simple_bus #(parameter int unsigned W = 8);
  logic [W-1:0] data;
endinterface

module iface_array_dim;
  simple_bus #(.W(8)) master[dim_pkg::N-1:0]();
endmodule
)sv";

    auto h = convertModule(argv0, "iface_array_dim", sv, "");
    expectContains(h, ",::simple_bus<8>> master;");
    expectContains(h, "dim_pkg::N");
    expectContains(h, "> master;");
    expectContains(h, "master[(unsigned)(uint64_t)((uint64_t)(i))]._work(reset);");
    expectContains(h, "master[(unsigned)(uint64_t)((uint64_t)(i))]._assign();");
    expectNotContains(h, ":N-1:0");
    expectNotContains(h, "dim_pkg >= ");
}

static void testDefaultParameterizedChildInstantiationUsesEmptyTemplateArgs(const char* argv0)
{
    const std::string sv = R"sv(
module default_param_parent (
    input  logic serial_i,
    output logic serial_o
);
  default_param_child i_child (
    .serial_i(serial_i),
    .serial_o(serial_o)
  );
endmodule

module default_param_child #(
    parameter int unsigned STAGES = 2
) (
    input  logic serial_i,
    output logic serial_o
);
  assign serial_o = serial_i;
endmodule
)sv";

    auto h = convertModule(argv0, "default_param_parent", sv, "");
    expectContains(h, "::default_param_child<> i_child;");
    expectNotContains(h, "::default_param_child i_child;");
}

static void testPositionalChildConnectionsPreserveOmittedClockOrdinal(const char* argv0)
{
    const std::string sv = R"sv(
module positional_child (
    input  logic clk_i,
    input  logic rst_i,
    input  logic data_i,
    output logic data_o
);
  always_ff @(posedge clk_i) begin
    if (!rst_i)
      data_o <= 1'b0;
    else
      data_o <= data_i;
  end
endmodule

module positional_parent (
    input  logic clk_i,
    input  logic rst_i,
    input  logic data_i,
    output logic data_o
);
  positional_child i_child (clk_i, rst_i, data_i, data_o);
endmodule
)sv";

    auto h = convertModule(argv0, "positional_parent", sv, "");
    expectNotContains(h, "i_child.clk_i_in");
    expectContains(h, "i_child.rst_i_in = _ASSIGN");
    expectContains(h, "rst_i_in()");
    expectContains(h, "i_child.data_i_in = _ASSIGN");
    expectContains(h, "data_i_in()");
    expectContains(h, "i_child.data_o_out()");
}

static void testPositionalChildConnectionsUseCrossFilePortMetadata(const char* argv0)
{
    const std::string sv = R"sv(
module positional_metadata_parent (
    input  logic clk_i,
    input  logic rst_i,
    input  logic data_i,
    output logic data_o
);
  external_positional_child i_child (clk_i, rst_i, data_i, data_o);
endmodule
)sv";
    const std::string ports =
        "external_positional_child.$port.0\tclk_i\n"
        "external_positional_child.$port.1\trst_i\n"
        "external_positional_child.$port.2\tdata_i\n"
        "external_positional_child.$port.3\tdata_o\n"
        "external_positional_child.rst_i\tinput:logic<1>\n"
        "external_positional_child.data_i\tinput:logic<1>\n"
        "external_positional_child.data_o\toutput:logic<1>\n";

    auto h = convertModule(argv0, "positional_metadata_parent", sv, "", "", "", "", "", ports);
    expectNotContains(h, "i_child.clk_i_in");
    expectContains(h, "i_child.rst_i_in = _ASSIGN");
    expectContains(h, "rst_i_in()");
    expectContains(h, "i_child.data_i_in = _ASSIGN");
    expectContains(h, "data_i_in()");
    expectContains(h, "i_child.data_o_out()");
}

static void testSequentialUnpackedArrayUsesDeferredMemoryUpdates(const char* argv0)
{
    const std::string sv = R"sv(
module sequential_unpacked_memory #(
    parameter int unsigned DEPTH = 64,
    parameter int unsigned WIDTH = 32
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    req_i,
    input  logic                    we_i,
    input  logic [$clog2(DEPTH)-1:0] addr_i,
    input  logic [WIDTH-1:0]        wdata_i,
    input  logic [WIDTH/8-1:0]      be_i,
    output logic [WIDTH-1:0]        rdata_o
);
  typedef logic [WIDTH-1:0] data_t;
  data_t mem [DEPTH-1:0];

  assign rdata_o = mem[addr_i];

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (rst_ni && req_i && we_i) begin
      for (int unsigned byte_idx = 0; byte_idx < WIDTH/8; byte_idx++) begin
        if (be_i[byte_idx]) begin
          mem[addr_i][byte_idx*8+:8] <= wdata_i[byte_idx*8+:8];
        end
      end
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "sequential_unpacked_memory", sv, "");
    expectContains(h, "memory<data_t,1,");
    expectContains(h, "> mem;");
    expectContains(h, "mem.apply();");
    expectContains(h, "auto __cpphdl_mem_row = mem.pending(");
    expectContains(h, "__cpphdl_mem_row.bits(");
    expectContains(h, "mem[(unsigned)");
    expectContains(h, "= __cpphdl_mem_row;");
    expectNotContains(h, "reg<array<DEPTH,data_t>> mem;");
    expectNotContains(h, "mem._next");
}

static void testSequentialUnpackedStructMemberUsesScalarMemoryElement(const char* argv0)
{
    const std::string sv = R"sv(
module sequential_unpacked_struct_memory #(
    parameter int unsigned DEPTH = 4
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic [$clog2(DEPTH)-1:0] index_i,
    output logic [7:0]            count_o
);
  typedef struct packed {
    logic       pending;
    logic [7:0] count;
  } entry_t;

  entry_t entries_q [DEPTH];

  assign count_o = entries_q[index_i].count;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int unsigned i = 0; i < DEPTH; i++) begin
        entries_q[i] <= '0;
        entries_q[i].count <= 8'd1;
      end
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "sequential_unpacked_struct_memory", sv, "");
    expectContains(h, "memory<entry_t,1,");
    expectContains(h, "entries_q[(unsigned)");
    expectContains(h, "][0].count");
    expectContains(h, "auto __cpphdl_mem_row = entries_q.pending(");
    expectContains(h, "__cpphdl_mem_row[0].count = logic<8>(1)");
    expectContains(h, "= __cpphdl_mem_row;");
    expectNotContains(h, "__cpphdl_mem_row.count");
    expectNotContains(h, "decltype(entries_q[index_i].count)");
}

static void testSequentialMemoryWholeArrayAssignmentSchedulesEachRow(const char* argv0)
{
    const std::string sv = R"sv(
module sequential_memory_whole_array #(
    parameter int unsigned DEPTH = 4
) (
    input logic clk_i,
    input logic rst_ni,
    input logic [7:0] data_i
);
  logic [7:0] state_q [DEPTH];
  logic [7:0] state_d [DEPTH];

  always_comb begin
    for (int unsigned i = 0; i < DEPTH; i++) begin
      state_d[i] = data_i + i;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int unsigned i = 0; i < DEPTH; i++) begin
        state_q[i] <= '0;
      end
    end else begin
      state_q <= state_d;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "sequential_memory_whole_array", sv, "");
    expectContains(h, "memory<logic<8>,1,");
    expectContains(h, "auto __cpphdl_mem_next = state_d_comb_func()");
    expectContains(h, "auto __cpphdl_mem_row = state_q.pending(__cpphdl_mem_i)");
    expectContains(h, "__cpphdl_mem_row[0] = __cpphdl_mem_next[__cpphdl_mem_i]");
    expectContains(h, "state_q[__cpphdl_mem_i] = __cpphdl_mem_row");
    expectNotContains(h, "state_q = state_d_comb_func()");
}

static void testPackedArrayMemberElementZeroUsesStoredValueType(const char* argv0)
{
    const std::string sv = R"sv(
module packed_array_member_zero #(
    parameter int unsigned PORTS = 2
) (
    input  logic        select_i,
    output logic [31:0] address_o
);
  typedef struct packed {
    logic [PORTS-1:0][31:0] addresses;
    logic [PORTS-1:0][1:0]  sizes;
  } result_t;

  result_t result;

  always_comb begin
    result = '0;
    for (int unsigned i = 0; i < PORTS; i++) begin
      result.addresses[i] = '0;
      result.sizes[i] = '0;
    end
    address_o = result.addresses[select_i];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "packed_array_member_zero", sv, "");
    expectContains(h, "result_comb.addresses[i] = logic<32>(0)");
    expectContains(h, "result_comb.sizes[i] = logic<2>(0)");
    expectNotContains(h, "std::remove_cvref_t<decltype(result_comb.addresses[i])>{}");
    expectNotContains(h, "std::remove_cvref_t<decltype(result_comb.sizes[i])>{}");
}

static void testTypeParameterPackedMemberElementZeroKeepsInferredWidth(const char* argv0)
{
    const std::string sv = R"sv(
package type_parameter_zero_pkg;
  typedef struct packed {
    logic [1:0][31:0] addresses;
    logic [1:0][1:0]  sizes;
  } default_result_t;
endpackage

module type_parameter_packed_member_zero #(
    parameter type RESULT_T = type_parameter_zero_pkg::default_result_t
) (
    output logic [31:0] address_o
);
  RESULT_T result;

  always_comb begin
    result = '0;
    for (int unsigned i = 0; i < 2; i++) begin
      result.addresses[i] = '0;
      result.sizes[i] = '0;
    end
    address_o = result.addresses[0];
  end
endmodule
)sv";

    auto h = convertModule(argv0, "type_parameter_packed_member_zero", sv, "");
    expectContains(h, "result_comb.addresses[i] = logic<cpphdl::type_width<cpphdl::value_type_for_ref_t<decltype(result_comb.addresses[i])>>()>(0)");
    expectContains(h, "result_comb.sizes[i] = logic<cpphdl::type_width<cpphdl::value_type_for_ref_t<decltype(result_comb.sizes[i])>>()>(0)");
    expectNotContains(h, "result_comb.addresses[i] = logic<1>(0)");
    expectNotContains(h, "result_comb.sizes[i] = logic<1>(0)");
}

static void testContinuousConcatOutputAssignSplitsIntoCombOutputs(const char* argv0)
{
    const std::string sv = R"sv(
module concat_output_assign (
    input  logic [7:0] bus_i,
    output logic [3:0] high_o,
    output logic [3:0] low_o
);
  assign {high_o, low_o} = bus_i;
endmodule
)sv";

    auto h = convertModule(argv0, "concat_output_assign", sv, "");
    expectNotContains(h, "{high_o, low_o} = _ASSIGN");
    expectContains(h, "_PORT(logic<4>) high_o_out = _ASSIGN_COMB( high_o_comb_func() );");
    expectContains(h, "_PORT(logic<4>) low_o_out = _ASSIGN_COMB( low_o_comb_func() );");
    expectContains(h, "low_o_comb = logic<4>(bus_i_in().bits");
    expectContains(h, "(uint64_t)(0)));");
    expectContains(h, "high_o_comb = logic<4>(bus_i_in().bits");
    expectContains(h, "(uint64_t)(((0) + (4)))));");
}

static void testMemberAccessRangeBoundsKeepMemberBeforeNumericCast(const char* argv0)
{
    const std::string sv = R"sv(
module member_range_bound #(
    parameter type cfg_t = struct packed {
      int unsigned N;
      int unsigned W;
    },
    parameter cfg_t Cfg = '{N: 2, W: 4}
) (
    input  logic [Cfg.N-1:0][Cfg.W-1:0] issue_pointer_i,
    output logic [7:0] issue_pointer_o
);
  assign issue_pointer_o = issue_pointer_i[Cfg.N-1:0];
endmodule
)sv";

    auto h = convertModule(argv0, "member_range_bound", sv, "");
    expectContains(h, "__cpphdl_slice_i <");
    expectContains(h, "array<");
    expectContains(h, "Cfg.N");
    expectContains(h, "std::remove_cvref_t<decltype(std::as_const(__cpphdl_slice_src)[0])>");
    expectNotContains(h, "(uint64_t)(Cfg).N");
    expectNotContains(h, "(uint64_t)((uint64_t)(Cfg)).N");
    expectNotContains(h, ")))); ++__cpphdl_slice_i");
}

static void testFunctionMemberAccessRangeBoundKeepsMemberInsideCall(const char* argv0)
{
    const std::string sv = R"sv(
module function_member_range_bound #(
    parameter type cfg_t = struct packed {
      int unsigned Depth;
      int unsigned W;
    },
    parameter cfg_t Cfg = '{Depth: 4, W: 8}
) (
    input  logic [Cfg.W-1:0] rows_i [Cfg.Depth],
    input  logic [Cfg.W-1:0] data_i,
    output logic [Cfg.W-1:0] data_o
);
  assign data_o = rows_i[data_i[$clog2(Cfg.Depth)-1:0]];
endmodule
)sv";

    auto h = convertModule(argv0, "function_member_range_bound", sv, "");
    expectContains(h, "Cfg.Depth");
    expectNotContains(h, "clog2(Cfg).Depth");
    expectNotContains(h, "sv_bits_runtime(");
    expectNotContains(h, "),(uint64_t)((uint64_t)(0))");
    expectNotContains(h, "clog2(Cfg))).Depth");
    expectLineParenthesesBalanced(h, "data_o_comb =");
    expectNotContains(h, "),(uint64_t)((uint64_t)(");
}

static void testMemberArithmeticArrayIndexKeepsOperatorsInsideIndex(const char* argv0)
{
    const std::string sv = R"sv(
module member_arithmetic_array_index #(
    parameter type cfg_t = struct packed { int unsigned N; },
    parameter cfg_t Cfg = '{N: 4}
) (
    input  logic [7:0] data_i [8],
    input  logic [1:0] offset_i,
    output logic [7:0] data_o
);
  assign data_o = data_i[Cfg.N + 1 - offset_i];
endmodule
)sv";

    auto h = convertModule(argv0, "member_arithmetic_array_index", sv, "");
    expectContains(h, "Cfg.N");
    expectContains(h, "Cfg.N");
    expectNotContains(h, "Cfg))).N");
    expectNotContains(h, "Cfg.N)))) +");
    expectLineParenthesesBalanced(h, "data_o_comb =");
}

static void testStructMemberPartSelectAsArrayIndexIsBalanced(const char* argv0)
{
    const std::string sv = R"sv(
package struct_member_part_select_pkg;
  typedef struct packed { int unsigned Depth; } cfg_t;
endpackage
module struct_member_part_select_index #(
    parameter struct_member_part_select_pkg::cfg_t Cfg = '{Depth: 4},
    parameter type request_t = struct packed { logic [31:0] vpn; }
) (
    input  logic [Cfg.Depth-1:0][15:0] table_i,
    input  request_t request_i,
    output logic [15:0] data_o
);
  assign data_o = table_i[request_i.vpn[$clog2(
      Cfg.Depth
  )-1:0]];
endmodule
)sv";

    auto h = convertModule(argv0, "struct_member_part_select_index", sv, "");
    expectContains(h, "Cfg.Depth");
    expectNotContains(h, "clog2(Cfg).Depth");
    expectNotContains(h, "clog2(Cfg))).Depth");
    expectNotContains(h, "(uint64_t)(Cfg).Depth");
    expectLineParenthesesBalanced(h, "data_o_comb =");
}

static void testStructFieldCombEnumComparisonDoesNotProjectEnum(const char* argv0)
{
    const std::string sv = R"sv(
package struct_field_enum_pkg;
  typedef enum logic [1:0] { NoCF, Branch, Jump } cf_t;
  typedef struct packed { logic valid; cf_t cf; } resolve_t;
  typedef struct packed { logic valid; cf_t cf; } update_t;
endpackage
module struct_field_comb_enum_compare(
    input struct_field_enum_pkg::resolve_t resolved_i,
    output struct_field_enum_pkg::update_t update_o
);
  import struct_field_enum_pkg::*;
  update_t update;
  assign update.valid = resolved_i.valid & (resolved_i.cf == Branch);
  assign update.cf = resolved_i.cf;
  assign update_o = update;
endmodule
)sv";

    auto h = convertModule(argv0, "struct_field_comb_enum_compare", sv, "");
    expectContains(h, "struct_field_enum_pkg::Branch");
    expectNotContains(h, "Branch.valid");
}

static void testNumericPackageFunctionKeepsAggregateArgument(const char* argv0)
{
    const std::string sv = R"sv(
package numeric_package_struct_arg_pkg;
  typedef struct packed { int unsigned width; logic enabled; } cfg_t;
  function automatic logic [63:0] make_mask(cfg_t cfg);
    return cfg.enabled ? ((64'(1) << cfg.width) - 1) : 64'(0);
  endfunction
endpackage
module numeric_package_struct_arg #(
    parameter numeric_package_struct_arg_pkg::cfg_t Cfg = '{width: 8, enabled: 1'b1}
) (output logic [63:0] mask_o);
  localparam logic [63:0] MASK = numeric_package_struct_arg_pkg::make_mask(Cfg);
  assign mask_o = MASK;
endmodule
)sv";

    auto h = convertModule(argv0, "numeric_package_struct_arg", sv, "");
    expectContains(h, "numeric_package_struct_arg_pkg::make_mask(Cfg)");
    expectNotContains(h, "make_mask((uint64_t)(Cfg))");
}

static void testPackageFunctionResultWidthInConcat(const char* argv0)
{
    const std::string sv = R"sv(
package package_function_concat_pkg;
  function automatic logic [31:0] make_jump(logic [4:0] rd, logic [20:0] imm);
    return {imm[20], imm[10:1], imm[11], imm[19:12], rd, 7'h6f};
  endfunction
endpackage

module package_function_concat (
    output logic [63:0] result_o
);
  localparam logic [11:0] TARGET = 12'h338;
  localparam logic [11:0] BASE = 12'h300;
  assign result_o = {32'b0,
                     package_function_concat_pkg::make_jump('0, 21'(TARGET) - 21'(BASE))};
endmodule
)sv";

    auto h = convertModule(argv0, "package_function_concat", sv, "");
    expectContains(h, "inline constexpr logic<32> make_jump");
    expectContains(h, "logic<cpphdl::type_width<std::remove_cvref_t<decltype(package_function_concat_pkg::make_jump");
    expectNotContains(h, "logic<21>((uint64_t)(package_function_concat_pkg::make_jump");
}

static void testChildClockPortAliasesAreNotBound(const char* argv0)
{
    const std::string sv = R"sv(
module clocked_child (
    input  logic HCLK,
    input  logic s_axi_aclk,
    input  logic dst_clk_i,
    input  logic d_i,
    output logic q_o
);
  assign q_o = d_i;
endmodule

module clock_alias_parent (
    input  logic clk_i,
    input  logic d_i,
    output logic q_o
);
  clocked_child u_child (
    .HCLK(clk_i),
    .s_axi_aclk(clk_i),
    .dst_clk_i(clk_i),
    .d_i(d_i),
    .q_o(q_o)
  );
endmodule
)sv";

    auto h = convertModule(argv0, "clock_alias_parent", sv, "");
    expectNotContains(h, "u_child.HCLK_in");
    expectNotContains(h, "u_child.s_axi_aclk_in");
    expectNotContains(h, "u_child.dst_clk_i_in");
    expectNotContains(h, "_ASSIGN(clk_i");
    expectContains(h, "u_child.d_i_in = _ASSIGN_COMB(d_i_in());");
}

static void testCurrentCombBlockReadBypassesEarlierGeneratedWireMap(const char* argv0)
{
    const std::string sv = R"SV(
module current_comb_block_read #(
  parameter int unsigned WIDTH = 64
) (
  input  logic             sel_i,
  input  logic [15:0]      data_i,
  output logic [3:0][31:0] words_o,
  output logic [15:0]      saved_o
);
  if (WIDTH == 32) begin
    always_comb begin
      words_o = '0;
      saved_o = '0;
    end
  end else if (WIDTH == 64) begin
    always_comb begin
      words_o[0] = '0;
      words_o[1] = '0;
      words_o[2] = '0;
      words_o[3] = {16'b0, data_i};
      saved_o = '0;
      if (sel_i) begin
        saved_o = words_o[3][15:0];
      end
    end
  end
endmodule
)SV";

    auto h = convertModule(argv0, "current_comb_block_read", sv, "");
    expectContains(h, "__comb_local_words_o");
    expectContains(h, "saved_o_comb = cpphdl::sv_bits<16>(__comb_local_words_o");
    expectNotContains(h, "saved_o_comb = cpphdl::sv_bits<16>(words_o_comb_func()");
}

static void testPackedStructArraySliceAssignmentCopiesElements(const char* argv0)
{
    const std::string sv = R"SV(
module packed_struct_array_slice #(
  parameter int unsigned DEPTH = 2
) (
  input logic push_i,
  input logic pop_i,
  input logic [31:0] data_i,
  output logic valid_o,
  output logic [31:0] data_o
);
  typedef struct packed {
    logic        valid;
    logic [31:0] data;
  } entry_t;

  entry_t [DEPTH-1:0] stack_q, stack_d;

  always_comb begin
    stack_d = stack_q;
    if (push_i) begin
      stack_d[DEPTH-1:1] = stack_q[DEPTH-2:0];
      stack_d[0].valid = 1'b1;
      stack_d[0].data = data_i;
    end
    if (pop_i) begin
      stack_d[DEPTH-2:0] = stack_q[DEPTH-1:1];
      stack_d[DEPTH-1] = '0;
    end
    valid_o = stack_d[0].valid;
    data_o = stack_d[0].data;
  end
endmodule
)SV";

    auto h = convertModule(argv0, "packed_struct_array_slice", sv, "");
    expectContains(h, "using __cpphdl_slice_array_t = std::remove_cvref_t<decltype(stack_d_comb)>");
    expectContains(h, "using __cpphdl_slice_elem_t = cpphdl::value_type_for_ref_t<");
    expectContains(h, "__cpphdl_slice_array_t::PACKED> __cpphdl_slice_rhs{}");
    expectContains(h, "__cpphdl_slice_rhs[__cpphdl_slice_i]");
    expectContains(h, ")[((uint64_t)(");
    expectNotContains(h, "stack_d_comb.bits(");
}

static void testPackedStructArrayElementPacksIntoVector(const char* argv0)
{
    const std::string sv = R"SV(
module packed_struct_array_element_to_vector (
  input  logic        select_i,
  input  logic [15:0] low_i,
  output logic [31:0] data_o
);
  typedef struct packed {
    logic [15:0] high;
    logic [15:0] low;
  } word_t;

  word_t [1:0] words;
  always_comb begin
    words = '0;
    words[0].high = 16'h1234;
    words[0].low = low_i;
    data_o = words[select_i];
  end
endmodule
)SV";

    auto h = convertModule(argv0, "packed_struct_array_element_to_vector", sv, "");
    expectContains(h, "data_o_comb = cpphdl::pack_value<cpphdl::type_width<logic<32>>()>(");
    expectContains(h, "words");
}

static void testPackedVectorArrayElementProxyPacksIntoVector(const char* argv0)
{
    const std::string sv = R"SV(
module packed_vector_array_element_to_vector (
  input  logic        select_i,
  input  logic [31:0] low_i,
  output logic [31:0] data_o
);
  logic [1:0][31:0] words;
  always_comb begin
    words = '0;
    words[0] = low_i;
    data_o = words[select_i];
  end
endmodule
)SV";

    auto h = convertModule(argv0, "packed_vector_array_element_to_vector", sv, "");
    expectContains(h, "data_o_comb = cpphdl::pack_value<cpphdl::type_width<logic<32>>()>(");
    expectContains(h, "words");
}

static void testSingleElementPackedArraySliceAssignmentIsNotDropped(const char* argv0)
{
    const std::string sv = R"SV(
module single_element_packed_array_slice #(
  parameter int unsigned COUNT = 1,
  parameter int unsigned ALIGNED_COUNT = 1
) (
  input  logic [COUNT-1:0][31:0] source_i,
  output logic [31:0] result_o
);
  logic [ALIGNED_COUNT-1:0][31:0] aligned;

  always_comb begin
    aligned = '0;
    aligned[COUNT-1:0] = source_i;
  end

  assign result_o = aligned[0];
endmodule
)SV";

    auto h = convertModule(argv0, "single_element_packed_array_slice", sv, "");
    expectContains(h, "aligned_comb_func");
    expectContains(h, "__cpphdl_slice_rhs");
    expectContains(h, "source_i_in()");
}

static void testPackedStructRegisterShiftPacksCurrentValue(const char* argv0)
{
    const std::string sv = R"SV(
module packed_struct_register_shift (
  input logic clk_i,
  input logic rst_ni,
  input logic serial_i,
  output logic [7:0] value_o
);
  typedef struct packed {
    logic [3:0] high;
    logic [3:0] low;
  } word_t;
  word_t word_q, word_d;

  always_comb begin
    word_d = {serial_i, 7'(word_q >> 1)};
    value_o = word_q;
  end
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) word_q <= '0;
    else word_q <= word_d;
  end
endmodule
)SV";

    auto h = convertModule(argv0, "packed_struct_register_shift", sv, "");
    expectContains(h, "cpphdl::pack_value<4 + 4>(word_q)");
    expectNotContains(h, "word_q >>");
}

static void testNestedArrayElementConditionalKeepsElementType(const char* argv0)
{
    const std::string sv = R"SV(
module nested_array_element_conditional #(
  parameter int unsigned WIDTH = 64,
  parameter int unsigned PORTS = 2,
  parameter int unsigned LATENCY = 2,
  parameter type data_t = logic [WIDTH-1:0]
) (
  input logic [PORTS-1:0] req_i,
  input data_t current_i [PORTS-1:0],
  input data_t held_i [PORTS-1:0],
  output data_t data_o [PORTS-1:0]
);
  data_t pipeline [PORTS-1:0][LATENCY-1:0];

  always_comb begin
    for (int unsigned i = 0; i < PORTS; i++) begin
      pipeline[i][LATENCY-1] = req_i[i] ? current_i[i] : held_i[i];
      data_o[i] = pipeline[i][LATENCY-1];
    end
  end
endmodule
)SV";

    auto h = convertModule(argv0, "nested_array_element_conditional", sv, "");
    expectContains(h, "cpphdl::sv_cast<data_t>(");
    expectNotContains(h, "? logic<1>(cpphdl::sv_cast<data_t>");
    expectNotContains(h, ": logic<1>(cpphdl::sv_cast<data_t>");
}

static void testModulePackedArrayInitializerIsPreserved(const char* argv0)
{
    const std::string sv = R"SV(
module module_packed_array_initializer (
  input logic index_i,
  output logic [63:0] data_o
);
  const logic [1:0][63:0] mem = {64'h0123456789abcdef, 64'hfedcba9876543210};
  assign data_o = mem[index_i];
endmodule
)SV";

    auto h = convertModule(argv0, "module_packed_array_initializer", sv, "");
    expectContains(h, "array<2,logic<64>,true> mem = cat{");
    expectContains(h, "logic<64>(0x0123456789abcdef)");
    expectContains(h, "logic<64>(0xfedcba9876543210)");
    expectNotContains(h, "array<2,logic<64>,true> mem;");
}

static void testIndexedStructInputUsesIndependentProjectedFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [3:0] id;
    logic [31:0] addr;
} aw_t;
typedef struct packed {
    aw_t aw;
    logic valid;
} req_t;

module projected_leaf(input req_t req_i, output logic [3:0] id_o);
    assign id_o = req_i.aw.id;
endmodule

module indexed_struct_projection(input req_t source_i [2], output logic [3:0] id_o);
    req_t req [2];
    for (genvar i = 0; i < 2; ++i) begin
        assign req[i].aw.id = source_i[i].aw.id;
        assign req[i].aw.addr = source_i[i].aw.addr;
        assign req[i].valid = source_i[i].valid;
    end
    projected_leaf child_i(.req_i(req[0]), .id_o(id_o));
endmodule
)";
    auto h = convertModule(argv0, "indexed_struct_projection", sv, "");
    expectContains(h, ">) req_i_in__field_aw_id;");
    expectContains(h, "req_i_in__field_aw_id()");
    expectContains(h, "array<2,std::remove_cvref_t<decltype(std::declval<req_t>().aw.id)>> req_aw_id_comb;");
    expectContains(h, "req_aw_id_comb[i] = source_i_in__field_aw_id()[");
    expectContains(h, "child_i.req_i_in__field_aw_id = _ASSIGN(req_aw_id_comb_func()[");
    expectNotContains(h, "req_comb_func()[0].aw.id");
}

static void testProjectedIndexedChildInputIgnoresDecltypeGetter(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [31:0] operand_a;
    logic [31:0] operand_b;
} projected_operand_t;

module projected_operand_leaf(input projected_operand_t data_i, output logic [31:0] result_o);
    assign result_o = data_i.operand_a + data_i.operand_b;
endmodule

module projected_indexed_parent(
    input projected_operand_t source_i [2],
    output logic [31:0] result_o
);
    projected_operand_leaf child_i(.data_i(source_i[0]), .result_o(result_o));
endmodule
)";
    auto h = convertModule(argv0, "projected_indexed_parent", sv, "");
    expectContains(h, "source_i_in__field_operand_a()[");
    expectContains(h, "source_i_in__field_operand_b()[");
    assert(countContains(h, "child_i.data_i_in__field_operand_a =") == 1);
    assert(countContains(h, "child_i.data_i_in__field_operand_b =") == 1);
    expectNotContains(h, "_ASSIGN_COMB(source_i_in__field_operand_a());");
    expectNotContains(h, "_ASSIGN_COMB(source_i_in__field_operand_b());");
}

static void testExternalParameterizedChildBindsIndexedInputProjections(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [31:0] operand_a;
    logic [31:0] operand_b;
    logic [7:0] operation;
} projected_fu_data_t;

module external_parameterized_projection_parent #(
    parameter type item_t = logic
) (
    input item_t source_i [1]
);
    external_parameterized_projection_leaf #(
        .item_t(item_t)
    ) child_i (
        .data_i(source_i[0])
    );
endmodule
)";
    const std::string moduleParams =
        "external_parameterized_projection_leaf\ttypename item_t = logic<1>\n";
    const std::string portTypes =
        "external_parameterized_projection_leaf.data_i\tinput:item_t\n";
    const std::string moduleTraits =
        "external_parameterized_projection_leaf\tinput_field.data_i.operand_a"
        "\tinput_field.data_i.operand_b\tinput_field.data_i.operation\n";
    auto h = convertModule(argv0, "external_parameterized_indexed_projection", sv, "", "", "", "",
                           moduleParams, portTypes, "", moduleTraits);
    expectContains(h, "source_i_in__field_operand_a");
    expectContains(h, "source_i_in__field_operand_b");
    expectContains(h, "source_i_in__field_operation");
    expectContains(h, "child_i.data_i_in__field_operand_a = _ASSIGN_COMB(source_i_in__field_operand_a()[");
    expectContains(h, "child_i.data_i_in__field_operand_b = _ASSIGN_COMB(source_i_in__field_operand_b()[");
    expectContains(h, "child_i.data_i_in__field_operation = _ASSIGN_COMB(source_i_in__field_operation()[");
}

static void testParameterizedStructFieldAdaptsToPackedArrayChildPort(const char* argv0)
{
    const std::string sv = R"(
module projected_packed_array_leaf(
    input  logic [1:0][31:0] data_i,
    output logic [31:0]       data_o
);
    assign data_o = data_i[0];
endmodule

module parameterized_field_to_packed_array #(
    parameter type response_t = logic
) (
    input  response_t    response_i,
    output logic [31:0]  data_o
);
    if (1) begin : generated_child
        projected_packed_array_leaf child_i(
            .data_i(response_i.data),
            .data_o(data_o)
        );
    end
endmodule
)";
    auto h = convertModule(argv0, "parameterized_field_to_packed_array", sv, "");
    expectContains(h, "response_i_in__field_data");
    expectContains(h, "__port_bind_child_i_data_i_in");
    expectContains(h, "child_i.data_i_in = _ASSIGN(__port_bind_child_i_data_i_in");
    expectNotContains(h, "child_i.data_i_in = _ASSIGN_COMB(response_i_in__field_data());");
}

static void testCommaSeparatedContinuousAggregateUsesOwnRhsForField(const char* argv0)
{
    const std::string sv = R"(
typedef logic [0:0] comma_index_t;
typedef struct packed {
    logic [2:0] address_offset;
    logic [7:0] operation;
} comma_entry_t;

module comma_continuous_aggregate(
    input  logic         use_rid_i,
    input  logic         rid_i,
    input  comma_entry_t entries_i [2],
    output logic [2:0]   offset_o,
    output logic [7:0]   operation_o
);
    comma_index_t selected_index;
    comma_entry_t selected_data;
    assign selected_index = use_rid_i ? comma_index_t'(rid_i) : 1'b0,
           selected_data = entries_i[selected_index];
    assign offset_o = selected_data.address_offset;
    assign operation_o = selected_data.operation;
endmodule
)";
    auto h = convertModule(argv0, "comma_continuous_aggregate", sv, "");
    expectContains(h, "selected_data_address_offset_comb");
    expectContains(h, "selected_data_operation_comb");
    expectNotContains(h, "selected_index_comb_func().address_offset");
    expectNotContains(h, "selected_index_comb_func().operation");
    expectNotContains(h, "selected_index_address_offset_comb_func()");
    expectNotContains(h, "selected_index_operation_comb_func()");
    expectContains(h, "(entries_i_in())[");
    expectContains(h, "selected_index_comb_func()");
}

static void testGeneratedExternalChildUsesProjectedStructFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [3:0] id;
    logic [31:0] addr;
} aw_t;
typedef struct packed {
    aw_t aw;
    logic valid;
} req_t;

module generated_external_projection(input req_t source_i [2]);
    req_t req [2][2];
    for (genvar i = 0; i < 2; ++i) begin
        for (genvar j = 0; j < 2; ++j) begin
            assign req[i][j] = source_i[j];
        end
    end
    for (genvar i = 0; i < 2; ++i) begin
        external_projected_leaf child_i(.req_i(req[i]));
    end
endmodule
)";
    const std::string portTypes =
        "external_projected_leaf.req_i\tinput:array<req_t,2>\n";
    const std::string moduleTraits =
        "external_projected_leaf\tinput_field.req_i.aw.id\n";
    auto h = convertModule(argv0, "generated_external_projection", sv, "", "", "", "", "",
                           portTypes, "", moduleTraits);
    expectContains(h, "child_i[(unsigned)(uint64_t)((uint64_t)(i))].req_i_in__field_aw_id = _ASSIGN_I(");
    expectContains(h, "req_aw_id_comb_func()[(unsigned)");
    expectContains(h, "__cpphdl_projected_source_0.aw.id;");
    expectNotContains(h, "(source_i_in()).aw.id");
}

static void testProjectedChildFieldKeepsSourceMemberPrefix(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [3:0] id;
    logic [31:0] addr;
} aw_t;
typedef struct packed {
    aw_t aw;
    logic valid;
} req_t;

module projected_source_member(input req_t source_i [2]);
    for (genvar i = 0; i < 2; ++i) begin
        external_aw_leaf child_i(.aw_i(source_i[i].aw));
    end
endmodule
)";
    const std::string portTypes =
        "external_aw_leaf.aw_i\tinput:aw_t\n";
    const std::string moduleTraits =
        "external_aw_leaf\tinput_field.aw_i.id\n";
    auto h = convertModule(argv0, "projected_source_member", sv, "", "", "", "", "",
                           portTypes, "", moduleTraits);
    expectContains(h, "source_i_in__field_aw_id");
    expectContains(h, ".aw_i_in__field_id = _ASSIGN_I(source_i_in__field_aw_id()[");
    expectNotContains(h, "source_i_in__field_id");
}

static void testProjectedChildFieldFromDependentTypeGetsCombMethod(const char* argv0)
{
    const std::string sv = R"(
module dependent_projection_parent #(
    parameter type req_t = logic
) (
    input req_t source_i
);
    req_t req;
    assign req = source_i;
    external_dependent_leaf child_i(.req_i(req));
endmodule
)";
    const std::string portTypes =
        "external_dependent_leaf.req_i\tinput:req_t\n";
    const std::string moduleTraits =
        "external_dependent_leaf\tinput_field.req_i.instr\n";
    auto h = convertModule(argv0, "dependent_projection_parent", sv, "", "", "", "", "",
                           portTypes, "", moduleTraits);
    expectContains(h, "std::declval<__cpphdl_projected_t>().instr");
    expectContains(h, "if constexpr (requires(req_t __cpphdl_projected_value)");
    expectContains(h, "req_instr_comb;");
    expectContains(h, "req_instr_comb_func()");
    expectContains(h, "child_i.req_i_in__field_instr = _ASSIGN_COMB(req_instr_comb_func());");
}

static void testOverridableStructDefaultKeepsOutputFieldTypeDependent(const char* argv0)
{
    const std::string sv = R"(
module dependent_struct_default_output #(
    parameter type item_t = struct packed {
        logic       csr;
        logic [7:0] payload;
    }
) (
    input  item_t source_i,
    output item_t result_o
);
    assign result_o = source_i;
endmodule
)";
    const std::string moduleTraits =
        "dependent_struct_default_output\toutput_field.result_o.csr\n";
    auto h = convertModule(argv0, "dependent_struct_default_output", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "_LAZY_COMB(result_o_csr_comb, std::remove_cvref_t<decltype(");
    expectContains(h, "std::declval<__cpphdl_projected_t>().csr");
    expectNotContains(h, "_LAZY_COMB(result_o_csr_comb, logic<1>)");
}

static void testProjectedNestedFieldThroughDependentMemberIsGuarded(const char* argv0)
{
    const std::string sv = R"(
module nested_dependent_projection_parent #(
    parameter type select_t = logic [3:0]
) (
    input select_t select_i
);
    typedef struct packed {
        logic [7:0] payload;
        select_t select;
    } item_t;
    item_t item;
    always_comb begin
        item = '0;
        item.select = select_i;
    end
    external_dependent_leaf child_i(.req_i(item.select));
endmodule
)";
    const std::string portTypes =
        "external_dependent_leaf.req_i\tinput:req_t\n";
    const std::string moduleTraits =
        "external_dependent_leaf\tinput_field.req_i.header\n";
    auto h = convertModule(argv0, "nested_dependent_projection_parent", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectContains(h, "std::declval<__cpphdl_projected_t>().select.header");
    expectContains(h, "if constexpr (requires(item_t __cpphdl_projected_value) { __cpphdl_projected_value.select.header; })");
    expectContains(h, "child_i.req_i_in__field_header = _ASSIGN_COMB(item_select_header_comb_func());");
}

static void testProjectedFieldAfterWholeStructCastStaysOutsideCast(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic valid;
    logic [3:0] id;
} cast_entry_t;

module projected_field_after_struct_cast(
    input  logic [4:0] raw_i [2],
    output logic       valid_o
);
    cast_entry_t entry [2];
    always_comb begin
        for (int i = 0; i < 2; ++i)
            entry[i] = cast_entry_t'(raw_i[i]);
    end
    assign valid_o = entry[0].valid;
endmodule
)";
    auto h = convertModule(argv0, "projected_field_after_struct_cast", sv, "");
    expectContains(h, "cpphdl::sv_cast<cast_entry_t>");
    expectContains(h, "__cpphdl_projected_source_0.valid");
    expectNotContains(h, "raw_i_in()[i].valid");
    expectNotContains(h, "(uint64_t)(0).valid");
}

static void testStructCastDoesNotProjectFieldsFromScalarInput(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [3:0] address;
    logic       valid;
} scalar_cast_result_t;

module scalar_input_struct_cast(
    input  logic [4:0] csr_addr_i,
    output logic [3:0] address_o,
    output logic       valid_o
);
    scalar_cast_result_t csr_addr;
    assign csr_addr = scalar_cast_result_t'(csr_addr_i);
    assign address_o = csr_addr.address;
    assign valid_o = csr_addr.valid;
endmodule
)";
    auto h = convertModule(argv0, "scalar_input_struct_cast", sv, "");
    expectContains(h, "csr_addr_i_in()");
    expectNotContains(h, "csr_addr_i_in__field_address");
    expectNotContains(h, "csr_addr_i_in__field_valid");
    expectNotContains(h, "std::declval<logic<5>>().address");
}

static void testWholeAggregateInputForwardingUsesProjectedFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic       ar_valid;
    logic       r_ready;
    logic [7:0] payload;
} forwarded_req_t;

module whole_aggregate_input_forwarding (
    input  forwarded_req_t req_i,
    input  logic           bypass_i,
    output logic           ar_valid_o
);
    forwarded_req_t forwarded_req;

    always_comb begin
        forwarded_req = '0;
        if (bypass_i)
            forwarded_req = req_i;
        if (req_i.r_ready)
            forwarded_req.ar_valid = 1'b1;
    end

    assign ar_valid_o = forwarded_req.ar_valid;
endmodule
)";
    auto h = convertModule(argv0, "whole_aggregate_input_forwarding", sv, "");
    expectContains(h, "req_i_in__field_ar_valid");
    expectContains(h, "req_i_in__field_r_ready()");
    expectContains(h, "forwarded_req_ar_valid_comb = req_i_in__field_ar_valid();");
    expectNotContains(h, "cpphdl::type_width<forwarded_req_t>()>(req_i_in())");
    expectNotContains(h, "req_i_in().r_ready");
}

static void testChildAggregateOutputUsesProjectedFieldPort(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic       aw_valid;
    logic       r_ready;
    logic [7:0] payload;
} projected_output_req_t;

module projected_output_child (
    input  projected_output_req_t req_i,
    input  logic                  select_i,
    output projected_output_req_t req_o [2]
);
    always_comb begin
        req_o = '0;
        req_o[0].aw_valid = select_i && req_i.aw_valid;
        req_o[0].r_ready = req_i.r_ready;
        req_o[1].payload = req_i.payload;
    end
endmodule

module child_aggregate_output_projection (
    input  projected_output_req_t req_i,
    input  logic                  select_i,
    output logic                  valid_o
);
    projected_output_req_t child_req [2];
    projected_output_child child_i (
        .req_i(req_i),
        .select_i(select_i),
        .req_o(child_req)
    );
    assign valid_o = child_req[0].aw_valid;
endmodule
)";
    auto h = convertModule(argv0, "child_aggregate_output_projection", sv, "");
    expectContains(h, "req_o_out__field_aw_valid = _ASSIGN_COMB( req_o_aw_valid_comb_func() )");
    expectContains(h, "child_i.req_o_out__field_aw_valid()");
    expectNotContains(h, "__cpphdl_projected_source_0 = cpphdl::unpack_value<array<2,projected_output_req_t>>(cpphdl::pack_value<cpphdl::type_width<array<2,projected_output_req_t>>()>(child_i.req_o_out())");
}

static void testProjectedNestedOutputRebasesDescendantMembers(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [3:0] code;
    logic [7:0] detail;
} projected_status_t;
typedef struct packed {
    logic              valid;
    projected_status_t status;
} projected_result_t;

module projected_nested_output_rebase(
    input  projected_status_t status_i,
    input  logic              replace_i,
    output projected_result_t result_o
);
    always_comb begin
        result_o = '0;
        result_o.status = status_i;
        if (replace_i) begin
            result_o.status.code = 4'ha;
            result_o.status.detail = 8'h5c;
        end
    end
endmodule
)";
    auto h = convertModule(argv0, "projected_nested_output_rebase", sv, "");
    expectContains(h, "result_o_status_comb.code = logic<4>(0xa);");
    expectContains(h, "result_o_status_comb.detail = logic<8>(0x5c);");
    expectNotContains(h, "result_o_status_comb.status.");
}

static void testProjectedNestedVectorPreservesPartSelect(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [21:0] ppn;
    logic        valid;
} projected_page_t;

module projected_nested_vector_part_select(
    input  logic [21:0] source_i,
    input  logic [8:0]  patch_i,
    output projected_page_t page_o
);
    always_comb begin
        page_o = '0;
        page_o.ppn = source_i;
        page_o.ppn[8:0] = patch_i;
    end
endmodule
)";
    auto h = convertModule(argv0, "projected_nested_vector_part_select", sv, "");
    expectContains(h, "page_o_ppn_comb.bits(");
    expectNotContains(h, "page_o_ppn_comb.bits =");
}

static void testCrossFileAggregateOutputProjectionUsesTraits(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic       aw_valid;
    logic       r_ready;
} external_output_req_t;

module cross_file_aggregate_output_projection(output logic valid_o);
    external_output_req_t child_req [2];
    external_output_child child_i(.req_o(child_req));
    assign valid_o = child_req[0].aw_valid;
endmodule
)";
    const std::string portTypes =
        "external_output_child.req_o\toutput:array<2,external_output_req_t>\n";
    const std::string moduleTraits =
        "external_output_child\toutput_field.req_o.aw_valid\n";
    auto h = convertModule(argv0, "cross_file_aggregate_output_projection", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectContains(h, "child_i.req_o_out__field_aw_valid()");
    expectNotContains(h, "type_width<array<2,external_output_req_t>>()>(child_i.req_o_out())");
}

static void testIndexedChildAggregateOutputProjectionUsesTraits(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic aw_valid;
    logic r_ready;
} indexed_output_req_t;

module indexed_child_aggregate_output_projection(output logic [1:0] valid_o);
    indexed_output_req_t child_req [2];
    for (genvar i = 0; i < 2; ++i) begin
        indexed_external_output_child child_i(.req_o(child_req[i]));
        assign valid_o[i] = child_req[i].aw_valid;
    end
endmodule
)";
    const std::string portTypes =
        "indexed_external_output_child.req_o\toutput:indexed_output_req_t\n";
    const std::string moduleTraits =
        "indexed_external_output_child\toutput_field.req_o.aw_valid\n";
    auto h = convertModule(argv0, "indexed_child_aggregate_output_projection", sv,
                           "", "", "", "", "", portTypes, "", moduleTraits);
    expectContains(h, "].req_o_out__field_aw_valid()");
    expectNotContains(h, "].req_o_out()).aw_valid");
}

static void testChildAggregateProjectionSurvivesArrayTranspose(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic       ar_valid;
    logic [7:0] addr;
} transposed_req_t;

module transposed_output_child(
    input  transposed_req_t req_i,
    output transposed_req_t req_o [2]
);
    always_comb begin
        req_o = '0;
        req_o[1] = req_i;
    end
endmodule

module child_aggregate_projection_array_transpose(
    input  transposed_req_t req_i [2],
    output logic            valid_o
);
    transposed_req_t slave_reqs [2][2];
    transposed_req_t master_reqs [2][2];

    for (genvar i = 0; i < 2; ++i) begin
        transposed_output_child child_i(
            .req_i(req_i[i]),
            .req_o(slave_reqs[i])
        );
    end

    always_comb begin
        for (int i = 0; i < 2; ++i)
            for (int k = 0; k < 2; ++k)
                master_reqs[k][i] = slave_reqs[i][k];
    end

    assign valid_o = master_reqs[1][0].ar_valid;
endmodule
)";
    const std::string moduleTraits =
        "transposed_output_child\toutput_field.req_o.ar_valid\n";
    auto h = convertModule(argv0, "child_aggregate_projection_array_transpose", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "].req_o_out__field_ar_valid()");
    expectNotContains(h,
        "cpphdl::type_width<transposed_req_t>()>(slave_reqs_comb_func()");
}

static void testIndexedChildNestedProjectionRequiresExactLeaf(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [3:0] user;
    logic [7:0] data;
} nested_channel_t;
typedef struct packed {
    nested_channel_t channel;
    logic            valid;
} nested_array_resp_t;

module nested_array_child(output nested_array_resp_t resp_o [2]);
    assign resp_o = '0;
endmodule

module indexed_child_nested_projection(output logic [3:0] user_o);
    nested_array_resp_t child_resp [2][2];
    nested_array_resp_t transposed [2][2];
    for (genvar i = 0; i < 2; ++i)
        nested_array_child child_i(.resp_o(child_resp[i]));
    always_comb begin
        for (int i = 0; i < 2; ++i)
            for (int k = 0; k < 2; ++k)
                transposed[i][k] = child_resp[k][i];
    end
    assign user_o = transposed[1][0].channel.user;
endmodule
)";
    const std::string ancestorTraits =
        "nested_array_child\toutput_field.resp_o.channel\n";
    auto ancestor = convertModule(argv0, "indexed_child_nested_projection_ancestor", sv,
                                  "", "", "", "", "", "", "", ancestorTraits);
    expectNotContains(ancestor, "resp_o_out__field_channel()[");

    const std::string exactTraits =
        "nested_array_child\toutput_field.resp_o.channel\n"
        "nested_array_child\toutput_field.resp_o.channel.user\n";
    auto exact = convertModule(argv0, "indexed_child_nested_projection_exact", sv,
                               "", "", "", "", "", "", "", exactTraits);
    expectContains(exact, "].resp_o_out__field_channel_user()[");
}

static void testWholeArrayChildNestedProjectionRequiresExactLeaf(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [7:0] tval;
    logic       valid;
} whole_array_exception_t;
typedef struct packed {
    whole_array_exception_t ex;
    logic [15:0]             instruction;
} whole_array_entry_t;

module whole_array_nested_projection(
    output logic [1:0][7:0] tval_o
);
    whole_array_entry_t [1:0] entries;
    whole_array_external_child child_i(.entries_o(entries));
    assign tval_o = entries.ex.tval;
endmodule
)";
    const std::string portTypes =
        "whole_array_external_child.entries_o\toutput:array<whole_array_entry_t,2>\n";
    const std::string ancestorTraits =
        "whole_array_external_child\toutput_field.entries_o.ex\n";
    auto ancestor = convertModule(argv0, "whole_array_nested_projection_ancestor", sv,
                                  "", "", "", "", "", portTypes, "", ancestorTraits);
    expectNotContains(ancestor, "entries_o_out__field_ex().tval");

    const std::string exactTraits =
        "whole_array_external_child\toutput_field.entries_o.ex\n"
        "whole_array_external_child\toutput_field.entries_o.ex.tval\n";
    auto exact = convertModule(argv0, "whole_array_nested_projection_exact", sv,
                               "", "", "", "", "", portTypes, "", exactTraits);
    expectContains(exact, "entries_o_out__field_ex_tval()");
}

static void testProjectedBypassReadsIndexedInputField(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [15:0] addr;
} projected_bypass_source_chan_t;
typedef struct packed {
    logic [15:0] addr;
} projected_bypass_target_chan_t;
typedef struct packed {
    projected_bypass_source_chan_t ar;
    logic                          ar_valid;
} projected_bypass_source_req_t;
typedef struct packed {
    projected_bypass_target_chan_t ar;
    logic                          ar_valid;
} projected_bypass_target_req_t;

module projected_bypass #(
    parameter type source_req_t = logic,
    parameter type target_req_t = logic,
    parameter int unsigned Count = 1
) (
    input  source_req_t req_i [Count],
    output target_req_t req_o
);
    if (Count == 1)
        assign req_o = req_i[0];
    else
        assign req_o = '0;
endmodule
)";
    const std::string moduleTraits =
        "projected_bypass\tinput_field.req_i.ar\n"
        "projected_bypass\toutput_field.req_o.ar\n";
    auto h = convertModule(argv0, "projected_bypass", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "req_o_ar_comb = cpphdl::unpack_value<");
    expectContains(h, "req_i_in__field_ar()[");
}

static void testNestedArrayProjectionIncludesAncestorFieldAssignment(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [63:0] data;
    logic        last;
} ancestor_projection_channel_t;
typedef struct packed {
    ancestor_projection_channel_t r;
    logic                         r_valid;
} ancestor_projection_response_t;

module ancestor_array_projection (
    input  ancestor_projection_response_t source_i,
    output ancestor_projection_response_t responses_o [2]
);
    always_comb begin
        responses_o = '0;
        for (int i = 0; i < 2; ++i)
            responses_o[i].r = source_i.r;
    end
endmodule
)";
    const std::string moduleTraits =
        "ancestor_array_projection\toutput_field.responses_o.r.data\n";
    auto h = convertModule(argv0, "ancestor_array_projection", sv,
                           "", "", "", "", "", "", "", moduleTraits);
    expectContains(h, "responses_o_r_data_comb[i] =");
    expectContains(h, ").data;");
}

static void testBitSelectComplementKeepsOneBitWidth(const char* argv0)
{
    const std::string sv = R"(
typedef struct packed {
    logic [15:0] addr;
} bit_select_arb_data_t;

module bit_select_arb (
    input  logic [1:0]         req_i,
    input  bit_select_arb_data_t data_i [2],
    output bit_select_arb_data_t data_o
);
    logic sel;
    assign sel = ~req_i[0] | (req_i[1] & 1'b0);
    assign data_o = sel ? data_i[1] : data_i[0];
endmodule
)";
    auto h = convertModule(argv0, "bit_select_arb", sv, "");
    expectContains(h, "sel_comb = logic<1>(((~");
    expectNotContains(h, "logic<2>(logic<1>");
}

static void testGeneratedContinuousTreeOrdersDependenciesBeforeParent(const char* argv0)
{
    const std::string sv = R"(
module generated_continuous_tree (
    input  logic [7:0] data_i [2],
    output logic [7:0] data_o
);
    logic [7:0] nodes [3];
    for (genvar level = 0; level < 2; ++level) begin
        if (level == 1) begin
            for (genvar item = 0; item < 2; ++item)
                assign nodes[(1 << level) - 1 + item] = data_i[item];
        end else begin
            assign nodes[(1 << level) - 1] = nodes[(1 << (level + 1)) - 1];
        end
    end
    assign data_o = nodes[0];
endmodule
)";
    auto h = convertModule(argv0, "generated_continuous_tree", sv, "");
    expectContains(h, "for (unsigned i = (unsigned)(");
    expectContains(h, "i-- > 0;) {");
}

static void testGeneratedPackedTreeOrdersDependenciesBeforeParent(const char* argv0)
{
    const std::string sv = R"(
module generated_packed_tree #(
    parameter int unsigned NumInputs = 4,
    parameter int unsigned NumLevels = $clog2(NumInputs)
) (
    input  logic [NumInputs-1:0] requests_i,
    output logic                 request_o
);
    logic [2**NumLevels-2:0] request_nodes;

    for (genvar level = 0; level < NumLevels; ++level) begin
        for (genvar item = 0; item < 2**level; ++item) begin
            localparam int unsigned Current = 2**level-1+item;
            localparam int unsigned Child = 2**(level+1)-1+item*2;
            if (level == NumLevels-1)
                assign request_nodes[Current] = requests_i[item*2] | requests_i[item*2+1];
            else
                assign request_nodes[Current] = request_nodes[Child] | request_nodes[Child+1];
        end
    end

    assign request_o = request_nodes[0];
endmodule
)";
    auto h = convertModule(argv0, "generated_packed_tree", sv, "");
    expectContains(h, "request_nodes_comb_func()");
    expectContains(h, "i-- > 0;) {");
}

static void testStructEnumFieldConditionalUsesFieldWidth(const char* argv0)
{
    const std::string sv = R"sv(
package enum_field_conditional_pkg;
  typedef enum logic [3:0] {
    UNIT_NONE = 4'd0,
    UNIT_LOAD = 4'd1,
    UNIT_MUL  = 4'd2,
    UNIT_ALU  = 4'd3
  } unit_t;
endpackage

module struct_enum_field_conditional #(
    parameter type result_t = logic
) (
    input  logic select_i,
    output logic [3:0] unit_o
);
  import enum_field_conditional_pkg::*;

  result_t result;
  always_comb begin
    result = '0;
    result.unit = select_i ? UNIT_MUL : UNIT_ALU;
  end
  assign unit_o = result.unit;
endmodule
)sv";

    auto h = convertModule(argv0, "struct_enum_field_conditional", sv, "");
    expectContains(h, "select_i_in() ? cpphdl::sv_cast<cpphdl::value_type_for_ref_t<decltype(result_comb.unit)>>(enum_field_conditional_pkg::UNIT_MUL)");
    expectContains(h, ": cpphdl::sv_cast<cpphdl::value_type_for_ref_t<decltype(result_comb.unit)>>(enum_field_conditional_pkg::UNIT_ALU)");
    expectNotContains(h, "logic<1>(enum_field_conditional_pkg::UNIT_MUL)");
    expectNotContains(h, "logic<1>(enum_field_conditional_pkg::UNIT_ALU)");
}

static void testContinuousStructFieldUsesFinalProceduralFieldValue(const char* argv0)
{
    const std::string sv = R"sv(
typedef struct packed {
    logic       valid;
    logic [4:0] cause;
} ordered_exception_t;

typedef struct packed {
    logic               valid;
    ordered_exception_t ex;
} ordered_result_t;

module continuous_struct_field_order(
    input  logic            request_i,
    output ordered_result_t result_o
);
  assign result_o.valid = result_o.ex.valid;

  always_comb begin
    result_o = '0;
    if (request_i) begin
      result_o.ex.valid = 1'b1;
      result_o.ex.cause = 5'd24;
    end
  end
endmodule
)sv";

    auto h = convertModule(argv0, "continuous_struct_field_order", sv, "");
    auto method = h.substr(h.find("ordered_result_t& result_o_comb_func()"));
    method.resize(method.find("return result_o_comb;") + std::string("return result_o_comb;").size());
    expectBefore(method,
                 "result_o_comb.ex.valid = logic<1>(0b1);",
                 "result_o_comb.valid = result_o_comb.ex.valid;");
}

int main(int argc, char** argv)
{
    testContinuousStructFieldUsesFinalProceduralFieldValue(argv[0]);
    testModuleDependencyMetadataUsesParsedInstances(argv[0]);
    testConfiguredGenerateSelectsZeroDelayPassThrough(argv[0]);
    testDelayedNetDeclarationInitializerIsContinuousComb(argv[0]);
    testExternalChildOutputDemandIsPublished(argv[0]);
    testConfiguredOutputFieldDemandMaterializesDependentPort(argv[0]);
    testTypeParameterizedChildOutputDemandUsesFieldPort(argv[0]);
    testMaterializedTypeParameterizedIndexedChildOutputUsesFieldPort(argv[0]);
    testGenericChildProjectionRejectsMissingConcreteSourceField(argv[0]);
    testConfiguredNestedInputProjectionDeclaresChildPort(argv[0]);
    testCrossFileScalarAliasRejectsNestedProjectedField(argv[0]);
    testConfiguredOutputProjectionRejectsMissingConcreteField(argv[0]);
    testScalarAliasChildOutputRejectsConfiguredFieldPort(argv[0]);
    testExternalScalarAliasChildOutputRejectsConfiguredFieldPort(argv[0]);
    testTypeParameterizedExternalChildInputBindsDemandedField(argv[0]);
    testImplicitNamedExternalChildInputBindsDemandedFields(argv[0]);
    testOptimizeRunnerNameFollowsMainFile(argv[0]);
    testOptimizeSeparatesUnitsByGeneratedDefinitionWeight(argv[0]);
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
    testRuntimeRangeConcatUsesRuntimeOperandWidths(argv[0]);
    testPackedArrayElementCastMaterializesStoredValue(argv[0]);
    testPackedStructArrayFieldConditionalUsesDeclaredElementType(argv[0]);
    testModulePackedArrayInitializerIsPreserved(argv[0]);
    testStaticParameterizedRangeSelectKeepsAllBitsArguments(argv[0]);
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
    testTypeTemplateCastShiftKeepsTargetWidth(argv[0]);
    testEnumNamesAreNotParameterSubstituted(argv[0]);
    testChildLocalAliasPortTypeIsSpecialized(argv[0]);
    testLogicCombInputUsesDirectCombBinding(argv[0]);
    testSequentialChildCombInputUsesLazyBinding(argv[0]);
    testDirectParentPortNarrowingDoesNotUseRegBinding(argv[0]);
    testParentPortPartSelectUsesCombBinding(argv[0]);
    testIndexedParentPortInputUsesCombBinding(argv[0]);
    testPackedParentPortElementInputUsesValueBinding(argv[0]);
    testPackedCombArrayElementInputUsesValueBinding(argv[0]);
    testCombVectorBitInputUsesCombBinding(argv[0]);
    testParentPortFieldInputUsesValueBindingAfterAdapter(argv[0]);
    testDynamicStructInputBindingsStayComb(argv[0]);
    testIndexedLocalCombInputUsesValueBinding(argv[0]);
    testIndexedCombArrayInputUsesCombBinding(argv[0]);
    testScalarParentPortToArrayChildPortUsesAdapter(argv[0]);
    testGeneratedChildArrayKeepsStructuredPortType(argv[0]);
    testGeneratedChildArrayScalarPortUsesCombBinding(argv[0]);
    testWrappedIndexedCombInputUsesCombBinding(argv[0]);
    testGeneratedIndexedBitExtractUsesValueBinding(argv[0]);
    testArrayInputPortCombBindingIsComplete(argv[0]);
    testArrayInputStructPortAdapterUsesChildPortType(argv[0]);
    testChildInputPortUsesActualTypeForAliasNarrowing(argv[0]);
    testZeroAggregateInputPortUsesActualType(argv[0]);
    testUnbasedOneInputPortUsesDestinationWidth(argv[0]);
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
    testConstantZeroFeedthroughMaskDoesNotEagerlyReadInput(argv[0]);
    testPowerOperatorPrecedenceInRanges(argv[0]);
    testCombMethodDependenciesEmitBeforeUsers(argv[0]);
    testParameterizedReplicationInConcatIsValidCatItem(argv[0]);
    testBracedReplicationCountIsNumeric(argv[0]);
    testNumericReplicationConstantUsesIntegerMask(argv[0]);
    testPackageArrayReplicationConstantUsesCppLambda(argv[0]);
    testImportedPackageValueIsQualified(argv[0]);
    testImportedPackageCallableIsQualified(argv[0]);
    testLocalEnumValueBeatsSingleImportedPackageFallback(argv[0]);
    testNumericConcatConstantUsesIntegerExpr(argv[0]);
    testParameterizedNumericConcatUsesCat(argv[0]);
    testConcatCaseKeepsOperandWidths(argv[0]);
    testConcatCaseStructFieldsKeepOperandWidths(argv[0]);
    testInterfaceMembersInConcatUseSignalWidths(argv[0]);
    testConcatArrayElementBitSelectUsesOneBitWidth(argv[0]);
    testConcatGeneratedLocalArrayBitSelectUsesOneBitWidth(argv[0]);
    testConcatPartSelectKeepsSelectedWidth(argv[0]);
    testConcatPackedArraySliceUsesElementWidth(argv[0]);
    testIntegerLocalparamConcatIsConstexprNumeric(argv[0]);
    testKnownWidthFunctionDoesNotForceStructArgumentNumeric(argv[0]);
    testParenthesizedWidthCastIsLogicCast(argv[0]);
    testZeroAssignmentToStructFieldUsesValueInit(argv[0]);
    testZeroAssignmentToTypeParameterizedPackedArrayKeepsPackedWidth(argv[0]);
    testContinuousZeroAssignmentToStructFieldUsesValueInit(argv[0]);
    testZeroAssignmentToPackedArrayElementUsesPlainZero(argv[0]);
    testZeroAggregateCastToIndexedPackedElementUsesStoredType(argv[0]);
    testImportedPackageTypeBitsUseConfiguredWidth(argv[0]);
    testReductionOfPackedAggregatePacksOperand(argv[0]);
    testReductionOrPortConnectionUsesVectorReduction(argv[0]);
    testChildInputStateExpressionUsesCombBinding(argv[0]);
    testGenerateCombOrSequentialSameSignalUsesCombRead(argv[0]);
    testMismatchedChildOutputIsMaterializedBeforeGetter(argv[0]);
    testMatchingChildOutputUsesDeferredAssignPort(argv[0]);
    testForwardedAggregateOutputProjectsChildField(argv[0]);
    testRegisteredAggregateOutputProjectsCurrentField(argv[0]);
    testRegisteredPackedStructArrayOutputProjectsEachField(argv[0]);
    testStructArrayFieldOutputProjectionKeepsElementAssignments(argv[0]);
    testMalformedConfiguredOutputFieldPathIsIgnored(argv[0]);
    testAggregateLambdaIsNotParsedAsChildOutputCall(argv[0]);
    testUnusedAggregateOutputDoesNotAdvertiseEveryField(argv[0]);
    testAggregatePartSelectZeroUsesWritableProxyAssignment(argv[0]);
    testPackedChildOutputFieldProjectionUnpacksBeforeFieldRead(argv[0]);
    testTypeParameterWidthBeatsConfiguredSuffixWidth(argv[0]);
    testLocalAliasWidthBeatsConfiguredSuffixWidth(argv[0]);
    testGenerateBlockUnpackedArrayDimensionOrder(argv[0]);
    testDependentStructTypeParametersStayTemplateParameters(argv[0]);
    testUnsignedCastOfGenerateLoopVariableIsIntegerWidth(argv[0]);
    testUnsignedCastOfClog2ResultPreservesIntegerWidth(argv[0]);
    testImplicitPackedArrayOutputPassThroughKeepsArrayShape(argv[0]);
    testGenerateScalarChildOutputKeepsGenerateGuard(argv[0]);
    testGenerateMetadataOnlyChildKeepsNamedPortBindings(argv[0]);
    testGenerateLoopLocalparamsAreSubstitutedIntoCombMethods(argv[0]);
    testUnpackedArrayDynamicIndexUsesArrayIndex(argv[0]);
    testWideLogicCompoundBitwiseDoesNotTruncateToUint64(argv[0]);
    testStructArrayToPackedArrayInputUsesElementPack(argv[0]);
    testPackedVectorToUnpackedArrayInputUsesExplicitSlices(argv[0]);
    testPackedConcatToUnpackedStructArrayDoesNotUseDirectAssignment(argv[0]);
    testSameStructArrayInputPortUsesDirectArrayBinding(argv[0]);
    testTypeParameterizedArrayPortForwardingDoesNotPack(argv[0]);
    testPackedStructArrayToChildArrayChecksRepresentation(argv[0]);
    testSequentialStorageMemberIsNotProjectedAsHdlField(argv[0]);
    testProjectedStructArrayWholeAssignmentMapsElements(argv[0]);
    testProjectedStructArrayPositionalPatternSelectsMember(argv[0]);
    testDemandedUndrivenStructLeafGetsDefaultComb(argv[0]);
    testStructInputFieldUsedOnlyByChildBindingIsProjected(argv[0]);
    testRegisteredAggregateChildInputBindsDemandedFields(argv[0]);
    testPackedLogicArrayChildOutputUnpacksIntoStructElement(argv[0]);
    testGeneratedChildStructArrayOutputProjectsNestedParentField(argv[0]);
    testSamePackedArrayCombAssignKeepsArrayShape(argv[0]);
    testSameUnpackedStructArrayRegAssignKeepsArrayShape(argv[0]);
    testPackedByteArrayAssignFromUnpackedWordArrayUsesPackValue(argv[0]);
    testContinuousPackedByteArrayAssignFromUnpackedWordArrayUsesPackValue(argv[0]);
    testPackedArrayAssignFromUnpackedArrayChecksRepresentation(argv[0]);
    testContinuousUnpackedWordArrayAssignFromPackedVectorUsesUnpackValue(argv[0]);
    testTypeParameterWholeAssignFromPackedVectorUsesUnpackValue(argv[0]);
    testTypeParameterCastFromPackedByteArrayUsesPackUnpack(argv[0]);
    testPackedArrayToStructArrayCombAssignUsesElementUnpack(argv[0]);
    testPackedArrayOutputToStructArrayUsesElementUnpack(argv[0]);
    testIndexedUnpackedArrayOutputToPackedVectorUsesPackValue(argv[0]);
    testPackedStructInputToLogicPortUsesPackValue(argv[0]);
    testPackedStructInputToLogicAliasPortUsesPackValue(argv[0]);
    testPackedStructCastAssignedToVectorUsesPackValue(argv[0]);
    testPackedStructInputPortUsesPackUnpackForDistinctStructTypes(argv[0]);
    testProjectedDistinctStructInputUsesTargetTypedValueBinding(argv[0]);
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
    testWideInputAssignedToPartSelectDoesNotNarrowToUint64(argv[0]);
    testReplicationOfArrayAliasElementUsesElementWidth(argv[0]);
    testPackedStructArrayReplicationKeepsPackedElementStride(argv[0]);
    testReplicationOfTypeParameterArrayElementUsesElementAccess(argv[0]);
    testNestedPackedArrayReplicationUsesTargetElementWidth(argv[0]);
    testTypeParameterArrayElementRangeSelectUsesElementWidth(argv[0]);
    testTypeParameterArrayElementRangeConditionalKeepsByteWidth(argv[0]);
    testOutOfOrderNamedAggregateBecomesPositional(argv[0]);
    testCombArrayElementBitSelectCastOperandIsParenthesized(argv[0]);
    testPackedArrayComparisonEmitsNumericPackValue(argv[0]);
    testTypeParameterStructFieldConditionalConcatUsesFieldType(argv[0]);
    testGenerateLocalCombSignalIsPerLoopIndex(argv[0]);
    testGenerateLocalChildOutputKeepsProducerIndex(argv[0]);
    testNestedGenerateLocalCombKeepsInnerLoop(argv[0]);
    testNestedGenerateModuleArrayKeepsEveryDimension(argv[0]);
    testAssignDrivenStructFieldReadUsesFieldProjection(argv[0]);
    testDefaultStructFieldProjectionUsesTypedZero(argv[0]);
    testNestedDefaultStructFieldProjectionUsesLeafZero(argv[0]);
    testAggregateOutputFieldReadUsesExtractedFieldComb(argv[0]);
    testExtractedStructFieldCombKeepsNestedUpdates(argv[0]);
    testExtractedScalarFieldCombSkipsSiblingStructFields(argv[0]);
    testExtractedNestedLeafReadsSiblingThroughFieldComb(argv[0]);
    testExtractedFieldConditionCallsSiblingFieldComb(argv[0]);
    testExtractedPackedArrayFieldKeepsElementIndex(argv[0]);
    testFieldExtractionKeepsWholeAggregateSiblingBranch(argv[0]);
    testFieldProjectionThroughSelectedAggregateAvoidsWholeCombRecursion(argv[0]);
    testFieldProjectionThroughTypeParamAggregateAvoidsWholeCombRecursion(argv[0]);
    testFieldProjectionFromIndexedZeroElementIsNotDefault(argv[0]);
    testFieldCombDoesNotPropagateThroughPlainWholeValueCall(argv[0]);
    testExtractedFieldConditionalDefaultBranchUsesFieldType(argv[0]);
    testExtractedFieldConditionalCastsWideConstantBranch(argv[0]);
    testAggregateCastDoesNotHideNestedFieldConditional(argv[0]);
    testSequentialChildInputPortBindingStaysLazy(argv[0]);
    testChildInputConnectedToParentRegUsesRegBinding(argv[0]);
    testCombVectorChildInputUsesDirectCombBinding(argv[0]);
    testCombOutputStorageChildInputUsesCombGetter(argv[0]);
    testChildOutputCombFeedingSequentialInputIsNoCache(argv[0]);
    testCppKeywordLocalSignalIsEscaped(argv[0]);
    testBinaryPrecedencePreservesNestedEqualityUnderBitwise(argv[0]);
    testGenerateBoundDivisionExpressionIsBalanced(argv[0]);
    testOneBitUnaryNotInConcatAvoidsLogicOperatorNot(argv[0]);
    testWideConditionalLogicBranchDoesNotNarrowToUint64(argv[0]);
    testFunctionOutputArgumentIsReference(argv[0]);
    testSequentialDpiOutputArgumentUsesBlockingStorage(argv[0]);
    testScopedPackedConstantConcatKeepsAggregateWidth(argv[0]);
    testExternalScopedConstantConcatUsesCppValueTypeWidth(argv[0]);
    testSingleOperandConcatsKeepCatTypeInArithmetic(argv[0]);
    testFunctionOutputStructFieldsUpdateAggregateComb(argv[0]);
    testTypedefArrayIndexedPartSelectKeepsSelectedWidth(argv[0]);
    testParameterizedInterfaceArrayUsesUnpackedDimension(argv[0]);
    testParameterizedInterfacePortInfersChildTemplateAndBindsByRef(argv[0]);
    testParameterizedInterfaceArrayInfersNestedTemplateArguments(argv[0]);
    testIndexedInterfaceConnectionBindsObjectByReference(argv[0]);
    testIncludedMacroExpandsInterfaceArrayBindings(argv[0]);
    testCrossFileInterfaceTraitEnablesSignalBinding(argv[0]);
    testConditionalGenerateInterfaceBindingDoesNotCaptureLoopIndex(argv[0]);
    testProceduralInterfaceMemberGetsIndependentCombBinding(argv[0]);
    testInstanceOutputBindsInterfaceMemberDirectly(argv[0]);
    testUnderscoreSignalIsNotInferredAsStructField(argv[0]);
    testStructFieldCombNameDoesNotCollideWithScalarSignal(argv[0]);
    testFieldDemandMethodCallRequiresIdentifierBoundary(argv[0]);
    testTypeParameterFieldDoesNotUseOtherModuleLocalType(argv[0]);
    testConfiguredInterfaceArrayInfersNestedTemplateArguments(argv[0]);
    testPackedStructArrayFieldReadsMaterializeElement(argv[0]);
    testUnpackedStructArrayFieldReadStaysAddressable(argv[0]);
    testDefaultParameterizedChildInstantiationUsesEmptyTemplateArgs(argv[0]);
    testPositionalChildConnectionsPreserveOmittedClockOrdinal(argv[0]);
    testPositionalChildConnectionsUseCrossFilePortMetadata(argv[0]);
    testSequentialUnpackedArrayUsesDeferredMemoryUpdates(argv[0]);
    testSequentialUnpackedStructMemberUsesScalarMemoryElement(argv[0]);
    testSequentialMemoryWholeArrayAssignmentSchedulesEachRow(argv[0]);
    testPackedArrayMemberElementZeroUsesStoredValueType(argv[0]);
    testTypeParameterPackedMemberElementZeroKeepsInferredWidth(argv[0]);
    testContinuousConcatOutputAssignSplitsIntoCombOutputs(argv[0]);
    testMemberAccessRangeBoundsKeepMemberBeforeNumericCast(argv[0]);
    testFunctionMemberAccessRangeBoundKeepsMemberInsideCall(argv[0]);
    testMemberArithmeticArrayIndexKeepsOperatorsInsideIndex(argv[0]);
    testStructMemberPartSelectAsArrayIndexIsBalanced(argv[0]);
    testStructFieldCombEnumComparisonDoesNotProjectEnum(argv[0]);
    testNumericPackageFunctionKeepsAggregateArgument(argv[0]);
    testPackageFunctionResultWidthInConcat(argv[0]);
    testChildClockPortAliasesAreNotBound(argv[0]);
    testCurrentCombBlockReadBypassesEarlierGeneratedWireMap(argv[0]);
    testPackedStructArraySliceAssignmentCopiesElements(argv[0]);
    testPackedStructArrayElementPacksIntoVector(argv[0]);
    testPackedVectorArrayElementProxyPacksIntoVector(argv[0]);
    testSingleElementPackedArraySliceAssignmentIsNotDropped(argv[0]);
    testPackedStructRegisterShiftPacksCurrentValue(argv[0]);
    testNestedArrayElementConditionalKeepsElementType(argv[0]);
    testIndexedStructInputUsesIndependentProjectedFieldPort(argv[0]);
    testProjectedIndexedChildInputIgnoresDecltypeGetter(argv[0]);
    testExternalParameterizedChildBindsIndexedInputProjections(argv[0]);
    testParameterizedStructFieldAdaptsToPackedArrayChildPort(argv[0]);
    testCommaSeparatedContinuousAggregateUsesOwnRhsForField(argv[0]);
    testGeneratedExternalChildUsesProjectedStructFieldPort(argv[0]);
    testProjectedChildFieldKeepsSourceMemberPrefix(argv[0]);
    testProjectedChildFieldFromDependentTypeGetsCombMethod(argv[0]);
    testOverridableStructDefaultKeepsOutputFieldTypeDependent(argv[0]);
    testProjectedNestedFieldThroughDependentMemberIsGuarded(argv[0]);
    testProjectedFieldAfterWholeStructCastStaysOutsideCast(argv[0]);
    testStructCastDoesNotProjectFieldsFromScalarInput(argv[0]);
    testWholeAggregateInputForwardingUsesProjectedFieldPort(argv[0]);
    testChildAggregateOutputUsesProjectedFieldPort(argv[0]);
    testProjectedNestedOutputRebasesDescendantMembers(argv[0]);
    testProjectedNestedVectorPreservesPartSelect(argv[0]);
    testCrossFileAggregateOutputProjectionUsesTraits(argv[0]);
    testIndexedChildAggregateOutputProjectionUsesTraits(argv[0]);
    testChildAggregateProjectionSurvivesArrayTranspose(argv[0]);
    testIndexedChildNestedProjectionRequiresExactLeaf(argv[0]);
    testWholeArrayChildNestedProjectionRequiresExactLeaf(argv[0]);
    testProjectedBypassReadsIndexedInputField(argv[0]);
    testNestedArrayProjectionIncludesAncestorFieldAssignment(argv[0]);
    testBitSelectComplementKeepsOneBitWidth(argv[0]);
    testGeneratedContinuousTreeOrdersDependenciesBeforeParent(argv[0]);
    testGeneratedPackedTreeOrdersDependenciesBeforeParent(argv[0]);
    testStructEnumFieldConditionalUsesFieldWidth(argv[0]);
    return 0;
}
