#pragma once

#include "AgentConfig.h"
#include "HttpClient.h"
#include "ProcessRunner.h"
#include "types.h"

#include <cstddef>
#include <filesystem>
#include <list>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

std::vector<std::string> SplitLinesPreserveText(const std::string & text);

std::string NormalizeLocalAiPrimaryIntent(const std::string & primary_intent) {
    const std::string lowered = ToLowerAscii(Trim(primary_intent));
    if (lowered == "delete comments"
        || lowered == "remove comments"
        || lowered == "strip comments"
        || lowered == "comment cleanup"
        || lowered == "cleanup comments"
        || lowered == "comment_cleanup"
        || lowered == "delete_comments"
        || lowered == "remove_comments"
        || lowered == "strip_comments"
        || lowered == "删除注释"
        || lowered == "清理注释"
        || lowered == "去除注释"
        || lowered == "移除注释"
        || lowered == "删注释") {
        return "comment_cleanup";
    }
    if (lowered == "format code"
        || lowered == "code format"
        || lowered == "format_code"
        || lowered == "code_format"
        || lowered == "formatting"
        || lowered == "whitespace_cleanup"
        || lowered == "whitespace cleanup"
        || lowered == "newline_cleanup"
        || lowered == "newline cleanup"
        || lowered == "remove extra newlines"
        || lowered == "delete extra newlines"
        || lowered == "删除多余回车换行"
        || lowered == "删除多余的回车换行"
        || lowered == "清理多余回车换行"
        || lowered == "清理空白"
        || lowered == "格式化代码") {
        return "code_format";
    }
    return primary_intent;
}

bool IsLocalAiCommentCleanupIntent(const std::string & primary_intent) {
    return NormalizeLocalAiPrimaryIntent(primary_intent) == "comment_cleanup";
}

bool IsLocalAiCodeFormatIntent(const std::string & primary_intent) {
    return NormalizeLocalAiPrimaryIntent(primary_intent) == "code_format";
}

bool IsLocalAiCodeSourceFilePath(const std::string & file_path) {
    const std::string extension = ToLowerAscii(std::filesystem::path(file_path).extension().string());
    return extension == ".cpp"
        || extension == ".cxx"
        || extension == ".cc"
        || extension == ".c"
        || extension == ".hpp"
        || extension == ".hh"
        || extension == ".h"
        || extension == ".ipp";
}

std::string BuildCommentCleanupProbeCallJson(
    const std::string & file_path,
    const std::string & trace_id,
    const std::string & directory_manifest_path = std::string(),
    int directory_current_file_index = 0,
    int directory_total_code_file_count = 0) {
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_probe_text_file\",\"arguments\":{\"file_path\":\""
           << codex_lan_agent::JsonEscape(file_path)
           << "\",\"primary_intent\":\"comment_cleanup\"";
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!directory_manifest_path.empty()) {
        output << ",\"directory_manifest_path\":\"" << codex_lan_agent::JsonEscape(directory_manifest_path) << "\""
               << ",\"directory_current_file_index\":" << std::max(0, directory_current_file_index)
               << ",\"directory_total_code_file_count\":" << std::max(0, directory_total_code_file_count)
               << ",\"directory_scope_active\":true";
    }
    output << "}}";
    return output.str();
}

std::string ReadCodeFilePathFromDirectoryManifest(
    const std::string & directory_manifest_path,
    int code_file_index) {
    if (Trim(directory_manifest_path).empty() || code_file_index < 0) {
        return std::string();
    }
    std::ifstream input;
    input.open(std::filesystem::path(directory_manifest_path), std::ios::in);
    if (!input.is_open()) {
        return std::string();
    }
    std::string line;
    int current_code_index = 0;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (!IsLocalAiCodeSourceFilePath(line)) {
            continue;
        }
        if (current_code_index == code_file_index) {
            return line;
        }
        ++current_code_index;
    }
    return std::string();
}

std::string BuildJsonStringArrayFromStrings(const std::vector<std::string> & values) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "\"" << codex_lan_agent::JsonEscape(values[index]) << "\"";
    }
    output << "]";
    return output.str();
}

constexpr std::size_t kStructuredBodyPageByteLimit = 64 * 1024;

std::vector<std::filesystem::path> BuildDirectoryAccessHelperCandidates(const AgentConfig & config) {
#ifdef _WIN32
    const std::string executable_name = "directory_access.exe";
#else
    const std::string executable_name = "directory_access";
#endif
    const std::filesystem::path config_dir(config.config_dir);
    const std::filesystem::path workspace_root(config.workspace_root);
    return {
        config_dir / executable_name,
        config_dir / "Release" / executable_name,
        config_dir / "Debug" / executable_name,
        config_dir / "RelWithDebInfo" / executable_name,
        config_dir / "build" / executable_name,
        config_dir / "build" / "Release" / executable_name,
        config_dir / "build" / "Debug" / executable_name,
        config_dir / "build" / "RelWithDebInfo" / executable_name,
        workspace_root / "codex-lan-agent" / executable_name,
        workspace_root / "codex-lan-agent" / "Release" / executable_name,
        workspace_root / "codex-lan-agent" / "Debug" / executable_name,
        workspace_root / "codex-lan-agent" / "RelWithDebInfo" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / "Release" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / "Debug" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / "RelWithDebInfo" / executable_name
    };
}

std::string FindDirectoryAccessHelperPath(
    const AgentConfig & config,
    std::string * searched_paths_json = nullptr) {
    const std::vector<std::filesystem::path> candidates = BuildDirectoryAccessHelperCandidates(config);
    std::ostringstream searched;
    searched << "[";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) {
            searched << ",";
        }
        searched << "\"" << codex_lan_agent::JsonEscape(candidates[index].string()) << "\"";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidates[index], ec) && !ec) {
            if (searched_paths_json != nullptr) {
                searched << "]";
                *searched_paths_json = searched.str();
            }
            return candidates[index].string();
        }
    }
    searched << "]";
    if (searched_paths_json != nullptr) {
        *searched_paths_json = searched.str();
    }
    return std::string();
}

std::string ExtractHelperKvValue(const std::string & text, const std::string & key) {
    const std::string prefix = key + "=";
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return Trim(line.substr(prefix.size()));
        }
    }
    return std::string();
}

std::string ExtractHelperContentBlock(const std::string & text) {
    const std::string begin_marker = "content_begin<<<";
    const std::string end_marker = ">>>content_end";
    const std::size_t begin = text.find(begin_marker);
    if (begin == std::string::npos) {
        return std::string();
    }
    std::size_t content_begin = begin + begin_marker.size();
    if (content_begin < text.size() && text[content_begin] == '\r') {
        ++content_begin;
    }
    if (content_begin < text.size() && text[content_begin] == '\n') {
        ++content_begin;
    }

    const std::size_t end = text.find(end_marker, content_begin);
    if (end == std::string::npos) {
        return text.substr(content_begin);
    }
    std::size_t content_end = end;
    while (content_end > content_begin &&
           (text[content_end - 1] == '\r' || text[content_end - 1] == '\n')) {
        --content_end;
    }
    return text.substr(content_begin, content_end - content_begin);
}

std::string QuoteDirectoryAccessArgument(const std::string & value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "\"";
    return quoted;
}
std::string BuildDirectoryAccessHelperLogToken() {
    static std::mutex mutex;
    static unsigned long long next_id = 1;
    std::lock_guard<std::mutex> lock(mutex);
    return SanitizeDispatchToken(
        "helper-" + CommOperations::TimeStampForFileName() + "-" + std::to_string(next_id++),
        "helper");
}

CommandResult RunDirectoryAccessHelper(
    const AgentConfig & config,
    const std::string & mode,
    const std::vector<std::string> & arguments,
    const std::string & log_label) {
    CommandResult result;
    std::string searched_paths_json;
    const std::string helper_path = FindDirectoryAccessHelperPath(config, &searched_paths_json);
    result.fields["directory_access_helper_paths_json"] = searched_paths_json;
    if (helper_path.empty()) {
        result.ok = false;
        result.exit_code = 90;
        result.fields["error"] = "directory_access helper not found";
        return result;
    }

    std::ostringstream command_line;
    command_line << "\"" << helper_path << "\""
                 << " --mode \"" << mode << "\"";
    for (const std::string &argument : arguments) {
        command_line << " " << argument;
    }

    const std::string helper_log_token =
        BuildDirectoryAccessHelperLogToken();
    const std::string log_path = BuildLogPath(config, log_label + "_" + helper_log_token);
    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line.str(),
            std::string(),
            log_path,
            120,
            60,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 91;
        result.fields["error"] = error_message;
        result.fields["log_path"] = log_path;
        return result;
    }

    std::string helper_output;
    std::string read_error;
    if (!ReadWholeFile(log_path, &helper_output, &read_error)) {
        result.ok = false;
        result.exit_code = 92;
        result.fields["error"] = read_error;
        result.fields["log_path"] = log_path;
        return result;
    }

    result.ok = run_result.exit_code == 0;
    result.exit_code = run_result.exit_code;
    result.fields["directory_access_helper_path"] = helper_path;
    result.fields["directory_access_helper_mode"] = mode;
    result.fields["directory_access_helper_log_path"] = log_path;
    result.fields["directory_access_helper_output"] = helper_output;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";
    return result;
}

std::string BuildDirectoryReadBatchRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "directory_read_batches");
}

std::string BuildDirectoryAnalysisBundleRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "directory_analysis_bundles");
}

std::string BuildDirectoryAnalysisBundlePath(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_directory_path) {
    const std::string trace_token = !trace_id.empty()
        ? SanitizeDispatchToken(trace_id, "trace")
        : SanitizeDispatchToken(std::filesystem::path(normalized_directory_path).filename().string(), "directory");
    return codex_lan_agent::JoinPath(
        BuildDirectoryAnalysisBundleRoot(config),
        "directory_analysis_bundle_" + trace_token + ".txt");
}


std::vector<std::filesystem::path> BuildOptfileHelperCandidates(const AgentConfig & config) {
#ifdef _WIN32
    const std::string executable_name = "optfile.exe";
#else
    const std::string executable_name = "optfile";
#endif
    const std::filesystem::path config_dir(config.config_dir);
    const std::filesystem::path workspace_root(config.workspace_root);
    return {
        config_dir / executable_name,
        config_dir / "optfile" / executable_name,
        config_dir / "optfile" / "Release" / executable_name,
        config_dir / "optfile" / "Debug" / executable_name,
        config_dir / "Release" / executable_name,
        config_dir / "Debug" / executable_name,
        workspace_root / "codex-lan-agent" / executable_name,
        workspace_root / "codex-lan-agent" / "optfile" / executable_name,
        workspace_root / "codex-lan-agent" / "optfile" / "Release" / executable_name,
        workspace_root / "codex-lan-agent" / "optfile" / "Debug" / executable_name,
        workspace_root / "optfile" / executable_name,
        workspace_root / "optfile" / "Release" / executable_name,
        workspace_root / "optfile" / "Debug" / executable_name
    };
}

std::string FindOptfileHelperPath(
    const AgentConfig & config,
    std::string * searched_paths_json = nullptr) {
    const std::vector<std::filesystem::path> candidates = BuildOptfileHelperCandidates(config);
    std::ostringstream searched;
    searched << "[";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) {
            searched << ",";
        }
        searched << "\"" << codex_lan_agent::JsonEscape(candidates[index].string()) << "\"";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidates[index], ec) && !ec) {
            if (searched_paths_json != nullptr) {
                searched << "]";
                *searched_paths_json = searched.str();
            }
            return candidates[index].string();
        }
    }
    searched << "]";
    if (searched_paths_json != nullptr) {
        *searched_paths_json = searched.str();
    }
    return std::string();
}

std::string QuoteOptfileArgument(const std::string & value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "\"";
    return quoted;
}

std::string ExtractLastJsonObjectLine(const std::string & text) {
    std::istringstream input(text);
    std::string line;
    std::string last_json;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}') {
            last_json = trimmed;
        }
    }
    return last_json;
}

int CountNonOverlappingOccurrences(
    const std::string & text,
    const std::string & needle) {
    if (text.empty() || needle.empty()) {
        return 0;
    }
    int count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t found = text.find(needle, offset);
        if (found == std::string::npos) {
            break;
        }
        ++count;
        offset = found + needle.size();
    }
    return count;
}

bool BuildOptfileTargetArguments(
    const AgentConfig & config,
    const std::string & file_path,
    std::vector<std::string> * arguments,
    std::string * normalized_path,
    std::string * error_message) {
    if (arguments == nullptr) {
        if (error_message != nullptr) {
            *error_message = "optfile arguments output is null";
        }
        return false;
    }
    std::filesystem::path normalized;
    if (!TryResolveAllowedPath(config, file_path, &normalized, error_message)) {
        return false;
    }
    if (normalized_path != nullptr) {
        *normalized_path = normalized.string();
    }
    arguments->push_back("--target-dir " + QuoteOptfileArgument(normalized.parent_path().string()));
    arguments->push_back("--test-file " + QuoteOptfileArgument(normalized.filename().string()));
    return true;
}

CommandResult RunOptfileHelper(
    const AgentConfig & config,
    const std::vector<std::string> & arguments,
    const std::string & log_label) {
    CommandResult result;
    std::string searched_paths_json;
    const std::string helper_path = FindOptfileHelperPath(config, &searched_paths_json);
    result.fields["optfile_helper_paths_json"] = searched_paths_json;
    if (helper_path.empty()) {
        result.ok = false;
        result.exit_code = 93;
        result.fields["error"] = "optfile helper not found";
        return result;
    }

    std::ostringstream command_line;
    command_line << "\"" << helper_path << "\"";
    for (const std::string & argument : arguments) {
        command_line << " " << argument;
    }

    const std::string helper_log_token = BuildDirectoryAccessHelperLogToken();
    const std::string log_path = BuildLogPath(config, log_label + "_" + helper_log_token);
    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line.str(),
            std::string(),
            log_path,
            120,
            60,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 94;
        result.fields["error"] = error_message;
        result.fields["log_path"] = log_path;
        return result;
    }

    std::string helper_output;
    std::string read_error;
    if (!ReadWholeFile(log_path, &helper_output, &read_error)) {
        result.ok = false;
        result.exit_code = 95;
        result.fields["error"] = read_error;
        result.fields["log_path"] = log_path;
        return result;
    }

    result.ok = run_result.exit_code == 0;
    result.exit_code = run_result.exit_code;
    result.fields["optfile_helper_path"] = helper_path;
    result.fields["optfile_helper_log_path"] = log_path;
    result.fields["optfile_helper_output"] = helper_output;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;

    const std::string json_output = ExtractLastJsonObjectLine(helper_output);
    result.fields["optfile_json_output"] = json_output;
    if (json_output.empty()) {
        result.ok = false;
        if (result.exit_code == 0) {
            result.exit_code = 96;
        }
        result.fields["error"] = "optfile helper did not return structured JSON";
        return result;
    }
    return result;
}

std::string ExtractFirstJsonArrayObject(const std::string & array_json) {
    const std::size_t object_begin = array_json.find('{');
    if (object_begin == std::string::npos) {
        return std::string();
    }
    int depth = 0;
    for (std::size_t index = object_begin; index < array_json.size(); ++index) {
        if (array_json[index] == '{') {
            ++depth;
        } else if (array_json[index] == '}') {
            --depth;
            if (depth == 0) {
                return array_json.substr(object_begin, index - object_begin + 1);
            }
        }
    }
    return std::string();
}

std::string BuildOptfileTextMutationSummary(
    const std::string & operation,
    const std::string & file_path,
    const std::string & detail) {
    return operation + " completed for " + file_path + (detail.empty() ? std::string() : (" (" + detail + ")"));
}

CommandResult LocateTextLinesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    bool show_preview,
    int fuzzy_threshold);
CommandResult FindLineMetadataResult(
    const AgentConfig & config,
    const std::string & file_path,
    int line_number,
    bool show_preview) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["line"] = std::to_string(line_number);
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }
    if (line_number < 1) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "line_number must be >= 1";
        return result;
    }

    std::vector<std::string> arguments;
    std::string normalized_path;
    std::string path_error;
    if (!BuildOptfileTargetArguments(config, file_path, &arguments, &normalized_path, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }
    arguments.push_back("--find-line " + std::to_string(line_number));
    if (show_preview) {
        arguments.push_back("--show-preview");
    }

    result = RunOptfileHelper(config, arguments, "optfile_find_line_metadata");
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized_path;
    result.fields["line"] = std::to_string(line_number);
    result.fields["show_preview"] = show_preview ? "true" : "false";
    if (!result.ok) {
        return result;
    }

    const std::string json_output = GetFieldOrDefault(result, "optfile_json_output", "");
    result.fields["operation"] = ExtractJsonString(json_output, "operation");
    result.fields["status"] = ExtractJsonString(json_output, "status");
    result.fields["line_hash"] = ExtractJsonString(json_output, "line_hash");
    result.fields["line_length"] = Trim(ExtractJsonRawValue(json_output, "line_length"));
    result.fields["preview"] = ExtractJsonString(json_output, "preview");
    result.fields["result"] = "find_line_metadata_complete";
    result.fields["summary"] = BuildOptfileTextMutationSummary(
        "find_line_metadata",
        normalized_path,
        "line=" + std::to_string(line_number));
    result.fields["analysis_allowed"] = "true";
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["content_text"] = json_output;
    return result;
}

CommandResult FindContentMatchesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    bool show_preview,
    int fuzzy_threshold) {
    CommandResult result = LocateTextLinesResult(config, file_path, anchor_text, show_preview, fuzzy_threshold);
    result.fields["query_text"] = anchor_text;
    if (result.ok) {
        result.fields["result"] = "find_content_matches_complete";
        result.fields["summary"] = BuildOptfileTextMutationSummary(
            "find_content_matches",
            GetFieldOrDefault(result, "normalized_path", file_path),
            "match_count=" + GetFieldOrDefault(result, "match_count", "0"));
    }
    return result;
}
CommandResult LocateTextLinesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    bool show_preview,
    int fuzzy_threshold) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["anchor_text"] = anchor_text;
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        result.fields["error_code"] = "missing_file_path";
        result.fields["next_action"] = "retry lan_agent_probe_text_file with file_path";
        return result;
    }
    if (anchor_text.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "anchor_text is required";
        return result;
    }

    std::vector<std::string> arguments;
    std::string normalized_path;
    std::string path_error;
    if (!BuildOptfileTargetArguments(config, file_path, &arguments, &normalized_path, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }
    arguments.push_back("--locate-text " + QuoteOptfileArgument(anchor_text));
    arguments.push_back("--fuzzy-threshold " + std::to_string(std::max(1, fuzzy_threshold)));
    if (show_preview) {
        arguments.push_back("--show-preview");
    }

    result = RunOptfileHelper(config, arguments, "optfile_locate_text");
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized_path;
    result.fields["anchor_text"] = anchor_text;
    result.fields["show_preview"] = show_preview ? "true" : "false";
    result.fields["fuzzy_threshold"] = std::to_string(std::max(1, fuzzy_threshold));
    if (!result.ok) {
        return result;
    }

    const std::string json_output = GetFieldOrDefault(result, "optfile_json_output", "");
    const std::string matches_json = ExtractJsonRawValue(json_output, "matches");
    const std::string first_match_json = ExtractFirstJsonArrayObject(matches_json);
    const std::string match_count = ExtractJsonRawValue(json_output, "match_count");
    result.fields["operation"] = ExtractJsonString(json_output, "operation");
    result.fields["match_type"] = ExtractJsonString(json_output, "match_type");
    result.fields["match_count"] = Trim(match_count);
    result.fields["is_unique"] = Trim(match_count) == "1" ? "true" : "false";
    result.fields["matches_json"] = matches_json;
    result.fields["first_match_line"] = Trim(ExtractJsonRawValue(first_match_json, "line"));
    result.fields["first_match_hash"] = ExtractJsonString(first_match_json, "line_hash");
    result.fields["first_match_preview"] = ExtractJsonString(first_match_json, "preview");
    result.fields["message"] = ExtractJsonString(json_output, "message");
    result.fields["status"] = "success";
    result.fields["result"] = "locate_text_complete";
    result.fields["summary"] = BuildOptfileTextMutationSummary(
        "locate_text",
        normalized_path,
        "match_count=" + Trim(match_count));
    result.fields["analysis_allowed"] = "true";
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["content_text"] = json_output;
    return result;
}

