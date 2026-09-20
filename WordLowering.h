#pragma once

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Basic/DiagnosticLex.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace cpphdl::word_lowering {

class Visitor : public clang::RecursiveASTVisitor<Visitor>
{
    clang::ASTContext& context;

    bool supported(clang::QualType type) const
    {
        auto* record = type.getNonReferenceType().getUnqualifiedType()->getAsCXXRecordDecl();
        auto* specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(record);
        if (!specialization) {
            return false;
        }
        const auto name = specialization->getQualifiedNameAsString();
        if (name == "cxxrtl::value") {
            return true;
        }
        const auto& arguments = specialization->getTemplateArgs();
        if (name == "cxxrtl::slice_expr") {
            return supported(arguments[0].getAsType());
        }
        if (name == "cxxrtl::concat_expr") {
            return supported(arguments[0].getAsType()) && supported(arguments[1].getAsType());
        }
        return false;
    }

public:
    std::map<unsigned, unsigned> openings;
    std::map<unsigned, unsigned> endings;
    explicit Visitor(clang::ASTContext& context) : context(context) {}

    bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* call)
    {
        auto* method = call->getMethodDecl();
        if (!method || method->getNameAsString() != "val" ||
            method->getParent()->getQualifiedNameAsString() != "cxxrtl::concat_expr") {
            return true;
        }
        auto* receiver = call->getImplicitObjectArgument();
        if (!receiver || !supported(receiver->getType())) {
            return true;
        }
        auto& manager = context.getSourceManager();
        auto begin = call->getBeginLoc();
        auto finish = call->getEndLoc();
        auto receiver_finish = receiver->getEndLoc();
        if (begin.isMacroID() || finish.isMacroID() || receiver_finish.isMacroID() ||
            !manager.isWrittenInMainFile(begin) || !manager.isWrittenInMainFile(finish)) {
            return true;
        }
        auto suffix = clang::Lexer::getLocForEndOfToken(receiver_finish, 0, manager, context.getLangOpts());
        auto end = clang::Lexer::getLocForEndOfToken(finish, 0, manager, context.getLangOpts());
        if (suffix.isInvalid() || end.isInvalid()) {
            return true;
        }
        unsigned suffix_offset = manager.getFileOffset(suffix);
        unsigned end_offset = manager.getFileOffset(end);
        if (suffix_offset >= end_offset || endings.count(suffix_offset)) {
            return true;
        }
        ++openings[manager.getFileOffset(begin)];
        endings.emplace(suffix_offset, end_offset);
        return true;
    }
};

class Consumer : public clang::ASTConsumer
{
    std::string& output;
    size_t& count;
public:
    Consumer(std::string& output, size_t& count) : output(output), count(count) {}

    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        if (context.getDiagnostics().hasErrorOccurred()) {
            return;
        }
        Visitor visitor(context);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
        auto source = context.getSourceManager().getBufferData(context.getSourceManager().getMainFileID());
        output = "#include \"cpphdl_netlist_words.h\"\n";
        for (unsigned offset = 0; offset < source.size();) {
            if (auto opening = visitor.openings.find(offset); opening != visitor.openings.end()) {
                for (unsigned index = 0; index < opening->second; ++index) {
                    output += "::cpphdl::netlist::assemble(";
                }
            }
            if (auto ending = visitor.endings.find(offset); ending != visitor.endings.end()) {
                output += ')';
                offset = ending->second;
            }
            else {
                output += source[offset++];
            }
        }
        count = visitor.endings.size();
    }
};

class Action : public clang::ASTFrontendAction
{
    std::string& output;
    size_t& count;
public:
    Action(std::string& output, size_t& count) : output(output), count(count) {}
    bool BeginSourceFileAction(clang::CompilerInstance& compiler) override
    {
        // Match the normal frontend's workaround for spurious failed-search
        // diagnostics in the supported Clang build. Missing includes still fail.
        compiler.getDiagnostics().setSeverity(clang::diag::err_cannot_open_file,
            clang::diag::Severity::Ignored, clang::SourceLocation());
        return true;
    }
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override
    {
        return std::make_unique<Consumer>(output, count);
    }
};

class Factory : public clang::tooling::FrontendActionFactory
{
    std::string& output;
    size_t& count;
public:
    Factory(std::string& output, size_t& count) : output(output), count(count) {}
    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<Action>(output, count);
    }
};

inline int run(int argc, const char** argv, const std::vector<std::string>& include_arguments)
{
    if (argc < 5 || std::string_view(argv[4]) != "--") {
        llvm::errs() << "usage: cpphdl --lower-word-model input.cc output.cc -- compiler flags\n";
        return 2;
    }
    const std::filesystem::path source = std::filesystem::absolute(argv[2]);
    const std::filesystem::path destination = std::filesystem::absolute(argv[3]);
    if (!std::filesystem::is_regular_file(source) || std::filesystem::exists(destination)) {
        llvm::errs() << "word lowering requires an existing source and a new output file\n";
        return 2;
    }
    std::vector<std::string> arguments;
    for (int index = 5; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    arguments.insert(arguments.end(), include_arguments.begin(), include_arguments.end());
    clang::tooling::FixedCompilationDatabase database(std::filesystem::current_path().string(), arguments);
    clang::tooling::ClangTool tool(database, {source.string()});
    std::string lowered;
    size_t count = 0;
    Factory factory(lowered, count);
    const int status = tool.run(&factory);
    if (status || lowered.empty()) {
        return status ? status : 1;
    }
    std::ofstream stream(destination);
    stream << lowered;
    stream.close();
    if (!stream) {
        llvm::errs() << "failed to write lowered word model\n";
        return 1;
    }
    llvm::outs() << "lowered concatenation trees: " << count << '\n';
    return 0;
}

inline int driver(int argc, const char** argv, const char* scriptName = "cpphdl-word.py")
{
#if defined(_WIN32)
    llvm::errs() << "word-model driver currently requires a POSIX host\n";
    return 1;
#else
#ifdef CPPHDL_SOURCE_DIR
    const auto root = std::filesystem::path(CPPHDL_SOURCE_DIR);
#else
    const auto root = std::filesystem::path(__FILE__).parent_path();
#endif
    const auto script = root / "tools" / scriptName;
    if (!std::filesystem::is_regular_file(script)) {
        llvm::errs() << "cannot locate " << script.string() << '\n';
        return 1;
    }
    std::vector<std::string> arguments{"python3", script.string(), "--cpphdl", argv[0]};
    for (int index = 2; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    std::vector<char*> pointers;
    for (auto& argument : arguments) {
        pointers.push_back(argument.data());
    }
    pointers.push_back(nullptr);
    execvp(pointers[0], pointers.data());
    llvm::errs() << "cannot execute " << script.string() << '\n';
    return 1;
#endif
}

}
