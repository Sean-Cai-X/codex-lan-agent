#include "CfGBuilder.h"
#include "ClangAstVisitor.h"
#include "comm.h"
#include "StructuredJsonOperations.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <queue>
#include <stack>
#include <set>
#include <map>
#ifdef _WIN32
#include <cstdlib>
#endif
#include <filesystem>
#include <system_error>

namespace codex_lan_agent
{
namespace
{

std::string EscapeJsonString(const std::string & input)
{
    std::string result;
    result.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}

std::string ExtractStatementText(const clang::Stmt * stmt)
{
    if (!stmt) {
        return "<null>";
    }
    std::string text;
    llvm::raw_string_ostream os(text);
    stmt->printPretty(os, nullptr, clang::PrintingPolicy(clang::LangOptions()));
    os.flush();
    if (text.size() > 120) {
        text = text.substr(0, 117) + "...";
    }
    return text;
}

int ExtractSourceLine(clang::ASTContext & ctx, const clang::Stmt * stmt)
{
    if (!stmt) return 0;
    auto src_loc = stmt->getBeginLoc();
    if (src_loc.isValid()) {
        const auto & sm = ctx.getSourceManager();
        return sm.getSpellingLineNumber(src_loc);
    }
    return 0;
}

std::string ExtractSourceFile(clang::ASTContext & ctx, const clang::Stmt * stmt)
{
    if (!stmt) return "";
    auto src_loc = stmt->getBeginLoc();
    if (src_loc.isValid()) {
        const auto & sm = ctx.getSourceManager();
        auto file_id = sm.getFileID(src_loc);
        if (const auto * entry = sm.getFileEntryForID(file_id)) {
            return entry->getName().str();
        }
    }
    return "";
}

std::string GetBlockTypeLabel(const clang::CFGBlock & block)
{
    if (&block == &block.getParent()->getEntry()) {
        return "ENTRY";
    }
    if (&block == &block.getParent()->getExit()) {
        return "EXIT";
    }
    if (block.succ_empty()) {
        return "TERMINAL";
    }
    return "BASIC";
}

std::string NormalizeComparablePath(const std::string & value)
{
    if (value.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path path(value);
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        normalized = path.lexically_normal();
    }

    std::string result = normalized.generic_string();
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return result;
}

struct CfgExtractionVisitor : public clang::RecursiveASTVisitor<CfgExtractionVisitor>
{
    clang::ASTContext * ctx_ = nullptr;
    std::vector<CfgFunctionInfo> functions_;
    std::string source_file_;
    std::string target_source_file_;
    std::set<std::string> emitted_function_keys_;

    bool VisitFunctionDecl(clang::FunctionDecl * func)
    {
        if (!func || !func->hasBody()) {
            return true;
        }

        clang::Stmt * body = func->getBody();
        if (!body) {
            return true;
        }

        if (!target_source_file_.empty()) {
            const auto & sm = ctx_->getSourceManager();
            const clang::SourceLocation decl_loc = sm.getExpansionLoc(func->getLocation());
            const clang::SourceLocation body_begin = sm.getExpansionLoc(body->getBeginLoc());
            if (!sm.isWrittenInMainFile(decl_loc) && !sm.isWrittenInMainFile(body_begin)) {
                return true;
            }

            clang::SourceLocation body_loc = sm.getSpellingLoc(body->getBeginLoc());
            if (!body_loc.isValid()) {
                return true;
            }
            auto file_id = sm.getFileID(body_loc);
            const auto * entry = sm.getFileEntryForID(file_id);
            if (entry == nullptr) {
                return true;
            }
            const std::string body_file = NormalizeComparablePath(entry->getName().str());
            if (body_file != target_source_file_) {
                return true;
            }
        }

        CfgFunctionInfo info;
        info.function_name = func->getNameAsString();
        info.qualified_name = func->getQualifiedNameAsString();

        const clang::DeclContext * ctx = func->getDeclContext();
        if (ctx->isNamespace()) {
            info.namespace_name =
                clang::cast<clang::NamespaceDecl>(ctx)->getNameAsString();
        } else if (ctx->isRecord()) {
            info.namespace_name =
                clang::cast<clang::RecordDecl>(ctx)->getNameAsString();
        }

        info.source_file = ExtractSourceFile(*ctx_, body);
        info.source_line = ExtractSourceLine(*ctx_, body);

        const std::string function_key =
            info.qualified_name + "@" + NormalizeComparablePath(info.source_file) +
            ":" + std::to_string(info.source_line);
        if (emitted_function_keys_.find(function_key) != emitted_function_keys_.end()) {
            return true;
        }
        emitted_function_keys_.insert(function_key);

        clang::CFG::BuildOptions opts;
        opts.PruneTriviallyFalseEdges = true;
        opts.AddLoopExit = true;

        std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
            func, body, ctx_, opts);