CommandResult InsertAfterAnchorAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    int occurrence,
    const std::string & insert_text,
    const std::string & expected_anchor_hash,
    const std::string & request_id,
    const std::string & trace_id) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["anchor_text"] = anchor_text;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }
    if (anchor_text.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "anchor_text is required";
        return result;
    }
    if (insert_text.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "insert_text is required";
        return result;
    }

    std::vector<std::string> arguments;
    std::string normalized_path;
    std::string path_error;
    if (!BuildOptfileTargetArguments(config, file_path, &arguments, &normalized_path, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }
    arguments.push_back("--insert-after-anchor " + QuoteOptfileArgument(anchor_text));
    arguments.push_back("--occurrence " + std::to_string(std::max(1, occurrence)));
    arguments.push_back("--replacement-text " + QuoteOptfileArgument(insert_text));
    if (!expected_anchor_hash.empty()) {
        arguments.push_back("--expected-anchor-hash " + QuoteOptfileArgument(expected_anchor_hash));
    }

    result = RunOptfileHelper(config, arguments, "optfile_insert_after_anchor");
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized_path;
    result.fields["anchor_text"] = anchor_text;
    result.fields["occurrence"] = std::to_string(std::max(1, occurrence));
    result.fields["expected_anchor_hash"] = expected_anchor_hash;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (!result.ok) {
        return result;
    }

    const std::string json_output = GetFieldOrDefault(result, "optfile_json_output", "");
    std::string final_content;
    std::string read_error;
    if (!ReadWholeFile(normalized_path, &final_content, &read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = read_error;
        return result;
    }
    result.fields["operation"] = ExtractJsonString(json_output, "operation");
    result.fields["status"] = ExtractJsonString(json_output, "status");
    result.fields["anchor_line"] = Trim(ExtractJsonRawValue(json_output, "anchor_line"));
    result.fields["insert_start_line"] = Trim(ExtractJsonRawValue(json_output, "insert_start_line"));
    result.fields["insert_line_count"] = Trim(ExtractJsonRawValue(json_output, "insert_line_count"));
    result.fields["anchor_line_hash_before"] = ExtractJsonString(json_output, "anchor_line_hash_before");
    result.fields["new_hash"] = StableContentChecksum(final_content);
    result.fields["written_text_bytes"] = std::to_string(insert_text.size());
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = "true";
    result.fields["disk_write_completed"] = "true";
    result.fields["final_write_tool"] = "optfile.exe";
    result.fields["result"] = "insert_after_anchor_applied";
    result.fields["summary"] = BuildOptfileTextMutationSummary(
        "insert_after_anchor_atomic",
        normalized_path,
        "insert_start_line=" + GetFieldOrDefault(result, "insert_start_line", ""));
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["content_text"] = json_output;
    return result;
}

CommandResult ReplaceLineRangeAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    int start_line,
    int end_line,
    const std::string & replacement_text,
    const std::string & expected_range_hash,
    const std::string & request_id,
    const std::string & trace_id) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }
    if (start_line < 1 || end_line < start_line) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "invalid replace line range";
        return result;
    }

    std::vector<std::string> arguments;
    std::string normalized_path;
    std::string path_error;
    if (!BuildOptfileTargetArguments(config, file_path, &arguments, &normalized_path, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }
    arguments.push_back("--replace-start-line " + std::to_string(start_line));
    arguments.push_back("--replace-end-line " + std::to_string(end_line));
    arguments.push_back("--replacement-text " + QuoteOptfileArgument(replacement_text));
    if (!expected_range_hash.empty()) {
        arguments.push_back("--expected-range-hash " + QuoteOptfileArgument(expected_range_hash));
    }

    result = RunOptfileHelper(config, arguments, "optfile_replace_line_range");
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized_path;
    result.fields["start_line"] = std::to_string(start_line);
    result.fields["end_line"] = std::to_string(end_line);
    result.fields["expected_range_hash"] = expected_range_hash;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (!result.ok) {
        return result;
    }

    const std::string json_output = GetFieldOrDefault(result, "optfile_json_output", "");
    std::string final_content;
    std::string read_error;
    if (!ReadWholeFile(normalized_path, &final_content, &read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = read_error;
        return result;
    }
    result.fields["operation"] = ExtractJsonString(json_output, "operation");
    result.fields["status"] = ExtractJsonString(json_output, "status");
    result.fields["replacement_line_count"] = Trim(ExtractJsonRawValue(json_output, "replacement_line_count"));
    result.fields["range_hash_before"] = ExtractJsonString(json_output, "range_hash_before");
    result.fields["new_hash"] = StableContentChecksum(final_content);
    result.fields["written_text_bytes"] = std::to_string(replacement_text.size());
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = "true";
    result.fields["disk_write_completed"] = "true";
    result.fields["final_write_tool"] = "optfile.exe";
    result.fields["result"] = "replace_line_range_applied";
    result.fields["summary"] = BuildOptfileTextMutationSummary(
        "replace_line_range_atomic",
        normalized_path,
        "start_line=" + std::to_string(start_line) + ",end_line=" + std::to_string(end_line));
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["content_text"] = json_output;
    return result;
}
CommandResult DeleteLineAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    int line_number,
    const std::string & expected_line_hash,
    const std::string & request_id,
    const std::string & trace_id) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["line"] = std::to_string(line_number);
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }
    if (line_number < 1) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "line_number must be >= 1";
        return result;
    }

    std::vector<std::string> arguments;
    std::string normalized_path;
    std::string path_error;
    if (!BuildOptfileTargetArguments(config, file_path, &arguments, &normalized_path, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }
    arguments.push_back("--delete-line " + std::to_string(line_number));
    if (!expected_line_hash.empty()) {
        arguments.push_back("--expected-line-hash " + QuoteOptfileArgument(expected_line_hash));
    }

    result = RunOptfileHelper(config, arguments, "optfile_delete_line");
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized_path;
    result.fields["line"] = std::to_string(line_number);
    result.fields["expected_line_hash"] = expected_line_hash;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (!result.ok) {
        return result;
    }

    const std::string json_output = GetFieldOrDefault(result, "optfile_json_output", "");
    std::string final_content;
    std::string read_error;
    if (!ReadWholeFile(normalized_path, &final_content, &read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = read_error;
        return result;
    }
    result.fields["operation"] = ExtractJsonString(json_output, "operation");
    result.fields["status"] = ExtractJsonString(json_output, "status");
    result.fields["deleted_line_hash_before"] = ExtractJsonString(json_output, "deleted_line_hash_before");
    result.fields["new_hash"] = StableContentChecksum(final_content);
    result.fields["written_text_bytes"] = "0";
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = "true";
    result.fields["disk_write_completed"] = "true";
    result.fields["final_write_tool"] = "optfile.exe";
    result.fields["result"] = "delete_line_atomic_applied";
    result.fields["summary"] = BuildOptfileTextMutationSummary(
        "delete_line_atomic",
        normalized_path,
        "line=" + std::to_string(line_number));
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["content_text"] = json_output;
    return result;
}

CommandResult DeleteContentAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    int occurrence,
    const std::string & expected_anchor_hash,
    const std::string & request_id,
    const std::string & trace_id) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["anchor_text"] = anchor_text;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }
    if (anchor_text.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "anchor_text is required";
        return result;
    }

    std::vector<std::string> arguments;
    std::string normalized_path;
    std::string path_error;
    if (!BuildOptfileTargetArguments(config, file_path, &arguments, &normalized_path, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }
    std::string before_content;
    std::string before_read_error;
    if (!ReadWholeFile(normalized_path, &before_content, &before_read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = before_read_error;
        result.fields["error_code"] = "file_read_before_delete_failed";
        return result;
    }
    const std::string old_hash = StableContentChecksum(before_content);
    const int before_anchor_count = CountNonOverlappingOccurrences(before_content, anchor_text);
    arguments.push_back("--delete-content " + QuoteOptfileArgument(anchor_text));
    arguments.push_back("--occurrence " + std::to_string(std::max(1, occurrence)));
    if (!expected_anchor_hash.empty()) {
        arguments.push_back("--expected-anchor-hash " + QuoteOptfileArgument(expected_anchor_hash));
    }

    result = RunOptfileHelper(config, arguments, "optfile_delete_content");
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized_path;
    result.fields["anchor_text"] = anchor_text;
    result.fields["occurrence"] = std::to_string(std::max(1, occurrence));
    result.fields["expected_anchor_hash"] = expected_anchor_hash;
    if (!request_id.empty()) result.fields["request_id"] = request_id;
    if (!trace_id.empty()) result.fields["trace_id"] = trace_id;
    if (!result.ok) {
        const int helper_exit_code = result.exit_code;
        const std::string helper_error = GetFieldOrDefault(result, "error", "");
        std::string final_content;
        std::string read_error;
        if (ReadWholeFile(normalized_path, &final_content, &read_error)) {
            const int after_anchor_count = CountNonOverlappingOccurrences(final_content, anchor_text);
            result.fields["old_hash"] = old_hash;
            result.fields["new_hash"] = StableContentChecksum(final_content);
            result.fields["before_anchor_occurrence_count"] = std::to_string(before_anchor_count);
            result.fields["after_anchor_occurrence_count"] = std::to_string(after_anchor_count);
            result.fields["helper_exit_code"] = std::to_string(helper_exit_code);
            result.fields["helper_error"] = helper_error;
            result.fields["final_write_tool"] = "optfile.exe";
            result.fields["content_payload_format"] = "json";
            result.fields["content_payload_scope"] = "metadata_only";
            result.fields["content_payload_boundary_safe"] = "true";
            if (before_anchor_count > after_anchor_count) {
                result.ok = true;
                result.exit_code = 0;
                result.fields["write_applied"] = "true";
                result.fields["write_verified"] = "true";
                result.fields["disk_write_completed"] = "true";
                result.fields["result"] = "delete_content_atomic_applied_after_readback";
                result.fields["summary"] = BuildOptfileTextMutationSummary(
                    "delete_content_atomic",
                    normalized_path,
                    "verified_by_occurrence_count_after_helper_failure");
                result.fields["recovered_after_helper_failure"] = "true";
                result.fields["recovery_reason"] = "target content occurrence count decreased after helper returned a failed or incomplete envelope";
                result.fields["verification_status"] = "verified_by_readback";
                result.fields["verification_ok"] = "true";
                result.fields["next_action"] = "rescan with lan_agent_scan_text_ranges(range_offset=0,max_ranges_per_call=1,scan_mode=comments) before the next edit";
            } else {
                result.fields["write_applied"] = "false";
                result.fields["write_verified"] = "false";
                result.fields["disk_write_completed"] = "false";
                result.fields["error_code"] = FirstNonEmpty(
                    GetFieldOrDefault(result, "error_code", ""),
                    "delete_content_not_verified",
                    "delete_content_not_verified");
                result.fields["failure_mode"] = result.fields["error_code"];
                result.fields["next_action"] = "do not retry blindly; rescan the current file and prepare a fresh single edit window";
            }
        } else {
            result.fields["readback_error"] = read_error;
            result.fields["error_code"] = "delete_content_readback_failed";
            result.fields["failure_mode"] = result.fields["error_code"];
        }
        return result;
    }

    const std::string json_output = GetFieldOrDefault(result, "optfile_json_output", "");
    std::string final_content;
    std::string read_error;
    if (!ReadWholeFile(normalized_path, &final_content, &read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = read_error;
        return result;
    }
    result.fields["operation"] = ExtractJsonString(json_output, "operation");
    result.fields["status"] = ExtractJsonString(json_output, "status");
    result.fields["deleted_line"] = Trim(ExtractJsonRawValue(json_output, "deleted_line"));
    result.fields["deleted_line_hash_before"] = ExtractJsonString(json_output, "deleted_line_hash_before");
    result.fields["old_hash"] = old_hash;
    result.fields["new_hash"] = StableContentChecksum(final_content);
    result.fields["before_anchor_occurrence_count"] = std::to_string(before_anchor_count);
    result.fields["after_anchor_occurrence_count"] = std::to_string(
        CountNonOverlappingOccurrences(final_content, anchor_text));
    result.fields["written_text_bytes"] = "0";
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = "true";
    result.fields["disk_write_completed"] = "true";
    result.fields["final_write_tool"] = "optfile.exe";
    result.fields["result"] = "delete_content_atomic_applied";
    result.fields["summary"] = BuildOptfileTextMutationSummary(
        "delete_content_atomic",
        normalized_path,
        "deleted_line=" + GetFieldOrDefault(result, "deleted_line", ""));
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["content_text"] = json_output;
    return result;
}
struct TextRangeDescriptor {
    int range_index = 0;
    int start_line = 0;
    int end_line = 0;
    int start_column = 0;
    int end_column = 0;
    std::string range_kind;
    std::string preview;
};

namespace {

// Cache for ScanTextRangesResult to avoid re-reading and re-parsing the same
// file for each paginated call. The cache is keyed by (path, scan_mode) and
// invalidated when the file modification time or size changes.
struct TextRangeScanCacheKey {
    std::string normalized_path;
    std::string scan_mode;
};

struct TextRangeScanCacheValue {
    std::filesystem::file_time_type last_write_time;
    std::uintmax_t file_size = 0;
    int total_lines = 0;
    std::vector<TextRangeDescriptor> ranges;
};

struct TextRangeScanCacheEntry {
    TextRangeScanCacheKey key;
    TextRangeScanCacheValue value;
};

constexpr std::size_t kMaxTextRangeScanCacheEntries = 16;

class TextRangeScanCache {
public:
    bool Find(
        const std::string & normalized_path,
        const std::string & scan_mode,
        std::filesystem::file_time_type last_write_time,
        std::uintmax_t file_size,
        TextRangeScanCacheValue * out_value) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->key.normalized_path == normalized_path &&
                it->key.scan_mode == scan_mode &&
                it->value.last_write_time == last_write_time &&
                it->value.file_size == file_size) {
                if (out_value != nullptr) {
                    *out_value = it->value;
                }
                // Move to front for simple LRU ordering.
                if (it != entries_.begin()) {
                    entries_.splice(entries_.begin(), entries_, it);
                }
                return true;
            }
        }
        return false;
    }

    void Store(
        const std::string & normalized_path,
        const std::string & scan_mode,
        const TextRangeScanCacheValue & value) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Remove any stale entry for the same key.
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (it->key.normalized_path == normalized_path &&
                it->key.scan_mode == scan_mode) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        entries_.push_front(TextRangeScanCacheEntry{{normalized_path, scan_mode}, value});
        while (entries_.size() > kMaxTextRangeScanCacheEntries) {
            entries_.pop_back();
        }
    }

private:
    std::mutex mutex_;
    std::list<TextRangeScanCacheEntry> entries_;
};

TextRangeScanCache & GetTextRangeScanCache() {
    static TextRangeScanCache cache;
    return cache;
}

}  // namespace

std::string BuildTextRangeScanRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "text_range_scans");
}

std::string BuildTextRangeScanPath(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_file_path) {
    const std::string trace_token = !trace_id.empty()
        ? SanitizeDispatchToken(trace_id, "trace")
        : SanitizeDispatchToken(std::filesystem::path(normalized_file_path).filename().string(), "file");
    return codex_lan_agent::JoinPath(
        BuildTextRangeScanRoot(config),
        "text_range_scan_" + trace_token + ".txt");
}

std::string BuildEditWindowBundleRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "edit_window_bundles");
}

std::string BuildEditWindowBundlePath(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_file_path) {
    const std::string trace_token = !trace_id.empty()
        ? SanitizeDispatchToken(trace_id, "trace")
        : SanitizeDispatchToken(std::filesystem::path(normalized_file_path).filename().string(), "file");
    return codex_lan_agent::JoinPath(
        BuildEditWindowBundleRoot(config),
        "edit_windows_" + trace_token + ".txt");
}

std::string BuildTextRangeDeleteRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "text_range_deletes");
}

std::string BuildTextRangeDeletePath(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_file_path) {
    const std::string trace_token = !trace_id.empty()
        ? SanitizeDispatchToken(trace_id, "trace")
        : SanitizeDispatchToken(std::filesystem::path(normalized_file_path).filename().string(), "file");
    return codex_lan_agent::JoinPath(
        BuildTextRangeDeleteRoot(config),
        "text_range_delete_" + trace_token + ".txt");
}

std::string BuildTextRangeWindowDeletePath(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_file_path) {
    const std::string trace_token = !trace_id.empty()
        ? SanitizeDispatchToken(trace_id, "trace")
        : SanitizeDispatchToken(std::filesystem::path(normalized_file_path).filename().string(), "file");
    return codex_lan_agent::JoinPath(
        BuildTextRangeDeleteRoot(config),
        "text_range_window_delete_" + trace_token + ".txt");
}

std::string CapPreviewText(const std::string & value, std::size_t max_chars = 160) {
    const std::size_t bounded_max_chars = std::max<std::size_t>(32, max_chars);
    if (value.size() <= bounded_max_chars) {
        return value;
    }
    return value.substr(0, bounded_max_chars) + "...";
}

std::string NormalizeTextRangeScanMode(const std::string & scan_mode) {
    const std::string lowered = ToLowerAscii(Trim(scan_mode));
    if (lowered.empty() || lowered == "comments" || lowered == "comment") {
        return "comments";
    }
    if (lowered == "line_comments" || lowered == "line-comment" || lowered == "line") {
        return "line_comments";
    }
    if (lowered == "block_comments" || lowered == "block-comment" || lowered == "block") {
        return "block_comments";
    }
    return std::string();
}

bool ScanModeIncludesLineComments(const std::string & normalized_mode) {
    return normalized_mode == "comments" || normalized_mode == "line_comments";
}

bool ScanModeIncludesBlockComments(const std::string & normalized_mode) {
    return normalized_mode == "comments" || normalized_mode == "block_comments";
}

std::string BuildTextRangeJsonArray(const std::vector<TextRangeDescriptor> & ranges) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        const TextRangeDescriptor& range = ranges[index];
        output << "{";
        output << "\"range_index\":" << range.range_index << ",";
        output << "\"start_line\":" << range.start_line << ",";
        output << "\"end_line\":" << range.end_line << ",";
        output << "\"start_column\":" << range.start_column << ",";
        output << "\"end_column\":" << range.end_column << ",";
        output << "\"range_kind\":\"" << codex_lan_agent::JsonEscape(range.range_kind) << "\",";
        output << "\"preview\":\"" << codex_lan_agent::JsonEscape(range.preview) << "\"";
        output << "}";
    }

    output << "]";
    return output.str();
}

std::string BuildTextRangeSummaryText(
    const std::string & normalized_path,
    const std::string & scan_mode,
    int total_lines,
    int total_range_count,
    int returned_range_count,
    int range_offset,
    const std::vector<TextRangeDescriptor> & ranges) {
    std::ostringstream output;
    output << "file_path=" << normalized_path << "\n";
    output << "scan_mode=" << scan_mode << "\n";
    output << "total_lines=" << total_lines << "\n";
    output << "total_range_count=" << total_range_count << "\n";
    output << "returned_range_count=" << returned_range_count << "\n";
    output << "range_offset=" << range_offset << "\n";
    for (const TextRangeDescriptor & range : ranges) {
        output << "range_index=" << range.range_index
               << "; kind=" << range.range_kind
               << "; lines=" << range.start_line << "-" << range.end_line
               << "; columns=" << range.start_column << "-" << range.end_column
               << "; preview=" << range.preview << "\n";
    }
    return output.str();
}

bool CollectCommentRanges(
    const std::string & raw_content,
    const std::string & normalized_scan_mode,
    std::vector<TextRangeDescriptor> * ranges,
    int * total_lines) {
    if (ranges == nullptr || total_lines == nullptr) {
        return false;
    }
    ranges->clear();
    const std::vector<std::string> lines = SplitLinesPreserveText(raw_content);
    *total_lines = static_cast<int>(lines.size());
    const bool include_line_comments = ScanModeIncludesLineComments(normalized_scan_mode);
    const bool include_block_comments = ScanModeIncludesBlockComments(normalized_scan_mode);

    bool in_block_comment = false;
    int block_start_line = 0;
    int block_start_column = 0;
    std::string block_preview;
    int next_range_index = 0;

    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const std::string & line = lines[line_index];
        bool in_string = false;
        bool in_char = false;
        bool escaping = false;
        for (std::size_t column = 0; column < line.size(); ++column) {
            const char ch = line[column];
            const char next = column + 1 < line.size() ? line[column + 1] : '\0';
            if (in_block_comment) {
                if (ch == '*' && next == '/') {
                    TextRangeDescriptor range;
                    range.range_index = next_range_index++;
                    range.start_line = block_start_line;
                    range.end_line = static_cast<int>(line_index) + 1;
                    range.start_column = block_start_column;
                    range.end_column = static_cast<int>(column) + 2;
                    range.range_kind = "block_comment";
                    range.preview = CapPreviewText(block_preview.empty() ? line : block_preview);
                    ranges->push_back(range);
                    in_block_comment = false;
                    block_start_line = 0;
                    block_start_column = 0;
                    block_preview.clear();
                    ++column;
                }
                continue;
            }
            if (escaping) {
                escaping = false;
                continue;
            }
            if (in_string) {
                if (ch == '\\') {
                    escaping = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }
            if (in_char) {
                if (ch == '\\') {
                    escaping = true;
                } else if (ch == '\'') {
                    in_char = false;
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == '\'') {
                in_char = true;
                continue;
            }
            if (include_line_comments && ch == '/' && next == '/') {
                TextRangeDescriptor range;
                range.range_index = next_range_index++;
                range.start_line = static_cast<int>(line_index) + 1;
                range.end_line = static_cast<int>(line_index) + 1;
                range.start_column = static_cast<int>(column) + 1;
                range.end_column = static_cast<int>(line.size());
                range.range_kind = "line_comment";
                range.preview = CapPreviewText(Trim(line.substr(column)));
                ranges->push_back(range);
                break;
            }
            if (include_block_comments && ch == '/' && next == '*') {
                const std::size_t closing = line.find("*/", column + 2);
                if (closing != std::string::npos) {
                    TextRangeDescriptor range;
                    range.range_index = next_range_index++;
                    range.start_line = static_cast<int>(line_index) + 1;
                    range.end_line = static_cast<int>(line_index) + 1;
                    range.start_column = static_cast<int>(column) + 1;
                    range.end_column = static_cast<int>(closing) + 2;
                    range.range_kind = "block_comment";
                    range.preview = CapPreviewText(Trim(line.substr(column, closing + 2 - column)));
                    ranges->push_back(range);
                    column = closing + 1;
                    continue;
                }
                in_block_comment = true;
                block_start_line = static_cast<int>(line_index) + 1;
                block_start_column = static_cast<int>(column) + 1;
                block_preview = CapPreviewText(Trim(line.substr(column)));
                break;
            }
        }
    }

    if (in_block_comment) {
        TextRangeDescriptor range;
        range.range_index = next_range_index++;
        range.start_line = block_start_line;
        range.end_line = std::max(block_start_line, *total_lines);
        range.start_column = block_start_column;
        range.end_column = 0;
        range.range_kind = "block_comment_unterminated";
        range.preview = CapPreviewText(block_preview);
        ranges->push_back(range);
    }

    return true;
}

std::string BuildDeletedRangeSummaryJsonArray(const std::vector<TextRangeDescriptor> & ranges) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        const TextRangeDescriptor & range = ranges[index];
        output << "{";
        output << "\"range_index\":" << range.range_index << ",";
        output << "\"range_kind\":\"" << codex_lan_agent::JsonEscape(range.range_kind) << "\",";
        output << "\"start_line\":" << range.start_line << ",";
        output << "\"end_line\":" << range.end_line << ",";
        output << "\"start_column\":" << range.start_column << ",";
        output << "\"end_column\":" << range.end_column << ",";
        output << "\"preview\":\"" << codex_lan_agent::JsonEscape(range.preview) << "\"";
        output << "}";
    }
    output << "]";
    return output.str();
}

