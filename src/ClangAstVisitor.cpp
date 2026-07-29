#include "ClangAstVisitor.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/ArgumentsAdjusters.h"

namespace codex_lan_agent
{

bool ClangApiVisitor::IsInTargetNamespace(const std::string & ns_name) const
{
    if (target_namespaces_.empty()) {
        return true;
    }
    for (const auto & target : target_namespaces_) {
        if (ns_name.find(target) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool ClangApiVisitor::ShouldTrackFunction(
    const clang::FunctionDecl * func) const
{
    if (!func || !func->isDefined()) {
        return false;
    }

    const clang::DeclContext * ctx = func->getDeclContext();
    if (ctx->isNamespace()) {
        const clang::NamespaceDecl * ns =
            clang::cast<clang::NamespaceDecl>(ctx);
        std::string ns_name = ns->getNameAsString();
        return IsInTargetNamespace(ns_name);
    }

    if (ctx->isRecord()) {
        return true;
    }

    return true;
}

std::string ClangApiVisitor::GetQualifiedName(
    const clang::NamedDecl * decl) const
{
    if (!decl) {
        return {};
    }

    std::string result = decl->getQualifiedNameAsString();
    return result;
}

ClangTypeInfo ClangApiVisitor::ConvertTypeInfo(
    const clang::Type * type) const
{
    ClangTypeInfo info;
    if (!type) {
        return info;
    }

    clang::QualType qt = type->getCanonicalTypeUnqualified();
    const clang::Type * canonical = qt.getTypePtr();
    info.spelling = canonical->getTypeClassName();
    info.qualified_name = type->getAsCXXRecordDecl()
        ? type->getAsCXXRecordDecl()->getQualifiedNameAsString()
        : canonical->getTypeClassName();

    info.is_const = qt.isConstQualified();
    info.is_ref = canonical->isReferenceType();
    info.is_ptr = canonical->isPointerType();
    info.is_builtin = canonical->isBuiltinType();

    return info;
}

ClangParamInfo ClangApiVisitor::ConvertParamInfo(
    const clang::ParmVarDecl * param) const
{
    ClangParamInfo info;
    if (!param) {
        return info;
    }

    info.name = param->getNameAsString();
    clang::QualType qt = param->getType();
    info.type = ConvertTypeInfo(qt.getTypePtr());

    const clang::Expr * default_arg = param->getDefaultArg();
    if (default_arg) {
        info.has_default = true;
        clang::SourceManager & sm = ctx_->getSourceManager();
        info.default_expr = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(
                default_arg->getSourceRange()),
            sm,
            clang::LangOptions()).str();
    }

    return info;
}

void ClangApiVisitor::ExtractSourceLocation(
    const clang::SourceLocation & loc,
    std::string * file,
    int * line,
    int * col) const
{
    if (!loc.isValid() || !loc.isFileID()) {
        return;
    }

    clang::SourceManager & sm = ctx_->getSourceManager();
    clang::PresumedLoc ploc = sm.getPresumedLoc(loc);
    if (ploc.isInvalid()) {
        return;
    }

    *file = ploc.getFilename();
    *line = ploc.getLine();
    *col = ploc.getColumn();
}

bool ClangApiVisitor::VisitFunctionDecl(clang::FunctionDecl * func)
{
    if (!func || !func->isDefined()) {
        return true;
    }

    const clang::DeclContext * ctx = func->getDeclContext();
    std::string qualified_name = GetQualifiedName(func);
    std::string func_name = func->getNameAsString();

    if (ctx->isNamespace()) {
        const clang::NamespaceDecl * ns =
            clang::cast<clang::NamespaceDecl>(ctx);
        std::string ns_name = ns->getNameAsString();

        if (!ns_name.empty() &&
            std::find(namespaces_.begin(),
                      namespaces_.end(),
                      ns_name) == namespaces_.end()) {
            namespaces_.push_back(ns_name);
        }

        if (!ShouldTrackFunction(func)) {
            return true;
        }

        ClangMethodInfo method;
        method.name = func_name;
        method.qualified_name = qualified_name;
        clang::QualType ret_type = func->getReturnType();
        method.return_type = ConvertTypeInfo(ret_type.getTypePtr());
        method.is_const = false;
        method.is_static = false;
        method.is_public = true;

        for (const clang::ParmVarDecl * param : func->parameters()) {
            method.params.push_back(ConvertParamInfo(param));
        }

        ExtractSourceLocation(
            func->getBeginLoc(),
            &method.source_file,
            &method.source_line,
            &method.source_col);

        free_functions_.push_back(method);
        function_index_[qualified_name] = free_functions_.size() - 1;

    } else if (ctx->isRecord()) {
        const clang::CXXRecordDecl * record =
            clang::dyn_cast<clang::CXXRecordDecl>(ctx);
        if (!record) {
            return true;
        }

        std::string class_name = record->getQualifiedNameAsString();

        auto it = class_index_.find(class_name);
        if (it == class_index_.end()) {
            ClangClassInfo cls;
            cls.name = record->getNameAsString();
            cls.qualified_name = class_name;
            cls.namespace_name = "";

            const clang::DeclContext * record_ctx = record->getDeclContext();
            if (record_ctx->isNamespace()) {
                const clang::NamespaceDecl * ns =
                    clang::cast<clang::NamespaceDecl>(record_ctx);
                cls.namespace_name = ns->getNameAsString();

                if (!cls.namespace_name.empty() &&
                    std::find(namespaces_.begin(),
                              namespaces_.end(),
                              cls.namespace_name) == namespaces_.end()) {
                    namespaces_.push_back(cls.namespace_name);
                }
            }

            if (!IsInTargetNamespace(cls.namespace_name)) {
                return true;
            }

            ExtractSourceLocation(
                record->getBeginLoc(),
                &cls.source_file,
                &cls.source_line,
                &cls.source_col);

            classes_.push_back(cls);
            class_index_[class_name] = classes_.size() - 1;
            it = class_index_.find(class_name);
        }

        ClangMethodInfo method;
        method.name = func_name;
        method.qualified_name = qualified_name;
        clang::QualType ret_type = func->getReturnType();
        method.return_type = ConvertTypeInfo(ret_type.getTypePtr());
        method.is_const = false;
        method.is_static = false;

        if (const auto * method_decl = clang::dyn_cast<clang::CXXMethodDecl>(func)) {
            method.is_const = method_decl->isConst();
        }

        if (func->getStorageClass() == clang::SC_Static) {
            method.is_static = true;
        }
        method.is_public = func->getAccess() == clang::AS_public;

        for (const clang::ParmVarDecl * param : func->parameters()) {
            method.params.push_back(ConvertParamInfo(param));
        }

        ExtractSourceLocation(
            func->getBeginLoc(),
            &method.source_file,
            &method.source_line,
            &method.source_col);

        classes_[it->second].methods.push_back(method);
        function_index_[qualified_name] =
            classes_[it->second].methods.size() - 1;
    }

    return true;
}

bool ClangApiVisitor::VisitCXXRecordDecl(
    clang::CXXRecordDecl * record)
{
    if (!record || !record->isThisDeclarationADefinition()) {
        return true;
    }

    std::string class_name = record->getQualifiedNameAsString();

    if (class_index_.find(class_name) == class_index_.end()) {
        ClangClassInfo cls;
        cls.name = record->getNameAsString();
        cls.qualified_name = class_name;
        cls.namespace_name = "";

        const clang::DeclContext * ctx = record->getDeclContext();
        if (ctx->isNamespace()) {
            const clang::NamespaceDecl * ns =
                clang::cast<clang::NamespaceDecl>(ctx);
            cls.namespace_name = ns->getNameAsString();

            if (!cls.namespace_name.empty() &&
                std::find(namespaces_.begin(),
                          namespaces_.end(),
                          cls.namespace_name) == namespaces_.end()) {
                namespaces_.push_back(cls.namespace_name);
            }
        }

        if (!IsInTargetNamespace(cls.namespace_name)) {
            return true;
        }

        ExtractSourceLocation(
            record->getBeginLoc(),
            &cls.source_file,
            &cls.source_line,
            &cls.source_col);

        classes_.push_back(cls);
        class_index_[class_name] = classes_.size() - 1;
    }

    return true;
}

bool ClangApiVisitor::VisitCallExpr(clang::CallExpr * call)
{
    const clang::Expr * callee = call->getCallee()->IgnoreParenCasts();

    std::string callee_name;
    if (const auto * decl_ref = clang::dyn_cast<clang::DeclRefExpr>(callee)) {
        const clang::NamedDecl * decl = decl_ref->getDecl();
        if (decl) {
            callee_name = GetQualifiedName(decl);
        }
    } else if (const auto * member_expr =
               clang::dyn_cast<clang::MemberExpr>(callee)) {
        const clang::NamedDecl * decl = member_expr->getMemberDecl();
        if (decl) {
            callee_name = GetQualifiedName(decl);
        }
    }

    if (!callee_name.empty()) {
        ClangCallRef ref;
        ref.caller_name = current_function_name_;
        ref.callee_name = callee_name;

        ExtractSourceLocation(
            call->getBeginLoc(),
            &ref.source_file,
            &ref.source_line,
            &ref.source_col);

        call_refs_.push_back(ref);
    }

    return true;
}

bool ClangApiVisitor::VisitDeclRefExpr(clang::DeclRefExpr * ref)
{
    if (!ref) {
        return true;
    }

    const clang::NamedDecl * decl = ref->getDecl();
    if (!decl) {
        return true;
    }

    if (clang::isa<clang::FunctionDecl>(decl)) {
        std::string func_name = GetQualifiedName(decl);
        current_function_name_ = func_name;
    }

    return true;
}

bool ClangApiVisitor::VisitMemberExpr(clang::MemberExpr * member)
{
    if (!member) {
        return true;
    }

    const clang::NamedDecl * decl = member->getMemberDecl();
    if (!decl) {
        return true;
    }

    if (clang::isa<clang::FunctionDecl>(decl)) {
        std::string method_name = GetQualifiedName(decl);
        current_function_name_ = method_name;
    }

    return true;
}

ClangAstParseResult ClangApiVisitor::GetResult() const
{
    ClangAstParseResult result;
    result.success = true;
    result.schema.free_functions = free_functions_;
    result.schema.classes = classes_;
    result.schema.namespaces = namespaces_;
    result.call_refs = call_refs_;
    result.target_namespaces = target_namespaces_;
    return result;
}

ClangAstParseResult RunClangAstParserImpl(
    const ClangIndexerOptions & options)
{
    ClangAstParseResult result;

    if (options.source_file.empty()) {
        result.success = false;
        result.error = "No source file specified";
        return result;
    }

    std::vector<std::string> target_namespaces = options.target_namespaces;
    ClangAstParseResult parsed_result;

    struct ActionFactory {
        std::vector<std::string> target_namespaces;
        ClangAstParseResult * out_result;
        std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
            return std::make_unique<ClangApiConsumer>(
                nullptr, target_namespaces, out_result);
        }
    };

    ActionFactory factory;
    factory.target_namespaces = target_namespaces;
    factory.out_result = &parsed_result;

    std::vector<std::string> argv_storage;
    std::vector<const char *> argv;
    argv.push_back("clang");
    argv.push_back(options.source_file.c_str());
    argv.push_back("--");

    if (!options.extra_include_dirs.empty()) {
        for (const auto & inc : options.extra_include_dirs) {
            std::string arg = "-I" + inc;
            argv_storage.push_back(arg);
            argv.push_back(argv_storage.back().c_str());
        }
    }

    if (!options.extra_defines.empty()) {
        for (const auto & def : options.extra_defines) {
            std::string arg = "-D" + def;
            argv_storage.push_back(arg);
            argv.push_back(argv_storage.back().c_str());
        }
    }

    int argc = static_cast<int>(argv.size());
    llvm::cl::OptionCategory category("Clang AST Indexer");
    auto expected_parser =
        clang::tooling::CommonOptionsParser::create(
            argc,
            argv.data(),
            category);

    if (!expected_parser) {
        result.success = false;
        result.error = "Failed to create options parser";
        return result;
    }

    clang::tooling::CommonOptionsParser & op = expected_parser.get();

    std::unique_ptr<clang::tooling::CompilationDatabase> compilation_db;
    std::string compile_db_dir;
    std::string compile_db_file_path;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &compile_db_dir,
            &compile_db_file_path,
            &compile_db_error)) {
        result.success = false;
        result.error = compile_db_error;
        return result;
    }

