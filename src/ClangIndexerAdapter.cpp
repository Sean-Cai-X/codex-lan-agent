#include "ClangIndexerAdapter.h"
#include "ClangAstVisitor.h"
#include "ClangAstParser.h"
#include "comm.h"
#include "StructuredJsonOperations.h"
#include "JsonRequestView.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace codex_lan_agent
{
namespace
{
std::string QuoteArg(const std::string & value)
{
    if (value.empty()) {
        return "\"\"";
    }

    bool needs_quotes = false;
    for (char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
            ch == '"' || ch == '\0') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }

    std::string result = "\"";
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
        }
        result += ch;
    }
    result += '"';
    return result;
}

bool FileExists(const std::string & path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}
}

bool BuildClangIndexerCommand(
    const std::string & indexer_path,
    const ClangIndexerOptions & options,
    std::string * command_line,
    std::vector<std::string> * args)
{
    if (command_line == nullptr || args == nullptr)
        return false;

    if (indexer_path.empty() || options.source_file.empty())
        return false;

    args->clear();
    args->push_back("--source");
    args->push_back(options.source_file);

    if (!options.compile_db_dir.empty()) {
        args->push_back("--compile-db");
        args->push_back(options.compile_db_dir);
    }

    if (!options.output_json_path.empty()) {
        args->push_back("--output");
        args->push_back(options.output_json_path);
    }

    if (!options.project_root.empty()) {
        args->push_back("--project-root");
        args->push_back(options.project_root);
    }

    for (const auto & inc : options.extra_include_dirs) {
        args->push_back("--include-dir");
        args->push_back(inc);
    }

    for (const auto & def : options.extra_defines) {
        args->push_back("--define");
        args->push_back(def);
    }

    if (options.verbose)
        args->push_back("--verbose");

    std::ostringstream cmd;
    cmd << QuoteArg(indexer_path);
    for (const auto & arg : *args) {
        cmd << " " << QuoteArg(arg);
    }
    *command_line = cmd.str();
    return true;
}

ClangIndexerResult ParseClangIndexerOutput(
    const std::string & output_json,
    const std::string & error_message)
{
    ClangIndexerResult result;

    if (!error_message.empty()) {
        result.success = false;
        result.error = error_message;
        return result;
    }

    if (output_json.empty()) {
        result.success = false;
        result.error = "empty output from clang indexer";
        return result;
    }

    result.success = true;

    result.schema.module_name = ExtractJsonString(output_json, "module_name");

    std::string classes_json = ExtractJsonString(output_json, "classes");
    if (!classes_json.empty()) {
        result.schema.classes.push_back({});
        result.schema.classes.back().name = ExtractJsonString(classes_json, "name");
        result.schema.classes.back().qualified_name = ExtractJsonString(classes_json, "qualified_name");
        result.schema.classes.back().namespace_name = ExtractJsonString(classes_json, "namespace_name");
    }

    result.symbol_count = 0;
    result.ref_count = 0;
    std::string elapsed = ExtractJsonString(output_json, "elapsed_ms");
    if (!elapsed.empty()) {
        try {
            result.elapsed_ms = std::stoi(elapsed);
        } catch (...) {
            result.elapsed_ms = 0;
        }
    }

    return result;
}

std::string FindClangIndexerExecutablePath(
    const std::string & config_dir)
{
    std::vector<std::string> candidates;

    if (!config_dir.empty()) {
        candidates.push_back(config_dir + "\\cxparser_clang_indexer.exe");
        candidates.push_back(config_dir + "\\clang_indexer.exe");
        candidates.push_back(config_dir + "\\bin\\cxparser_clang_indexer.exe");
    }

    for (const auto & path : candidates) {
        if (FileExists(path)) {
            return path;
        }
    }

    return std::string();
}

ClangAstParseResult RunClangAstParser(
    const ClangIndexerOptions & options)
{
    ClangAstParseResult result = RunClangAstParserImpl(options);
    return result;
}

