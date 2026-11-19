#include "pipeline_scheduler.h"
#include "delayed_scheduler.h"
#include "../synth/Verilog.h"
#include "../synth/StreamPipeline.h"
#include "../synth/ReadPipeline.h"
#include "../synth/LoopPipeline.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Attr.h"

std::string cpphdlHlsReplacement(const clang::CXXRecordDecl*, const clang::ASTContext&);
std::string cpphdlHlsAnnotationValue(const clang::CXXRecordDecl*, const clang::ASTContext&, const std::string&);

namespace cpphdl::hls {
using namespace graph;

static bool memoryPipeline(const clang::CXXRecordDecl* record) {
    for(const auto* attr:record->specific_attrs<clang::AnnotateAttr>())
        if(attr->getAnnotation()=="CPPHDL_HLS_EXTERNAL_MEMORY")return true;
    return false;
}

unsigned pipelineStages(const clang::CXXRecordDecl* record) {
    if(memoryPipeline(record))return 1;
    auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
    if (!specialization || specialization->getTemplateArgs().size() < 2)
        throw std::runtime_error("ClockedPipeline requires a method class and a stage count");
    auto stages = specialization->getTemplateArgs()[1].getAsIntegral().getLimitedValue();
    if (!stages || stages > 64) throw std::runtime_error("ClockedPipeline STAGES must be in 1..64");
    return unsigned(stages);
}

std::map<std::string,Value> exportPipelineGraph(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record, Graph& output, const std::string& scope) {
    const unsigned stages = pipelineStages(record);
    auto design = lowerPipelineSource(ctx, sema, record);
    if(memoryPipeline(record) && std::any_of(design.blocks.begin(),design.blocks.end(),[](const auto& b){return b.suspend;})) {
        const auto& args=llvm::cast<clang::ClassTemplateSpecializationDecl>(record)->getTemplateArgs();
        return synth::buildLoopPipeline(output,design,scope,args[4].getAsIntegral().getLimitedValue());
    }
    Graph command;
    auto ports = synth::exportCombinationalCommand(command, design, scope);
    for (auto& node : command.nodes) if (node.op == "wire") node.op = "input";
    for (const auto& [name,bits] : ports)
        if (name == "result_out" || name == "fault_out" || name == "operation_in" ||
            name == "index_in" || name == "value_in")
            command.ports.push_back({name,bits,name.find("_in") != std::string::npos});
    command.optimize();
    const auto* object = llvm::cast<clang::ClassTemplateSpecializationDecl>(record)->getTemplateArgs()[0].getAsType()->getAsCXXRecordDecl();
    std::string replacementModule;
    unsigned minimumLatency = 1;
    for (const auto* attr : object->specific_attrs<clang::AnnotateAttr>()) {
        auto text = attr->getAnnotation().str();
        const std::string modulePrefix = "CPPHDL_REPLACEMENT_MODULE=", minimumPrefix = "CPPHDL_REPLACEMENT_MIN_LATENCY=";
        if (text.find(modulePrefix) == 0) {
            if (!replacementModule.empty()) throw std::runtime_error("duplicate pipeline replacement module");
            replacementModule = ::cpphdlHlsAnnotationValue(object,ctx,text.substr(modulePrefix.size()));
            if (replacementModule.empty() || (!std::isalpha(replacementModule[0]) && replacementModule[0]!='_') ||
                std::any_of(replacementModule.begin(),replacementModule.end(),[](unsigned char c){return !std::isalnum(c)&&c!='_';}))
                throw std::runtime_error("invalid pipeline replacement module name");
        }
        if (text.find(minimumPrefix)==0) {
            auto value=text.substr(minimumPrefix.size()); size_t used=0;
            minimumLatency=std::stoul(value,&used);
            if (used!=value.size() || !minimumLatency || minimumLatency>64)
                throw std::runtime_error("replacement minimum latency must be in 1..64");
        }
    }
    if (!replacementModule.empty()) {
        if (!command.states.empty()) throw std::runtime_error("pipeline replacement requires a stateless command class");
        auto body=::cpphdlHlsReplacement(object,ctx);
        if (body.empty()) throw std::runtime_error("pipeline replacement requires a Verilog annotation body");
        output.attributes.push_back({scope,"pipeline_replacement",replacementModule});
        output.attributes.push_back({scope,"pipeline_replacement_body",body});
        output.attributes.push_back({scope,"pipeline_replacement_min_latency",std::to_string(minimumLatency)});
    }
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
    if(memoryPipeline(record)) {
        const auto& args=llvm::cast<clang::ClassTemplateSpecializationDecl>(record)->getTemplateArgs();
        auto pins=synth::buildReadPipeline(output,std::move(command),scope,
            args[4].getAsIntegral().getLimitedValue(),design.argumentBits,design.externalDataBits,design.blockRam);
        if(design.blockRam) {
            synth::connectLocalMemory(output,scope+".local_memory",design.memoryBytes,design.externalDataBits,pins);
            for(auto p=pins.begin();p!=pins.end();)if(p->first.find("local_out.")==0)p=pins.erase(p);else ++p;
        }
        return pins;
    }
    StreamPipeline pipeline;
    pipeline.scope = scope; pipeline.stages = stages;
    pipeline.latency = std::max(stages,minimumLatency);
    synth::compactPipelineLogic(command);
    std::ostringstream serialized; command.save(serialized); pipeline.logic = serialized.str();
    output.currentScope = scope;
    for (auto [name,width] : std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},
         {"response_ready_in",1},{"operation_in",design.argumentBits},{"index_in",design.argumentBits},{"value_in",design.argumentBits}})
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
    for (auto& [port,bits] : ports) {
        bool input=port=="reset" || port=="command_valid_in" || port=="response_ready_in" ||
            port=="operation_in" || port=="index_in" || port=="value_in";
        auto external=port;
        if(port.find("memory_out.")==0) {
            input=port=="memory_out.ready_out" || port=="memory_out.valid_out" ||
                  port=="memory_out.data_out" || port=="memory_out.error_out";
            external="memory_out__"+port.substr(11);
            external=external.substr(0,external.rfind('_'))+(input?"_in":"_out");
        }
        graph.ports.push_back({external,bits,input});
    }
    if(memoryPipeline(record))
        return "// HLS ordered memory continuations; variable memory latency, pipelined requests.\n"+
            synth::verilogText(graph,name);
    unsigned latency = 0;
    for (const auto& pipeline : graph.pipelines) if (pipeline.scope == name) latency = pipeline.latency;
    if (!latency) throw std::runtime_error("pipeline generator lost its scheduled latency");
    return "// HLS pipelined_logic: II=1, STAGES=" + std::to_string(pipelineStages(record)) +
        ", LATENCY=" + std::to_string(latency) +
        "; stage boundaries balance operation depth, not technology delay.\n" + synth::verilogText(graph,name);
}
}