        if (!cfg) {
            functions_.push_back(info);
            return true;
        }

        std::map<const clang::CFGBlock *, int> block_id_map;
        int next_id = 0;

        auto get_block_id = [&](const clang::CFGBlock * block) -> int {
            auto it = block_id_map.find(block);
            if (it != block_id_map.end()) {
                return it->second;
            }
            int id = next_id++;
            block_id_map[block] = id;
            return id;
        };

        std::queue<const clang::CFGBlock *> bfs_queue;
        std::set<const clang::CFGBlock *> visited;
        std::map<int, CfGBlockInfo> block_info_map;

        const clang::CFGBlock & entry = cfg->getEntry();
        const clang::CFGBlock & exit = cfg->getExit();

        bfs_queue.push(&entry);
        visited.insert(&entry);

        std::map<int, int> predecessor_count;

        while (!bfs_queue.empty()) {
            const clang::CFGBlock * block = bfs_queue.front();
            bfs_queue.pop();

            int block_id = get_block_id(block);
            CfGBlockInfo bi;
            bi.block_id = block_id;
            bi.is_entry = (block == &entry);
            bi.is_exit = (block == &exit);
            bi.block_type = GetBlockTypeLabel(*block);

            if (bi.is_entry) {
                bi.label = "Entry";
            } else if (bi.is_exit) {
                bi.label = "Exit";
            } else {
                bi.label = "BB" + std::to_string(block_id);
            }

            if (block->size() > 0) {
                const auto & last_elem = (*block)[block->size() - 1];
                if (last_elem.getKind() == clang::CFGElement::Statement) {
                    const clang::Stmt * stmt = last_elem.castAs<clang::CFGStmt>()
                        .getStmt();
                    bi.source_line = ExtractSourceLine(*ctx_, stmt);
                    bi.source_file = ExtractSourceFile(*ctx_, stmt);
                }
            }

            for (auto elem = block->begin(); elem != block->end(); ++elem) {
                if (elem->getKind() == clang::CFGElement::Statement) {
                    const clang::Stmt * stmt = elem->castAs<clang::CFGStmt>()
                        .getStmt();
                    bi.statements.push_back(ExtractStatementText(stmt));
                }
            }

            for (auto succ_it = block->succ_begin();
                 succ_it != block->succ_end(); ++succ_it) {
                if (*succ_it == nullptr) {
                    continue;
                }
                int succ_id = get_block_id(*succ_it);
                bi.successor_ids.push_back(succ_id);

                auto pred_it = predecessor_count.find(succ_id);
                if (pred_it == predecessor_count.end()) {
                    predecessor_count[succ_id] = 1;
                } else {
                    predecessor_count[succ_id]++;
                }

                if (visited.find(*succ_it) == visited.end()) {
                    visited.insert(*succ_it);
                    bfs_queue.push(*succ_it);
                }
            }

            block_info_map[block_id] = bi;
        }

        for (auto & [bid, bi] : block_info_map) {
            auto pit = predecessor_count.find(bid);
            int pred_count = (pit != predecessor_count.end()) ? pit->second : 0;
            for (int i = 0; i < pred_count; ++i) {
                bi.predecessor_ids.push_back(-1);
            }
            bi.predecessor_ids.resize(pred_count);
        }