void SetClangAstResultCallback(
    ClangAstResultCallback callback,
    void * user_data)
{
    static ClangAstResultCallback s_callback = nullptr;
    static void * s_user_data = nullptr;
    s_callback = callback;
    s_user_data = user_data;
}
}

CommandResult BuildRunClangAstParserResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;
    
    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.output_json_path = params.GetString("output_json_path");
    options.project_root = params.GetString("project_root");
    options.verbose = params.GetBool("verbose", false);

    std::string include_dirs_json = params.GetString("extra_include_dirs");
    if (!include_dirs_json.empty()) {
        std::string cleaned = include_dirs_json;
        if (!cleaned.empty() && cleaned.front() == '[') {
            cleaned = cleaned.substr(1);
        }
        if (!cleaned.empty() && cleaned.back() == ']') {
            cleaned.pop_back();
        }
        std::istringstream iss(cleaned);
        std::string item;
        while (std::getline(iss, item, ',')) {
            while (!item.empty() && (item.front() == '"' || item.front() == ' ')) {
                item.erase(item.begin());
            }
            while (!item.empty() && (item.back() == '"' || item.back() == ' ')) {
                item.pop_back();
            }
            if (!item.empty()) {
                options.extra_include_dirs.push_back(item);
            }
        }
    }

    std::string defines_json = params.GetString("extra_defines");
    if (!defines_json.empty()) {
        std::string cleaned = defines_json;
        if (!cleaned.empty() && cleaned.front() == '[') {
            cleaned = cleaned.substr(1);
        }
        if (!cleaned.empty() && cleaned.back() == ']') {
            cleaned.pop_back();
        }
        std::istringstream iss(cleaned);
        std::string item;
        while (std::getline(iss, item, ',')) {
            while (!item.empty() && (item.front() == '"' || item.front() == ' ')) {
                item.erase(item.begin());
            }
            while (!item.empty() && (item.back() == '"' || item.back() == ' ')) {
                item.pop_back();
            }
            if (!item.empty()) {
                options.extra_defines.push_back(item);
            }
        }
    }

    if (options.source_file.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "source_file is required for Clang AST parsing";
        result.fields["result"] = "parse_blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_source_file";
        result.fields["summary"] = "Clang AST parse blocked: missing source_file";
        result.fields["next_action"] = "provide source_file path to parse";
        return result;
    }

    ClangAstParseResult ast_result = RunClangAstParser(options);

    if (!ast_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = ast_result.error.empty()
            ? "Clang AST parsing failed"
            : ast_result.error;
        result.fields["result"] = "parse_failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "clang_ast_parse_failed";
        result.fields["summary"] = "Clang AST parse failed";
        result.fields["error_detail"] = ast_result.error;
        return result;
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["result"] = "parse_success";
    result.fields["status"] = "completed";
    result.fields["tool"] = "clang_ast_indexer";
    result.fields["symbol_count"] = std::to_string(
        ast_result.schema.classes.size() +
        ast_result.schema.free_functions.size());
    result.fields["class_count"] = std::to_string(
        ast_result.schema.classes.size());
    result.fields["function_count"] = std::to_string(
        ast_result.schema.free_functions.size());
    result.fields["call_ref_count"] = std::to_string(
        ast_result.call_refs.size());
    result.fields["namespace_count"] = std::to_string(
        ast_result.schema.namespaces.size());
    result.fields["elapsed_ms"] = std::to_string(ast_result.elapsed_ms);

    std::string json_output = SerializeAstParseResultToJson(ast_result);
    result.fields["ast_json"] = json_output;

    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
    }

    result.fields["summary"] = "Clang AST parsed successfully: " +
        std::to_string(ast_result.schema.classes.size()) + " classes, " +
        std::to_string(ast_result.schema.free_functions.size()) + " functions, " +
        std::to_string(ast_result.call_refs.size()) + " call refs";

    result.fields["next_action"] = "Use ast_json field to access parsed API schema";

    return result;
}
