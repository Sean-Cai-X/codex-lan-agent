#pragma once

#include "AgentConfig.h"
#include "comm.h"
#include "types.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace codex_lan_agent {

inline std::string FlowObsTrim(const std::string & value) {
    return CommOperations::Trim(value);
}

inline std::string FlowObsSingleQuotePowerShell(const std::string & value) {
    std::string output = "'";
    for (char ch : value) {
        if (ch == '\'') {
            output += "''";
        } else {
            output += ch;
        }
    }
    output += "'";
    return output;
}

inline std::string FlowObsDoubleQuoteCmd(const std::string & value) {
    std::string output = "\"";
    for (char ch : value) {
        if (ch == '"') {
            output += "\\\"";
        } else {
            output += ch;
        }
    }
    output += "\"";
    return output;
}

inline bool FlowObsWriteTextFile(const std::filesystem::path & path, const std::string & text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    return output.good();
}

inline std::string FlowObsReadTextFile(const std::filesystem::path & path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::string();
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

inline std::string FlowObsExtractJsonString(const std::string & json, const std::string & key) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return std::string();
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return std::string();
    }
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) {
        return std::string();
    }
    std::string output;
    bool escaped = false;
    for (std::size_t index = pos + 1; index < json.size(); ++index) {
        const char ch = json[index];
        if (escaped) {
            switch (ch) {
                case 'n': output += '\n'; break;
                case 'r': output += '\r'; break;
                case 't': output += '\t'; break;
                case '\\': output += '\\'; break;
                case '"': output += '"'; break;
                default: output += ch; break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        output += ch;
    }
    return output;
}

inline int FlowObsExtractJsonInt(const std::string & json, const std::string & key, int default_value = 0) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return default_value;
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return default_value;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
        ++pos;
    }
    std::size_t end = pos;
    if (end < json.size() && json[end] == '-') {
        ++end;
    }
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end])) != 0) {
        ++end;
    }
    if (end == pos) {
        return default_value;
    }
    try {
        return std::stoi(json.substr(pos, end - pos));
    } catch (...) {
        return default_value;
    }
}

inline bool FlowObsPathWithinWorkspace(const AgentConfig & config, const std::filesystem::path & raw_path) {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::weakly_canonical(raw_path, ec);
    if (ec) {
        return false;
    }
    const std::filesystem::path workspace = std::filesystem::weakly_canonical(config.workspace_root, ec);
    if (ec) {
        return false;
    }
    const std::wstring path_text = path.wstring();
    const std::wstring workspace_text = workspace.wstring();
    return path_text.size() >= workspace_text.size()
        && std::equal(workspace_text.begin(), workspace_text.end(), path_text.begin(),
            [](wchar_t left, wchar_t right) {
                return std::towlower(left) == std::towlower(right);
            });
}

inline std::filesystem::path FlowObsRuleRoot(const AgentConfig & config, const std::string & explicit_rule_root) {
    if (!FlowObsTrim(explicit_rule_root).empty()) {
        return std::filesystem::path(explicit_rule_root);
    }
    return std::filesystem::path(config.workspace_root) / "src" / "clips_rules";
}

inline std::filesystem::path FlowObsDefaultOutRoot(
    const AgentConfig & config,
    const std::string & prefix,
    const std::string & out_dir) {
    if (!FlowObsTrim(out_dir).empty()) {
        return std::filesystem::path(out_dir);
    }
    const std::filesystem::path root = FlowObsTrim(config.log_root).empty()
        ? (std::filesystem::path(config.workspace_root) / "logs")
        : std::filesystem::path(config.log_root);
    return root / prefix / ("run_" + CommOperations::TimeStampForFileName());
}