std::string BuildDeleteNextTextRangeCallJson(
    const std::string & file_path,
    const std::string & scan_mode,
    const std::string & primary_intent,
    const std::string & trace_id,
    const std::string & probe_ref) {
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_delete_next_text_range_atomic\",\"arguments\":{";
    output << "\"file_path\":\"" << codex_lan_agent::JsonEscape(file_path) << "\"";
    output << ",\"scan_mode\":\"" << codex_lan_agent::JsonEscape(scan_mode) << "\"";
    if (!primary_intent.empty()) {
        output << ",\"primary_intent\":\"" << codex_lan_agent::JsonEscape(primary_intent) << "\"";
    }
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!probe_ref.empty()) {
        output << ",\"probe_ref\":\"" << codex_lan_agent::JsonEscape(probe_ref) << "\",\"probe_ready\":true";
    }
    output << "}}";
    return output.str();
}

std::string BuildDeleteTextRangeWindowCallJson(
    const std::string & file_path,
    const std::string & scan_mode,
    int next_start_line,
    int max_lines,
    const std::string & primary_intent,
    const std::string & trace_id,
    const std::string & probe_ref) {
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_delete_text_range_window_atomic\",\"arguments\":{";
    output << "\"file_path\":\"" << codex_lan_agent::JsonEscape(file_path) << "\"";
    output << ",\"scan_mode\":\"" << codex_lan_agent::JsonEscape(scan_mode) << "\"";
    output << ",\"start_line\":" << std::max(1, next_start_line);
    output << ",\"next_start_line\":" << std::max(1, next_start_line);
    output << ",\"max_lines\":" << std::max(1, max_lines);
    if (!primary_intent.empty()) {
        output << ",\"primary_intent\":\"" << codex_lan_agent::JsonEscape(primary_intent) << "\"";
    }
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!probe_ref.empty()) {
        output << ",\"probe_ref\":\"" << codex_lan_agent::JsonEscape(probe_ref) << "\",\"probe_ready\":true";
    }
    output << "}}";
    return output.str();
}

std::vector<std::size_t> BuildRawLineStartOffsets(const std::string & raw_content) {
    std::vector<std::size_t> offsets;
    offsets.push_back(0);
    for (std::size_t index = 0; index < raw_content.size(); ++index) {
        if (raw_content[index] == '\n' && index + 1 < raw_content.size()) {
            offsets.push_back(index + 1);
        }
    }
    return offsets;
}

std::size_t FindRawLineEndExcludingNewline(
    const std::string & raw_content,
    std::size_t line_start) {
    std::size_t index = line_start;
    while (index < raw_content.size() && raw_content[index] != '\r' && raw_content[index] != '\n') {
        ++index;
    }
    return index;
}

std::size_t FindRawLineEndIncludingNewline(
    const std::string & raw_content,
    std::size_t line_end) {
    if (line_end < raw_content.size() && raw_content[line_end] == '\r') {
        return line_end + 1 < raw_content.size() && raw_content[line_end + 1] == '\n'
            ? line_end + 2
            : line_end + 1;
    }
    if (line_end < raw_content.size() && raw_content[line_end] == '\n') {
        return line_end + 1;
    }
    return line_end;
}

bool IsAsciiSpaceOrTab(char ch) {
    return ch == ' ' || ch == '\t';
}

bool BuildContentWithDeletedTextRange(
    const std::string & raw_content,
    const TextRangeDescriptor & range,
    std::string * new_content,
    std::string * deleted_text,
    std::string * delete_mode,
    std::string * error_message) {
    if (new_content == nullptr || deleted_text == nullptr || delete_mode == nullptr) {
        if (error_message != nullptr) {
            *error_message = "delete range output is null";
        }
        return false;
    }
    const std::vector<std::size_t> line_starts = BuildRawLineStartOffsets(raw_content);
    if (range.start_line < 1 || range.end_line < range.start_line ||
        static_cast<std::size_t>(range.start_line) > line_starts.size() ||
        static_cast<std::size_t>(range.end_line) > line_starts.size()) {
        if (error_message != nullptr) {
            *error_message = "range line is outside file";
        }
        return false;
    }

    const std::size_t start_line_start = line_starts[static_cast<std::size_t>(range.start_line - 1)];
    const std::size_t end_line_start = line_starts[static_cast<std::size_t>(range.end_line - 1)];
    const std::size_t start_line_end = FindRawLineEndExcludingNewline(raw_content, start_line_start);
    const std::size_t end_line_end = FindRawLineEndExcludingNewline(raw_content, end_line_start);
    const std::size_t start_column_offset = static_cast<std::size_t>(std::max(1, range.start_column) - 1);
    std::size_t delete_start = std::min(start_line_start + start_column_offset, start_line_end);
    std::size_t delete_end = range.end_column > 0
        ? std::min(end_line_start + static_cast<std::size_t>(range.end_column), end_line_end)
        : end_line_end;
    *delete_mode = "range_text";

    if (range.range_kind == "line_comment" && range.start_line == range.end_line) {
        std::string prefix = raw_content.substr(start_line_start, delete_start - start_line_start);
        const bool prefix_is_whitespace = Trim(prefix).empty();
        if (prefix_is_whitespace) {
            delete_start = start_line_start;
            delete_end = FindRawLineEndIncludingNewline(raw_content, start_line_end);
            *delete_mode = "whole_line_comment_line";
        } else {
            while (delete_start > start_line_start && IsAsciiSpaceOrTab(raw_content[delete_start - 1])) {
                --delete_start;
            }
            delete_end = start_line_end;
            *delete_mode = "line_comment_suffix";
        }
    } else if (range.range_kind == "block_comment" && range.start_line == range.end_line) {
        const std::string prefix = raw_content.substr(start_line_start, delete_start - start_line_start);
        const std::string suffix = raw_content.substr(delete_end, end_line_end - delete_end);
        if (Trim(prefix).empty() && Trim(suffix).empty()) {
            delete_start = start_line_start;
            delete_end = FindRawLineEndIncludingNewline(raw_content, end_line_end);
            *delete_mode = "whole_line_block_comment";
        }
    }

    if (delete_end < delete_start || delete_start > raw_content.size() || delete_end > raw_content.size()) {
        if (error_message != nullptr) {
            *error_message = "computed delete range is invalid";
        }
        return false;
    }
    *deleted_text = raw_content.substr(delete_start, delete_end - delete_start);
    *new_content = raw_content.substr(0, delete_start) + raw_content.substr(delete_end);
    return true;
}

bool WriteWholeFileDirect(
    const std::filesystem::path & path,
    const std::string & content,
    std::string * error_message) {
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open file for writing";
        }
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "failed to write file content";
        }
        return false;
    }
    return true;
}

std::vector<std::string> ExtractTopLevelJsonObjects(const std::string & array_text) {
    std::vector<std::string> objects;
    int depth = 0;
    bool in_string = false;
    bool escaping = false;
    std::size_t object_start = std::string::npos;
    for (std::size_t index = 0; index < array_text.size(); ++index) {
        const char ch = array_text[index];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (in_string) {
            if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                object_start = index;
            }
            ++depth;
            continue;
        }
        if (ch == '}') {
            if (depth <= 0) {
                continue;
            }
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.push_back(array_text.substr(object_start, index - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }
    return objects;
}

bool ParseTextRangeDescriptors(
    const std::string & ranges_json,
    std::vector<TextRangeDescriptor> * ranges,
    std::string * error_message) {
    if (ranges == nullptr) {
        return false;
    }
    ranges->clear();
    if (Trim(ranges_json).empty()) {
        if (error_message != nullptr) {
            *error_message = "ranges_json is required";
        }
        return false;
    }
    const std::vector<std::string> objects = ExtractTopLevelJsonObjects(ranges_json);
    if (objects.empty()) {
        if (error_message != nullptr) {
            *error_message = "ranges_json did not contain any range objects";
        }
        return false;
    }
    for (const std::string & object_json : objects) {
        const JsonRequestView object_view(object_json);
        TextRangeDescriptor range;
        range.range_index = std::max(0, object_view.GetInt("range_index", static_cast<int>(ranges->size())));
        range.start_line = std::max(1, object_view.GetInt("start_line", 0));
        range.end_line = std::max(range.start_line, object_view.GetInt("end_line", range.start_line));
        range.start_column = std::max(0, object_view.GetInt("start_column", 0));
        range.end_column = std::max(0, object_view.GetInt("end_column", 0));
        range.range_kind = object_view.GetString("range_kind");
        range.preview = object_view.GetString("preview");
        if (range.start_line <= 0) {
            continue;
        }
        if (range.range_kind.empty()) {
            range.range_kind = "text_range";
        }
        ranges->push_back(range);
    }
    if (ranges->empty()) {
        if (error_message != nullptr) {
            *error_message = "ranges_json did not contain any valid ranges";
        }
        return false;
    }
    std::sort(
        ranges->begin(),
        ranges->end(),
        [](const TextRangeDescriptor & left, const TextRangeDescriptor & right) {
            if (left.start_line != right.start_line) {
                return left.start_line < right.start_line;
            }
            if (left.end_line != right.end_line) {
                return left.end_line < right.end_line;
            }
            return left.range_index < right.range_index;
        });
    return true;
}

std::string BuildEditWindowJsonArray(
    const std::vector<TextRangeDescriptor> & ranges,
    const std::vector<std::pair<int, int>> & window_bounds,
    const std::vector<std::string> & window_texts,
    const std::vector<std::string> & window_flags) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "{";
        output << "\"window_index\":" << index << ",";
        output << "\"range_index\":" << ranges[index].range_index << ",";
        output << "\"range_kind\":\"" << codex_lan_agent::JsonEscape(ranges[index].range_kind) << "\",";
        output << "\"range_start_line\":" << ranges[index].start_line << ",";
        output << "\"range_end_line\":" << ranges[index].end_line << ",";
        output << "\"window_start_line\":" << window_bounds[index].first << ",";
        output << "\"window_end_line\":" << window_bounds[index].second << ",";
        output << "\"window_flags\":\"" << codex_lan_agent::JsonEscape(window_flags[index]) << "\",";
        output << "\"content\":\"" << codex_lan_agent::JsonEscape(window_texts[index]) << "\"";
        output << "}";
    }
    output << "]";
    return output.str();
}

bool ReadFileExcerptPreview(
    const std::filesystem::path & path,
    int max_lines,
    std::size_t max_chars,
    std::string * excerpt_text,
    int * excerpt_line_count,
    bool * truncated) {
    if (excerpt_text == nullptr || excerpt_line_count == nullptr || truncated == nullptr) {
        return false;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    const int bounded_max_lines = std::max(1, max_lines);
    const std::size_t bounded_max_chars = std::max<std::size_t>(256, max_chars);
    std::ostringstream excerpt;
    std::string line;
    int lines = 0;
    bool has_more = false;
    while (std::getline(input, line)) {
        if (lines >= bounded_max_lines) {
            has_more = true;
            break;
        }
        const std::string next_line = line + "\n";
        if (excerpt.tellp() > 0 &&
            static_cast<std::size_t>(excerpt.tellp()) + next_line.size() > bounded_max_chars) {
            has_more = true;
            break;
        }
        excerpt << next_line;
        ++lines;
    }

    *excerpt_text = excerpt.str();
    *excerpt_line_count = lines;
    *truncated = has_more;
    return true;
}

std::string BuildLocalMcpTraceAuditEventsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "mcp_trace_audit_events.jsonl");
}

std::string BuildDirectoryReadManifestPath(
    const AgentConfig & config,
    const std::string & trace_id) {
    const std::string trace_token = trace_id.empty()
        ? "default"
        : SanitizeDispatchToken(trace_id, "trace");
    return codex_lan_agent::JoinPath(
        BuildDirectoryReadBatchRoot(config),
        "directory_read_manifest_" + trace_token + ".txt");
}

bool SaveDirectoryReadManifest(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_directory_path,
    const std::vector<std::string> & file_paths,
    std::string * manifest_path) {
    const std::string path = BuildDirectoryReadManifestPath(config, trace_id);
    std::filesystem::create_directories(BuildDirectoryReadBatchRoot(config));
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << "# directory_path=" << normalized_directory_path << "\n";
    for (const std::string & file_path : file_paths) {
        output << file_path << "\n";
    }
    if (manifest_path != nullptr) {
        *manifest_path = path;
    }
    return true;
}

std::vector<std::string> LoadDirectoryReadManifest(
    const AgentConfig & config,
    const std::string & trace_id,
    std::string * manifest_path = nullptr) {
    const std::string path = BuildDirectoryReadManifestPath(config, trace_id);
    if (manifest_path != nullptr) {
        *manifest_path = path;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::vector<std::string>();
    }
    std::vector<std::string> file_paths;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed.rfind("#", 0) == 0) {
            continue;
        }
        file_paths.push_back(trimmed);
    }
    return file_paths;
}

std::unordered_set<std::string> LoadTraceReadFileSet(
    const AgentConfig & config,
    const std::string & trace_id) {
    std::unordered_set<std::string> read_files;
    if (trace_id.empty()) {
        return read_files;
    }
    std::string content;
    std::string read_error;
    if (!ReadWholeFile(BuildLocalMcpTraceAuditEventsPath(config), &content, &read_error)) {
        return read_files;
    }
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"") == std::string::npos) {
            continue;
        }
        if (ExtractJsonString(line, "tool_name") != "lan_agent_read_text_file") {
            continue;
        }
        if (ExtractJsonString(line, "read_complete") != "true") {
            continue;
        }
        const std::string normalized_path = ExtractJsonString(line, "normalized_path");
        const std::string file_path = ExtractJsonString(line, "file_path");
        const std::string selected_path = !normalized_path.empty() ? normalized_path : file_path;
        if (!selected_path.empty()) {
            read_files.insert(selected_path);
        }
    }
    return read_files;
}

std::string LoadDirectoryReadManifestDirectory(
    const AgentConfig & config,
    const std::string & trace_id) {
    const std::string path = BuildDirectoryReadManifestPath(config, trace_id);
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::string();
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string prefix = "# directory_path=";
        if (line.rfind(prefix, 0) == 0) {
            return Trim(line.substr(prefix.size()));
        }
    }
    return std::string();
}

std::string BuildCxParserDirectoryFlowParamsJson(
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_lines_per_file,
    int file_index,
    int start_line) {
    std::ostringstream output;
    output << "{\"directory_path\":\"" << codex_lan_agent::JsonEscape(directory_path) << "\","
           << "\"file_extensions_csv\":\"" << codex_lan_agent::JsonEscape(file_extensions_csv) << "\","
           << "\"max_files\":" << max_files << ","
           << "\"max_lines_per_file\":" << max_lines_per_file << ","
           << "\"file_index\":" << file_index << ","
           << "\"start_line\":" << start_line << "}";
    return output.str();
}

std::string BuildCxParserDirectoryFlowCallJson(
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_lines_per_file,
    int file_index,
    int start_line,
    const std::string & trace_id) {
    const std::string params_json = BuildCxParserDirectoryFlowParamsJson(
        directory_path,
        file_extensions_csv,
        max_files,
        max_lines_per_file,
        file_index,
        start_line);
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_run_cxparser_flow\",\"arguments\":{"
           << "\"flow_id\":\"read_directory_files\","
           << "\"params_json\":\"" << codex_lan_agent::JsonEscape(params_json) << "\"";
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    output << "}}";
    return output.str();
}

std::string BuildDirectoryReadContinuationCallJson(
    const std::string & directory_path,
    int max_files,
    int max_lines_per_file,
    int max_files_per_call,
    int max_total_lines,
    int file_index,
    int start_line,
    const std::string & trace_id) {
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_read_directory_files\",\"arguments\":{"
           << "\"directory_path\":\"" << codex_lan_agent::JsonEscape(directory_path) << "\","
           << "\"max_files\":" << std::max(1, max_files) << ","
           << "\"max_lines_per_file\":" << std::max(1, max_lines_per_file) << ","
           << "\"max_files_per_call\":" << std::max(1, max_files_per_call) << ","
           << "\"max_total_lines\":" << std::max(1, max_total_lines) << ","
           << "\"file_index\":" << std::max(0, file_index) << ","
           << "\"start_line\":" << std::max(1, start_line);
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    output << "}}";
    return output.str();
}