    if (!compile_db_dir.empty()) {
        std::string err_msg;
        compilation_db =
            clang::tooling::CompilationDatabase::loadFromDirectory(
                compile_db_dir, err_msg);
        if (!compilation_db) {
            result.success = false;
            result.error = "Failed to load compilation database from: " +
                compile_db_dir + " - " + err_msg;
            return result;
        }
    }

    clang::tooling::ClangTool tool(
        compilation_db
            ? *compilation_db
            : op.getCompilations(),
        op.getSourcePathList());

    if (!compilation_db) {
        std::vector<std::string> std_args = {
            "-std=c++17",
            "-fsyntax-only"
        };
        tool.appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster(
                std_args,
                clang::tooling::ArgumentInsertPosition::BEGIN));
    }

    if (!options.extra_include_dirs.empty()) {
        std::vector<std::string> include_args;
        for (const auto & inc : options.extra_include_dirs) {
            include_args.push_back("-I" + inc);
        }
        tool.appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster(
                include_args,
                clang::tooling::ArgumentInsertPosition::BEGIN));
    }

    if (!options.extra_defines.empty()) {
        std::vector<std::string> define_args;
        for (const auto & def : options.extra_defines) {
            define_args.push_back("-D" + def);
        }
        tool.appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster(
                define_args,
                clang::tooling::ArgumentInsertPosition::BEGIN));
    }

