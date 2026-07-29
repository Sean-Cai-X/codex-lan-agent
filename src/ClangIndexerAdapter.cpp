#include "ClangIndexerAdapter.h"
#include "ClangAstVisitor.h"
#include "ClangAstParser.h"
#include "CfGBuilder.h"
#include "comm.h"
#include "StructuredJsonOperations.h"
#include "JsonRequestView.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

std::string NormalizePathString(const std::filesystem::path & path)
{
    std::error_code ec;
    const std::filesystem::path weak = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal().string() : weak.string();
}
}

bool ResolveCompilationDatabaseLocation(
    const ClangIndexerOptions & options,
    std::string * resolved_directory,
    std::string * resolved_file_path,
    std::string * error_message)
{
    if (resolved_directory != nullptr) {
        resolved_directory->clear();
    }
    if (resolved_file_path != nullptr) {
        resolved_file_path->clear();
    }
    if (error_message != nullptr) {
        error_message->clear();
    }

    std::string candidate = options.compilation_database_path;
    if (candidate.empty()) {
        candidate = options.compile_db_dir;
    }
    if (candidate.empty()) {
        return true;
    }

    std::error_code ec;
    const std::filesystem::path raw_path(candidate);
    if (!std::filesystem::exists(raw_path, ec) || ec) {
        if (error_message != nullptr) {
            *error_message = "compilation database path does not exist: " + candidate;
        }
        return false;
    }

    std::filesystem::path directory_path;
    std::filesystem::path file_path;
    if (std::filesystem::is_regular_file(raw_path, ec) && !ec) {
        if (raw_path.filename() != "compile_commands.json") {
            if (error_message != nullptr) {
                *error_message =
                    "compilation_database_path must point to compile_commands.json: " + candidate;
            }
            return false;
        }
        file_path = raw_path;
        directory_path = raw_path.parent_path();
    } else if (std::filesystem::is_directory(raw_path, ec) && !ec) {
        directory_path = raw_path;
        file_path = raw_path / "compile_commands.json";
        if (!std::filesystem::exists(file_path, ec) || ec) {
            if (error_message != nullptr) {
                *error_message =
                    "compile_commands.json was not found in directory: " + candidate;
            }
            return false;
        }
    } else {
        if (error_message != nullptr) {
            *error_message = "invalid compilation database path: " + candidate;
        }
        return false;
    }

    if (resolved_directory != nullptr) {
        *resolved_directory = NormalizePathString(directory_path);
    }
    if (resolved_file_path != nullptr) {
        *resolved_file_path = NormalizePathString(file_path);
    }
    return true;
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

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            nullptr)) {
        return false;
    }

    if (!resolved_compile_db_dir.empty()) {
        args->push_back("--compile-db");
        args->push_back(resolved_compile_db_dir);
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

CommandResult BuildRunClangAstParserResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;
    
    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.output_json_path = params.GetString("output_json_path");
    options.project_root = params.GetString("project_root");
    options.verbose = params.GetBool("verbose", false);

    std::string target_ns_json = params.GetString("target_namespaces");
    if (!target_ns_json.empty()) {
        std::string cleaned = target_ns_json;
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
                options.target_namespaces.push_back(item);
            }
        }
    }

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

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = compile_db_error;
        result.fields["result"] = "parse_blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "Clang AST parse blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
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

    std::string cxscript_output = BuildCxScriptFromSchema(ast_result.schema);
    result.fields["generated_cxscript"] = cxscript_output;

    CxScriptValidationResult validation = ValidateCxScriptSyntax(cxscript_output);
    result.fields["generated_cxscript_valid"] = validation.valid ? "true" : "false";
    result.fields["generated_cxscript_validation"] = SerializeCxScriptValidationToJson(validation);

    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["result"] = "parse_failed";
            result.fields["preflight_status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Clang AST parse failed while writing output_json_path";
            return result;
        }
        output << json_output;
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to write parser JSON to output_json_path";
            result.fields["result"] = "parse_failed";
            result.fields["preflight_status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Clang AST parse failed while writing output_json_path";
            return result;
        }
    }

    result.fields["summary"] = "Clang AST parsed successfully: " +
        std::to_string(ast_result.schema.classes.size()) + " classes, " +
        std::to_string(ast_result.schema.free_functions.size()) + " functions, " +
        std::to_string(ast_result.call_refs.size()) + " call refs";

    result.fields["next_action"] = validation.valid
        ? "generated_cxscript is syntactically valid CxScript. Use it as a template for writing cxsc test scripts."
        : "generated_cxscript has syntax issues. Check generated_cxscript_validation for details.";

    return result;
}

CommandResult BuildRunCfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;

    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.verbose = params.GetBool("verbose", false);
    options.output_json_path = params.GetString("output_json_path");

    std::string extra_includes_json = params.GetString("extra_include_dirs");
    if (!extra_includes_json.empty()) {
        std::string cleaned = extra_includes_json;
        if (!cleaned.empty() && cleaned.front() == '[') cleaned = cleaned.substr(1);
        if (!cleaned.empty() && cleaned.back() == ']') cleaned.pop_back();
        std::istringstream iss(cleaned);
        std::string token;
        while (std::getline(iss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t\""));
            token.erase(token.find_last_not_of(" \t\"") + 1);
            if (!token.empty()) {
                options.extra_include_dirs.push_back(token);
            }
        }
    }

    result.fields["source_file"] = options.source_file;
    result.fields["compile_db_dir"] = options.compile_db_dir;

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["error"] = compile_db_error;
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "CFG construction blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
    }

    CfgBuildResult cfg_result = RunCfgBuilder(options);

    if (!cfg_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = cfg_result.error;
        result.fields["summary"] = "CFG construction failed: " + cfg_result.error;
        result.fields["build_time_ms"] = std::to_string(cfg_result.build_time_ms);
        return result;
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["cfg_json"] = SerializeCfgBuildResultToJson(cfg_result);
    result.fields["cfg_dot"] = SerializeCfgToDot(cfg_result, params.GetString("function_name"));
    result.fields["total_functions"] = std::to_string(cfg_result.total_functions);
    result.fields["total_blocks"] = std::to_string(cfg_result.total_blocks);
    result.fields["total_edges"] = std::to_string(cfg_result.total_edges);
    result.fields["build_time_ms"] = std::to_string(cfg_result.build_time_ms);
    result.fields["preflight_status"] = "ready";
    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["status"] = "failed";
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["summary"] = "CFG construction failed while writing output_json_path";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            return result;
        }
        output << result.fields["cfg_json"];
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["status"] = "failed";
            result.fields["error"] = "failed to write cfg_json to output_json_path";
            result.fields["summary"] = "CFG construction failed while writing output_json_path";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            return result;
        }
    }

    std::ostringstream summary;
    summary << "CFG built: "
            << cfg_result.total_functions << " functions, "
            << cfg_result.total_blocks << " blocks, "
            << cfg_result.total_edges << " edges";
    result.fields["summary"] = summary.str();
    result.fields["next_action"] = "Use cfg_json for detailed block/edge structure. Build call graph next.";

    return result;
}
}

CommandResult BuildRunClangAstParserResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunClangAstParserResult(config, params);
}

CommandResult BuildRunCfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunCfgResult(config, params);
}