        info.entry_block_id = get_block_id(&entry);
        info.exit_block_id = get_block_id(&exit);
        info.block_count = static_cast<int>(block_info_map.size());

        int edge_count = 0;
        int branch_count = 0;
        for (auto & [bid, bi] : block_info_map) {
            edge_count += static_cast<int>(bi.successor_ids.size());
            if (bi.successor_ids.size() > 1) {
                branch_count++;
            }
        }
        info.edge_count = edge_count;
        info.branch_count = branch_count;
        info.cyclomatic_complexity = static_cast<float>(edge_count - block_info_map.size() + 2);

        std::vector<int> bfs_order;
        std::set<int> bfs_visited;
        std::queue<int> id_queue;
        id_queue.push(info.entry_block_id);
        bfs_visited.insert(info.entry_block_id);
        while (!id_queue.empty()) {
            int curr = id_queue.front();
            id_queue.pop();
            bfs_order.push_back(curr);
            auto it = block_info_map.find(curr);
            if (it != block_info_map.end()) {
                for (int succ : it->second.successor_ids) {
                    if (bfs_visited.find(succ) == bfs_visited.end()) {
                        bfs_visited.insert(succ);
                        id_queue.push(succ);
                    }
                }
            }
        }

        for (int bid : bfs_order) {
            auto it = block_info_map.find(bid);
            if (it != block_info_map.end()) {
                info.blocks.push_back(it->second);
            }
        }

        info.has_cycle = (edge_count > block_info_map.size());

        functions_.push_back(info);
        return true;
    }
};

class CfgAstConsumer : public clang::ASTConsumer
{
public:
    explicit CfgAstConsumer(CfgExtractionVisitor * visitor,
                            ClangAstParseResult * out_result = nullptr)
        : visitor_(visitor), out_result_(out_result) {}

    void HandleTranslationUnit(clang::ASTContext & ctx) override
    {
        visitor_->ctx_ = &ctx;
        visitor_->TraverseDecl(ctx.getTranslationUnitDecl());
    }

private:
    CfgExtractionVisitor * visitor_;
    ClangAstParseResult * out_result_;
};

class CfgFrontendAction : public clang::ASTFrontendAction
{
public:
    explicit CfgFrontendAction(CfgExtractionVisitor * visitor)
        : visitor_(visitor) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance & compiler,
        llvm::StringRef in_file) override
    {
        compiler.getLangOpts().CPlusPlus = true;
        compiler.getLangOpts().CPlusPlus17 = true;
        return std::make_unique<CfgAstConsumer>(visitor_);
    }

private:
    CfgExtractionVisitor * visitor_;
};

struct CfgActionFactory {
    CfgExtractionVisitor * visitor = nullptr;
    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
        return std::make_unique<CfgAstConsumer>(visitor);
    }
};

}

CfgBuildResult RunCfgBuilder(const ClangIndexerOptions & options)
{
    CfgBuildResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    try {

    if (options.source_file.empty()) {
        result.error = "source_file is required for CFG building";
        return result;
    }

    if (!std::filesystem::exists(options.source_file)) {
        result.error = "source_file does not exist: " + options.source_file;
        return result;
    }

    std::string compile_db_dir;
    std::string compile_db_file_path;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &compile_db_dir,
            &compile_db_file_path,
            &compile_db_error)) {
        result.error = compile_db_error;
        return result;
    }

    std::unique_ptr<clang::tooling::CompilationDatabase> compilation_db;
    if (!compile_db_dir.empty()) {
        std::string err_msg;
        compilation_db = clang::tooling::CompilationDatabase::loadFromDirectory(
            compile_db_dir, err_msg);
        if (!compilation_db) {
            result.error = "Failed to load compilation database from: " +
                compile_db_dir + " - " + err_msg;
            return result;
        }
    }

    const bool use_fallback_arguments = !compilation_db;
    std::vector<std::string> std_args;
    if (use_fallback_arguments) {
        std_args.push_back("-std=c++17");
        std_args.push_back("-fsyntax-only");
        std_args.push_back("-Wno-everything");
    }

