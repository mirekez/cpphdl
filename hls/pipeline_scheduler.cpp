#include "pipeline_scheduler.h"
#include "delayed_scheduler.h"
#include "../synth/Verilog.h"
#include "../synth/StreamPipeline.h"
#include "clang/AST/DeclTemplate.h"

namespace cpphdl::hls {
using namespace graph;

unsigned pipelineStages(const clang::CXXRecordDecl* record) {
    auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
    if (!specialization || specialization->getTemplateArgs().size() != 2)
        throw std::runtime_error("ClockedPipeline requires a method class and a stage count");
    auto stages = specialization->getTemplateArgs()[1].getAsIntegral().getLimitedValue();
    if (!stages || stages > 64) throw std::runtime_error("ClockedPipeline STAGES must be in 1..64");
    return unsigned(stages);
}

std::map<std::string,Value> exportPipelineGraph(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record, Graph& output, const std::string& scope) {
    const unsigned stages = pipelineStages(record);
    auto design = lowerPipelineSource(ctx, sema, record);
    Graph command;
    auto ports = synth::exportCombinationalCommand(command, design, scope);
    for (auto& node : command.nodes) if (node.op == "wire") node.op = "input";
    for (const auto& [name,bits] : ports)
        if (name == "result_out" || name == "fault_out" || name == "operation_in" ||
            name == "index_in" || name == "value_in")
            command.ports.push_back({name,bits,name.find("_in") != std::string::npos});
    command.optimize();
    auto order = command.dependencyOrder();
    std::vector<unsigned> depth(command.nodes.size());
    unsigned longest = 1;
    for (auto n : order) {
        const auto& node = command.nodes[n];
        if (node.op == "input" || node.op == "state") continue;
        for (auto* operand : {&node.left,&node.right,&node.select})
            for (auto bit : command.resolved(*operand)) if (bit > 1)
                depth[n] = std::max(depth[n],depth.at(Graph::owner(bit)));
        longest = std::max(longest,++depth[n]);
    }
    for (auto n : order) if (depth[n])
        command.nodes[n].scope = scope + ".hls_stage_" + std::to_string((depth[n]-1)*stages/longest);
    StreamPipeline pipeline;
    pipeline.scope = scope; pipeline.stages = stages;
    std::ostringstream serialized; command.save(serialized); pipeline.logic = serialized.str();
    output.currentScope = scope;
    for (auto [name,width] : std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},
         {"response_ready_in",1},{"operation_in",32},{"index_in",32},{"value_in",32}})
        pipeline.pins[name] = output.wire(width,scope + "." + name);
    synth::buildStreamPipeline(output,pipeline,std::move(command));
    auto result = pipeline.pins;
    output.pipelines.push_back(std::move(pipeline));
    output.attributes.push_back({scope,"hls_pipeline_ii","1"});
    output.attributes.push_back({scope,"hls_pipeline_stages",std::to_string(stages)});
    return result;
}

std::string generatePipeline(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record, const std::string& name) {
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto ports = exportPipelineGraph(ctx,sema,record,graph,name);
    for (auto& node : graph.nodes) if (node.op == "wire") node.op = "input";
    for (auto& [port,bits] : ports)
        graph.ports.push_back({port,bits,port == "reset" || port.find("_in") == port.size()-3});
    return "// HLS pipelined_logic: II=1, STAGES=" + std::to_string(pipelineStages(record)) +
        "; stage boundaries balance operation depth, not technology delay.\n" + synth::verilogText(graph,name);
}
}