inline CommandResult FlowObsRunScript(
    const AgentConfig & config,
    const std::filesystem::path & script_path,
    const std::string & arguments,
    const std::filesystem::path & out_dir,
    const std::string & result_file_name) {
    CommandResult result;
    result.ok = false;
    result.exit_code = 1;
    result.fields["provider_id"] = "codex-lan-agent";
    result.fields["capability_id"] = "mcp_flow_observability";
    result.fields["script_path"] = script_path.string();
    result.fields["out_dir"] = out_dir.string();
    result.fields["result_ref"] = (out_dir / result_file_name).string();

    std::error_code ec;
    if (!std::filesystem::exists(script_path, ec) || ec) {
        result.fields["status"] = "failed";
        result.fields["error"] = "observer script not found";
        return result;
    }
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to create output directory";
        result.fields["error_message"] = ec.message();
        return result;
    }

    const std::filesystem::path stdout_path = out_dir / result_file_name;
    const std::filesystem::path stderr_path = out_dir / "script_stderr.txt";
    const std::string command =
        "powershell -ExecutionPolicy Bypass -File "
        + FlowObsDoubleQuoteCmd(script_path.string())
        + " " + arguments
        + " > " + FlowObsDoubleQuoteCmd(stdout_path.string())
        + " 2> " + FlowObsDoubleQuoteCmd(stderr_path.string());
    const int exit_code = std::system(command.c_str());
    result.exit_code = exit_code;
    result.fields["script_exit_code"] = std::to_string(exit_code);
    result.fields["stderr_ref"] = stderr_path.string();
    result.fields["script_stdout_ref"] = stdout_path.string();
    result.fields["script_stdout_json"] = CommOperations::Trim(FlowObsReadTextFile(stdout_path));
    result.fields["script_stderr"] = CommOperations::Trim(FlowObsReadTextFile(stderr_path));
    if (exit_code != 0) {
        result.fields["status"] = "failed";
        result.fields["error"] = "observer script failed";
        result.fields["summary"] = "MCP flow observability script failed";
        return result;
    }
    result.ok = true;
    result.fields["status"] = "success";
    return result;
}