void ApplyDirectoryBatchProgress(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_file_path,
    CommandResult * result) {
    if (result == nullptr || trace_id.empty()) {
        return;
    }
    std::string manifest_path;
    const std::vector<std::string> manifest_files =
        LoadDirectoryReadManifest(config, trace_id, &manifest_path);
    if (manifest_files.empty()) {
        return;
    }

    result->fields["batch_trace_id"] = trace_id;
    result->fields["batch_manifest_path"] = manifest_path;
    result->fields["batch_total_files"] = std::to_string(manifest_files.size());
    result->fields["known_file_list_complete"] = "true";
    result->fields["directory_listing_complete"] = "true";
    result->fields["batch_manifest_complete"] = "true";
    result->fields["incomplete_scope"] = "remaining_file_contents";

    const auto current_it =
        std::find(manifest_files.begin(), manifest_files.end(), normalized_file_path);
    if (current_it == manifest_files.end()) {
        result->fields["batch_membership"] = "external_to_manifest";
        return;
    }

    std::unordered_set<std::string> read_files = LoadTraceReadFileSet(config, trace_id);
    read_files.insert(normalized_file_path);

    int read_count = 0;
    std::string next_batch_file_path;
    int next_batch_file_index = -1;
    int manifest_index = 0;
    for (const std::string & file_path : manifest_files) {
        if (read_files.find(file_path) != read_files.end()) {
            ++read_count;
            ++manifest_index;
            continue;
        }
        if (next_batch_file_path.empty()) {
            next_batch_file_path = file_path;
            next_batch_file_index = manifest_index;
        }
        ++manifest_index;
    }

    const int remaining_count = static_cast<int>(manifest_files.size()) - read_count;
    const bool batch_complete = remaining_count <= 0;
    result->fields["batch_membership"] = "manifest_tracked";
    result->fields["batch_read_file_count"] = std::to_string(std::max(0, read_count));
    result->fields["remaining_batch_file_count"] = std::to_string(std::max(0, remaining_count));
    result->fields["batch_completion"] = batch_complete ? "complete" : "incomplete";
    result->fields["directory_complete"] = batch_complete ? "true" : "false";
    result->fields["next_batch_file_path"] = batch_complete ? "" : next_batch_file_path;
    result->fields["next_batch_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";

    const bool file_read_complete = GetFieldOrDefault(*result, "read_complete", "true") == "true";
    if (!file_read_complete) {
        result->fields["batch_next_action"] = "finish paging the current file before advancing to the next file";
        return;
    }

    if (!batch_complete) {
        result->fields["task_completion"] = "incomplete";
        result->fields["analysis_allowed"] = "false";
        result->fields["continue_required"] = "true";
        result->fields["auto_continue_required"] = "true";
        result->fields["user_confirmation_required"] = "false";
        result->fields["analysis_blocked_reason"] = "directory_batch_read_incomplete";
        result->fields["partial_read_policy"] =
            "the directory file list is already complete; do not relist; continue reading remaining files from the directory batch manifest";
        result->fields["stop_condition"] = "batch_completion=complete";
        result->fields["next_tool_name"] = "lan_agent_read_directory_files";
        result->fields["next_file_path"] = next_batch_file_path;
        result->fields["next_start_line"] = "1";
        result->fields["next_max_lines"] = GetFieldOrDefault(*result, "max_lines", "500");
        result->fields["truncated"] = "true";
        result->fields["result"] = "directory_batch_partial";
        result->fields["outcome_hint"] = "PARTIAL";
        result->fields["next_action"] = "directory file list is complete; read next_batch_file_path from the same manifest before any conclusion";
        result->fields["batch_next_action"] = "do not relist the directory; continue reading remaining files from the directory batch manifest";
        const std::string manifest_directory = LoadDirectoryReadManifestDirectory(config, trace_id);
        const std::string directory_for_flow = manifest_directory.empty()
            ? std::filesystem::path(next_batch_file_path).parent_path().string()
            : manifest_directory;
        result->fields["next_call_json"] = BuildDirectoryReadContinuationCallJson(
            directory_for_flow,
            static_cast<int>(manifest_files.size()),
            std::max(1, std::atoi(GetFieldOrDefault(*result, "max_lines", "500").c_str())),
            5,
            2500,
            std::max(0, next_batch_file_index),
            1,
            trace_id);
    } else {
        result->fields["task_completion"] = "complete";
        result->fields["analysis_allowed"] = "true";
        result->fields["continue_required"] = "false";
        result->fields["auto_continue_required"] = "false";
        result->fields["user_confirmation_required"] = "false";
        result->fields["analysis_blocked_reason"] = "";
        result->fields["truncated"] = "false";
        result->fields["result"] = "directory_batch_complete";
        result->fields["outcome_hint"] = "PASS";
        result->fields["next_action"] = "all files in the directory batch were read; analysis is allowed";
        result->fields["next_tool_name"] = "";
        result->fields["next_file_path"] = "";
        result->fields["next_start_line"] = "";
        result->fields["next_max_lines"] = "";
        result->fields["next_call_json"] = "";
        result->fields["required_tool_name"] = "";
        result->fields["required_tool_arguments_json"] = "";
        result->fields["batch_next_action"] = "directory batch is fully read";
        result->fields["content_read_completion"] = "complete";
        result->fields["continuation_status"] = "complete";
        result->fields["incomplete_scope"] = "";
    }
}

bool IsStructuredBodyReadCandidate(const std::filesystem::path & normalized_path) {
    const std::string extension = ToLowerAscii(normalized_path.extension().string());
    if (extension != ".json" && extension != ".jsonl") {
        return false;
    }
    const std::string path_text = ToLowerAscii(normalized_path.string());
    return path_text.find("rag_index") != std::string::npos
        || path_text.find("\\build\\") != std::string::npos
        || path_text.find("/build/") != std::string::npos;
}

std::string StructuredPayloadFormatForPath(const std::filesystem::path & normalized_path) {
    const std::string extension = ToLowerAscii(normalized_path.extension().string());
    if (extension == ".json") {
        return "json";
    }
    if (extension == ".jsonl") {
        return "jsonl";
    }
    return "plain_text";
}

CommandResult ReadTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines = 500,
    int start_line = 1,
    const std::string & trace_id = std::string(),
    std::size_t start_byte_offset = 0,
    const std::string & probe_ref = std::string()) {
    CommandResult result;
    result.fields["task_type"] = "file_read";
    result.fields["file_path"] = file_path;
    if (!probe_ref.empty()) {
        result.fields["probe_ref"] = probe_ref;
        result.fields["probe_ready"] = "true";
    }
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        result.fields["error_code"] = path_error == "path is outside allowed roots"
            ? "path_outside_allowed_roots"
            : "path_resolution_failed";
        result.fields["next_action"] = "add the target project root to allowed_roots or retry with a path under an allowed root";
        return result;
    }

    const bool structured_body_read = IsStructuredBodyReadCandidate(normalized);
    result.fields["structured_body_read_mode"] = structured_body_read ? "native_structured_body" : "disabled";
    result.fields["structured_body_helper_bypassed"] = structured_body_read ? "true" : "false";
    result.fields["pagination_basis"] = "line_based";
    result.fields["start_byte_offset"] = std::to_string(start_byte_offset);
    result.fields["effective_page_byte_limit"] = std::to_string(kStructuredBodyPageByteLimit);

    if (!structured_body_read) {
        const CommandResult helper_result = RunDirectoryAccessHelper(
            config,
            "read-file-page",
            {
                "--file " + QuoteDirectoryAccessArgument(normalized.string()),
                "--start-line " + std::to_string(start_line > 0 ? start_line : 1),
                "--max-lines " + std::to_string(max_lines > 0 ? max_lines : 500)
            },
            "directory_access_read_file_page");
        if (helper_result.ok) {
            const int bounded_start_line = start_line > 0 ? start_line : 1;
            const int bounded_max_lines = max_lines > 0 ? max_lines : 500;
            const int line_count = std::max(0, std::atoi(ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "line_count").c_str()));
            const int total_lines = std::max(0, std::atoi(ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "total_lines").c_str()));
            const int end_line = std::max(0, std::atoi(ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "end_line").c_str()));
            const bool has_more = ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "has_more") == "true";
            const bool read_complete = !has_more;
            const int next_start_line = bounded_start_line + line_count;
            const int page_index = ((bounded_start_line - 1) / bounded_max_lines) + 1;
            const int page_count = std::max(1, (total_lines + bounded_max_lines - 1) / bounded_max_lines);

            CommandResult result_from_helper;
            result_from_helper.ok = true;
            result_from_helper.exit_code = 0;
            result_from_helper.fields["task_type"] = "file_read";
            result_from_helper.fields["file_path"] = file_path;
            if (!trace_id.empty()) {
                result_from_helper.fields["trace_id"] = trace_id;
            }
            result_from_helper.fields["normalized_path"] = normalized.string();
            result_from_helper.fields["current_file_path"] = normalized.string();
            result_from_helper.fields["start_line"] = std::to_string(bounded_start_line);
            result_from_helper.fields["end_line"] = line_count > 0 ? std::to_string(end_line) : "";
            result_from_helper.fields["line_count"] = std::to_string(line_count);
            result_from_helper.fields["returned_lines"] = std::to_string(line_count);
            result_from_helper.fields["total_lines"] = std::to_string(total_lines);
            result_from_helper.fields["remaining_lines"] = has_more
                ? std::to_string(std::max(0, total_lines - end_line))
                : "0";
            result_from_helper.fields["max_lines"] = std::to_string(bounded_max_lines);
            result_from_helper.fields["next_start_line"] = has_more ? std::to_string(next_start_line) : "";
            result_from_helper.fields["has_more"] = has_more ? "true" : "false";
            result_from_helper.fields["read_complete"] = read_complete ? "true" : "false";
            result_from_helper.fields["file_complete"] = read_complete ? "true" : "false";
            result_from_helper.fields["read_status"] = read_complete ? "complete" : "partial";
            result_from_helper.fields["task_completion"] = read_complete ? "complete" : "incomplete";
            result_from_helper.fields["page_status"] = has_more ? "partial_page" : "final_page";
            result_from_helper.fields["page_index"] = std::to_string(page_index);
            result_from_helper.fields["page_count"] = std::to_string(page_count);
            result_from_helper.fields["requires_followup"] = has_more ? "true" : "false";
            result_from_helper.fields["file_bytes"] = "0";
            result_from_helper.fields["content"] = ExtractHelperContentBlock(
                helper_result.fields.at("directory_access_helper_output"));
            result_from_helper.fields["content_text"] = result_from_helper.fields["content"];
            result_from_helper.fields["content_begin_marker"] = "content_begin<<<";
            result_from_helper.fields["content_end_marker"] = ">>>content_end";
            result_from_helper.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
            result_from_helper.fields["pagination_basis"] = "line_based";
            result_from_helper.fields["start_byte_offset"] = "0";
            result_from_helper.fields["returned_bytes"] =
                std::to_string(result_from_helper.fields["content"].size());
            result_from_helper.fields["total_bytes"] = "0";
            result_from_helper.fields["remaining_bytes"] = "0";
            result_from_helper.fields["next_byte_offset"] = "";
            result_from_helper.fields["effective_page_byte_limit"] =
                std::to_string(kStructuredBodyPageByteLimit);
            result_from_helper.fields["directory_access_helper_used"] = "true";
            result_from_helper.fields["directory_access_helper_path"] =
                GetFieldOrDefault(helper_result, "directory_access_helper_path", "");
            result_from_helper.fields["directory_access_helper_log_path"] =
                GetFieldOrDefault(helper_result, "directory_access_helper_log_path", "");
            result_from_helper.fields["result_ref"] =
                result_from_helper.fields["directory_access_helper_log_path"];
            result_from_helper.fields["evidence_ref"] =
                result_from_helper.fields["directory_access_helper_log_path"];
            result_from_helper.fields["structured_body_read_mode"] = "helper_line_page";
            result_from_helper.fields["structured_body_helper_bypassed"] = "false";
            if (has_more) {
            result_from_helper.fields["continue_required"] = "true";
            result_from_helper.fields["auto_continue_required"] = "true";
            result_from_helper.fields["user_confirmation_required"] = "false";
            result_from_helper.fields["analysis_allowed"] = "false";
            result_from_helper.fields["analysis_blocked_reason"] = "file_read_incomplete";
            result_from_helper.fields["result"] = "file_read_partial";
            result_from_helper.fields["outcome_hint"] = "PARTIAL";
            result_from_helper.fields["next_action"] = "continue reading the next page of the same file before concluding";
            result_from_helper.fields["next_tool_name"] = "lan_agent_read_text_file";
            result_from_helper.fields["next_call_json"] =
                "{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
                + codex_lan_agent::JsonEscape(file_path)
                + "\",\"max_lines\":" + std::to_string(bounded_max_lines)
                + ",\"start_line\":" + std::to_string(next_start_line)
                + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
                + "}}";
            } else {
            result_from_helper.fields["continue_required"] = "false";
            result_from_helper.fields["auto_continue_required"] = "false";
            result_from_helper.fields["user_confirmation_required"] = "false";
            result_from_helper.fields["analysis_allowed"] = "true";
            result_from_helper.fields["analysis_blocked_reason"] = "";
            result_from_helper.fields["result"] = "file_read_complete";
            result_from_helper.fields["outcome_hint"] = "PASS";
            result_from_helper.fields["next_action"] = "file page read is complete";
            result_from_helper.fields["next_tool_name"] = "";
            result_from_helper.fields["next_call_json"] = "";
            }
            return result_from_helper;
        }
    }

    if (structured_body_read) {
        std::string raw_content;
        std::string read_error;
        if (!ReadWholeFile(normalized, &raw_content, &read_error)) {
            result.ok = false;
            result.exit_code = 23;
            result.fields["error"] = read_error;
            return result;
        }

        const std::size_t total_bytes = raw_content.size();
        const std::size_t bounded_offset = std::min(start_byte_offset, total_bytes);
        const bool use_byte_chunk_paging =
            bounded_offset > 0 || total_bytes > kStructuredBodyPageByteLimit;
        if (use_byte_chunk_paging) {
            const std::size_t returned_bytes =
                std::min(kStructuredBodyPageByteLimit, total_bytes - bounded_offset);
            const std::size_t next_byte_offset = bounded_offset + returned_bytes;
            const bool has_more = next_byte_offset < total_bytes;
            const bool read_complete = !has_more;
            const std::string chunk = raw_content.substr(bounded_offset, returned_bytes);

            const int total_lines = raw_content.empty()
                ? 0
                : static_cast<int>(std::count(raw_content.begin(), raw_content.end(), '\n')) + 1;
            const int prefix_line_count = bounded_offset == 0
                ? 0
                : static_cast<int>(std::count(raw_content.begin(), raw_content.begin() + bounded_offset, '\n'));
            const int chunk_newline_count =
                static_cast<int>(std::count(chunk.begin(), chunk.end(), '\n'));
            const int estimated_start_line = total_lines == 0 ? 1 : prefix_line_count + 1;
            const int estimated_end_line = chunk.empty()
                ? 0
                : (estimated_start_line + chunk_newline_count);
            const int estimated_line_count = chunk.empty()
                ? 0
                : std::max(1, estimated_end_line - estimated_start_line + 1);

            result.fields["normalized_path"] = normalized.string();
            result.fields["current_file_path"] = normalized.string();
            result.fields["start_line"] = std::to_string(estimated_start_line);
            result.fields["end_line"] = chunk.empty() ? "" : std::to_string(estimated_end_line);
            result.fields["line_count"] = std::to_string(estimated_line_count);
            result.fields["returned_lines"] = std::to_string(estimated_line_count);
            result.fields["total_lines"] = std::to_string(total_lines);
            result.fields["remaining_lines"] = has_more ? "1" : "0";
            result.fields["max_lines"] = std::to_string(max_lines > 0 ? max_lines : 500);
            result.fields["next_start_line"] = "";
            result.fields["has_more"] = has_more ? "true" : "false";
            result.fields["read_complete"] = read_complete ? "true" : "false";
            result.fields["file_complete"] = read_complete ? "true" : "false";
            result.fields["read_status"] = read_complete ? "complete" : "partial";
            result.fields["task_completion"] = read_complete ? "complete" : "incomplete";
            result.fields["page_status"] = has_more ? "partial_page" : "final_page";
            result.fields["page_index"] = std::to_string(
                static_cast<int>(bounded_offset / kStructuredBodyPageByteLimit) + 1);
            result.fields["page_count"] = std::to_string(
                static_cast<int>((total_bytes + kStructuredBodyPageByteLimit - 1) / kStructuredBodyPageByteLimit));
            result.fields["requires_followup"] = has_more ? "true" : "false";
            result.fields["continue_required"] = has_more ? "true" : "false";
            result.fields["auto_continue_required"] = has_more ? "true" : "false";
            result.fields["user_confirmation_required"] = "false";
            result.fields["analysis_allowed"] = read_complete ? "true" : "false";
            result.fields["analysis_blocked_reason"] = has_more ? "file_read_incomplete" : "";
            result.fields["partial_read_policy"] = has_more
                ? "structured body is chunk-paged by byte offset; continue with next_call_json until read_complete=true"
                : "structured body read is complete; analysis is allowed";
            result.fields["stop_condition"] = "read_complete=true";
            result.fields["next_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
            result.fields["next_file_path"] = has_more ? file_path : "";
            result.fields["next_max_lines"] = has_more ? std::to_string(max_lines > 0 ? max_lines : 500) : "";
            result.fields["truncated"] = has_more ? "true" : "false";
            result.fields["file_bytes"] = std::to_string(total_bytes);
            result.fields["total_bytes"] = std::to_string(total_bytes);
            result.fields["returned_bytes"] = std::to_string(returned_bytes);
            result.fields["remaining_bytes"] =
                has_more ? std::to_string(total_bytes - next_byte_offset) : "0";
            result.fields["next_byte_offset"] = has_more ? std::to_string(next_byte_offset) : "";
            result.fields["read_mode"] = "structured_body_byte_page";
            result.fields["read_contract"] =
                "repeat lan_agent_read_text_file with start_byte_offset=next_byte_offset until has_more=false";
            result.fields["completion_rule"] = "do not claim the file is fully read unless read_complete=true";
            result.fields["result"] = has_more ? "partial_read" : "complete_read";
            result.fields["outcome_hint"] = has_more ? "PARTIAL" : "PASS";
            result.fields["next_action"] = has_more
                ? "continue reading with next_byte_offset"
                : "file read is complete";
            result.fields["content"] = chunk;
            result.fields["content_text"] = chunk;
            result.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
            result.fields["content_payload_scope"] = has_more ? "file_body_chunk" : "file_body_only";
            result.fields["content_payload_boundary_safe"] = "true";
            result.fields["structured_body_read_mode"] = "native_structured_body_byte_page";
            result.fields["structured_body_helper_bypassed"] = "true";
            result.fields["pagination_basis"] = "byte_offset_for_structured_body";
            result.fields["single_line_payload_warning"] =
                (total_lines <= 1 && total_bytes > kStructuredBodyPageByteLimit) ? "true" : "false";
            result.fields["next_call_json"] = has_more
                ? ("{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
                    + codex_lan_agent::JsonEscape(file_path)
                    + "\",\"start_byte_offset\":"
                    + std::to_string(next_byte_offset)
                    + ",\"max_lines\":"
                    + std::to_string(max_lines > 0 ? max_lines : 500)
                    + ",\"start_line\":1"
                    + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
                    + (probe_ref.empty() ? std::string() : ",\"probe_ref\":\"" + codex_lan_agent::JsonEscape(probe_ref) + "\",\"probe_ready\":true")
                    + "}}")
                : "";
            result.fields["required_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
            result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
            return result;
        }
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = "failed to open file";
        result.fields["error_code"] = "file_open_failed";
        result.fields["next_action"] = "verify the file exists, is readable, and is not locked";
        return result;
    }

    const int bounded_start_line = start_line > 0 ? start_line : 1;
    const int bounded_max_lines = max_lines > 0 ? max_lines : 500;
    std::uintmax_t file_bytes = 0;
    std::error_code size_ec;
    file_bytes = std::filesystem::file_size(normalized, size_ec);
    if (size_ec) {
        file_bytes = 0;
    }

    std::ostringstream content;
    std::string line;
    int current_line = 0;
    int line_count = 0;
    int last_line_read = 0;
    while (std::getline(input, line)) {
        ++current_line;
        if (current_line < bounded_start_line) {
            continue;
        }
        if (line_count >= bounded_max_lines) {
            continue;
        }
        content << line << "\n";
        ++line_count;
        last_line_read = current_line;
    }
    const int total_lines = current_line;
    const int next_start_line = bounded_start_line + line_count;
    const bool has_more = line_count > 0 && last_line_read < total_lines;
    const bool read_complete = !has_more;
    const int page_index = bounded_max_lines > 0
        ? ((bounded_start_line - 1) / bounded_max_lines) + 1
        : 1;
    const int page_count = bounded_max_lines > 0
        ? std::max(1, (total_lines + bounded_max_lines - 1) / bounded_max_lines)
        : 1;

    result.fields["normalized_path"] = normalized.string();
    result.fields["current_file_path"] = normalized.string();
    result.fields["start_line"] = std::to_string(bounded_start_line);
    result.fields["end_line"] = line_count > 0 ? std::to_string(last_line_read) : "";
    result.fields["line_count"] = std::to_string(line_count);
    result.fields["returned_lines"] = std::to_string(line_count);
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["remaining_lines"] = has_more
        ? std::to_string(std::max(0, total_lines - last_line_read))
        : "0";
    result.fields["max_lines"] = std::to_string(bounded_max_lines);
    result.fields["next_start_line"] =
        has_more ? std::to_string(next_start_line) : "";
    result.fields["has_more"] = has_more ? "true" : "false";
    result.fields["read_complete"] = read_complete ? "true" : "false";
    result.fields["file_complete"] = read_complete ? "true" : "false";
    result.fields["read_status"] = read_complete ? "complete" : "partial";
    result.fields["task_completion"] = read_complete ? "complete" : "incomplete";
    result.fields["page_status"] = has_more ? "partial_page" : "final_page";
    result.fields["page_index"] = std::to_string(page_index);
    result.fields["page_count"] = std::to_string(page_count);
    result.fields["requires_followup"] = has_more ? "true" : "false";
    result.fields["continue_required"] = has_more ? "true" : "false";
    result.fields["auto_continue_required"] = has_more ? "true" : "false";
    result.fields["user_confirmation_required"] = "false";
    result.fields["analysis_allowed"] = read_complete ? "true" : "false";
    result.fields["analysis_blocked_reason"] = has_more ? "file_read_incomplete" : "";
    result.fields["partial_read_policy"] = has_more
        ? "do not ask user whether to continue; automatically call next_call_json until read_complete=true unless the user explicitly requested a partial range"
        : "file read is complete; analysis is allowed";
    result.fields["stop_condition"] = "read_complete=true";
    result.fields["next_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
    result.fields["next_file_path"] = has_more ? file_path : "";
    result.fields["next_max_lines"] = has_more ? std::to_string(bounded_max_lines) : "";
    result.fields["truncated"] = has_more ? "true" : "false";
    result.fields["file_bytes"] = std::to_string(file_bytes);
    result.fields["read_mode"] = "paged_lines";
    result.fields["read_contract"] = "repeat lan_agent_read_text_file with start_line=next_start_line until has_more=false";
    result.fields["completion_rule"] = "do not claim the file is fully read unless read_complete=true";
    result.fields["result"] = has_more ? "partial_read" : "complete_read";
    result.fields["outcome_hint"] = has_more ? "PARTIAL" : "PASS";
    result.fields["next_action"] = has_more
        ? "continue reading with next_start_line"
        : "file read is complete";
    result.fields["next_call_json"] = has_more
        ? ("{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
            + codex_lan_agent::JsonEscape(file_path)
            + "\",\"start_line\":"
            + std::to_string(next_start_line)
            + ",\"max_lines\":"
            + std::to_string(bounded_max_lines)
            + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
            + (probe_ref.empty() ? std::string() : ",\"probe_ref\":\"" + codex_lan_agent::JsonEscape(probe_ref) + "\",\"probe_ready\":true")
            + "}}")
        : "";
    result.fields["required_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    result.fields["content"] = content.str();
    result.fields["content_text"] = result.fields["content"];
    result.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
    result.fields["content_payload_scope"] = "file_body_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["returned_bytes"] = std::to_string(result.fields["content"].size());
    result.fields["total_bytes"] = std::to_string(file_bytes);
    result.fields["remaining_bytes"] = "0";
    result.fields["next_byte_offset"] = "";
    ApplyDirectoryBatchProgress(config, trace_id, normalized.string(), &result);
    return result;
}

CommandResult ProbeTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & primary_intent,
    const std::string & trace_id,
    const std::string & directory_manifest_path,
    int directory_current_file_index,
    int directory_total_code_file_count) {
    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["task_type"] = "file_probe";
    result.fields["file_path"] = file_path;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        result.fields["error_code"] = path_error == "path is outside allowed roots"
            ? "path_outside_allowed_roots"
            : "path_resolution_failed";
        result.fields["next_action"] = "add the target project root to allowed_roots or retry with a path under an allowed root";
        return result;
    }

    std::error_code size_ec;
    std::uintmax_t file_bytes = std::filesystem::file_size(normalized, size_ec);
    if (size_ec) {
        file_bytes = 0;
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = "failed to open file";
        result.fields["error_code"] = "file_open_failed";
        result.fields["next_action"] = "verify the file exists, is readable, and is not locked";
        return result;
    }

    std::string line;
    int total_lines = 0;
    while (std::getline(input, line)) {
        ++total_lines;
    }

    const std::string canonical_primary_intent = NormalizeLocalAiPrimaryIntent(primary_intent);
    const std::string lowered_intent = ToLowerAscii(Trim(canonical_primary_intent));
    const bool comment_cleanup_intent = IsLocalAiCommentCleanupIntent(primary_intent);
    const bool code_format_intent = IsLocalAiCodeFormatIntent(primary_intent);
    const bool segmented_intent = comment_cleanup_intent
        || (!code_format_intent && lowered_intent.find("cleanup") != std::string::npos)
        || lowered_intent.find("comment") != std::string::npos
        || lowered_intent.find("localized") != std::string::npos
        || lowered_intent.find("edit") != std::string::npos
        || lowered_intent.find("source_edit_planning") != std::string::npos;
    const bool structured_body_read = IsStructuredBodyReadCandidate(normalized);
    const std::string file_length_class = file_bytes >= (1024ULL * 1024ULL)
        ? "large"
        : (file_bytes >= 64ULL * 1024ULL ? "medium" : "small");
    const int recommended_read_max_lines = file_bytes >= (256ULL * 1024ULL) || total_lines >= 3000 ? 200 : 500;
    const std::string recommended_next_tool = code_format_intent
        ? "lan_agent_format_code_file"
        : comment_cleanup_intent
        ? "lan_agent_delete_text_range_window_atomic"
        : (segmented_intent
        ? "lan_agent_scan_text_ranges"
        : "lan_agent_read_text_file");
    const std::string recommended_scan_mode = segmented_intent ? "comments" : "";

    std::ostringstream next_call_json;
    if (code_format_intent) {
        next_call_json << "{\"name\":\"lan_agent_format_code_file\",\"arguments\":{\"source_file\":\""
                       << codex_lan_agent::JsonEscape(file_path)
                       << "\",\"dry_run\":true}}";
    } else if (comment_cleanup_intent) {
        next_call_json << "{\"name\":\"lan_agent_delete_text_range_window_atomic\",\"arguments\":{\"file_path\":\""
                       << codex_lan_agent::JsonEscape(file_path)
                       << "\",\"scan_mode\":\"comments\",\"start_line\":1,\"next_start_line\":1,\"max_lines\":200"
                       << ",\"primary_intent\":\"comment_cleanup\"";
        if (!directory_manifest_path.empty()) {
            next_call_json << ",\"directory_manifest_path\":\""
                           << codex_lan_agent::JsonEscape(directory_manifest_path)
                           << "\",\"directory_current_file_index\":"
                           << std::max(0, directory_current_file_index)
                           << ",\"directory_total_code_file_count\":"
                           << std::max(0, directory_total_code_file_count)
                           << ",\"directory_scope_active\":true";
        }
        if (!trace_id.empty()) {
            next_call_json << ",\"trace_id\":\""
                           << codex_lan_agent::JsonEscape(trace_id)
                           << "\"";
        }
        next_call_json << ",\"probe_ref\":\""
                       << codex_lan_agent::JsonEscape(normalized.string())
                       << "\",\"probe_ready\":true}}";
    } else if (segmented_intent) {
        next_call_json << "{\"name\":\"lan_agent_scan_text_ranges\",\"arguments\":{\"file_path\":\""
                       << codex_lan_agent::JsonEscape(file_path)
                       << "\",\"scan_mode\":\"comments\"";
        if (!canonical_primary_intent.empty()) {
            next_call_json << ",\"primary_intent\":\""
                           << codex_lan_agent::JsonEscape(canonical_primary_intent)
                           << "\"";
        }
        if (!trace_id.empty()) {
            next_call_json << ",\"trace_id\":\""
                           << codex_lan_agent::JsonEscape(trace_id)
                           << "\"";
        }
        next_call_json << ",\"probe_ref\":\""
                       << codex_lan_agent::JsonEscape(normalized.string())
                       << "\",\"probe_ready\":true}}";
    } else {
        next_call_json << "{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
                       << codex_lan_agent::JsonEscape(file_path)
                       << "\",\"max_lines\":"
                       << recommended_read_max_lines
                       << ",\"start_line\":1";
        if (!canonical_primary_intent.empty()) {
            next_call_json << ",\"primary_intent\":\""
                           << codex_lan_agent::JsonEscape(canonical_primary_intent)
                           << "\"";
        }
        if (!trace_id.empty()) {
            next_call_json << ",\"trace_id\":\""
                           << codex_lan_agent::JsonEscape(trace_id)
                           << "\"";
        }
        next_call_json << ",\"probe_ref\":\""
                       << codex_lan_agent::JsonEscape(normalized.string())
                       << "\",\"probe_ready\":true}}";
    }

    RememberRecentProbePath(normalized.string());
    result.fields["normalized_path"] = normalized.string();
    result.fields["current_file_path"] = normalized.string();
    result.fields["probe_mode"] = "true";
    result.fields["probe_complete"] = "true";
    result.fields["probe_ref"] = normalized.string();
    result.fields["probe_ready"] = "true";
    result.fields["probe_contract"] = "probe file length before choosing read/write/edit mode";
    result.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
    result.fields["content_payload_scope"] = "file_metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["structured_body_read_mode"] = structured_body_read ? "probe_only_structured_candidate" : "probe_only";
    result.fields["structured_body_helper_bypassed"] = structured_body_read ? "true" : "false";
    result.fields["pagination_basis"] = "metadata_only";
    result.fields["file_bytes"] = std::to_string(file_bytes);
    result.fields["total_bytes"] = std::to_string(file_bytes);
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["line_count"] = "0";
    result.fields["returned_lines"] = "0";
    result.fields["remaining_lines"] = "0";
    result.fields["read_complete"] = "false";
    result.fields["file_complete"] = "false";
    result.fields["has_more"] = "false";
    result.fields["task_completion"] = "complete";
    result.fields["continue_required"] = "false";
    result.fields["auto_continue_required"] = "false";
    result.fields["analysis_allowed"] = "true";
    result.fields["file_length_class"] = file_length_class;
    result.fields["primary_intent"] = canonical_primary_intent;
    result.fields["normalized_primary_intent"] = canonical_primary_intent;
    if (!directory_manifest_path.empty()) {
        result.fields["directory_scope_active"] = "true";
        result.fields["directory_manifest_path"] = directory_manifest_path;
        result.fields["directory_current_file_index"] = std::to_string(std::max(0, directory_current_file_index));
        result.fields["directory_total_code_file_count"] = std::to_string(std::max(0, directory_total_code_file_count));
        const int remaining = std::max(0, directory_total_code_file_count - std::max(0, directory_current_file_index));
        result.fields["directory_remaining_code_file_count"] = std::to_string(remaining);
    }
    result.fields["recommended_next_tool"] = recommended_next_tool;
    result.fields["recommended_read_max_lines"] = std::to_string(recommended_read_max_lines);
    result.fields["recommended_scan_mode"] = recommended_scan_mode;
        result.fields["next_call_json"] = next_call_json.str();
    result.fields["result_ref"] = normalized.string();
    result.fields["evidence_ref"] = normalized.string();
    result.fields["next_tool_name"] = recommended_next_tool;
    result.fields["next_action"] = code_format_intent
        ? "call lan_agent_format_code_file with dry_run=true first; if would_change=true and user requested cleanup, call it again with dry_run=false"
        : segmented_intent
        ? (comment_cleanup_intent
            ? "call lan_agent_delete_text_range_window_atomic with max_lines=200 and repeat next_call_json until has_more=false"
            : "call lan_agent_scan_text_ranges first, then lan_agent_prepare_edit_windows for segmented edits")
        : "call lan_agent_read_text_file with a bounded page after the probe confirms file size";
    result.fields["summary"] = "text file probe complete";
    result.fields["status"] = "success";
    result.fields["result"] = "probe_complete";
    result.fields["outcome_hint"] = "PASS";
    result.fields["content"] = "";
    result.fields["content_text"] = "";
    result.fields["required_tool_name"] = recommended_next_tool;
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    result.fields["single_line_payload_warning"] = "false";
    return result;
}
CommandResult TailTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines = 120) {
    CommandResult result;
    result.fields["file_path"] = file_path;

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 24;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 25;
        result.fields["error"] = path_error;
        return result;
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 27;
        result.fields["error"] = "failed to open file";
        return result;
    }

    std::deque<std::string> tail_lines;
    std::string line;
    int total_lines = 0;
    const int bounded_max_lines = max_lines > 0 ? max_lines : 1;
    while (std::getline(input, line)) {
        if (static_cast<int>(tail_lines.size()) >= bounded_max_lines) {
            tail_lines.pop_front();
        }
        tail_lines.push_back(line);
        ++total_lines;
    }

    std::ostringstream content;
    for (const std::string & tail_line : tail_lines) {
        content << tail_line << "\n";
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["line_count"] = std::to_string(static_cast<int>(tail_lines.size()));
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["content"] = content.str();
    return result;
}

