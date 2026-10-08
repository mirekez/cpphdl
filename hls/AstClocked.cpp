#include "AstClocked.h"
#include "delayed_scheduler.h"
#include "pipeline_scheduler.h"
#include "../Module.h"
#include "../Project.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "llvm/Support/raw_ostream.h"
namespace cpphdl::hls {
using namespace clang;
namespace {
ASTContext* context;
Sema* semantics;
bool success = true;
}
bool isPipeline(const CXXRecordDecl* record) {
    for (const auto* attr : record->specific_attrs<AnnotateAttr>())
        if (attr->getAnnotation() == "CPPHDL_HLS_PIPELINE") return true;
    return false;
}
void prepareClocked(clang::ASTContext& ctx, clang::Sema& sema) { context = &ctx; semantics = &sema; }
bool isClocked(const clang::CXXRecordDecl* record) {
    for (const auto* attr : record->specific_attrs<clang::AnnotateAttr>())
        if (attr->getAnnotation() == "CPPHDL_HLS_CLOCKED") return true;
    return isPipeline(record);
}
std::string clockedName(const clang::CXXRecordDecl* record, const std::string& base) {
    std::string name = base;
    if (auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(record)) {
        const auto& args = specialization->getTemplateArgs();
        for (const auto* attr : record->specific_attrs<AnnotateAttr>())
            if (attr->getAnnotation() == "CPPHDL_HLS_EXTERNAL_MEMORY")
                return name + "_A" + std::to_string(args[1].getAsIntegral().getLimitedValue());
        if (isPipeline(record) && args.size() > 1 && args[1].getKind() == TemplateArgument::Integral)
            return name + "_P" + std::to_string(args[1].getAsIntegral().getLimitedValue());
        if (args.size() > 1 && args[1].getKind() == TemplateArgument::Integral) {
            auto bound = args[1].getAsIntegral().getLimitedValue();
            if (bound) name += "_R" + std::to_string(bound);
        }
        if (args.size() > 2 && args[2].getKind() == TemplateArgument::Integral) {
            auto width = args[2].getAsIntegral().getLimitedValue();
            if (width != 64) name += "_A" + std::to_string(width);
        }
        if (args.size() > 3 && args[3].getKind() == TemplateArgument::Integral) {
            auto bytes = args[3].getAsIntegral().getLimitedValue();
            if (bytes != 4096) name += "_H" + std::to_string(bytes);
        }
        if (args.size() > 4 && args[4].getAsIntegral().getBoolValue()) name += "_M1";
        if (args.size() > 5 && args[5].getAsIntegral().getBoolValue()) name += "_B1";
    }
    return name;
}
bool exportClocked(const clang::CXXRecordDecl* record, cpphdl::Module& module) {
    try {
        if (!context || !semantics) throw std::runtime_error("HLS frontend context is unavailable");
        if (!currProject->clocks.empty()) throw std::runtime_error("Clocked<T> currently requires the default clk, not named/CDC clocks");
        module.replacement = isPipeline(record) ? generatePipeline(*context, *semantics, record, module.name)
            : generateDelayed(*context, *semantics, record, module.name);
        llvm::outs() << "HLS: source AST scheduled for " << module.name << "\n";
    } catch (const std::exception& error) {
        llvm::errs() << "HLS AST error: " << error.what() << "\n"; success = false;
    }
    return true;
}
bool clockedSucceeded() { return success; }
std::map<std::string, graph::Value> exportClockedGraph(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record, graph::Graph& graph, const std::string& scope) {
    if (!graph.clocks.empty()) throw std::runtime_error("Clocked synthesis currently requires the default clk");
    return isPipeline(record) ? exportPipelineGraph(ctx, sema, record, graph, scope)
        : exportDelayedGraph(ctx, sema, record, graph, scope);
}
}
