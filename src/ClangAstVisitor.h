#ifndef CODEX_LAN_AGENT_CLANG_AST_VISITOR_H
#define CODEX_LAN_AGENT_CLANG_AST_VISITOR_H

#include "ClangIndexerAdapter.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <unordered_set>
#include <unordered_map>

namespace codex_lan_agent
{

class ClangApiConsumer;

class ClangApiVisitor
    : public clang::RecursiveASTVisitor<ClangApiVisitor>
{
    friend class ClangApiConsumer;

public:
    explicit ClangApiVisitor(clang::ASTContext * ctx)
        : ctx_(ctx)
    {
    }

    bool VisitFunctionDecl(clang::FunctionDecl * func);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl * record);
    bool VisitCallExpr(clang::CallExpr * call);
    bool VisitDeclRefExpr(clang::DeclRefExpr * ref);
    bool VisitMemberExpr(clang::MemberExpr * member);

    ClangAstParseResult GetResult() const;

    void SetTargetNamespaces(const std::vector<std::string> & namespaces)
    {
        target_namespaces_ = namespaces;
    }

private:
    bool IsInTargetNamespace(const std::string & ns_name) const;
    bool ShouldTrackFunction(const clang::FunctionDecl * func) const;
    std::string GetQualifiedName(const clang::NamedDecl * decl) const;
    ClangTypeInfo ConvertTypeInfo(const clang::Type * type) const;
    ClangParamInfo ConvertParamInfo(const clang::ParmVarDecl * param) const;
    void ExtractSourceLocation(
        const clang::SourceLocation & loc,
        std::string * file,
        int * line,
        int * col) const;

private:
    clang::ASTContext * ctx_;
    std::vector<std::string> target_namespaces_;

    std::vector<ClangClassInfo> classes_;
    std::vector<ClangMethodInfo> free_functions_;
    std::vector<ClangCallRef> call_refs_;
    std::vector<std::string> namespaces_;

    std::unordered_map<std::string, size_t> class_index_;
    std::unordered_map<std::string, size_t> function_index_;

    std::string current_function_name_;
    std::string current_class_name_;
};

class ClangApiConsumer : public clang::ASTConsumer
{
public:
    explicit ClangApiConsumer(
        clang::ASTContext * ctx,
        const std::vector<std::string> & target_namespaces,
        ClangAstParseResult * out_result = nullptr)
        : visitor_(ctx), out_result_(out_result)
    {
        visitor_.SetTargetNamespaces(target_namespaces);
    }

    void HandleTranslationUnit(clang::ASTContext & ctx) override
    {
        visitor_.ctx_ = &ctx;
        visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
        if (out_result_) {
            *out_result_ = visitor_.GetResult();
        }
    }

    ClangApiVisitor & GetVisitor() { return visitor_; }
    ClangAstParseResult GetParseResult() const { return parsed_result_; }

private:
    ClangApiVisitor visitor_;
    ClangAstParseResult parsed_result_;
    ClangAstParseResult * out_result_;
};

class ClangApiAction : public clang::ASTFrontendAction
{
public:
    explicit ClangApiAction(
        const std::vector<std::string> & target_namespaces)
        : target_namespaces_(target_namespaces)
    {
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance & ci,
        clang::StringRef file) override
    {
        return std::make_unique<ClangApiConsumer>(
            &ci.getASTContext(),
            target_namespaces_);
    }

private:
    std::vector<std::string> target_namespaces_;
};

ClangAstParseResult RunClangAstParserImpl(
    const ClangIndexerOptions & options);

}

#endif