#ifdef _WIN32
    if (use_fallback_arguments) {
        _putenv_s("CPATH", "");
        _putenv_s("C_INCLUDE_PATH", "");
        _putenv_s("CPLUS_INCLUDE_PATH", "");
        _putenv_s("INCLUDE", "");

        char * path_env = getenv("PATH");
        if (path_env) {
            std::string sanitized_path;
            std::string path_str(path_env);
            size_t pos = 0;
            while (pos < path_str.size()) {
                size_t semicolon = path_str.find(';', pos);
                std::string entry;
                if (semicolon == std::string::npos) {
                    entry = path_str.substr(pos);
                    pos = path_str.size();
                } else {
                    entry = path_str.substr(pos, semicolon - pos);
                    pos = semicolon + 1;
                }
                bool skip = false;
                if (entry.find("\\\\?\\") != std::string::npos) skip = true;
                if (entry.find("TRAESOLOCN") != std::string::npos) skip = true;
                if (entry.find("ripgrep") != std::string::npos) skip = true;
                if (entry.find("Visual Studio") != std::string::npos) skip = true;
                if (entry.find("Windows Kits") != std::string::npos) skip = true;
                if (!skip && !entry.empty()) {
                    if (!sanitized_path.empty()) {
                        sanitized_path += ";";
                    }
                    sanitized_path += entry;
                }
            }
            _putenv_s("PATH", sanitized_path.c_str());
        }

        std_args.push_back("-nostdinc");
        std_args.push_back("-target");
        std_args.push_back("x86_64-pc-windows-msvc");
        std_args.push_back("-fno-ms-compatibility");
        std_args.push_back("-D_MSC_VER=1900");
        std_args.push_back("-DWIN32");
        std_args.push_back("-D_WINDOWS");
        std_args.push_back("-DUNICODE");
        std_args.push_back("-D_UNICODE");

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

        for (const auto & inc : msvc_includes) {
            std_args.push_back("-I");
            std_args.push_back(inc);
        }
    }
#endif

    std::string project_root;
    std::filesystem::path src_path(options.source_file);
    if (src_path.has_parent_path()) {
        project_root = src_path.parent_path().parent_path().string();
    }
    if (!project_root.empty()) {
        std::string llvm_include = project_root + "/third_party/llvm/include";
        if (std::filesystem::exists(llvm_include)) {
            std_args.push_back("-I");
            std_args.push_back(llvm_include);
        }
        std::string src_include = project_root + "/src";
        if (std::filesystem::exists(src_include)) {
            std_args.push_back("-I");
            std_args.push_back(src_include);
        }
    }

    if (use_fallback_arguments) {
        for (const auto & dir : options.extra_include_dirs) {
            std_args.push_back("-I");
            std_args.push_back(dir);
        }

        for (const auto & define : options.extra_defines) {
            std_args.push_back("-D" + define);
        }
    }

    std::vector<std::string> source_files = { options.source_file };

    std::unique_ptr<clang::tooling::FixedCompilationDatabase> fixed_db;
    if (use_fallback_arguments) {
        std::vector<const char *> fake_argv;
        fake_argv.push_back("clang_tool");
        fake_argv.push_back("--");
        for (const auto & arg : std_args) {
            fake_argv.push_back(arg.c_str());
        }
        int fake_argc = static_cast<int>(fake_argv.size());
        std::string err_msg;
        fixed_db = clang::tooling::FixedCompilationDatabase::loadFromCommandLine(
            fake_argc, fake_argv.data(), err_msg);
        if (!fixed_db) {
            result.error = "Failed to create FixedCompilationDatabase: " + err_msg;
            return result;
        }
    }

    clang::tooling::ClangTool tool(
        compilation_db
            ? *compilation_db
            : *fixed_db,
        source_files);

    if (compilation_db) {
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
    }

    CfgExtractionVisitor visitor;
    visitor.target_source_file_ = NormalizeComparablePath(options.source_file);
    CfgActionFactory factory;
    factory.visitor = &visitor;

    int exit_code = tool.run(
        clang::tooling::newFrontendActionFactory(&factory).get());

    auto end_time = std::chrono::high_resolution_clock::now();
    result.build_time_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time).count();

    if (exit_code != 0) {
        result.error = "ClangTool exited with code " + std::to_string(exit_code);
        return result;
    }

    result.success = true;
    result.source_file = options.source_file;
    result.functions = visitor.functions_;
    result.total_functions = static_cast<int>(visitor.functions_.size());

    int total_blocks = 0;
    int total_edges = 0;
    for (const auto & func : result.functions) {
        total_blocks += func.block_count;
        total_edges += func.edge_count;
    }
    result.total_blocks = total_blocks;
    result.total_edges = total_edges;

    return result;

    } catch (const std::exception & e) {
        result.error = std::string("CFG builder exception: ") + e.what();
        return result;
    } catch (...) {
        result.error = "CFG builder unknown exception";
        return result;
    }
}

