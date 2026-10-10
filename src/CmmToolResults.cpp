#include "CmmToolResults.h"

#include "CmmBridge.h"
#include "HttpClient.h"
#include "comm.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace codex_lan_agent {
namespace {

std::string NormalizeCmmProjectNameLocal(const std::string & abs_path) {
    if (abs_path.empty()) {
        return "root";
    }

    std::string path = abs_path;
    for (char & ch : path) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    if (path.empty()) {
        return "root";
    }

    static const char hex_digits[] = "0123456789abcdef";
    std::string mapped;
    mapped.reserve(path.size() * 2 + 1);
    for (unsigned char c : path) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                          c == '-';
        if (safe) {
            mapped.push_back(static_cast<char>(c));
        } else if (c >= 0x80) {
            mapped.push_back(hex_digits[(c >> 4) & 0xF]);
            mapped.push_back(hex_digits[c & 0xF]);
        } else {
            mapped.push_back('-');
        }
    }

    std::string collapsed;
    collapsed.reserve(mapped.size());
    char prev = 0;
    for (char ch : mapped) {
        if ((ch == '-' && prev == '-') || (ch == '.' && prev == '.')) {
            continue;
        }
        collapsed.push_back(ch);
        prev = ch;
    }

    std::size_t start = 0;
    while (start < collapsed.size() &&
           (collapsed[start] == '-' || collapsed[start] == '.')) {
        ++start;
    }
    std::string result = collapsed.substr(start);

    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }

    if (result.empty()) {
        return "root";
    }

    return result;
}

CommandResult BuildCmmToolResult(
    const AgentConfig & config,
    const JsonRequestView & params,
    const char * tool_name,
    const std::vector<std::string> & simple_keys,
    int default_timeout_ms) {
    const std::string args_json = BuildCmmArgsJson(params, simple_keys);
    const int timeout_ms = std::max(1000, params.GetInt("timeout_ms", default_timeout_ms));

    CommandResult result = RunCmmToolCli(config, tool_name, args_json, timeout_ms);
    result.fields["cmm_tool"] = tool_name;
    result.fields["cmm_args_json"] = args_json;
    if (result.fields.find("result_json") == result.fields.end()) {
        result.fields["result_json"] = std::string();
    }
    return result;
}

}  // namespace

CommandResult BuildCmmIndexRepositoryResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "index_repository",
        {"repo_path", "name", "include", "exclude", "target_projects", "watch"},
        600000);
}

CommandResult BuildCmmSearchCodeResult(
    const AgentConfig & config, const JsonRequestView & params) {
    const std::string raw_args_json = params.GetRawJson("args_json");
    std::string args_json;
    if (!raw_args_json.empty()) {
        args_json = raw_args_json;
    } else {
        args_json = BuildCmmArgsJson(
            params,
            {"project",
             "query",
             "pattern",
             "file_pattern",
             "path_filter",
             "mode",
             "context",
             "regex",
             "limit",
             "offset"});
    }

    if (args_json.find("\"pattern\"") == std::string::npos) {
        const std::string query_value = params.GetString("query");
        if (!query_value.empty()) {
            if (args_json.size() > 1) {
                args_json.pop_back();
                args_json += ",\"pattern\":\"";
                args_json += JsonEscape(query_value);
                args_json += "\"}";
            } else {
                args_json += "\"pattern\":\"";
                args_json += JsonEscape(query_value);
                args_json += "\"}";
            }
        }
    }

    const int timeout_ms = std::max(1000, params.GetInt("timeout_ms", 60000));
    CommandResult result =
        RunCmmToolCli(config, "search_code", args_json, timeout_ms);
    result.fields["cmm_tool"] = "search_code";
    result.fields["cmm_args_json"] = args_json;
    if (result.fields.find("result_json") == result.fields.end()) {
        result.fields["result_json"] = std::string();
    }

    const std::string file_path = params.GetString("file_path");
    if (!file_path.empty()) {
        RememberRecentProbePath(file_path);
    }

    return result;
}

CommandResult BuildCmmSearchGraphResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "search_graph",
        {"project",
         "name_pattern",
         "label",
         "relationship",
         "kind",
         "limit",
         "offset",
         "min_degree",
         "max_degree",
         "exclude_entry_points",
         "fields"},
        60000);
}