inline CommandResult BuildMcpFlowVisualizeResult(
    const AgentConfig & config,
    const std::string & input_jsonl,
    const std::string & out_dir) {
    const std::filesystem::path input_path(input_jsonl);
    CommandResult result;
    result.fields["provider_id"] = "codex-lan-agent";
    result.fields["capability_id"] = "mcp_flow_visualize";
    result.fields["input_jsonl"] = input_jsonl;
    if (FlowObsTrim(input_jsonl).empty() || !std::filesystem::exists(input_path)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "failed";
        result.fields["error"] = "input_jsonl is required and must exist";
        return result;
    }
    const std::filesystem::path output_dir = FlowObsDefaultOutRoot(config, "mcp_flow_visualize", out_dir);
    const std::filesystem::path script_path =
        std::filesystem::path(config.workspace_root) / "src" / "clips_rules" / "observability" / "mcp_flow_observer.ps1";
    CommandResult run = FlowObsRunScript(
        config,
        script_path,
        "-InputJsonl " + FlowObsDoubleQuoteCmd(input_path.string())
            + " -OutDir " + FlowObsDoubleQuoteCmd(output_dir.string()),
        output_dir,
        "visualize_result.json");
    run.fields["capability_id"] = "mcp_flow_visualize";
    run.fields["result"] = "mcp_flow_visualized";
    run.fields["summary"] = run.ok ? "MCP flow graph generated" : "MCP flow graph generation failed";
    run.fields["input_jsonl"] = input_path.string();
    run.fields["flow_events_jsonl_path"] = (output_dir / "flow_events.jsonl").string();
    run.fields["flow_graph_dot_path"] = (output_dir / "flow_graph.dot").string();
    run.fields["flow_graph_mermaid_path"] = (output_dir / "flow_graph.mmd").string();
    run.fields["flow_state_json_path"] = (output_dir / "flow_state.json").string();
    run.fields["flow_state_graph_dot_path"] = (output_dir / "flow_state_graph.dot").string();
    run.fields["flow_state_graph_mermaid_path"] = (output_dir / "flow_state_graph.mmd").string();
    run.fields["flow_state_dashboard_html_path"] = (output_dir / "flow_state_dashboard.html").string();
    run.fields["flow_report_md_path"] = (output_dir / "flow_report.md").string();
    run.fields["violations_md_path"] = (output_dir / "violations.md").string();
    const std::string stdout_json = run.fields["script_stdout_json"];
    run.fields["event_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "event_count"));
    run.fields["tool_call_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "tool_call_count"));
    run.fields["tool_result_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "tool_result_count"));
    run.fields["violation_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "violation_count"));
    run.fields["completion_state"] = FlowObsExtractJsonString(stdout_json, "completion_state");
    run.fields["current_node"] = FlowObsExtractJsonString(stdout_json, "current_node");
    run.fields["next_expected_node"] = FlowObsExtractJsonString(stdout_json, "next_expected_node");
    return run;
}

inline CommandResult BuildMcpFlowAnalyzeResult(
    const AgentConfig & config,
    const std::string & input_jsonl,
    const std::string & rule_root,
    const std::string & out_root) {
    const std::filesystem::path input_path(input_jsonl);
    CommandResult result;
    result.fields["provider_id"] = "codex-lan-agent";
    result.fields["capability_id"] = "mcp_flow_analyze";
    result.fields["input_jsonl"] = input_jsonl;
    if (FlowObsTrim(input_jsonl).empty() || !std::filesystem::exists(input_path)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "failed";
        result.fields["error"] = "input_jsonl is required and must exist";
        return result;
    }
    const std::filesystem::path resolved_rule_root = FlowObsRuleRoot(config, rule_root);
    const std::filesystem::path output_dir = FlowObsDefaultOutRoot(config, "mcp_flow_analyze", out_root);
    const std::filesystem::path script_path =
        std::filesystem::path(config.workspace_root) / "src" / "clips_rules" / "observability" / "run_mcp_observability_acceptance.ps1";
    CommandResult run = FlowObsRunScript(
        config,
        script_path,
        "-RuleRoot " + FlowObsDoubleQuoteCmd(resolved_rule_root.string())
            + " -InputJsonl " + FlowObsDoubleQuoteCmd(input_path.string())
            + " -OutRoot " + FlowObsDoubleQuoteCmd(output_dir.string()),
        output_dir,
        "analyze_result.json");
    run.fields["capability_id"] = "mcp_flow_analyze";
    run.fields["result"] = "mcp_flow_analyzed";
    run.fields["summary"] = run.ok ? "MCP flow analysis report generated" : "MCP flow analysis failed";
    run.fields["input_jsonl"] = input_path.string();
    run.fields["rule_root"] = resolved_rule_root.string();
    run.fields["acceptance_summary_json_path"] = (output_dir / "acceptance_summary.json").string();
    run.fields["index_md_path"] = (output_dir / "index.md").string();
    run.fields["rules_report_md_path"] = (output_dir / "rules" / "rules_impact_report.md").string();
    run.fields["rules_fact_graph_dot_path"] = (output_dir / "rules" / "rules_fact_graph.dot").string();
    run.fields["flow_report_md_path"] = (output_dir / "flow" / "flow_report.md").string();
    run.fields["flow_graph_dot_path"] = (output_dir / "flow" / "flow_graph.dot").string();
    run.fields["flow_graph_mermaid_path"] = (output_dir / "flow" / "flow_graph.mmd").string();
    run.fields["flow_state_json_path"] = (output_dir / "flow" / "flow_state.json").string();
    run.fields["flow_state_graph_dot_path"] = (output_dir / "flow" / "flow_state_graph.dot").string();
    run.fields["flow_state_graph_mermaid_path"] = (output_dir / "flow" / "flow_state_graph.mmd").string();
    run.fields["flow_state_dashboard_html_path"] = (output_dir / "flow" / "flow_state_dashboard.html").string();
    run.fields["violations_md_path"] = (output_dir / "flow" / "violations.md").string();
    const std::string stdout_json = run.fields["script_stdout_json"];
    run.fields["conclusion"] = FlowObsExtractJsonString(stdout_json, "conclusion");
    run.fields["event_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "event_count"));
    run.fields["tool_call_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "tool_call_count"));
    run.fields["tool_result_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "tool_result_count"));
    run.fields["violation_count"] = std::to_string(FlowObsExtractJsonInt(stdout_json, "violation_count"));
    run.fields["completion_state"] = FlowObsExtractJsonString(stdout_json, "completion_state");
    run.fields["current_node"] = FlowObsExtractJsonString(stdout_json, "current_node");
    run.fields["next_expected_node"] = FlowObsExtractJsonString(stdout_json, "next_expected_node");
    return run;
}

inline std::string FlowObsHtmlEscape(const std::string & text) {
    std::string output;
    for (char ch : text) {
        switch (ch) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            default: output += ch; break;
        }
    }
    return output;
}

inline CommandResult BuildMcpFlowExportResult(
    const AgentConfig & config,
    const std::string & input_jsonl,
    const std::string & rule_root,
    const std::string & out_root) {
    CommandResult analysis = BuildMcpFlowAnalyzeResult(config, input_jsonl, rule_root, out_root);
    analysis.fields["capability_id"] = "mcp_flow_export";
    analysis.fields["result"] = "mcp_flow_exported";
    if (!analysis.ok) {
        analysis.fields["summary"] = "MCP flow export failed during analysis";
        return analysis;
    }
    const std::filesystem::path output_dir(analysis.fields["out_dir"]);
    const std::filesystem::path html_path = output_dir / "mcp_flow_report.html";
    const std::string index_md = FlowObsReadTextFile(output_dir / "index.md");
    const std::string flow_report = FlowObsReadTextFile(output_dir / "flow" / "flow_report.md");
    const std::string violations = FlowObsReadTextFile(output_dir / "flow" / "violations.md");
    const std::string flow_mermaid = FlowObsReadTextFile(output_dir / "flow" / "flow_graph.mmd");
    const std::string flow_state = FlowObsReadTextFile(output_dir / "flow" / "flow_state.json");
    const std::string flow_state_mermaid = FlowObsReadTextFile(output_dir / "flow" / "flow_state_graph.mmd");
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><title>MCP Flow Report</title>"
         << "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:24px;line-height:1.45}"
         << "pre{background:#f6f8fa;border:1px solid #d0d7de;padding:12px;overflow:auto}"
         << "section{margin:20px 0}h1,h2{margin-bottom:8px}</style></head><body>"
         << "<h1>MCP Flow Observability Report</h1>"
         << "<section><h2>Summary</h2><pre>" << FlowObsHtmlEscape(index_md) << "</pre></section>"
         << "<section><h2>Fixed Runtime State</h2><pre>" << FlowObsHtmlEscape(flow_state) << "</pre></section>"
         << "<section><h2>Fixed State Mermaid</h2><pre>" << FlowObsHtmlEscape(flow_state_mermaid) << "</pre></section>"
         << "<section><h2>Flow Report</h2><pre>" << FlowObsHtmlEscape(flow_report) << "</pre></section>"
         << "<section><h2>Violations</h2><pre>" << FlowObsHtmlEscape(violations) << "</pre></section>"
         << "<section><h2>Mermaid</h2><pre>" << FlowObsHtmlEscape(flow_mermaid) << "</pre></section>"
         << "</body></html>";
    if (!FlowObsWriteTextFile(html_path, html.str())) {
        analysis.ok = false;
        analysis.exit_code = 1;
        analysis.fields["status"] = "failed";
        analysis.fields["error"] = "failed to write html export";
        return analysis;
    }
    analysis.fields["status"] = "success";
    analysis.fields["summary"] = "MCP flow HTML report exported";
    analysis.fields["html_report_path"] = html_path.string();
    analysis.fields["artifact_bundle_dir"] = output_dir.string();
    return analysis;
}

inline std::string FlowObsField(
    const CommandResult & result,
    const std::string & key,
    const std::string & fallback = std::string()) {
    const auto found = result.fields.find(key);
    return found == result.fields.end() ? fallback : found->second;
}

inline CommandResult BuildMcpFlowConformanceCheckResult(
    const AgentConfig & config,
    const std::string & input_jsonl,
    const std::string & out_dir) {
    CommandResult result = BuildMcpFlowVisualizeResult(config, input_jsonl, out_dir);
    result.fields["capability_id"] = "mcp_flow_conformance_check";
    result.fields["result"] = "mcp_flow_conformance_checked";
    const int violations = std::atoi(FlowObsField(result, "violation_count", "0").c_str());
    const bool pass = result.ok && violations == 0;
    result.fields["conformance_pass"] = pass ? "true" : "false";
    result.fields["conclusion"] =
        pass ? "MCP_FLOW_CONFORMANCE_PASS" : "MCP_FLOW_CONFORMANCE_HAS_VIOLATIONS";
    result.fields["summary"] =
        pass ? "MCP flow conforms to the fixed flow template" : "MCP flow conformance violations detected";
    return result;
}

}  // namespace codex_lan_agent