std::string SerializeCfgBuildResultToJson(const CfgBuildResult & result)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"success\":" << (result.success ? "true" : "false") << ",";
    oss << "\"error\":\"" << EscapeJsonString(result.error) << "\",";
    oss << "\"source_file\":\"" << EscapeJsonString(result.source_file) << "\",";
    oss << "\"build_time_ms\":" << std::fixed << result.build_time_ms << ",";
    oss << "\"total_functions\":" << result.total_functions << ",";
    oss << "\"total_blocks\":" << result.total_blocks << ",";
    oss << "\"total_edges\":" << result.total_edges << ",";
    oss << "\"functions\":[";

    for (size_t fi = 0; fi < result.functions.size(); ++fi) {
        if (fi > 0) oss << ",";
        const auto & func = result.functions[fi];
        oss << "{";
        oss << "\"function_name\":\"" << EscapeJsonString(func.function_name) << "\",";
        oss << "\"qualified_name\":\"" << EscapeJsonString(func.qualified_name) << "\",";
        oss << "\"namespace_name\":\"" << EscapeJsonString(func.namespace_name) << "\",";
        oss << "\"source_file\":\"" << EscapeJsonString(func.source_file) << "\",";
        oss << "\"source_line\":" << func.source_line << ",";
        oss << "\"entry_block_id\":" << func.entry_block_id << ",";
        oss << "\"exit_block_id\":" << func.exit_block_id << ",";
        oss << "\"block_count\":" << func.block_count << ",";
        oss << "\"edge_count\":" << func.edge_count << ",";
        oss << "\"branch_count\":" << func.branch_count << ",";
        oss << "\"cyclomatic_complexity\":" << std::fixed << func.cyclomatic_complexity << ",";
        oss << "\"has_cycle\":" << (func.has_cycle ? "true" : "false") << ",";
        oss << "\"blocks\":[";

        for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
            if (bi > 0) oss << ",";
            const auto & blk = func.blocks[bi];
            oss << "{";
            oss << "\"block_id\":" << blk.block_id << ",";
            oss << "\"block_type\":\"" << EscapeJsonString(blk.block_type) << "\",";
            oss << "\"label\":\"" << EscapeJsonString(blk.label) << "\",";
            oss << "\"is_entry\":" << (blk.is_entry ? "true" : "false") << ",";
            oss << "\"is_exit\":" << (blk.is_exit ? "true" : "false") << ",";
            oss << "\"source_line\":" << blk.source_line << ",";
            oss << "\"source_file\":\"" << EscapeJsonString(blk.source_file) << "\",";

            oss << "\"statements\":[";
            for (size_t si = 0; si < blk.statements.size(); ++si) {
                if (si > 0) oss << ",";
                oss << "\"" << EscapeJsonString(blk.statements[si]) << "\"";
            }
            oss << "],";

            oss << "\"successor_ids\":[";
            for (size_t si = 0; si < blk.successor_ids.size(); ++si) {
                if (si > 0) oss << ",";
                oss << blk.successor_ids[si];
            }
            oss << "],";

            oss << "\"predecessor_ids\":[";
            for (size_t si = 0; si < blk.predecessor_ids.size(); ++si) {
                if (si > 0) oss << ",";
                oss << blk.predecessor_ids[si];
            }
            oss << "]";

            oss << "}";
        }
        oss << "]";
        oss << "}";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