CommandResult BuildCmmQueryGraphResult(
    const AgentConfig & config, const JsonRequestView & params) {
    // CMM query_graph defaults to a tree-table response; force JSON so the
    // bridge can parse it with the standard JSON extractor.
    const std::string raw_args_json = params.GetRawJson("args_json");
    std::string args_json;
    if (!raw_args_json.empty()) {
        args_json = raw_args_json;
    } else {
        args_json = BuildCmmArgsJson(
            params, {"project", "query", "max_rows", "graph"});
    }
    if (args_json.find("\"format\"") == std::string::npos) {
        args_json.pop_back();
        if (args_json.size() > 1) {
            args_json += ",";
        }
        args_json += "\"format\":\"json\"}";
    }

    const int timeout_ms = std::max(1000, params.GetInt("timeout_ms", 60000));
    CommandResult result =
        RunCmmToolCli(config, "query_graph", args_json, timeout_ms);
    result.fields["cmm_tool"] = "query_graph";
    result.fields["cmm_args_json"] = args_json;
    if (result.fields.find("result_json") == result.fields.end()) {
        result.fields["result_json"] = std::string();
    }
    return result;
}

CommandResult BuildCmmTracePathResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "trace_path",
        {"project",
         "function_name",
         "qualified_name",
         "direction",
         "depth",
         "risk_labels",
         "limit"},
        60000);
}

CommandResult BuildCmmGetCodeSnippetResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "get_code_snippet",
        {"project", "qualified_name", "file_path", "line_start", "line_end"},
        60000);
}

CommandResult BuildCmmGetGraphSchemaResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config, params, "get_graph_schema", {"project"}, 30000);
}

CommandResult BuildCmmGetArchitectureResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "get_architecture",
        {"project", "scope_path", "aspects", "include_tests"},
        60000);
}

CommandResult BuildCmmListProjectsResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(config, params, "list_projects", {}, 30000);
}

CommandResult BuildCmmIndexStatusResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config, params, "index_status", {"project"}, 30000);
}

CommandResult BuildCmmDetectChangesResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "detect_changes",
        {"project", "base_branch", "max_depth", "include_tests"},
        120000);
}

CommandResult BuildCmmDeleteProjectResult(
    const AgentConfig & config, const JsonRequestView & params) {
    return BuildCmmToolResult(
        config,
        params,
        "delete_project",
        {"project"},
        30000);
}

CommandResult BuildCmmEnsureIndexedResult(
    const AgentConfig & config, const JsonRequestView & params) {
    const std::string repo_path = params.GetString("repo_path");
    const std::string project_name = params.GetString("project");
    const std::string subpath = params.GetString("subpath");

    CommandResult result;
    result.ok = true;
    result.exit_code = 0;

    if (!project_name.empty()) {
        result.fields["resolved_project"] = project_name;
        result.fields["ensure_action"] = "check_only";
        result.fields["message"] = "project name provided; verify with index_status if needed";
        if (!subpath.empty()) {
            result.fields["path_filter_suggestion"] = subpath;
        }
        if (!repo_path.empty()) {
            RememberRecentProbePath(repo_path);
        }
        return result;
    }

    if (repo_path.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "repo_path or project is required";
        return result;
    }

    const std::string normalized = NormalizeCmmProjectNameLocal(repo_path);

    result.fields["repo_path"] = repo_path;
    result.fields["normalized_project"] = normalized;
    result.fields["ensure_action"] = "list_then_match";
    result.fields["message"] = "call list_projects to check if this path or a parent project is indexed";

    std::string cmm_args_json = "{\"project\":\"";
    cmm_args_json += JsonEscape(normalized);
    cmm_args_json += "\"}";

    CommandResult list_result = RunCmmToolCli(config, "list_projects", "{}", 30000);
    result.fields["list_projects_exit_code"] = std::to_string(list_result.exit_code);

    auto it = list_result.fields.find("result_json");
    if (it != list_result.fields.end() && !it->second.empty()) {
        result.fields["list_projects_result"] = it->second;
    }

    result.fields["cmm_tool"] = "ensure_indexed";
    result.fields["cmm_args_json"] = cmm_args_json;

    RememberRecentProbePath(repo_path);
    RememberRecentProbePath(normalized);

    return result;
}

}  // namespace codex_lan_agent