CommandResult ListDirectoryResult(
    const AgentConfig & config,
    const std::string & directory_path,
    int max_entries,
    const std::string & trace_id,
    const std::string & primary_intent) {
    CommandResult result;
    result.fields["task_type"] = "directory_list";
    result.fields["directory_path"] = directory_path;
    const bool comment_cleanup_intent = IsLocalAiCommentCleanupIntent(primary_intent);
    if (!primary_intent.empty()) {
        result.fields["primary_intent"] = NormalizeLocalAiPrimaryIntent(primary_intent);
    }
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 30;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, directory_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 31;
        result.fields["error"] = path_error;
        return result;
    }

    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 33;
        result.fields["error"] = "directory does not exist";
        return result;
    }

    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 34;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    const CommandResult helper_result = RunDirectoryAccessHelper(
        config,
        "list-directory",
        {
            "--directory " + QuoteDirectoryAccessArgument(normalized.string()),
            "--max-entries " + std::to_string(max_entries > 0 ? max_entries : 200)
        },
        "directory_access_list_directory");
    if (helper_result.ok) {
        std::vector<std::string> manifest_files;
        std::vector<std::string> preview_files;
        std::vector<std::string> file_names;
        std::vector<std::string> preview_file_names;
        std::vector<std::string> all_entry_labels;
        const std::string helper_output =
            GetFieldOrDefault(helper_result, "directory_access_helper_output", "");
        const int total_entries = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "total_entries").c_str()));
        const int returned_count = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "returned_count").c_str()));
        const int file_count = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "file_count").c_str()));
        const int directory_count = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "directory_count").c_str()));
        for (int index = 0; index < returned_count; ++index) {
            const std::string label = ExtractHelperKvValue(helper_output, "entry_" + std::to_string(index));
            if (!label.empty()) {
                result.fields["entry_" + std::to_string(index)] = label;
                all_entry_labels.push_back(label);
            }
        }
        for (int index = 0; index < file_count; ++index) {
            const std::string path = ExtractHelperKvValue(helper_output, "file_path_" + std::to_string(index));
            const std::string name = ExtractHelperKvValue(helper_output, "file_name_" + std::to_string(index));
            if (!path.empty()) {
                manifest_files.push_back(path);
                if (static_cast<int>(preview_files.size()) < returned_count) {
                    preview_files.push_back(path);
                }
            }
            if (!name.empty()) {
                file_names.push_back(name);
                if (static_cast<int>(preview_file_names.size()) < returned_count) {
                    preview_file_names.push_back(name);
                }
            }
        }

        result.ok = true;
        result.exit_code = 0;
        result.fields["normalized_path"] = normalized.string();
        result.fields["entry_count"] = std::to_string(returned_count);
        result.fields["returned_count"] = std::to_string(returned_count);
        result.fields["total_entries"] = std::to_string(total_entries);
        result.fields["file_count"] = std::to_string(file_count);
        result.fields["directory_count"] = std::to_string(directory_count);
        result.fields["entry_labels_json"] = BuildJsonStringArrayFromStrings(all_entry_labels);
        result.fields["file_paths_json"] = BuildJsonStringArrayFromStrings(preview_files);
        result.fields["file_names_json"] = BuildJsonStringArrayFromStrings(preview_file_names);
        result.fields["file_paths_total_count"] = std::to_string(manifest_files.size());
        result.fields["file_names_total_count"] = std::to_string(file_names.size());
        result.fields["response_preview_truncated"] =
            (manifest_files.size() > preview_files.size() || file_names.size() > preview_file_names.size())
                ? "true"
                : "false";
        result.fields["remaining_entries"] = std::to_string(std::max(0, total_entries - returned_count));
        result.fields["read_mode"] = "directory_manifest";
        result.fields["directory_listing_complete"] = "true";
        result.fields["known_file_list_complete"] = "true";
        result.fields["directory_access_helper_used"] = "true";
        result.fields["directory_access_helper_path"] =
            GetFieldOrDefault(helper_result, "directory_access_helper_path", "");
        result.fields["directory_access_helper_log_path"] =
            GetFieldOrDefault(helper_result, "directory_access_helper_log_path", "");
        result.fields["result_ref"] = result.fields["directory_access_helper_log_path"];
        result.fields["evidence_ref"] = result.fields["directory_access_helper_log_path"];

        std::string manifest_path;
        const bool manifest_saved =
            !trace_id.empty() && SaveDirectoryReadManifest(config, trace_id, normalized.string(), manifest_files, &manifest_path);
        result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
        result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
        result.fields["batch_manifest_complete"] = manifest_saved ? "true" : "false";
        result.fields["batch_total_files"] = std::to_string(manifest_files.size());
        const bool can_continue_directory_batch = manifest_saved && !manifest_files.empty();
        std::vector<std::string> code_files;
        for (const std::string & file_path : manifest_files) {
            if (IsLocalAiCodeSourceFilePath(file_path)) {
                code_files.push_back(file_path);
            }
        }
        const bool can_continue_comment_cleanup = comment_cleanup_intent && manifest_saved && !code_files.empty();
        const std::string next_comment_file_path = can_continue_comment_cleanup ? code_files.front() : std::string();
        result.fields["batch_read_file_count"] = "0";
        result.fields["remaining_batch_file_count"] = can_continue_directory_batch
            ? std::to_string(manifest_files.size())
            : "0";
        result.fields["code_file_count"] = std::to_string(code_files.size());
        result.fields["remaining_code_file_count"] = can_continue_comment_cleanup
            ? std::to_string(code_files.size())
            : "0";
        result.fields["directory_mutation_flow"] = can_continue_comment_cleanup
            ? "comment_cleanup_probe_then_200_line_window_delete"
            : "";
        result.fields["batch_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
        result.fields["content_read_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
        result.fields["incomplete_scope"] = can_continue_comment_cleanup
            ? "remaining_code_files"
            : (can_continue_directory_batch ? "remaining_file_contents" : "");
        result.fields["current_file_path"] = "";
        result.fields["current_file_index"] = "";
        result.fields["next_batch_file_path"] = can_continue_comment_cleanup
            ? next_comment_file_path
            : (can_continue_directory_batch ? manifest_files.front() : "");
        result.fields["next_batch_tool_name"] = can_continue_comment_cleanup
            ? "lan_agent_probe_text_file"
            : (can_continue_directory_batch ? "lan_agent_read_directory_files" : "");
        result.fields["continue_required"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "true" : "false";
        result.fields["auto_continue_required"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "true" : "false";
        result.fields["user_confirmation_required"] = "false";
        result.fields["analysis_allowed"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "false" : "true";
        result.fields["analysis_blocked_reason"] = can_continue_comment_cleanup
            ? "directory_comment_cleanup_incomplete"
            : (can_continue_directory_batch ? "directory_batch_read_incomplete" : "");
        result.fields["task_completion"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "incomplete" : "complete";
        result.fields["partial_read_policy"] = can_continue_comment_cleanup
            ? "directory listing is complete; probe one code file, then delete comments with 200-line windows; do not read all file bodies"
            : (can_continue_directory_batch
            ? "directory listing is complete; continue with lan_agent_read_directory_files until batch_completion=complete"
            : "directory listing is complete; no implicit file read continuation is emitted");
        result.fields["stop_condition"] = can_continue_comment_cleanup
            ? "all code files probed and each delete window reaches has_more=false"
            : (can_continue_directory_batch
            ? "batch_completion=complete"
            : "directory_listing_complete=true");
        result.fields["result"] = can_continue_comment_cleanup
            ? "directory_comment_cleanup_manifest_ready"
            : (can_continue_directory_batch ? "directory_list_manifest_ready" : "directory_list_complete");
        result.fields["outcome_hint"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "PARTIAL" : "PASS";
        result.fields["next_action"] = can_continue_comment_cleanup
            ? "tool_call_only: probe next_batch_file_path, then run lan_agent_delete_text_range_window_atomic with max_lines=200 until has_more=false"
            : (can_continue_directory_batch
            ? "directory listing is complete; continue reading files from the generated manifest before any conclusion"
            : "directory listing is complete");
        result.fields["next_tool_name"] = can_continue_comment_cleanup
            ? "lan_agent_probe_text_file"
            : (can_continue_directory_batch ? "lan_agent_read_directory_files" : "");
        result.fields["next_file_path"] = can_continue_comment_cleanup
            ? next_comment_file_path
            : (can_continue_directory_batch ? manifest_files.front() : "");
        result.fields["next_start_line"] = can_continue_directory_batch ? "1" : "";
        result.fields["next_max_lines"] = can_continue_comment_cleanup
            ? "200"
            : (can_continue_directory_batch ? "500" : "");
        result.fields["truncated"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "true" : "false";
        result.fields["next_call_json"] = can_continue_comment_cleanup
            ? BuildCommentCleanupProbeCallJson(
                next_comment_file_path,
                trace_id,
                manifest_path,
                0,
                static_cast<int>(code_files.size()))
            : (can_continue_directory_batch ? BuildDirectoryReadContinuationCallJson(
                normalized.string(),
                static_cast<int>(manifest_files.size()),
                500,
                5,
                2500,
                0,
                1,
                trace_id) : "");
        result.fields["required_tool_name"] = can_continue_comment_cleanup
            ? "lan_agent_probe_text_file"
            : (can_continue_directory_batch ? "lan_agent_read_directory_files" : "");
        result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
        return result;
    }

    std::vector<std::filesystem::directory_entry> all_entries;
    std::vector<std::string> manifest_files;
    std::vector<std::string> preview_files;
    std::vector<std::string> all_entry_labels;
    std::vector<std::string> file_names;
    std::vector<std::string> preview_file_names;
    for (const auto & entry : std::filesystem::directory_iterator(normalized)) {
        all_entries.push_back(entry);
        if (entry.is_regular_file()) {
            manifest_files.push_back(entry.path().string());
            file_names.push_back(entry.path().filename().string());
        }
    }
    std::sort(
        all_entries.begin(),
        all_entries.end(),
        [](const std::filesystem::directory_entry & left, const std::filesystem::directory_entry & right) {
            return ToLowerAscii(left.path().filename().string()) < ToLowerAscii(right.path().filename().string());
        });
    std::sort(
        manifest_files.begin(),
        manifest_files.end(),
        [](const std::string & left, const std::string & right) {
            return ToLowerAscii(std::filesystem::path(left).filename().string())
                < ToLowerAscii(std::filesystem::path(right).filename().string());
        });

    int index = 0;
    for (const auto & entry : all_entries) {
        const std::string type = entry.is_directory() ? "[dir] " : "[file] ";
        if (index >= max_entries) {
            break;
        }
        all_entry_labels.push_back(type + entry.path().filename().string());
        const std::string key = "entry_" + std::to_string(index);
        result.fields[key] = type + entry.path().filename().string();
        if (entry.is_regular_file()) {
            preview_files.push_back(entry.path().string());
            preview_file_names.push_back(entry.path().filename().string());
        }
        ++index;
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["entry_count"] = std::to_string(index);
    result.fields["returned_count"] = std::to_string(index);
    result.fields["total_entries"] = std::to_string(all_entries.size());
    result.fields["file_count"] = std::to_string(manifest_files.size());
    result.fields["directory_count"] = std::to_string(static_cast<int>(all_entries.size()) - static_cast<int>(manifest_files.size()));
    result.fields["entry_labels_json"] = BuildJsonStringArrayFromStrings(all_entry_labels);
    result.fields["file_paths_json"] = BuildJsonStringArrayFromStrings(preview_files);
    result.fields["file_names_json"] = BuildJsonStringArrayFromStrings(preview_file_names);
    result.fields["file_paths_total_count"] = std::to_string(manifest_files.size());
    result.fields["file_names_total_count"] = std::to_string(file_names.size());
    result.fields["response_preview_truncated"] =
        (manifest_files.size() > preview_files.size() || file_names.size() > preview_file_names.size())
            ? "true"
            : "false";
    result.fields["remaining_entries"] =
        std::to_string(std::max(0, static_cast<int>(all_entries.size()) - index));
    result.fields["read_mode"] = "directory_manifest";
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";

    std::string manifest_path;
    const bool manifest_saved =
        !trace_id.empty() && SaveDirectoryReadManifest(config, trace_id, normalized.string(), manifest_files, &manifest_path);
    result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
    result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
    result.fields["batch_manifest_complete"] = manifest_saved ? "true" : "false";
    result.fields["batch_total_files"] = std::to_string(manifest_files.size());
    const bool can_continue_directory_batch = manifest_saved && !manifest_files.empty();
    std::vector<std::string> code_files;
    for (const std::string & file_path : manifest_files) {
        if (IsLocalAiCodeSourceFilePath(file_path)) {
            code_files.push_back(file_path);
        }
    }
    const bool can_continue_comment_cleanup = comment_cleanup_intent && manifest_saved && !code_files.empty();
    const std::string next_comment_file_path = can_continue_comment_cleanup ? code_files.front() : std::string();
    result.fields["batch_read_file_count"] = "0";
    result.fields["remaining_batch_file_count"] = can_continue_directory_batch
        ? std::to_string(manifest_files.size())
        : "0";
    result.fields["code_file_count"] = std::to_string(code_files.size());
    result.fields["remaining_code_file_count"] = can_continue_comment_cleanup
        ? std::to_string(code_files.size())
        : "0";
    result.fields["directory_mutation_flow"] = can_continue_comment_cleanup
        ? "comment_cleanup_probe_then_200_line_window_delete"
        : "";
    result.fields["batch_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
    result.fields["content_read_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
    result.fields["incomplete_scope"] = can_continue_comment_cleanup
        ? "remaining_code_files"
        : (can_continue_directory_batch ? "remaining_file_contents" : "");
    result.fields["current_file_path"] = "";
    result.fields["current_file_index"] = "";
    result.fields["next_batch_file_path"] = can_continue_comment_cleanup
        ? next_comment_file_path
        : (can_continue_directory_batch ? manifest_files.front() : "");
    result.fields["next_batch_tool_name"] = can_continue_comment_cleanup
        ? "lan_agent_probe_text_file"
        : (can_continue_directory_batch ? "lan_agent_read_directory_files" : "");
    result.fields["continue_required"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "true" : "false";
    result.fields["auto_continue_required"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "true" : "false";
    result.fields["user_confirmation_required"] = "false";
    result.fields["analysis_allowed"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "false" : "true";
    result.fields["analysis_blocked_reason"] = can_continue_comment_cleanup
        ? "directory_comment_cleanup_incomplete"
        : (can_continue_directory_batch ? "directory_batch_read_incomplete" : "");
    result.fields["task_completion"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "incomplete" : "complete";
    result.fields["partial_read_policy"] = can_continue_comment_cleanup
        ? "directory listing is complete; probe one code file, then delete comments with 200-line windows; do not read all file bodies"
        : (can_continue_directory_batch
        ? "directory listing is complete; continue with lan_agent_read_directory_files until batch_completion=complete"
        : "directory listing is complete; no implicit file read continuation is emitted");
    result.fields["stop_condition"] = can_continue_comment_cleanup
        ? "all code files probed and each delete window reaches has_more=false"
        : (can_continue_directory_batch
        ? "batch_completion=complete"
        : "directory_listing_complete=true");
    result.fields["result"] = can_continue_comment_cleanup
        ? "directory_comment_cleanup_manifest_ready"
        : (can_continue_directory_batch ? "directory_list_manifest_ready" : "directory_list_complete");
    result.fields["outcome_hint"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "PARTIAL" : "PASS";
    result.fields["next_action"] = can_continue_comment_cleanup
        ? "tool_call_only: probe next_batch_file_path, then run lan_agent_delete_text_range_window_atomic with max_lines=200 until has_more=false"
        : (can_continue_directory_batch
        ? "directory listing is complete; continue reading files from the generated manifest before any conclusion"
        : "directory listing is complete");
    result.fields["next_tool_name"] = can_continue_comment_cleanup
        ? "lan_agent_probe_text_file"
        : (can_continue_directory_batch ? "lan_agent_read_directory_files" : "");
    result.fields["next_file_path"] = can_continue_comment_cleanup
        ? next_comment_file_path
        : (can_continue_directory_batch ? manifest_files.front() : "");
    result.fields["next_start_line"] = can_continue_directory_batch ? "1" : "";
    result.fields["next_max_lines"] = can_continue_comment_cleanup
        ? "200"
        : (can_continue_directory_batch ? "500" : "");
    result.fields["truncated"] = (can_continue_comment_cleanup || can_continue_directory_batch) ? "true" : "false";
    result.fields["next_call_json"] = can_continue_comment_cleanup
        ? BuildCommentCleanupProbeCallJson(
            next_comment_file_path,
            trace_id,
            manifest_path,
            0,
            static_cast<int>(code_files.size()))
        : (can_continue_directory_batch ? BuildDirectoryReadContinuationCallJson(
            normalized.string(),
            static_cast<int>(manifest_files.size()),
            500,
            5,
            2500,
            0,
            1,
            trace_id) : "");
    result.fields["required_tool_name"] = can_continue_comment_cleanup
        ? "lan_agent_probe_text_file"
        : (can_continue_directory_batch ? "lan_agent_read_directory_files" : "");
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    return result;
}

std::vector<std::string> ParseDirectoryReadExtensionsCsv(const std::string & raw_csv) {
    std::vector<std::string> extensions;
    std::istringstream input(raw_csv);
    std::string token;
    while (std::getline(input, token, ',')) {
        std::string normalized = ToLowerAscii(Trim(token));
        if (normalized.empty()) {
            continue;
        }
        if (normalized.front() != '.') {
            normalized.insert(normalized.begin(), '.');
        }
        if (std::find(extensions.begin(), extensions.end(), normalized) == extensions.end()) {
            extensions.push_back(normalized);
        }
    }
    return extensions;
}

std::string JoinDirectoryReadExtensionsCsv(const std::vector<std::string> & extensions) {
    std::ostringstream output;
    for (std::size_t index = 0; index < extensions.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << extensions[index];
    }
    return output.str();
}

bool MatchesDirectoryReadExtensions(
    const std::filesystem::path & path,
    const std::vector<std::string> & extensions) {
    if (extensions.empty()) {
        return true;
    }
    const std::string extension = ToLowerAscii(path.extension().string());
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

const std::set<std::string> & DirectoryAnalysisSkippedDirectories() {
    static const std::set<std::string> skipped = {
        ".git",
        ".vs",
        ".vscode",
        ".idea",
        ".codex",
        "build",
        "out",
        "dist",
        "node_modules",
        "bin",
        "obj",
        "analysis_workspace",
        "archive_workspace",
    };
    return skipped;
}

std::string DirectoryAnalysisRelativePath(
    const std::filesystem::path & root,
    const std::filesystem::path & path) {
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, root, ec);
    return ec ? path.string() : relative.generic_string();
}

CommandResult ReadDirectoryFilesResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files = 200,
    int max_lines_per_file = 500,
    int max_files_per_call = 5,
    int max_total_lines = 2500,
    int file_index = 0,
    int start_line = 1,
    const std::string & trace_id = std::string(),
    std::size_t start_byte_offset = 0) {
    (void)max_files_per_call;
    (void)max_total_lines;

    CommandResult result;
    result.fields["task_type"] = "directory_file_read";
    result.fields["directory_path"] = directory_path;
    result.fields["trace_id"] = trace_id;

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 60;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    const std::vector<std::string> extension_filters =
        ParseDirectoryReadExtensionsCsv(file_extensions_csv);
    result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
    result.fields["file_type_filter_applied"] = extension_filters.empty() ? "false" : "true";

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, directory_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 61;
        result.fields["error"] = path_error;
        return result;
    }
    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 63;
        result.fields["error"] = "directory does not exist";
        return result;
    }
    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 64;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    std::vector<std::string> matching_files;
    for (const auto & entry : std::filesystem::directory_iterator(normalized)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!MatchesDirectoryReadExtensions(entry.path(), extension_filters)) {
            continue;
        }
        matching_files.push_back(entry.path().string());
    }

    std::sort(
        matching_files.begin(),
        matching_files.end(),
        [](const std::string & left, const std::string & right) {
            return ToLowerAscii(std::filesystem::path(left).filename().string())
                < ToLowerAscii(std::filesystem::path(right).filename().string());
        });

    const int bounded_max_files = max_files > 0 ? max_files : 200;
    if (static_cast<int>(matching_files.size()) > bounded_max_files) {
        matching_files.resize(static_cast<std::size_t>(bounded_max_files));
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";
    result.fields["matched_file_count"] = std::to_string(matching_files.size());
    result.fields["batch_total_files"] = std::to_string(matching_files.size());

    std::string manifest_path;
    const bool manifest_saved =
        !trace_id.empty() && SaveDirectoryReadManifest(config, trace_id, normalized.string(), matching_files, &manifest_path);
    result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
    result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
    result.fields["batch_manifest_complete"] = matching_files.empty() ? "true" : (manifest_saved ? "true" : "false");

    if (matching_files.empty()) {
        result.fields["task_type"] = "directory_file_read";
        result.fields["analysis_allowed"] = "true";
        result.fields["directory_complete"] = "true";
        result.fields["task_completion"] = "complete";
        result.fields["batch_completion"] = "complete";
        result.fields["content_read_completion"] = "complete";
        result.fields["incomplete_scope"] = "";
        result.fields["continue_required"] = "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["remaining_batch_file_count"] = "0";
        result.fields["batch_read_file_count"] = "0";
        result.fields["current_file_index"] = "";
        result.fields["current_file_path"] = "";
        result.fields["next_file_index"] = "";
        result.fields["next_file_path"] = "";
        result.fields["next_batch_file_path"] = "";
        result.fields["next_batch_tool_name"] = "";
        result.fields["result"] = "directory_file_batch_complete";
        result.fields["next_action"] = "no matching files remain to read";
        result.fields["stop_condition"] = "batch_completion=complete";
        result.fields["read_contract"] =
            "repeat lan_agent_read_directory_files with next_file_index and next_start_line until batch_completion=complete";
        result.fields["next_tool_name"] = "";
        result.fields["next_call_json"] = "";
        result.fields["required_tool_name"] = "";
        result.fields["required_tool_arguments_json"] = "";
        return result;
    }

    const int bounded_file_index =
        std::min(
            std::max(0, file_index),
            static_cast<int>(matching_files.size()) - 1);
    const int bounded_max_lines_per_file = max_lines_per_file > 0 ? max_lines_per_file : 500;
    const int bounded_start_line = start_line > 0 ? start_line : 1;
    const std::string current_file_path = matching_files[bounded_file_index];
    const CommandResult page_result = ReadTextFileResult(
        config,
        current_file_path,
        bounded_max_lines_per_file,
        bounded_start_line,
        trace_id,
        start_byte_offset);
    result = page_result;
    if (!result.ok) {
        result.fields["task_type"] = "directory_file_read";
        result.fields["directory_path"] = directory_path;
        result.fields["normalized_directory_path"] = normalized.string();
        result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
        result.fields["matched_file_count"] = std::to_string(matching_files.size());
        result.fields["current_file_index"] = std::to_string(bounded_file_index);
        result.fields["current_file_path"] = current_file_path;
        return result;
    }

    const bool current_file_complete = GetFieldOrDefault(result, "read_complete", "false") == "true";
    const int files_read = bounded_file_index + (current_file_complete ? 1 : 0);
    const int remaining_files =
        std::max(0, static_cast<int>(matching_files.size()) - files_read);
    const bool batch_complete = current_file_complete && remaining_files == 0;
    int next_file_index = bounded_file_index;
    int next_start_line = std::atoi(GetFieldOrDefault(result, "next_start_line", "0").c_str());
    const std::string next_byte_offset = GetFieldOrDefault(result, "next_byte_offset", "");
    std::string next_file_path = current_file_path;
    if (current_file_complete && !batch_complete) {
        next_file_index = bounded_file_index + 1;
        next_start_line = 1;
        next_file_path = matching_files[static_cast<std::size_t>(next_file_index)];
    }

    result.fields["directory_path"] = directory_path;
    result.fields["task_type"] = "directory_file_read";
    result.fields["normalized_directory_path"] = normalized.string();
    result.fields["trace_id"] = trace_id;
    result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
    result.fields["file_type_filter_applied"] = extension_filters.empty() ? "false" : "true";
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";
    result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
    result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
    result.fields["batch_manifest_complete"] = manifest_saved ? "true" : "false";
    result.fields["matched_file_count"] = std::to_string(matching_files.size());
    result.fields["batch_total_files"] = std::to_string(matching_files.size());
    result.fields["batch_read_file_count"] = std::to_string(files_read);
    result.fields["remaining_batch_file_count"] = std::to_string(remaining_files);
    result.fields["directory_complete"] = batch_complete ? "true" : "false";
    result.fields["batch_completion"] = batch_complete ? "complete" : "incomplete";
    result.fields["content_read_completion"] = batch_complete ? "complete" : "incomplete";
    result.fields["incomplete_scope"] = batch_complete ? "" : "remaining_file_contents";
    result.fields["analysis_allowed"] = batch_complete ? "true" : "false";
    result.fields["continue_required"] = batch_complete ? "false" : "true";
    result.fields["auto_continue_required"] = batch_complete ? "false" : "true";
    result.fields["task_completion"] = batch_complete ? "complete" : "incomplete";
    result.fields["result"] = batch_complete ? "directory_file_batch_complete" : "directory_file_batch_partial";
    result.fields["continuation_status"] = batch_complete ? "complete" : "needs_continue";
    result.fields["current_file_index"] = std::to_string(bounded_file_index);
    result.fields["current_file_path"] = current_file_path;
    result.fields["last_file_index"] = std::to_string(static_cast<int>(matching_files.size()) - 1);
    result.fields["last_file_path"] = matching_files.back();
    result.fields["next_batch_file_path"] = batch_complete ? "" : next_file_path;
    result.fields["next_batch_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";
    result.fields["next_file_path"] = batch_complete ? "" : next_file_path;
    result.fields["next_file_index"] = batch_complete ? "" : std::to_string(next_file_index);
    result.fields["next_start_line"] = batch_complete ? "" : std::to_string(next_start_line);
    result.fields["next_byte_offset"] = batch_complete ? "" : next_byte_offset;
    result.fields["next_max_lines"] = batch_complete ? "" : std::to_string(bounded_max_lines_per_file);
    result.fields["next_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";
    result.fields["required_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";
    result.fields["next_action"] = batch_complete
        ? "all matching files were read"
        : (current_file_complete
            ? "read the next file page from the directory batch"
            : "continue reading the current file page before advancing");
    result.fields["stop_condition"] = "batch_completion=complete";
    result.fields["partial_read_policy"] =
        "repeat lan_agent_read_directory_files with next_file_index and next_start_line until batch_completion=complete";
    result.fields["read_contract"] =
        "repeat lan_agent_read_directory_files with next_file_index and next_start_line until batch_completion=complete";
    result.fields["next_call_json"] = batch_complete
        ? ""
        : ("{\"name\":\"lan_agent_read_directory_files\",\"arguments\":{\"directory_path\":\""
            + codex_lan_agent::JsonEscape(directory_path)
            + "\",\"file_extensions_csv\":\""
            + codex_lan_agent::JsonEscape(JoinDirectoryReadExtensionsCsv(extension_filters))
            + "\",\"max_files\":"
            + std::to_string(bounded_max_files)
            + ",\"max_lines_per_file\":"
            + std::to_string(bounded_max_lines_per_file)
            + ",\"max_files_per_call\":"
            + std::to_string(max_files_per_call > 0 ? max_files_per_call : 1)
            + ",\"max_total_lines\":"
            + std::to_string(max_total_lines > 0 ? max_total_lines : 1)
            + ",\"file_index\":"
            + std::to_string(next_file_index)
            + (next_byte_offset.empty()
                ? ",\"start_line\":" + std::to_string(next_start_line)
                : ",\"start_byte_offset\":" + next_byte_offset + ",\"start_line\":1")
            + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
            + "}}");
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    result.fields["server_side_atomic_read"] = "false";
    return result;
}

CommandResult PrepareDirectoryAnalysisResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files = 200,
    int max_excerpt_lines_per_file = 80,
    int max_total_excerpt_lines = 1200,
    std::size_t max_excerpt_chars = 24000,
    const std::string & trace_id = std::string()) {
    CommandResult result;
    result.fields["task_type"] = "directory_analysis_bundle";
    result.fields["directory_path"] = directory_path;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 68;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    std::filesystem::path normalized;
    std::error_code ec;
    std::string path_error;
    if (!TryResolveAllowedPath(config, directory_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 69;
        result.fields["error"] = path_error;
        return result;
    }
    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 71;
        result.fields["error"] = "directory does not exist";
        return result;
    }
    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 72;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    const std::vector<std::string> extension_filters =
        ParseDirectoryReadExtensionsCsv(file_extensions_csv);
    result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
    result.fields["file_type_filter_applied"] = extension_filters.empty() ? "false" : "true";
    result.fields["directory_walk_mode"] = "recursive";
    result.fields["directory_walk_skip_policy"] = "skip_common_generated_dirs";

    std::vector<std::string> matching_files;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    const std::set<std::string> & skipped_directories = DirectoryAnalysisSkippedDirectories();
    for (auto it = std::filesystem::recursive_directory_iterator(normalized, options, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path current = it->path();
        if (it->is_directory(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const std::string directory_name = ToLowerAscii(current.filename().string());
            if (skipped_directories.count(directory_name) > 0) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (!MatchesDirectoryReadExtensions(current, extension_filters)) {
            continue;
        }
        matching_files.push_back(current.string());
    }

    std::sort(
        matching_files.begin(),
        matching_files.end(),
        [&normalized](const std::string & left, const std::string & right) {
            return ToLowerAscii(DirectoryAnalysisRelativePath(normalized, std::filesystem::path(left)))
                < ToLowerAscii(DirectoryAnalysisRelativePath(normalized, std::filesystem::path(right)));
        });

    const int bounded_max_files = max_files > 0 ? max_files : 200;
    if (static_cast<int>(matching_files.size()) > bounded_max_files) {
        matching_files.resize(static_cast<std::size_t>(bounded_max_files));
    }

    const int bounded_max_excerpt_lines_per_file = std::max(1, max_excerpt_lines_per_file);
    const int bounded_max_total_excerpt_lines = std::max(1, max_total_excerpt_lines);
    const std::size_t bounded_max_excerpt_chars = std::max<std::size_t>(1024, max_excerpt_chars);

    std::ostringstream bundle;
    bundle << "directory_path=" << normalized.string() << "\n";
    bundle << "matched_file_count=" << matching_files.size() << "\n";
    bundle << "file_extensions_csv=" << JoinDirectoryReadExtensionsCsv(extension_filters) << "\n";
    bundle << "analysis_contract=directory framework bundle for downstream AI summarization\n\n";

    int excerpt_file_count = 0;
    int truncated_file_count = 0;
    int total_excerpt_lines = 0;
    std::vector<std::string> excerpted_file_paths;
    for (const std::string & file_path : matching_files) {
        if (total_excerpt_lines >= bounded_max_total_excerpt_lines) {
            break;
        }
        std::string excerpt_text;
        int excerpt_lines = 0;
        bool truncated = false;
        const int remaining_line_budget = bounded_max_total_excerpt_lines - total_excerpt_lines;
        const int per_file_line_budget = std::min(bounded_max_excerpt_lines_per_file, remaining_line_budget);
        const std::size_t remaining_char_budget =
            bounded_max_excerpt_chars > static_cast<std::size_t>(bundle.tellp())
                ? bounded_max_excerpt_chars - static_cast<std::size_t>(bundle.tellp())
                : static_cast<std::size_t>(0);
        if (remaining_char_budget < 256) {
            break;
        }
        if (!ReadFileExcerptPreview(
                std::filesystem::path(file_path),
                per_file_line_budget,
                remaining_char_budget,
                &excerpt_text,
                &excerpt_lines,
                &truncated)) {
            continue;
        }
        if (excerpt_lines <= 0 && excerpt_text.empty()) {
            continue;
        }
        ++excerpt_file_count;
        total_excerpt_lines += excerpt_lines;
        if (truncated) {
            ++truncated_file_count;
        }
        excerpted_file_paths.push_back(file_path);
        bundle << "===== FILE " << excerpt_file_count << ": "
               << DirectoryAnalysisRelativePath(normalized, std::filesystem::path(file_path))
               << " =====\n";
        bundle << excerpt_text;
        if (truncated) {
            bundle << "[truncated]\n";
        }
        bundle << "\n";
    }

    const int omitted_file_count = std::max(0, static_cast<int>(matching_files.size()) - excerpt_file_count);
    const std::string bundle_text = bundle.str();
    const std::string bundle_path = BuildDirectoryAnalysisBundlePath(config, trace_id, normalized.string());
    std::filesystem::create_directories(BuildDirectoryAnalysisBundleRoot(config));
    std::ofstream output(bundle_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 73;
        result.fields["error"] = "failed to write directory analysis bundle";
        return result;
    }
    output << bundle_text;
    output.close();

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "directory_analysis_bundle_ready";
    result.fields["summary"] = excerpt_file_count > 0
        ? "directory analysis bundle prepared"
        : "directory analysis bundle prepared with no readable excerpts";
    result.fields["normalized_path"] = normalized.string();
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";
    result.fields["analysis_allowed"] = "true";
    result.fields["task_completion"] = "complete";
    result.fields["batch_completion"] = "complete";
    result.fields["content_read_completion"] = "complete";
    result.fields["continue_required"] = "false";
    result.fields["auto_continue_required"] = "false";
    result.fields["user_confirmation_required"] = "false";
    result.fields["incomplete_scope"] = "";
    result.fields["stop_condition"] = "single_call_complete";
    result.fields["next_action"] =
        "pass content_text or analysis_bundle_ref to analysis tooling for project overview";
    result.fields["content"] = bundle_text;
    result.fields["content_text"] = bundle_text;
    result.fields["content_payload_format"] = "plain_text";
    result.fields["content_payload_scope"] = "directory_analysis_bundle";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["matched_file_count"] = std::to_string(matching_files.size());
    result.fields["excerpt_file_count"] = std::to_string(excerpt_file_count);
    result.fields["truncated_file_count"] = std::to_string(truncated_file_count);
    result.fields["omitted_file_count"] = std::to_string(omitted_file_count);
    result.fields["total_excerpt_lines"] = std::to_string(total_excerpt_lines);
    result.fields["max_excerpt_lines_per_file"] = std::to_string(bounded_max_excerpt_lines_per_file);
    result.fields["max_total_excerpt_lines"] = std::to_string(bounded_max_total_excerpt_lines);
    result.fields["max_excerpt_chars"] = std::to_string(bounded_max_excerpt_chars);
    result.fields["source_excerpt_chars"] = std::to_string(bundle_text.size());
    result.fields["excerpted_file_paths_json"] = BuildJsonStringArrayFromStrings(excerpted_file_paths);
    result.fields["analysis_bundle_ref"] = bundle_path;
    result.fields["result_ref"] = bundle_path;
    result.fields["evidence_ref"] = bundle_path;
    result.fields["log_path"] = bundle_path;
    result.fields["analysis_bundle_contract"] =
        "single-call directory overview bundle; use content_text for immediate analysis and analysis_bundle_ref for auditable replay";
    result.fields["server_side_atomic_read"] = "true";
    return result;
}

CommandResult ScanTextRangesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & scan_mode = std::string(),
    int max_ranges_per_call = 64,
    int range_offset = 0,
    const std::string & trace_id = std::string(),
    const std::string & probe_ref = std::string()) {
    CommandResult result;
    result.fields["task_type"] = "text_range_scan";
    result.fields["file_path"] = file_path;
    result.fields["scan_mode"] = scan_mode;
    if (!probe_ref.empty()) {
        result.fields["probe_ref"] = probe_ref;
        result.fields["probe_ready"] = "true";
    }
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 74;
        result.fields["error"] = "file_path is required";
        result.fields["error_code"] = "missing_file_path";
        result.fields["next_action"] = "retry lan_agent_scan_text_ranges with file_path";
        return result;
    }

    const std::string normalized_scan_mode = NormalizeTextRangeScanMode(scan_mode);
    if (normalized_scan_mode.empty()) {
        result.ok = false;
        result.exit_code = 75;
        result.fields["error"] = "unsupported scan_mode; use comments, line_comments, or block_comments";
        result.fields["error_code"] = "unsupported_scan_mode";
        result.fields["next_action"] = "retry lan_agent_scan_text_ranges with scan_mode=comments for comment cleanup";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 76;
        result.fields["error"] = path_error;
        result.fields["error_code"] = path_error == "path is outside allowed roots"
            ? "path_outside_allowed_roots"
            : "path_resolution_failed";
        result.fields["next_action"] = "add the target project root to allowed_roots or retry with a path under an allowed root";
        return result;
    }

    std::vector<TextRangeDescriptor> all_ranges;
    int total_lines = 0;
    bool cache_hit = false;

    const std::string normalized_path_string = normalized.string();
    std::error_code metadata_ec;
    const auto file_last_write_time = std::filesystem::last_write_time(normalized, metadata_ec);
    const std::uintmax_t file_size = metadata_ec ? 0 : std::filesystem::file_size(normalized, metadata_ec);

    TextRangeScanCacheValue cached_value;
    if (!metadata_ec &&
        GetTextRangeScanCache().Find(
            normalized_path_string,
            normalized_scan_mode,
            file_last_write_time,
            file_size,
            &cached_value)) {
        all_ranges = cached_value.ranges;
        total_lines = cached_value.total_lines;
        cache_hit = true;
    } else {
        std::string raw_content;
        std::string read_error;
        if (!ReadWholeFile(normalized, &raw_content, &read_error)) {
            result.ok = false;
            result.exit_code = 77;
            result.fields["error"] = read_error;
            return result;
        }

        if (!CollectCommentRanges(raw_content, normalized_scan_mode, &all_ranges, &total_lines)) {
            result.ok = false;
            result.exit_code = 78;
            result.fields["error"] = "failed to collect text ranges";
            return result;
        }

        if (!metadata_ec) {
            TextRangeScanCacheValue value;
            value.last_write_time = file_last_write_time;
            value.file_size = file_size;
            value.total_lines = total_lines;
            value.ranges = all_ranges;
            GetTextRangeScanCache().Store(normalized_path_string, normalized_scan_mode, value);
        }
    }

    const int requested_max_ranges_per_call = std::max(1, max_ranges_per_call);
    const int bounded_max_ranges_per_call = 1;
    const int bounded_range_offset = std::max(0, range_offset);
    const int total_range_count = static_cast<int>(all_ranges.size());
    const int end_index = std::min(total_range_count, bounded_range_offset + bounded_max_ranges_per_call);
    std::vector<TextRangeDescriptor> page_ranges;
    for (int index = bounded_range_offset; index < end_index; ++index) {
        page_ranges.push_back(all_ranges[static_cast<std::size_t>(index)]);
    }
    const bool has_more = end_index < total_range_count;
    const int next_range_offset = has_more ? end_index : -1;
    const std::string ranges_json = BuildTextRangeJsonArray(page_ranges);
    const std::string summary_text = BuildTextRangeSummaryText(
        normalized.string(),
        normalized_scan_mode,
        total_lines,
        total_range_count,
        static_cast<int>(page_ranges.size()),
        bounded_range_offset,
        page_ranges);
    const std::string result_path = BuildTextRangeScanPath(config, trace_id, normalized.string());
    std::filesystem::create_directories(BuildTextRangeScanRoot(config));
    std::ofstream output(result_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 79;
        result.fields["error"] = "failed to write text range scan bundle";
        return result;
    }
    output << summary_text;
    output.close();

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = has_more ? "text_ranges_partial" : "text_ranges_ready";
    result.fields["summary"] = total_range_count > 0
        ? "text ranges scanned and paged"
        : "text range scan completed with no matching ranges";
    result.fields["normalized_path"] = normalized.string();
    result.fields["scan_mode"] = normalized_scan_mode;
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["total_range_count"] = std::to_string(total_range_count);
    result.fields["returned_range_count"] = std::to_string(page_ranges.size());
    result.fields["range_offset"] = std::to_string(bounded_range_offset);
    result.fields["requested_max_ranges_per_call"] = std::to_string(requested_max_ranges_per_call);
    result.fields["max_ranges_per_call"] = std::to_string(bounded_max_ranges_per_call);
    result.fields["next_range_offset"] = has_more ? std::to_string(next_range_offset) : "";
    result.fields["has_more"] = has_more ? "true" : "false";
    result.fields["has_more_after_current_step"] = has_more ? "true" : "false";
    result.fields["cache_hit"] = cache_hit ? "true" : "false";
    result.fields["task_completion"] = page_ranges.empty() ? "complete" : "single_item_ready";
    result.fields["step_completion"] = page_ranges.empty() ? "no_item" : "current_item_ready";
    result.fields["continue_required"] = page_ranges.empty() ? "false" : "true";
    result.fields["auto_continue_required"] = "false";
    result.fields["analysis_allowed"] = "true";
    result.fields["single_step_required"] = "true";
    result.fields["operation_granularity"] = "single_text_range";
    result.fields["max_items_per_call"] = "1";
    result.fields["batch_mutation_allowed"] = "false";
    result.fields["content"] = summary_text;
    result.fields["content_text"] = summary_text;
    result.fields["content_payload_format"] = "plain_text";
    result.fields["content_payload_scope"] = "text_range_scan_summary";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["ranges_json"] = ranges_json;
    result.fields["next_action"] = page_ranges.empty()
        ? "no matching range remains; stop the edit loop or verify final state"
        : "process only the current range: call lan_agent_prepare_edit_windows with this ranges_json, apply one atomic edit, verify, then rescan from range_offset=0";
    result.fields["next_call_json"] = page_ranges.empty()
        ? ""
        : ("{\"name\":\"lan_agent_prepare_edit_windows\",\"arguments\":{\"file_path\":\""
            + codex_lan_agent::JsonEscape(file_path)
            + "\",\"ranges_json\":\"" + codex_lan_agent::JsonEscape(ranges_json)
            + "\",\"context_before\":2,\"context_after\":2,\"max_windows_per_call\":1,\"window_offset\":0"
            + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
            + (probe_ref.empty() ? std::string() : ",\"probe_ref\":\"" + codex_lan_agent::JsonEscape(probe_ref) + "\",\"probe_ready\":true")
            + "}}");
    result.fields["result_ref"] = result_path;
    result.fields["evidence_ref"] = result_path;
    result.fields["log_path"] = result_path;
    result.fields["scan_result_ref"] = result_path;
    result.fields["scan_contract"] =
        "single-step loop: scan one range, prepare one window, apply one atomic edit, verify, then rescan from offset 0";
    result.fields["step_contract"] = result.fields["scan_contract"];
    result.fields["server_side_atomic_read"] = "true";
    return result;
}

CommandResult DeleteNextTextRangeAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & scan_mode = std::string(),
    const std::string & primary_intent = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & probe_ref = std::string()) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["primary_intent"] = primary_intent;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (!probe_ref.empty()) {
        result.fields["probe_ref"] = probe_ref;
        result.fields["probe_ready"] = "true";
    }
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        result.fields["error_code"] = "missing_file_path";
        return result;
    }

    const std::string normalized_scan_mode = NormalizeTextRangeScanMode(scan_mode);
    if (normalized_scan_mode.empty()) {
        result.ok = false;
        result.exit_code = 75;
        result.fields["error"] = "unsupported scan_mode; use comments, line_comments, or block_comments";
        result.fields["error_code"] = "unsupported_scan_mode";
        result.fields["next_action"] = "retry with scan_mode=comments for comment cleanup";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        result.fields["error_code"] = path_error == "path is outside allowed roots"
            ? "path_outside_allowed_roots"
            : "path_resolution_failed";
        return result;
    }

    std::string raw_content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &raw_content, &read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = read_error;
        result.fields["error_code"] = "file_read_failed";
        return result;
    }

    std::vector<TextRangeDescriptor> ranges;
    int total_lines = 0;
    if (!CollectCommentRanges(raw_content, normalized_scan_mode, &ranges, &total_lines)) {
        result.ok = false;
        result.exit_code = 77;
        result.fields["error"] = "failed to collect text ranges";
        result.fields["error_code"] = "text_range_scan_failed";
        return result;
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["scan_mode"] = normalized_scan_mode;
    result.fields["total_lines_before"] = std::to_string(total_lines);
    result.fields["total_range_count_before"] = std::to_string(ranges.size());
    result.fields["operation_granularity"] = "single_text_range_delete";
    result.fields["single_step_required"] = "true";
    result.fields["max_items_per_call"] = "1";
    result.fields["batch_mutation_allowed"] = "false";
    result.fields["server_side_optimized_step"] = "true";

    if (ranges.empty()) {
        result.ok = true;
        result.exit_code = 0;
        result.fields["status"] = "needs_continue";
        result.fields["result"] = "no_text_range_remaining";
        result.fields["summary"] = "no matching text range remains";
        result.fields["write_applied"] = "false";
        result.fields["write_verified"] = "true";
        result.fields["disk_write_completed"] = "false";
        result.fields["task_completion"] = "complete";
        result.fields["continue_required"] = "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["has_more"] = "false";
        result.fields["next_action"] = "stop; no matching comment range remains";
        return result;
    }

    const TextRangeDescriptor range = ranges.front();
    std::string new_content;
    std::string deleted_text;
    std::string delete_mode;
    std::string delete_error;
    if (!BuildContentWithDeletedTextRange(
            raw_content,
            range,
            &new_content,
            &deleted_text,
            &delete_mode,
            &delete_error)) {
        result.ok = false;
        result.exit_code = 78;
        result.fields["error"] = delete_error;
        result.fields["error_code"] = "delete_range_build_failed";
        return result;
    }
    if (new_content == raw_content) {
        result.ok = false;
        result.exit_code = 79;
        result.fields["error"] = "delete range produced no content change";
        result.fields["error_code"] = "delete_range_no_effect";
        return result;
    }

    const std::string old_hash = StableContentChecksum(raw_content);
    std::string write_error;
    if (!WriteWholeFileDirect(normalized, new_content, &write_error)) {
        result.ok = false;
        result.exit_code = 80;
        result.fields["error"] = write_error;
        result.fields["error_code"] = "file_write_failed";
        return result;
    }

    std::string verified_content;
    if (!ReadWholeFile(normalized, &verified_content, &read_error)) {
        result.ok = false;
        result.exit_code = 81;
        result.fields["error"] = read_error;
        result.fields["error_code"] = "file_read_after_write_failed";
        return result;
    }

    std::vector<TextRangeDescriptor> after_ranges;
    int total_lines_after = 0;
    CollectCommentRanges(verified_content, normalized_scan_mode, &after_ranges, &total_lines_after);
    const std::string new_hash = StableContentChecksum(verified_content);
    const std::string result_path = BuildTextRangeDeletePath(config, trace_id, normalized.string());
    std::filesystem::create_directories(BuildTextRangeDeleteRoot(config));
    std::ofstream audit(result_path, std::ios::out | std::ios::trunc);
    if (audit.is_open()) {
        audit << "file_path=" << normalized.string() << "\n";
        audit << "scan_mode=" << normalized_scan_mode << "\n";
        audit << "deleted_range_kind=" << range.range_kind << "\n";
        audit << "deleted_range_lines=" << range.start_line << "-" << range.end_line << "\n";
        audit << "deleted_range_columns=" << range.start_column << "-" << range.end_column << "\n";
        audit << "delete_mode=" << delete_mode << "\n";
        audit << "total_range_count_before=" << ranges.size() << "\n";
        audit << "total_range_count_after=" << after_ranges.size() << "\n";
        audit << "old_hash=" << old_hash << "\n";
        audit << "new_hash=" << new_hash << "\n";
        audit << "deleted_preview=" << range.preview << "\n";
    }

    result.ok = true;
    result.exit_code = 0;
    const bool single_delete_complete = after_ranges.empty();
    result.fields["status"] = single_delete_complete ? "success" : "needs_continue";
    result.fields["result"] = "delete_next_text_range_atomic_applied";
    result.fields["summary"] = single_delete_complete
        ? "deleted one text range and verified by readback; no matching text range remains"
        : "deleted one text range and verified by readback; task is not complete and must continue";
    result.fields["range_index"] = std::to_string(range.range_index);
    result.fields["range_kind"] = range.range_kind;
    result.fields["range_start_line"] = std::to_string(range.start_line);
    result.fields["range_end_line"] = std::to_string(range.end_line);
    result.fields["range_start_column"] = std::to_string(range.start_column);
    result.fields["range_end_column"] = std::to_string(range.end_column);
    result.fields["deleted_preview"] = range.preview;
    result.fields["delete_mode"] = delete_mode;
    result.fields["deleted_text_bytes"] = std::to_string(deleted_text.size());
    result.fields["old_hash"] = old_hash;
    result.fields["new_hash"] = new_hash;
    result.fields["total_lines_after"] = std::to_string(total_lines_after);
    result.fields["total_range_count_after"] = std::to_string(after_ranges.size());
    result.fields["has_more"] = single_delete_complete ? "false" : "true";
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = verified_content == new_content ? "true" : "false";
    result.fields["disk_write_completed"] = "true";
    result.fields["final_write_tool"] = "mcp_direct_text_range_delete";
    result.fields["verification_status"] = verified_content == new_content ? "verified" : "not_verified";
    result.fields["verification_ok"] = verified_content == new_content ? "true" : "false";
    result.fields["task_completion"] = single_delete_complete ? "complete" : "single_item_applied";
    result.fields["continue_required"] = single_delete_complete ? "false" : "true";
    result.fields["auto_continue_required"] = "false";
    result.fields["terminal_state"] = single_delete_complete ? "true" : "false";
    result.fields["task_done"] = single_delete_complete ? "true" : "false";
    result.fields["completion_claim_allowed"] = single_delete_complete ? "true" : "false";
    result.fields["must_continue_until"] = single_delete_complete ? "" : "has_more=false";
    result.fields["next_action"] = single_delete_complete
        ? "stop; no matching comment range remains"
        : "call lan_agent_delete_next_text_range_atomic again with the same file_path, scan_mode=comments, and probe_ref";
    result.fields["next_call_json"] = single_delete_complete
        ? ""
        : BuildDeleteNextTextRangeCallJson(
            file_path,
            normalized_scan_mode,
            primary_intent,
            trace_id,
            probe_ref);
    result.fields["result_ref"] = result_path;
    result.fields["evidence_ref"] = result_path;
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    return result;
}

CommandResult DeleteTextRangeWindowAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & scan_mode = std::string(),
    int start_line = 1,
    int max_lines = 200,
    const std::string & primary_intent = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & probe_ref = std::string(),
    const std::string & directory_manifest_path = std::string(),
    int directory_current_file_index = 0,
    int directory_total_code_file_count = 0) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["primary_intent"] = primary_intent;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (!probe_ref.empty()) {
        result.fields["probe_ref"] = probe_ref;
        result.fields["probe_ready"] = "true";
    }
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        result.fields["error_code"] = "missing_file_path";
        return result;
    }

    const std::string normalized_scan_mode = NormalizeTextRangeScanMode(scan_mode);
    if (normalized_scan_mode.empty()) {
        result.ok = false;
        result.exit_code = 75;
        result.fields["error"] = "unsupported scan_mode; use comments, line_comments, or block_comments";
        result.fields["error_code"] = "unsupported_scan_mode";
        result.fields["next_action"] = "retry with scan_mode=comments for comment cleanup";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        result.fields["error_code"] = path_error == "path is outside allowed roots"
            ? "path_outside_allowed_roots"
            : "path_resolution_failed";
        return result;
    }

    std::string raw_content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &raw_content, &read_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = read_error;
        result.fields["error_code"] = "file_read_failed";
        return result;
    }

    std::vector<TextRangeDescriptor> ranges;
    int total_lines = 0;
    if (!CollectCommentRanges(raw_content, normalized_scan_mode, &ranges, &total_lines)) {
        result.ok = false;
        result.exit_code = 77;
        result.fields["error"] = "failed to collect text ranges";
        result.fields["error_code"] = "text_range_scan_failed";
        return result;
    }

    const int bounded_start_line = std::max(1, start_line);
    const int requested_max_lines = std::max(1, max_lines);
    const std::string normalized_primary_intent = ToLowerAscii(Trim(primary_intent));
    const bool comment_cleanup_window =
        normalized_scan_mode == "comments"
        || normalized_scan_mode == "line_comments"
        || normalized_scan_mode == "block_comments"
        || normalized_primary_intent.empty()
        || normalized_primary_intent == "comment_cleanup"
        || normalized_primary_intent == "remove_comments"
        || normalized_primary_intent == "strip_comments"
        || normalized_primary_intent == "text_cleaning"
        || normalized_primary_intent == "删除注释"
        || normalized_primary_intent == "清理注释"
        || normalized_primary_intent == "去除注释"
        || normalized_primary_intent == "移除注释"
        || normalized_primary_intent == "删注释";
    const int bounded_max_lines = comment_cleanup_window
        ? 200
        : std::min(200, requested_max_lines);
    const int window_end_line = std::min(total_lines, bounded_start_line + bounded_max_lines - 1);
    std::vector<TextRangeDescriptor> window_ranges;
    std::vector<TextRangeDescriptor> boundary_ranges;
    for (const TextRangeDescriptor & range : ranges) {
        const bool starts_in_window = range.start_line >= bounded_start_line && range.start_line <= window_end_line;
        const bool ends_in_window = range.end_line >= bounded_start_line && range.end_line <= window_end_line;
        if (starts_in_window && ends_in_window && range.range_kind != "block_comment_unterminated") {
            window_ranges.push_back(range);
        } else if (starts_in_window || ends_in_window) {
            boundary_ranges.push_back(range);
        }
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["scan_mode"] = normalized_scan_mode;
    result.fields["start_line"] = std::to_string(bounded_start_line);
    result.fields["window_start_line"] = std::to_string(bounded_start_line);
    result.fields["window_end_line"] = std::to_string(window_end_line);
    result.fields["requested_max_lines"] = std::to_string(requested_max_lines);
    result.fields["max_lines"] = std::to_string(bounded_max_lines);
    result.fields["effective_window_policy"] = comment_cleanup_window
        ? "comment_cleanup_fixed_200_line_window"
        : "caller_bounded_window";
    result.fields["small_window_request_upgraded"] =
        comment_cleanup_window && requested_max_lines < 200 ? "true" : "false";
    result.fields["total_lines_before"] = std::to_string(total_lines);
    result.fields["total_range_count_before"] = std::to_string(ranges.size());
    result.fields["window_range_count_before"] = std::to_string(window_ranges.size());
    result.fields["boundary_range_count"] = std::to_string(boundary_ranges.size());
    result.fields["operation_granularity"] = "bounded_line_window_text_range_delete";
    result.fields["single_step_required"] = "false";
    result.fields["window_step_required"] = "true";
    result.fields["max_items_per_call"] = std::to_string(bounded_max_lines) + "_lines";
    result.fields["max_lines_per_call"] = std::to_string(bounded_max_lines);
    result.fields["batch_mutation_allowed"] = "bounded_window_only";
    result.fields["server_side_optimized_step"] = "true";
    result.fields["window_batch_scope"] = "single_file_bounded_line_window";
    if (!directory_manifest_path.empty()) {
        result.fields["directory_scope_active"] = "true";
        result.fields["directory_manifest_path"] = directory_manifest_path;
        result.fields["directory_current_file_index"] = std::to_string(std::max(0, directory_current_file_index));
        result.fields["directory_total_code_file_count"] = std::to_string(std::max(0, directory_total_code_file_count));
    }
    const bool directory_scope_active = !directory_manifest_path.empty();
    const int next_directory_file_index = std::max(0, directory_current_file_index) + 1;
    const int directory_remaining_after_current = directory_scope_active
        ? std::max(0, std::max(0, directory_total_code_file_count) - next_directory_file_index)
        : 0;
    const std::string next_directory_file_path =
        directory_remaining_after_current > 0
            ? ReadCodeFilePathFromDirectoryManifest(directory_manifest_path, next_directory_file_index)
            : std::string();
    const std::string directory_next_probe_call_json =
        !next_directory_file_path.empty()
            ? BuildCommentCleanupProbeCallJson(
                next_directory_file_path,
                trace_id,
                directory_manifest_path,
                next_directory_file_index,
                directory_total_code_file_count)
            : std::string();

    if (ranges.empty()) {
        const bool directory_has_next_file = !directory_next_probe_call_json.empty();
        result.ok = true;
        result.exit_code = 0;
        result.fields["status"] = directory_has_next_file ? "needs_continue" : "success";
        result.fields["result"] = "no_text_range_remaining";
        result.fields["summary"] = directory_has_next_file
            ? "no matching text range remains in the current file; directory cleanup must continue with the next code file"
            : "no matching text range remains";
        result.fields["write_applied"] = "false";
        result.fields["write_verified"] = "true";
        result.fields["disk_write_completed"] = "false";
        result.fields["task_completion"] = directory_has_next_file ? "current_file_complete" : "complete";
        result.fields["continue_required"] = directory_has_next_file ? "true" : "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["has_more"] = directory_has_next_file ? "true" : "false";
        result.fields["terminal_state"] = directory_has_next_file ? "false" : "true";
        result.fields["task_done"] = directory_has_next_file ? "false" : "true";
        result.fields["completion_claim_allowed"] = directory_has_next_file ? "false" : "true";
        result.fields["must_continue_until"] = directory_has_next_file ? "directory_scope_complete" : "";
        result.fields["next_start_line"] = std::to_string(bounded_start_line);
        result.fields["next_action"] = directory_has_next_file
            ? "tool_call_only: current file has no remaining comments; probe the next directory code file"
            : "stop; no matching comment range remains";
        if (directory_scope_active) {
            result.fields["directory_next_file_index"] = directory_has_next_file ? std::to_string(next_directory_file_index) : "";
            result.fields["directory_remaining_code_file_count"] = std::to_string(directory_remaining_after_current);
            result.fields["directory_scope_incomplete"] = directory_has_next_file ? "true" : "false";
            result.fields["directory_next_probe_call_json"] = directory_next_probe_call_json;
        }
        result.fields["next_call_json"] = directory_next_probe_call_json;
        result.fields["required_tool_name"] = directory_has_next_file ? "lan_agent_probe_text_file" : "";
        result.fields["required_tool_arguments_json"] = directory_next_probe_call_json;
        return result;
    }

    if (!boundary_ranges.empty() && window_ranges.empty()) {
        result.ok = true;
        result.exit_code = 0;
        result.fields["status"] = "success";
        result.fields["result"] = "window_boundary_range_detected";
        result.fields["summary"] =
            "a text range crosses the current window boundary; task is not complete and must continue with the single-range delete tool";
        result.fields["write_applied"] = "false";
        result.fields["write_verified"] = "true";
        result.fields["disk_write_completed"] = "false";
        result.fields["task_completion"] = "boundary_blocked";
        result.fields["continue_required"] = "true";
        result.fields["auto_continue_required"] = "false";
        result.fields["has_more"] = "true";
        result.fields["terminal_state"] = "false";
        result.fields["task_done"] = "false";
        result.fields["completion_claim_allowed"] = "false";
        result.fields["must_continue_until"] = "has_more=false";
        result.fields["next_start_line"] = std::to_string(bounded_start_line);
        result.fields["boundary_ranges_json"] = BuildDeletedRangeSummaryJsonArray(boundary_ranges);
        result.fields["next_action"] =
            "use lan_agent_delete_next_text_range_atomic for the boundary-spanning range, then retry this window";
        result.fields["next_call_json"] = BuildDeleteNextTextRangeCallJson(
            file_path,
            normalized_scan_mode,
            primary_intent,
            trace_id,
            probe_ref);
        return result;
    }

    if (window_ranges.empty()) {
        const int next_start_line = bounded_start_line + bounded_max_lines;
        const bool has_more_windows = next_start_line <= total_lines;
        const bool directory_has_next_file =
            !has_more_windows && !directory_next_probe_call_json.empty();
        const bool needs_continue = has_more_windows || directory_has_next_file;
        result.ok = true;
        result.exit_code = 0;
        result.fields["status"] = needs_continue ? "needs_continue" : "success";
        result.fields["result"] = "no_text_range_in_window";
        result.fields["summary"] = has_more_windows
            ? "no matching text range in the current bounded window; task is not complete and must continue"
            : directory_has_next_file
            ? "no matching text range in the current file tail; directory cleanup must continue with the next code file"
            : "no matching text range in the current bounded window; reached the end of the file window scan";
        result.fields["write_applied"] = "false";
        result.fields["write_verified"] = "true";
        result.fields["disk_write_completed"] = "false";
        result.fields["task_completion"] = has_more_windows ? "window_complete" : (directory_has_next_file ? "current_file_complete" : "complete");
        result.fields["continue_required"] = needs_continue ? "true" : "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["has_more"] = needs_continue ? "true" : "false";
        result.fields["terminal_state"] = needs_continue ? "false" : "true";
        result.fields["task_done"] = needs_continue ? "false" : "true";
        result.fields["completion_claim_allowed"] = needs_continue ? "false" : "true";
        result.fields["must_continue_until"] = has_more_windows ? "has_more=false" : (directory_has_next_file ? "directory_scope_complete" : "");
        result.fields["next_start_line"] = std::to_string(next_start_line);
        result.fields["next_action"] = has_more_windows
            ? "call lan_agent_delete_text_range_window_atomic again with next_start_line"
            : directory_has_next_file
            ? "tool_call_only: current file scan reached the end; probe the next directory code file"
            : "stop; reached the end of the file window scan";
        result.fields["next_call_json"] = has_more_windows
            ? BuildDeleteTextRangeWindowCallJson(
                file_path,
                normalized_scan_mode,
                next_start_line,
                bounded_max_lines,
                primary_intent,
                trace_id,
                probe_ref)
            : directory_next_probe_call_json;
        if (directory_scope_active) {
            result.fields["directory_next_file_index"] = directory_has_next_file ? std::to_string(next_directory_file_index) : "";
            result.fields["directory_remaining_code_file_count"] = std::to_string(directory_remaining_after_current);
            result.fields["directory_scope_incomplete"] = directory_has_next_file ? "true" : "false";
            result.fields["directory_next_probe_call_json"] = directory_next_probe_call_json;
        }
        result.fields["required_tool_name"] = has_more_windows
            ? "lan_agent_delete_text_range_window_atomic"
            : (directory_has_next_file ? "lan_agent_probe_text_file" : "");
        result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
        return result;
    }

    std::string working_content = raw_content;
    std::vector<TextRangeDescriptor> applied_ranges = window_ranges;
    std::sort(applied_ranges.begin(), applied_ranges.end(), [](const TextRangeDescriptor & lhs, const TextRangeDescriptor & rhs) {
        if (lhs.start_line != rhs.start_line) {
            return lhs.start_line > rhs.start_line;
        }
        return lhs.start_column > rhs.start_column;
    });

    std::size_t deleted_text_bytes = 0;
    std::vector<std::string> delete_modes;
    for (const TextRangeDescriptor & range : applied_ranges) {
        std::string next_content;
        std::string deleted_text;
        std::string delete_mode;
        std::string delete_error;
        if (!BuildContentWithDeletedTextRange(
                working_content,
                range,
                &next_content,
                &deleted_text,
                &delete_mode,
                &delete_error)) {
            result.ok = false;
            result.exit_code = 78;
            result.fields["error"] = delete_error;
            result.fields["error_code"] = "delete_range_build_failed";
            result.fields["failed_range_start_line"] = std::to_string(range.start_line);
            return result;
        }
        if (next_content == working_content) {
            result.ok = false;
            result.exit_code = 79;
            result.fields["error"] = "delete range produced no content change";
            result.fields["error_code"] = "delete_range_no_effect";
            result.fields["failed_range_start_line"] = std::to_string(range.start_line);
            return result;
        }
        deleted_text_bytes += deleted_text.size();
        delete_modes.push_back(delete_mode);
        working_content.swap(next_content);
    }

    const std::string old_hash = StableContentChecksum(raw_content);
    std::string write_error;
    if (!WriteWholeFileDirect(normalized, working_content, &write_error)) {
        result.ok = false;
        result.exit_code = 80;
        result.fields["error"] = write_error;
        result.fields["error_code"] = "file_write_failed";
        return result;
    }

    std::string verified_content;
    if (!ReadWholeFile(normalized, &verified_content, &read_error)) {
        result.ok = false;
        result.exit_code = 81;
        result.fields["error"] = read_error;
        result.fields["error_code"] = "file_read_after_write_failed";
        return result;
    }

    std::vector<TextRangeDescriptor> after_ranges;
    int total_lines_after = 0;
    CollectCommentRanges(verified_content, normalized_scan_mode, &after_ranges, &total_lines_after);
    const std::string new_hash = StableContentChecksum(verified_content);
    const int after_window_end_line = std::min(total_lines_after, bounded_start_line + bounded_max_lines - 1);
    bool has_range_in_current_window_after = false;
    int earliest_remaining_start_line = 0;
    for (const TextRangeDescriptor & range : after_ranges) {
        if (earliest_remaining_start_line == 0 || range.start_line < earliest_remaining_start_line) {
            earliest_remaining_start_line = range.start_line;
        }
        if (range.start_line >= bounded_start_line && range.start_line <= after_window_end_line) {
            has_range_in_current_window_after = true;
        }
    }
    int next_start_line = bounded_start_line + bounded_max_lines;
    if (has_range_in_current_window_after) {
        next_start_line = bounded_start_line;
    } else if (earliest_remaining_start_line > 0 && earliest_remaining_start_line < next_start_line) {
        next_start_line = earliest_remaining_start_line;
    }
    const bool has_more = !after_ranges.empty();
    const bool directory_has_next_file_after_write =
        !has_more && !directory_next_probe_call_json.empty();
    const bool has_more_or_directory = has_more || directory_has_next_file_after_write;

    const std::string result_path = BuildTextRangeWindowDeletePath(config, trace_id, normalized.string());
    std::filesystem::create_directories(BuildTextRangeDeleteRoot(config));
    std::ofstream audit(result_path, std::ios::out | std::ios::trunc);
    if (audit.is_open()) {
        audit << "file_path=" << normalized.string() << "\n";
        audit << "scan_mode=" << normalized_scan_mode << "\n";
        audit << "window_lines=" << bounded_start_line << "-" << window_end_line << "\n";
        audit << "deleted_range_count=" << window_ranges.size() << "\n";
        audit << "total_range_count_before=" << ranges.size() << "\n";
        audit << "total_range_count_after=" << after_ranges.size() << "\n";
        audit << "old_hash=" << old_hash << "\n";
        audit << "new_hash=" << new_hash << "\n";
        audit << "deleted_ranges_json=" << BuildDeletedRangeSummaryJsonArray(window_ranges) << "\n";
    }

    std::ostringstream modes;
    for (std::size_t index = 0; index < delete_modes.size(); ++index) {
        if (index != 0) {
            modes << ",";
        }
        modes << delete_modes[index];
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = has_more_or_directory ? "needs_continue" : "success";
    result.fields["result"] = "delete_text_range_window_atomic_applied";
    result.fields["summary"] = has_more
        ? "deleted text ranges in one bounded line window and verified by readback; task is not complete and must continue"
        : directory_has_next_file_after_write
        ? "deleted text ranges in one bounded line window and verified by readback; current file is clean and directory cleanup must continue"
        : "deleted text ranges in one bounded line window and verified by readback; no matching text range remains";
    result.fields["deleted_range_count"] = std::to_string(window_ranges.size());
    result.fields["deleted_ranges_json"] = BuildDeletedRangeSummaryJsonArray(window_ranges);
    result.fields["delete_modes_csv"] = modes.str();
    result.fields["deleted_text_bytes"] = std::to_string(deleted_text_bytes);
    result.fields["old_hash"] = old_hash;
    result.fields["new_hash"] = new_hash;
    result.fields["total_lines_after"] = std::to_string(total_lines_after);
    result.fields["total_range_count_after"] = std::to_string(after_ranges.size());
    result.fields["has_range_in_current_window_after"] = has_range_in_current_window_after ? "true" : "false";
    result.fields["has_more"] = has_more_or_directory ? "true" : "false";
    result.fields["next_start_line"] = std::to_string(next_start_line);
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = verified_content == working_content ? "true" : "false";
    result.fields["disk_write_completed"] = "true";
    result.fields["final_write_tool"] = "mcp_direct_text_range_window_delete";
    result.fields["verification_status"] = verified_content == working_content ? "verified" : "not_verified";
    result.fields["verification_ok"] = verified_content == working_content ? "true" : "false";
    result.fields["task_completion"] = has_more ? "window_applied" : (directory_has_next_file_after_write ? "current_file_complete" : "complete");
    result.fields["continue_required"] = has_more_or_directory ? "true" : "false";
    result.fields["auto_continue_required"] = "false";
    result.fields["terminal_state"] = has_more_or_directory ? "false" : "true";
    result.fields["task_done"] = has_more_or_directory ? "false" : "true";
    result.fields["completion_claim_allowed"] = has_more_or_directory ? "false" : "true";
    result.fields["must_continue_until"] = has_more ? "has_more=false" : (directory_has_next_file_after_write ? "directory_scope_complete" : "");
    result.fields["next_action"] = has_more
        ? "call lan_agent_delete_text_range_window_atomic again with next_start_line"
        : directory_has_next_file_after_write
        ? "tool_call_only: current file has no remaining comments; probe the next directory code file"
        : "stop; no matching comment range remains";
    result.fields["next_call_json"] = has_more
        ? BuildDeleteTextRangeWindowCallJson(
            file_path,
            normalized_scan_mode,
            next_start_line,
            bounded_max_lines,
            primary_intent,
            trace_id,
            probe_ref)
        : directory_next_probe_call_json;
    if (directory_scope_active) {
        result.fields["directory_next_file_index"] = directory_has_next_file_after_write ? std::to_string(next_directory_file_index) : "";
        result.fields["directory_remaining_code_file_count"] = std::to_string(directory_remaining_after_current);
        result.fields["directory_scope_incomplete"] = directory_has_next_file_after_write ? "true" : "false";
        result.fields["directory_next_probe_call_json"] = directory_next_probe_call_json;
    }
    result.fields["required_tool_name"] = has_more
        ? "lan_agent_delete_text_range_window_atomic"
        : (directory_has_next_file_after_write ? "lan_agent_probe_text_file" : "");
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    result.fields["result_ref"] = result_path;
    result.fields["evidence_ref"] = result_path;
    result.fields["content_payload_format"] = "json";
    result.fields["content_payload_scope"] = "metadata_only";
    result.fields["content_payload_boundary_safe"] = "true";
    return result;
}

CommandResult PrepareEditWindowsResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & ranges_json,
    int context_before = 6,
    int context_after = 6,
    int max_windows_per_call = 12,
    int window_offset = 0,
    std::size_t max_window_chars = 12000,
    const std::string & trace_id = std::string(),
    const std::string & probe_ref = std::string()) {
    CommandResult result;
    result.fields["task_type"] = "edit_window_bundle";
    result.fields["file_path"] = file_path;
    if (!probe_ref.empty()) {
        result.fields["probe_ref"] = probe_ref;
        result.fields["probe_ready"] = "true";
    }
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 80;
        result.fields["error"] = "file_path is required";
        return result;
    }
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 81;
        result.fields["error"] = path_error;
        return result;
    }
    std::vector<TextRangeDescriptor> all_ranges;
    std::string parse_error;
    if (!ParseTextRangeDescriptors(ranges_json, &all_ranges, &parse_error)) {
        result.ok = false;
        result.exit_code = 82;
        result.fields["error"] = parse_error;
        return result;
    }
    std::string raw_content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &raw_content, &read_error)) {
        result.ok = false;
        result.exit_code = 83;
        result.fields["error"] = read_error;
        return result;
    }
    const std::vector<std::string> lines = SplitLinesPreserveText(raw_content);
    const int total_lines = static_cast<int>(lines.size());
    const int bounded_context_before = std::max(0, context_before);
    const int bounded_context_after = std::max(0, context_after);
    const int requested_max_windows_per_call = std::max(1, max_windows_per_call);
    const int bounded_max_windows_per_call = 1;
    const int bounded_window_offset = std::max(0, window_offset);
    const std::size_t bounded_max_window_chars = std::max<std::size_t>(256, max_window_chars);
    const int total_window_count = static_cast<int>(all_ranges.size());
    const int end_index = std::min(total_window_count, bounded_window_offset + bounded_max_windows_per_call);

    std::vector<TextRangeDescriptor> page_ranges;
    std::vector<std::pair<int, int>> window_bounds;
    std::vector<std::string> window_texts;
    std::vector<std::string> window_flags;
    std::ostringstream bundle;
    bundle << "file_path=" << normalized.string() << "\n";
    bundle << "total_window_count=" << total_window_count << "\n";
    bundle << "returned_window_count=" << std::max(0, end_index - bounded_window_offset) << "\n";
    bundle << "window_offset=" << bounded_window_offset << "\n\n";

    for (int index = bounded_window_offset; index < end_index; ++index) {
        const TextRangeDescriptor & range = all_ranges[static_cast<std::size_t>(index)];
        const int window_start_line = std::max(1, range.start_line - bounded_context_before);
        const int window_end_line = std::min(total_lines, range.end_line + bounded_context_after);
        std::ostringstream window_text;
        for (int line_number = window_start_line; line_number <= window_end_line; ++line_number) {
            if (line_number <= 0 || line_number > total_lines) {
                continue;
            }
            window_text << lines[static_cast<std::size_t>(line_number - 1)] << "\n";
        }
        std::string final_window_text = window_text.str();
        std::string flag = "complete";
        if (final_window_text.size() > bounded_max_window_chars) {
            final_window_text = final_window_text.substr(0, bounded_max_window_chars);
            flag = "truncated_chars";
        }
        page_ranges.push_back(range);
        window_bounds.push_back({window_start_line, window_end_line});
        window_texts.push_back(final_window_text);
        window_flags.push_back(flag);
        bundle << "===== WINDOW " << page_ranges.size() << " =====\n";
        bundle << "range_index=" << range.range_index << "\n";
        bundle << "range_kind=" << range.range_kind << "\n";
        bundle << "range_lines=" << range.start_line << "-" << range.end_line << "\n";
        bundle << "window_lines=" << window_start_line << "-" << window_end_line << "\n";
        bundle << "window_flags=" << flag << "\n";
        bundle << "content_begin<<<\n";
        bundle << final_window_text;
        bundle << ">>>content_end\n\n";
    }

    const bool has_more = end_index < total_window_count;
    const int next_window_offset = has_more ? end_index : -1;
    const std::string windows_json = BuildEditWindowJsonArray(page_ranges, window_bounds, window_texts, window_flags);
    const std::string bundle_text = bundle.str();
    const std::string result_path = BuildEditWindowBundlePath(config, trace_id, normalized.string());
    std::filesystem::create_directories(BuildEditWindowBundleRoot(config));
    std::ofstream output(result_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 84;
        result.fields["error"] = "failed to write edit window bundle";
        return result;
    }
    output << bundle_text;
    output.close();

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = has_more ? "edit_windows_partial" : "edit_windows_ready";
    result.fields["summary"] = page_ranges.empty()
        ? "no edit windows were prepared"
        : "localized edit windows prepared";
    result.fields["normalized_path"] = normalized.string();
    result.fields["content"] = bundle_text;
    result.fields["content_text"] = bundle_text;
    result.fields["content_payload_format"] = "plain_text";
    result.fields["content_payload_scope"] = "edit_window_bundle";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["total_window_count"] = std::to_string(total_window_count);
    result.fields["returned_window_count"] = std::to_string(page_ranges.size());
    result.fields["window_offset"] = std::to_string(bounded_window_offset);
    result.fields["requested_max_windows_per_call"] = std::to_string(requested_max_windows_per_call);
    result.fields["max_windows_per_call"] = std::to_string(bounded_max_windows_per_call);
    result.fields["context_before"] = std::to_string(bounded_context_before);
    result.fields["context_after"] = std::to_string(bounded_context_after);
    result.fields["max_window_chars"] = std::to_string(bounded_max_window_chars);
    result.fields["windows_json"] = windows_json;
    result.fields["has_more"] = has_more ? "true" : "false";
    result.fields["has_more_after_current_step"] = has_more ? "true" : "false";
    result.fields["next_window_offset"] = has_more ? std::to_string(next_window_offset) : "";
    result.fields["task_completion"] = page_ranges.empty() ? "complete" : "single_item_ready";
    result.fields["step_completion"] = page_ranges.empty() ? "no_item" : "current_item_ready";
    result.fields["continue_required"] = page_ranges.empty() ? "false" : "true";
    result.fields["auto_continue_required"] = "false";
    result.fields["analysis_allowed"] = page_ranges.empty() ? "false" : "true";
    result.fields["single_step_required"] = "true";
    result.fields["operation_granularity"] = "single_edit_window";
    result.fields["max_items_per_call"] = "1";
    result.fields["batch_mutation_allowed"] = "false";
    result.fields["next_action"] = page_ranges.empty()
        ? "no edit window was produced; rescan or stop"
        : "apply exactly one atomic edit for this window, verify the file state, then call lan_agent_scan_text_ranges again with range_offset=0";
    result.fields["next_call_json"] = "";
    result.fields["result_ref"] = result_path;
    result.fields["evidence_ref"] = result_path;
    result.fields["log_path"] = result_path;
    result.fields["edit_window_bundle_ref"] = result_path;
    result.fields["edit_window_contract"] =
        "single-step loop: consume exactly one window, apply one atomic edit, verify, then rescan; do not batch windows or patch bottom-up in one call";
    result.fields["step_contract"] = result.fields["edit_window_contract"];
    result.fields["server_side_atomic_read"] = "true";
    return result;
}

CommandResult DiscoverLogsResult(
    const AgentConfig & config,
    int max_entries,
    int tail_lines) {
    CommandResult result;
    result.fields["log_root"] = config.log_root;
    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const int bounded_tail_lines = tail_lines > 0 ? tail_lines : 20;

    struct LogEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type write_time;
        std::uintmax_t size = 0;
    };

    std::vector<LogEntry> entries;
    std::error_code ec;
    for (const auto & entry : std::filesystem::directory_iterator(config.log_root, ec)) {
        if (ec) {
            result.ok = false;
            result.exit_code = 49;
            result.fields["error"] = "failed to list log_root";
            return result;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".log") {
            continue;
        }
        LogEntry item;
        item.path = entry.path();
        item.write_time = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        item.size = entry.file_size(ec);
        if (ec) {
            item.size = 0;
            ec.clear();
        }
        entries.push_back(item);
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const LogEntry & left, const LogEntry & right) {
            return left.write_time > right.write_time;
        });

    const std::size_t count =
        std::min<std::size_t>(entries.size(), static_cast<std::size_t>(bounded_max_entries));
    result.fields["log_count"] = std::to_string(entries.size());
    result.fields["returned_count"] = std::to_string(count);
    for (std::size_t index = 0; index < count; ++index) {
        const LogEntry & item = entries[index];
        const auto system_now = std::chrono::system_clock::now();
        const auto file_now = std::filesystem::file_time_type::clock::now();
        const auto system_write_time =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                item.write_time - file_now + system_now);
        const std::time_t write_time = std::chrono::system_clock::to_time_t(system_write_time);
        const std::string prefix = "log_" + std::to_string(index);
        result.fields[prefix + "_path"] = item.path.string();
        result.fields[prefix + "_name"] = item.path.filename().string();
        result.fields[prefix + "_time"] = std::to_string(static_cast<long long>(write_time));
        result.fields[prefix + "_bytes"] = std::to_string(item.size);
    }

    if (count > 0) {
        CommandResult tail = TailTextFileResult(config, entries.front().path.string(), bounded_tail_lines);
        result.fields["latest_log_path"] = entries.front().path.string();
        result.fields["latest_log_name"] = entries.front().path.filename().string();
        result.fields["latest_log_tail"] = GetFieldOrDefault(tail, "content", "");
    } else {
        result.fields["latest_log_path"] = "";
        result.fields["latest_log_name"] = "";
        result.fields["latest_log_tail"] = "";
    }
    return result;
}