std::string EscapeDotString(const std::string & input)
{
    std::string result;
    result.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}

std::string SerializeCfgToDot(
    const CfgBuildResult & result,
    const std::string & function_name)
{
    std::ostringstream oss;

    oss << "digraph CFG {\n";
    oss << "    rankdir=TB;\n";
    oss << "    node [shape=box, style=filled, fillcolor=white, fontname=\"Courier\"];\n";
    oss << "    edge [fontname=\"Courier\"];\n\n";

    int func_index = 0;
    auto write_function = [&](const CfgFunctionInfo & func) {
        std::string safe_name = func.qualified_name;
        std::replace(safe_name.begin(), safe_name.end(), ':', '_');
        std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
        std::string cluster_name = "cluster_" + safe_name + "_" + std::to_string(func_index);
        int current_func_idx = func_index;
        func_index++;

        oss << "    subgraph " << cluster_name << " {\n";
        oss << "        label=\"" << EscapeDotString(func.qualified_name) << "\\n"
            << "Blocks: " << func.block_count
            << " | Edges: " << func.edge_count
            << " | CC: " << func.cyclomatic_complexity << "\";\n";
        oss << "        style=filled;\n";
        oss << "        color=lightgrey;\n";
        oss << "        fillcolor=lightyellow;\n\n";

        for (const auto & blk : func.blocks) {
            std::string node_id = "node_" + safe_name + "_" + std::to_string(current_func_idx) + "_" + std::to_string(blk.block_id);
            std::string label = blk.label + "\\n";
            
            if (!blk.statements.empty()) {
                for (size_t si = 0; si < blk.statements.size() && si < 3; ++si) {
                    if (si > 0) label += "\\n";
                    std::string stmt = blk.statements[si];
                    if (stmt.size() > 40) {
                        stmt = stmt.substr(0, 37) + "...";
                    }
                    label += EscapeDotString(stmt);
                }
                if (blk.statements.size() > 3) {
                    label += "\\n...";
                }
            } else {
                label += "(empty)";
            }

            std::string color = "white";
            std::string shape = "box";
            if (blk.is_entry) {
                color = "lightgreen";
                shape = "ellipse";
            } else if (blk.is_exit) {
                color = "salmon";
                shape = "ellipse";
            } else if (blk.successor_ids.size() > 1) {
                color = "lightblue";
            }

            oss << "        " << node_id
                << " [label=\"" << label << "\""
                << ", shape=" << shape
                << ", fillcolor=" << color
                << "];\n";
        }

        oss << "    }\n\n";

        for (const auto & blk : func.blocks) {
            std::string from_id = "node_" + safe_name + "_" + std::to_string(current_func_idx) + "_" + std::to_string(blk.block_id);
            for (int succ_id : blk.successor_ids) {
                std::string to_id = "node_" + safe_name + "_" + std::to_string(current_func_idx) + "_" + std::to_string(succ_id);
                oss << "    " << from_id << " -> " << to_id << ";\n";
            }
        }
        oss << "\n";
    };

    if (!function_name.empty()) {
        for (const auto & func : result.functions) {
            if (func.function_name == function_name ||
                func.qualified_name.find(function_name) != std::string::npos) {
                write_function(func);
            }
        }
    } else {
        for (const auto & func : result.functions) {
            write_function(func);
        }
    }

    oss << "}\n";
    return oss.str();
}

}