#ifdef _WIN32
    if (!compilation_db) {
        std::vector<std::string> msvc_includes;
        if (llvm::sys::fs::exists("C:/Program Files/Microsoft Visual Studio")) {
            std::vector<std::string> versions = { "2022", "2019", "2017" };
            std::vector<std::string> editions = { "Community", "Professional", "Enterprise" };
            for (const auto & ver : versions) {
                for (const auto & ed : editions) {
                    std::string vc_path = "C:/Program Files/Microsoft Visual Studio/" +
                        ver + "/" + ed + "/VC/Tools/MSVC";
                    if (llvm::sys::fs::exists(vc_path)) {
                        std::error_code ec;
                        for (auto it = llvm::sys::fs::directory_iterator(vc_path, ec);
                             it != llvm::sys::fs::directory_iterator(); it.increment(ec)) {
                            std::string dir_path = it->path();
                            std::string include_path = dir_path + "/include";
                            if (llvm::sys::fs::exists(include_path)) {
                                msvc_includes.push_back(include_path);
                            }
                            std::string atlmfc_path = dir_path + "/atlmfc/include";
                            if (llvm::sys::fs::exists(atlmfc_path)) {
                                msvc_includes.push_back(atlmfc_path);
                            }
                        }
                        break;
                    }
                }
                if (!msvc_includes.empty()) break;
            }
        }

        std::string sdk_include = "C:/Program Files (x86)/Windows Kits/10/Include";
        if (llvm::sys::fs::exists(sdk_include)) {
            std::error_code ec;
            std::string latest_sdk;
            for (auto it = llvm::sys::fs::directory_iterator(sdk_include, ec);
                 it != llvm::sys::fs::directory_iterator(); it.increment(ec)) {
                std::string dir_path = it->path();
                std::string ver = llvm::sys::path::filename(dir_path).str();
                if (ver.size() >= 4 && ver[0] >= '0' && ver[0] <= '9') {
                    if (latest_sdk.empty() || ver > latest_sdk) {
                        latest_sdk = ver;
                    }
                }
            }
            if (!latest_sdk.empty()) {
                std::string ucrt_path = sdk_include + "/" + latest_sdk + "/ucrt";
                if (llvm::sys::fs::exists(ucrt_path)) {
                    msvc_includes.push_back(ucrt_path);
                }
                std::string um_path = sdk_include + "/" + latest_sdk + "/um";
                if (llvm::sys::fs::exists(um_path)) {
                    msvc_includes.push_back(um_path);
                }
                std::string shared_path = sdk_include + "/" + latest_sdk + "/shared";
                if (llvm::sys::fs::exists(shared_path)) {
                    msvc_includes.push_back(shared_path);
                }
            }
        }

        if (!msvc_includes.empty()) {
            std::vector<std::string> msvc_args;
            for (const auto & inc : msvc_includes) {
                msvc_args.push_back("-I" + inc);
            }
            tool.appendArgumentsAdjuster(
                clang::tooling::getInsertArgumentAdjuster(
                    msvc_args,
                    clang::tooling::ArgumentInsertPosition::BEGIN));
        }
    }
#endif

    int run_result = tool.run(
        clang::tooling::newFrontendActionFactory(&factory).get());

    if (run_result != 0) {
        result.success = false;
        result.error = "Clang AST parsing failed with code: " +
            std::to_string(run_result);
        return result;
    }

    result = parsed_result;
    return result;
}

}
