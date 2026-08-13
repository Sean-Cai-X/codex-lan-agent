#pragma once

#include "AgentConfig.h"
#include "HttpClient.h"
#include "types.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <functional>
#include <chrono>
#include <map>
#include <cctype>

namespace codex_lan_agent {
namespace mcp_flow_dashboard {

inline bool ReadTextFileLocal(const std::filesystem::path & path, std::string * content) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *content = buffer.str();
    return true;
}

inline std::filesystem::path LatestFlowStatePath(const AgentConfig & config, const std::string & goal_id) {
    const char * override_path = std::getenv("CODEX_LAN_AGENT_FLOW_STATE_PATH");
    if (override_path != nullptr && override_path[0] != '\0') {
        return std::filesystem::path(override_path);
    }

    const std::filesystem::path log_root(config.log_root);
    std::vector<std::filesystem::path> candidates;
    if (!goal_id.empty()) {
        candidates.push_back(log_root / "mcp_flow_runtime" / goal_id / "flow_state.json");
        candidates.push_back(log_root / "task_memory" / goal_id / "flow_state.json");
    }

    std::error_code ec;
    std::filesystem::path latest;
    std::filesystem::file_time_type latest_time{};
    for (const auto & candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
            const auto write_time = std::filesystem::last_write_time(candidate, ec);
            if (!ec && (latest.empty() || write_time > latest_time)) {
                latest = candidate;
                latest_time = write_time;
            }
        }
        ec.clear();
    }
    if (!latest.empty()) {
        return latest;
    }

    int inspected = 0;
    for (const auto & entry : std::filesystem::recursive_directory_iterator(log_root, ec)) {
        if (ec) {
            break;
        }
        if (++inspected > 20000) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec || entry.path().filename() != "flow_state.json") {
            ec.clear();
            continue;
        }
        const auto write_time = entry.last_write_time(ec);
        if (!ec && (latest.empty() || write_time > latest_time)) {
            latest = entry.path();
            latest_time = write_time;
        }
        ec.clear();
    }
    return latest;
}

inline std::string DashboardDoubleQuoteCmd(const std::string & value) {
    std::string output = "\"";
    for (char ch : value) {
        if (ch == '"') {
            output += "\\\"";
        } else {
            output.push_back(ch);
        }
    }
    output += "\"";
    return output;
}

inline std::string DashboardHexHash(const std::string & value) {
    std::ostringstream output;
    output << std::hex << std::hash<std::string>{}(value);
    return output.str();
}

inline std::string DashboardExtractJsonString(const std::string & json, const std::string & key) {
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

inline std::string DashboardStripUtf8Bom(std::string value) {
    if (value.size() >= 3
        && static_cast<unsigned char>(value[0]) == 0xEF
        && static_cast<unsigned char>(value[1]) == 0xBB
        && static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

inline std::string DashboardExtractJsonArrayRaw(const std::string & json, const std::string & key) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return std::string();
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return std::string();
    }
    pos = json.find('[', pos + 1);
    if (pos == std::string::npos) {
        return std::string();
    }
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = pos; index < json.size(); ++index) {
        const char ch = json[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(pos, index - pos + 1);
            }
        }
    }
    return std::string();
}

inline std::vector<std::string> DashboardSplitTopLevelJsonObjects(const std::string & array_json) {
    std::vector<std::string> objects;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string::npos;
    for (std::size_t index = 0; index < array_json.size(); ++index) {
        const char ch = array_json[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                start = index;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(array_json.substr(start, index - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

inline bool DashboardJsonBool(const std::string & json, const std::string & key, bool fallback = false) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (json.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        return false;
    }
    return fallback;
}

inline std::string DashboardJsonNumber(const std::string & json, const std::string & key, const std::string & fallback = "") {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    std::size_t end = pos;
    while (end < json.size()
        && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
        ++end;
    }
    return end > pos ? json.substr(pos, end - pos) : fallback;
}

inline std::string DashboardToolContentField(const std::string & content, const std::string & key) {
    const std::string marker = key + "=";
    std::size_t pos = content.find(marker);
    if (pos == std::string::npos) {
        return std::string();
    }
    pos += marker.size();
    const std::size_t end = content.find('\n', pos);
    return end == std::string::npos ? content.substr(pos) : content.substr(pos, end - pos);
}

inline std::filesystem::path LatestTaskMemoryBudgetRunPath(const AgentConfig & config) {
    const std::filesystem::path root = std::filesystem::path(config.log_root) / "task_memory";
    std::filesystem::path latest;
    std::filesystem::file_time_type latest_time{};
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return latest;
    }
    int inspected = 0;
    for (const auto & entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (++inspected > 30000) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec || entry.path().extension() != ".json") {
            ec.clear();
            continue;
        }
        if (entry.path().parent_path().filename() != "budget_runs") {
            continue;
        }
        const auto write_time = entry.last_write_time(ec);
        if (!ec && (latest.empty() || write_time > latest_time)) {
            latest = entry.path();
            latest_time = write_time;
        }
        ec.clear();
    }
    return latest;
}

inline std::string DashboardNodeStatus(
    const std::string & node_id,
    const std::map<std::string, int> & visits,
    const std::map<std::string, std::string> & last_status,
    const std::string & current_node,
    bool terminal_state) {
    auto visit_it = visits.find(node_id);
    if (node_id == "complete" && terminal_state) {
        return "success";
    }
    if (node_id == current_node && visit_it != visits.end() && visit_it->second > 0) {
        return "running";
    }
    auto status_it = last_status.find(node_id);
    if (status_it == last_status.end()) {
        return "not_started";
    }
    if (status_it->second == "success") {
        return "success";
    }
    if (status_it->second == "needs_continue") {
        return "running";
    }
    if (status_it->second == "failed") {
        return "violated";
    }
    return status_it->second;
}

inline std::string BuildFlowStateFromTaskMemoryBudget(
    const std::filesystem::path & budget_path,
    std::string * error) {
    std::string budget_json;
    if (budget_path.empty() || !ReadTextFileLocal(budget_path, &budget_json)) {
        if (error != nullptr) {
            *error = "latest task memory budget run not found";
        }
        return std::string();
    }
    budget_json = DashboardStripUtf8Bom(budget_json);
    const std::vector<std::string> step_events =
        DashboardSplitTopLevelJsonObjects(DashboardExtractJsonArrayRaw(budget_json, "step_events"));
    const bool terminal_state = DashboardJsonBool(budget_json, "terminal_state", false);
    const bool completion_claim_allowed = DashboardJsonBool(budget_json, "completion_claim_allowed", false);
    const bool budget_exhausted = DashboardJsonBool(budget_json, "budget_exhausted", false);
    const std::string budget_status = DashboardExtractJsonString(budget_json, "budget_status");
    std::map<std::string, int> visits;
    std::map<std::string, std::string> last_status;
    std::vector<std::string> visited_nodes;
    std::vector<std::pair<std::string, std::string>> transitions;
    std::string previous_node;
    std::string current_node;
    std::string next_expected_node;
    int violation_count = 0;

    for (const std::string & step : step_events) {
        const std::string tool = DashboardExtractJsonString(step, "tool_name");
        const std::string status = DashboardExtractJsonString(step, "status");
        const std::string matched_rule = DashboardExtractJsonString(step, "clips_post_result_matched_rule");
        std::string node;
        if (tool == "lan_agent_probe_text_file") {
            node = visits["probe_current_file"] == 0 ? "probe_current_file" : "probe_next_file";
        } else if (tool == "lan_agent_delete_text_range_window_atomic"
            || tool == "lan_agent_delete_next_text_range_atomic") {
            node = "delete_current_file_window";
        } else if (tool == "lan_agent_mcp_route") {
            node = "route_request";
        } else if (tool == "lan_agent_list_directory") {
            node = "list_directory";
        } else {
            node = "route_request";
        }
        ++visits[node];
        last_status[node] = status;
        current_node = node;
        visited_nodes.push_back(node);
        if (!previous_node.empty()) {
            transitions.push_back({previous_node, node});
        }
        previous_node = node;
        if (status == "failed") {
            ++violation_count;
        }
        if (matched_rule == "directory-scope-next-file-requires-probe") {
            next_expected_node = "probe_next_file";
        } else if (matched_rule == "declared-next-call-json-requires-continuation") {
            next_expected_node = "delete_current_file_window";
        }
    }
    if (terminal_state && completion_claim_allowed) {
        next_expected_node = "";
        transitions.push_back({current_node, "complete"});
        current_node = "complete";
    }
    const std::string last_step = step_events.empty() ? std::string() : step_events.back();
    const std::string last_tool_name = DashboardExtractJsonString(last_step, "tool_name");
    const std::string last_tool_status = DashboardExtractJsonString(last_step, "status");
    const std::string last_required_tool_name = DashboardExtractJsonString(last_step, "required_tool_name");
    const std::string last_matched_rule = DashboardExtractJsonString(last_step, "clips_post_result_matched_rule");
    const std::string last_reason_code = DashboardExtractJsonString(last_step, "clips_post_result_reason_code");
    const std::string last_remaining = DashboardJsonNumber(last_step, "directory_remaining_code_file_count");
    const std::string last_result_ref = DashboardExtractJsonString(last_step, "result_ref");
    const std::string last_evidence_ref = DashboardExtractJsonString(last_step, "evidence_ref");
    const bool last_has_more = DashboardJsonBool(last_step, "has_more", false);
    std::string problem_class;
    std::string problem_reason;
    if (!terminal_state && budget_exhausted) {
        problem_class = "continuation_budget_exhausted_not_terminal";
        problem_reason = "本轮 budget 已用完，但 terminal_state=false；必须用 task_memory_resume_and_execute 或 execute_continuation_budget 继续。";
    } else if (!terminal_state && !last_required_tool_name.empty()) {
        problem_class = "required_tool_pending";
        problem_reason = "最后一步仍给出 required_tool_name，不能宣称完成。";
    } else if (!terminal_state && last_has_more) {
        problem_class = "text_range_delete_incomplete";
        problem_reason = "最后一步 has_more=true，窗口删除仍未完成。";
    } else if (!terminal_state) {
        problem_class = "completion_gate_not_satisfied";
        problem_reason = "terminal_state=false 且 completion_claim_allowed=false。";
    }

    auto node_json = [&](const std::string & id, const std::string & tool, const std::string & purpose) {
        std::ostringstream node;
        node << "{\"id\":\"" << JsonEscape(id) << "\","
             << "\"tool\":\"" << JsonEscape(tool) << "\","
             << "\"purpose\":\"" << JsonEscape(purpose) << "\","
             << "\"status\":\"" << JsonEscape(DashboardNodeStatus(id, visits, last_status, current_node, terminal_state && completion_claim_allowed)) << "\","
             << "\"visit_count\":" << visits[id] << ","
             << "\"last_event_status\":\"" << JsonEscape(last_status[id]) << "\","
             << "\"violations\":[]}";
        return node.str();
    };

    std::ostringstream output;
    output
        << "{"
        << "\"flow_id\":\"directory_comment_cleanup_bounded_window_v1\","
        << "\"title\":\"Directory comment cleanup by bounded file windows\","
        << "\"state_source\":\"latest_task_memory_budget\","
        << "\"task_memory_budget_path\":\"" << JsonEscape(budget_path.string()) << "\","
        << "\"dashboard_probe_available\":true,"
        << "\"dashboard_probe_tool_result_count\":" << step_events.size() << ","
        << "\"dashboard_last_tool_name\":\"" << JsonEscape(last_tool_name) << "\","
        << "\"dashboard_last_tool_status\":\"" << JsonEscape(last_tool_status) << "\","
        << "\"dashboard_last_budget_status\":\"" << JsonEscape(budget_status) << "\","
        << "\"dashboard_last_executed_step_count\":\"" << JsonEscape(DashboardJsonNumber(budget_json, "executed_step_count")) << "\","
        << "\"dashboard_last_terminal_state\":\"" << (terminal_state ? "true" : "false") << "\","
        << "\"dashboard_last_completion_claim_allowed\":\"" << (completion_claim_allowed ? "true" : "false") << "\","
        << "\"dashboard_last_final_answer_allowed\":\"" << (completion_claim_allowed ? "true" : "false") << "\","
        << "\"dashboard_last_verification_ok\":\"" << (terminal_state && completion_claim_allowed ? "true" : "false") << "\","
        << "\"dashboard_last_continue_required\":\"" << (!terminal_state ? "true" : "false") << "\","
        << "\"dashboard_last_required_tool_name\":\"" << JsonEscape(last_required_tool_name) << "\","
        << "\"dashboard_last_directory_remaining_code_file_count\":\"" << JsonEscape(last_remaining) << "\","
        << "\"dashboard_last_evidence_ref\":\"" << JsonEscape(last_evidence_ref.empty() ? last_result_ref : last_evidence_ref) << "\","
        << "\"dashboard_last_tool_content_preview\":\""
        << JsonEscape("matched_rule=" + last_matched_rule
            + "\\nreason_code=" + last_reason_code
            + "\\nrequired_tool_name=" + last_required_tool_name
            + "\\nhas_more=" + (last_has_more ? std::string("true") : std::string("false"))
            + "\\ndirectory_remaining_code_file_count=" + last_remaining)
        << "\","
        << "\"dashboard_last_problem_class\":\"" << JsonEscape(problem_class) << "\","
        << "\"dashboard_last_problem_reason\":\"" << JsonEscape(problem_reason) << "\","
        << "\"goal_id\":\"" << JsonEscape(DashboardExtractJsonString(budget_json, "goal_id")) << "\","
        << "\"trace_id\":\"" << JsonEscape(DashboardExtractJsonString(budget_json, "trace_id")) << "\","
        << "\"completion_state\":\"" << (terminal_state && completion_claim_allowed ? "terminal_complete" : "not_complete") << "\","
        << "\"current_node\":\"" << JsonEscape(current_node) << "\","
        << "\"next_expected_node\":\"" << JsonEscape(next_expected_node) << "\","
        << "\"completion_gate_satisfied\":" << (terminal_state && completion_claim_allowed ? "true" : "false") << ","
        << "\"violation_count\":" << violation_count << ","
        << "\"visited_nodes\":[";
    for (std::size_t i = 0; i < visited_nodes.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        output << "\"" << JsonEscape(visited_nodes[i]) << "\"";
    }
    output
        << "],\"nodes\":["
        << node_json("route_request", "lan_agent_mcp_route", "Normalize user intent and enter CLIPS guard domain.") << ","
        << node_json("list_directory", "lan_agent_list_directory", "Build a file manifest only. Do not read file bodies.") << ","
        << node_json("probe_current_file", "lan_agent_probe_text_file", "Probe exactly one concrete file before mutation.") << ","
        << node_json("delete_current_file_window", "lan_agent_delete_text_range_window_atomic", "Delete comments in one bounded 200-line window and verify readback.") << ","
        << node_json("probe_next_file", "lan_agent_probe_text_file", "Move to the next concrete file from the manifest.") << ","
        << node_json("complete", "final_answer", "Allowed only after all completion gate fields are true.")
        << "],\"template_edges\":["
        << "{\"from\":\"route_request\",\"to\":\"list_directory\"},"
        << "{\"from\":\"route_request\",\"to\":\"probe_current_file\"},"
        << "{\"from\":\"probe_current_file\",\"to\":\"delete_current_file_window\"},"
        << "{\"from\":\"delete_current_file_window\",\"to\":\"probe_next_file\"},"
        << "{\"from\":\"probe_next_file\",\"to\":\"delete_current_file_window\"},"
        << "{\"from\":\"delete_current_file_window\",\"to\":\"complete\"}"
        << "],\"actual_transitions\":[";
    for (std::size_t i = 0; i < transitions.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        output << "{\"from\":\"" << JsonEscape(transitions[i].first)
               << "\",\"to\":\"" << JsonEscape(transitions[i].second) << "\"}";
    }
    output << "]}";
    return output.str();
}

inline std::string DashboardTruncate(const std::string & value, std::size_t max_chars) {
    if (value.size() <= max_chars) {
        return value;
    }
    return value.substr(0, max_chars) + "...";
}

inline std::string DashboardBuildProbeMetadata(
    const std::filesystem::path & jsonl_path,
    const std::filesystem::path & state_path = std::filesystem::path()) {
    if (jsonl_path.empty()) {
        return "\"dashboard_probe_available\":false,";
    }
    std::ifstream input(jsonl_path, std::ios::binary);
    if (!input.is_open()) {
        return "\"dashboard_probe_available\":false,";
    }
    std::string line;
    int step_index = 0;
    int tool_result_count = 0;
    int last_tool_step = -1;
    int last_assistant_text_step = -1;
    bool saw_directory_cleanup_flow = false;
    std::string last_tool_content;
    std::string last_assistant_text;
    while (std::getline(input, line)) {
        if (line.find("\"role\":\"tool\"") != std::string::npos) {
            ++tool_result_count;
            last_tool_step = step_index;
            last_tool_content = DashboardExtractJsonString(line, "content");
            if (last_tool_content.find("directory_mutation_flow=comment_cleanup") != std::string::npos
                || last_tool_content.find("remaining_code_file_count=") != std::string::npos) {
                saw_directory_cleanup_flow = true;
            }
        } else if (line.find("\"role\":\"assistant\"") != std::string::npos) {
            const std::string content = DashboardExtractJsonString(line, "content");
            if (!content.empty()) {
                last_assistant_text_step = step_index;
                last_assistant_text = content;
            }
        }
        ++step_index;
    }

    const std::string terminal_state = DashboardToolContentField(last_tool_content, "terminal_state");
    const std::string completion_claim_allowed = DashboardToolContentField(last_tool_content, "completion_claim_allowed");
    const std::string final_answer_allowed = DashboardToolContentField(last_tool_content, "final_answer_allowed");
    const std::string assistant_response_allowed = DashboardToolContentField(last_tool_content, "assistant_response_allowed");
    const std::string verification_ok = DashboardToolContentField(last_tool_content, "verification_ok");
    const std::string continue_required = DashboardToolContentField(last_tool_content, "continue_required");
    const std::string required_tool_name = DashboardToolContentField(last_tool_content, "required_tool_name");
    const std::string required_tool_arguments_json = DashboardToolContentField(last_tool_content, "required_tool_arguments_json");
    const std::string budget_status = DashboardToolContentField(last_tool_content, "budget_status");
    const std::string executed_step_count = DashboardToolContentField(last_tool_content, "executed_step_count");
    const std::string current_node = DashboardToolContentField(last_tool_content, "current_tool_chain_node");
    const std::string routed_tool = DashboardToolContentField(last_tool_content, "routed_tool_name");
    const std::string tool_use_decision = DashboardToolContentField(last_tool_content, "tool_use_decision");
    const std::string pre_call_reason =
        DashboardToolContentField(last_tool_content, "clips_pre_call_tool_reason_code");
    const std::string pre_call_route_target =
        DashboardToolContentField(last_tool_content, "clips_pre_call_tool_route_target");
    const std::string directory_remaining = DashboardToolContentField(last_tool_content, "directory_remaining_code_file_count");
    const std::string directory_scope_active = DashboardToolContentField(last_tool_content, "directory_scope_active");
    const std::string directory_current_file_index = DashboardToolContentField(last_tool_content, "directory_current_file_index");
    const std::string directory_total_code_file_count = DashboardToolContentField(last_tool_content, "directory_total_code_file_count");
    const std::string flow_id = DashboardToolContentField(last_tool_content, "flow_id");
    const std::string flow_task_list_required = DashboardToolContentField(last_tool_content, "flow_task_list_required");
    const std::string flow_current_task_id = DashboardToolContentField(last_tool_content, "flow_current_task_id");
    const std::string flow_next_task_id = DashboardToolContentField(last_tool_content, "flow_next_task_id");
    const std::string flow_task_list_path = DashboardToolContentField(last_tool_content, "flow_task_list_path");
    const std::string flow_task_list_md_path = DashboardToolContentField(last_tool_content, "flow_task_list_md_path");
    const std::string evidence_ref = DashboardToolContentField(last_tool_content, "evidence_ref");
    const std::string last_tool_status = DashboardToolContentField(last_tool_content, "status");
    const std::string last_tool_error = DashboardToolContentField(last_tool_content, "error");
    const int directory_current_index_int = directory_current_file_index.empty()
        ? -1
        : std::atoi(directory_current_file_index.c_str());
    const int directory_total_count_int = directory_total_code_file_count.empty()
        ? 0
        : std::atoi(directory_total_code_file_count.c_str());

    std::string problem_class;
    std::string problem_reason;
    if (pre_call_reason == "pending_continuation_mismatch") {
        const std::string content_tool_name = DashboardToolContentField(last_tool_content, "tool_name");
        const bool current_node_self_reroute =
            !pre_call_route_target.empty() && pre_call_route_target == current_node;
        const bool legacy_gateway_self_reroute =
            content_tool_name == "lan_agent_mcp_route"
            && !pre_call_route_target.empty()
            && pre_call_route_target == required_tool_name
            && DashboardToolContentField(last_tool_content, "result") == "pre_guard_rerouted";
        if (current_node_self_reroute || legacy_gateway_self_reroute) {
            problem_class = "gateway_outer_preflight_self_reroute";
            problem_reason =
                "历史回放显示当前工具节点与 CLIPS 改道目标相同；这是旧版网关外层预检把 lan_agent_mcp_route 与内部目标工具混淆后造成的自我重路由。当前服务已改为只在内部 target_tool_name 层执行 pending continuation 检查。";
        } else {
            problem_class = "pending_continuation_mismatch";
            problem_reason = "当前调用没有执行 MCP 已保存的下一步；CLIPS 已改道到 "
                + pre_call_route_target
                + "，必须原样调用 required_tool_arguments_json。";
        }
    } else if (last_tool_status == "failed" && last_tool_error == "goal_id is required") {
        problem_class = "task_memory_freeze_goal_id_missing";
        problem_reason = "task_memory_freeze 收到的参数缺少 goal_id；常见原因是模型把 goal_id 改写成 current_goal_id 或没有原样调用 required_tool_arguments_json。";
    } else if (last_tool_status == "failed") {
        problem_class = "last_tool_failed";
        problem_reason = "最后一次 MCP 工具调用失败: " + last_tool_error;
    } else if (tool_use_decision == "no_tool_resolved") {
        problem_class = "route_no_tool_resolved_not_executed";
        problem_reason = "路由层没有解析出可执行内部工具；本轮没有执行文件处理，不能宣称开始、等待或完成。需要重新用明确 file_path/directory_path + primary_intent 路由，或调用 MCP 返回的恢复工具。";
    } else if (directory_scope_active == "true"
        && terminal_state == "true"
        && directory_total_count_int > 0
        && directory_current_index_int >= 0
        && directory_current_index_int + 1 < directory_total_count_int) {
        problem_class = "directory_scope_single_file_terminal";
        problem_reason = "目录级任务只处理到第 "
            + directory_current_file_index
            + " 个代码文件，但最后工具返回 terminal_state=true；这是单文件完成被误当成目录完成。";
    } else if (last_assistant_text_step > last_tool_step
        && (assistant_response_allowed == "false" || final_answer_allowed == "false" || continue_required == "true")) {
        problem_class = "assistant_text_after_non_terminal_tool";
        problem_reason = "最后一次工具结果仍要求继续，但后续出现了自然语言结论或等待文本。";
    } else if (terminal_state == "true"
        && completion_claim_allowed == "true"
        && saw_directory_cleanup_flow
        && directory_remaining.empty()
        && routed_tool == "lan_agent_task_memory_execute_continuation_budget") {
        problem_class = "terminal_budget_missing_directory_scope";
        problem_reason = "budget runner 给出 terminal，但最后结果缺少目录级剩余文件字段；不能证明 61 个代码文件都已处理。";
    } else if (!directory_remaining.empty() && directory_remaining != "0" && terminal_state == "true") {
        problem_class = "terminal_with_directory_remaining";
        problem_reason = "结果声明 terminal，但目录级 remaining_code_file_count 仍大于 0。";
    } else if (verification_ok == "false") {
        problem_class = "verification_not_ok";
        problem_reason = "最后工具结果 verification_ok=false。";
    } else if (!required_tool_name.empty() && continue_required == "true") {
        problem_class = "required_tool_pending";
        problem_reason = "最后工具结果要求继续调用 required_tool_arguments_json。";
    }
    if (problem_class.empty() && !state_path.empty()) {
        const std::filesystem::path violations_path = state_path.parent_path() / "violations.json";
        std::string violations_json;
        if (ReadTextFileLocal(violations_path, &violations_json)) {
            if (violations_json.find("required_tool_arguments_ignored") != std::string::npos) {
                problem_class = "required_tool_arguments_ignored";
                problem_reason =
                    "历史回放显示模型没有执行工具返回的 required_tool_arguments_json；应按 MCP 指定的下一步工具原样续跑。";
            } else if (violations_json.find("tool_result_failed") != std::string::npos) {
                problem_class = "tool_result_failed";
                problem_reason = "历史回放显示工具调用失败后仍被当作可接受证据。";
            } else if (violations_json.find("manual_content_processing_forbidden") != std::string::npos) {
                problem_class = "manual_content_processing_forbidden";
                problem_reason =
                    "历史回放显示长流程仍未完成时，助手输出了自然语言完成/处理结论。";
            }
        }
    }

    std::ostringstream output;
    output
        << "\"dashboard_probe_available\":true,"
        << "\"dashboard_probe_tool_result_count\":" << tool_result_count << ","
        << "\"dashboard_last_tool_step\":" << last_tool_step << ","
        << "\"dashboard_last_assistant_text_step\":" << last_assistant_text_step << ","
        << "\"dashboard_last_tool_node\":\"" << JsonEscape(current_node) << "\","
        << "\"dashboard_last_tool_name\":\"" << JsonEscape(routed_tool) << "\","
        << "\"dashboard_last_tool_status\":\"" << JsonEscape(last_tool_status) << "\","
        << "\"dashboard_last_tool_error\":\"" << JsonEscape(last_tool_error) << "\","
        << "\"dashboard_last_tool_use_decision\":\"" << JsonEscape(tool_use_decision) << "\","
        << "\"dashboard_last_pre_call_reason\":\"" << JsonEscape(pre_call_reason) << "\","
        << "\"dashboard_last_pre_call_route_target\":\"" << JsonEscape(pre_call_route_target) << "\","
        << "\"dashboard_last_tool_result\":\"" << JsonEscape(DashboardToolContentField(last_tool_content, "result")) << "\","
        << "\"dashboard_last_budget_status\":\"" << JsonEscape(budget_status) << "\","
        << "\"dashboard_last_executed_step_count\":\"" << JsonEscape(executed_step_count) << "\","
        << "\"dashboard_last_terminal_state\":\"" << JsonEscape(terminal_state) << "\","
        << "\"dashboard_last_completion_claim_allowed\":\"" << JsonEscape(completion_claim_allowed) << "\","
        << "\"dashboard_last_final_answer_allowed\":\"" << JsonEscape(final_answer_allowed) << "\","
        << "\"dashboard_last_verification_ok\":\"" << JsonEscape(verification_ok) << "\","
        << "\"dashboard_last_continue_required\":\"" << JsonEscape(continue_required) << "\","
        << "\"dashboard_last_required_tool_name\":\"" << JsonEscape(required_tool_name) << "\","
        << "\"dashboard_last_required_tool_arguments_json\":\"" << JsonEscape(required_tool_arguments_json) << "\","
        << "\"dashboard_last_directory_remaining_code_file_count\":\"" << JsonEscape(directory_remaining) << "\","
        << "\"dashboard_last_directory_current_file_index\":\"" << JsonEscape(directory_current_file_index) << "\","
        << "\"dashboard_last_directory_total_code_file_count\":\"" << JsonEscape(directory_total_code_file_count) << "\","
        << "\"dashboard_last_flow_id\":\"" << JsonEscape(flow_id) << "\","
        << "\"dashboard_last_flow_task_list_required\":\"" << JsonEscape(flow_task_list_required) << "\","
        << "\"dashboard_last_flow_current_task_id\":\"" << JsonEscape(flow_current_task_id) << "\","
        << "\"dashboard_last_flow_next_task_id\":\"" << JsonEscape(flow_next_task_id) << "\","
        << "\"dashboard_last_flow_task_list_path\":\"" << JsonEscape(flow_task_list_path) << "\","
        << "\"dashboard_last_flow_task_list_md_path\":\"" << JsonEscape(flow_task_list_md_path) << "\","
        << "\"dashboard_last_evidence_ref\":\"" << JsonEscape(evidence_ref) << "\","
        << "\"dashboard_last_assistant_text\":\"" << JsonEscape(DashboardTruncate(last_assistant_text, 500)) << "\","
        << "\"dashboard_last_tool_content_preview\":\"" << JsonEscape(DashboardTruncate(last_tool_content, 1200)) << "\","
        << "\"dashboard_last_problem_class\":\"" << JsonEscape(problem_class) << "\","
        << "\"dashboard_last_problem_reason\":\"" << JsonEscape(problem_reason) << "\",";
    return output.str();
}

inline std::string DashboardFileTimeTicks(const std::filesystem::path & path) {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::string();
    }
    return std::to_string(write_time.time_since_epoch().count());
}

inline long long DashboardFileAgeSeconds(const std::filesystem::path & path) {
    if (path.empty()) {
        return -1;
    }
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return -1;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - write_time).count();
}

inline std::filesystem::path LatestConversationJsonlPath() {
    std::filesystem::path latest;
    std::filesystem::file_time_type latest_time{};
    std::error_code ec;
    const std::filesystem::path root("D:\\");
    for (const auto & entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path path = entry.path();
        const std::string name = path.filename().string();
        if (path.extension() != ".jsonl" || name.find("_conv_") == std::string::npos) {
            continue;
        }
        const auto write_time = entry.last_write_time(ec);
        if (!ec && (latest.empty() || write_time > latest_time)) {
            latest = path;
            latest_time = write_time;
        }
        ec.clear();
    }
    return latest;
}

inline bool DashboardPathIsNewer(
    const std::filesystem::path & left,
    const std::filesystem::path & right) {
    if (left.empty() || right.empty()) {
        return false;
    }
    std::error_code ec;
    const auto left_time = std::filesystem::last_write_time(left, ec);
    if (ec) {
        return false;
    }
    ec.clear();
    const auto right_time = std::filesystem::last_write_time(right, ec);
    if (ec) {
        return false;
    }
    return left_time > right_time;
}

inline std::string AppendFlowStateDashboardMetadata(
    const std::string & state_json,
    const std::string & source_mode,
    const std::filesystem::path & requested_jsonl_path,
    const std::filesystem::path & effective_jsonl_path,
    const std::filesystem::path & latest_jsonl_path,
    const std::filesystem::path & state_path,
    const std::string & generation_error) {
    std::size_t insert_pos = state_json.find('{');
    if (insert_pos == std::string::npos) {
        return state_json;
    }
    ++insert_pos;
    const bool latest_newer = DashboardPathIsNewer(latest_jsonl_path, effective_jsonl_path);
    const bool static_replay = source_mode == "jsonl_replay";
    const long long source_age_seconds = DashboardFileAgeSeconds(effective_jsonl_path);
    const bool active_recent =
        (source_mode == "live_latest_jsonl" || source_mode == "latest_task_memory_budget")
        && source_age_seconds >= 0
        && source_age_seconds <= 30;
    const std::string runtime_status = active_recent
        ? "active_recent"
        : (source_mode == "live_latest_jsonl"
            ? "idle_history"
            : (source_mode == "latest_task_memory_budget" ? "idle_task_memory_history" : "replay_history"));
    std::ostringstream metadata;
    metadata
        << "\"dashboard_source_mode\":\"" << JsonEscape(source_mode) << "\","
        << "\"dashboard_runtime_status\":\"" << JsonEscape(runtime_status) << "\","
        << "\"dashboard_source_age_seconds\":" << source_age_seconds << ","
        << "\"dashboard_active_recent\":" << (active_recent ? "true" : "false") << ","
        << "\"dashboard_static_replay\":" << (static_replay ? "true" : "false") << ","
        << "\"dashboard_requested_jsonl_path\":\"" << JsonEscape(requested_jsonl_path.string()) << "\","
        << "\"dashboard_effective_jsonl_path\":\"" << JsonEscape(effective_jsonl_path.string()) << "\","
        << "\"dashboard_effective_source_path\":\"" << JsonEscape(effective_jsonl_path.string()) << "\","
        << "\"dashboard_latest_jsonl_path\":\"" << JsonEscape(latest_jsonl_path.string()) << "\","
        << "\"dashboard_latest_jsonl_newer\":" << (latest_newer ? "true" : "false") << ","
        << "\"dashboard_state_path\":\"" << JsonEscape(state_path.string()) << "\","
        << "\"dashboard_state_last_write_ticks\":\"" << JsonEscape(DashboardFileTimeTicks(state_path)) << "\","
        << "\"dashboard_source_last_write_ticks\":\"" << JsonEscape(DashboardFileTimeTicks(effective_jsonl_path)) << "\","
        << "\"dashboard_generation_error\":\"" << JsonEscape(generation_error) << "\","
        << DashboardBuildProbeMetadata(effective_jsonl_path, state_path);
    std::string output = state_json;
    output.insert(insert_pos, metadata.str());
    return output;
}

inline std::filesystem::path FlowStatePathFromJsonl(
    const AgentConfig & config,
    const std::string & jsonl_path,
    std::string * error) {
    if (jsonl_path.empty()) {
        return {};
    }

    const std::filesystem::path input_path(jsonl_path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(input_path, ec) || ec) {
        if (error != nullptr) {
            *error = "jsonl_path does not exist or is not a file";
        }
        return {};
    }

    const std::filesystem::path out_dir =
        std::filesystem::path(config.log_root) / "mcp_flow_runtime" / ("jsonl_" + DashboardHexHash(input_path.string()));
    const std::filesystem::path flow_state = out_dir / "flow_state.json";
    const auto input_time = std::filesystem::last_write_time(input_path, ec);
    const bool input_time_ok = !ec;
    ec.clear();
    const bool state_exists = std::filesystem::is_regular_file(flow_state, ec) && !ec;
    const auto state_time = state_exists ? std::filesystem::last_write_time(flow_state, ec) : std::filesystem::file_time_type{};
    if (state_exists && (!input_time_ok || state_time >= input_time)) {
        return flow_state;
    }

    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to create flow runtime output directory";
        }
        return {};
    }

    const std::filesystem::path script_path =
        std::filesystem::path(config.workspace_root) / "src" / "clips_rules" / "observability" / "mcp_flow_observer.ps1";
    if (!std::filesystem::is_regular_file(script_path, ec) || ec) {
        if (error != nullptr) {
            *error = "mcp_flow_observer.ps1 not found";
        }
        return {};
    }

    const std::string command =
        "powershell -ExecutionPolicy Bypass -File "
        + DashboardDoubleQuoteCmd(script_path.string())
        + " -InputJsonl " + DashboardDoubleQuoteCmd(input_path.string())
        + " -OutDir " + DashboardDoubleQuoteCmd(out_dir.string())
        + " > " + DashboardDoubleQuoteCmd((out_dir / "dashboard_refresh_stdout.json").string())
        + " 2> " + DashboardDoubleQuoteCmd((out_dir / "dashboard_refresh_stderr.txt").string());
    const int exit_code = std::system(command.c_str());
    if (exit_code != 0 || !std::filesystem::is_regular_file(flow_state, ec) || ec) {
        if (error != nullptr) {
            *error = "failed to generate flow_state.json from jsonl_path";
        }
        return {};
    }
    return flow_state;
}

inline std::string BuildFlowStateFallbackJson(
    const AgentConfig & config,
    const std::string & goal_id,
    const std::string & error) {
    return std::string("{")
        + "\"flow_id\":\"runtime_flow_state\","
        + "\"goal_id\":\"" + JsonEscape(goal_id) + "\","
        + "\"completion_state\":\"not_started\","
        + "\"current_node\":\"\","
        + "\"next_expected_node\":\"\","
        + "\"completion_gate_satisfied\":false,"
        + "\"violation_count\":0,"
        + "\"nodes\":[],"
        + "\"template_edges\":[],"
        + "\"actual_transitions\":[],"
        + "\"state_source\":\"none\","
        + "\"log_root\":\"" + JsonEscape(config.log_root) + "\","
        + "\"error\":\"" + JsonEscape(error) + "\""
        + "}";
}

inline bool HandleFlowStateRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    if (request.path != "/flow-state") {
        return false;
    }
    if (request.method == "HEAD" || request.method == "OPTIONS") {
        response->status_code = request.method == "HEAD" ? 200 : 204;
        response->status_text = request.method == "HEAD" ? "OK" : "No Content";
        response->content_type = "application/json; charset=utf-8";
        response->body.clear();
        return true;
    }
    if (request.method != "GET") {
        response->status_code = 405;
        response->status_text = "Method Not Allowed";
        response->body = "{\"ok\":false,\"error\":\"method not allowed\"}";
        return true;
    }

    const std::string goal_id = UrlDecode(GetQueryParamValue(request, "goal_id"));
    const std::string requested_jsonl_path = UrlDecode(GetQueryParamValue(request, "jsonl_path"));
    const std::string live_jsonl = UrlDecode(GetQueryParamValue(request, "live_jsonl"));
    const bool no_explicit_source = requested_jsonl_path.empty() && goal_id.empty() && live_jsonl.empty();
    const bool live_jsonl_mode =
        no_explicit_source || live_jsonl == "1" || live_jsonl == "true" || live_jsonl == "yes";
    const std::filesystem::path latest_jsonl_path = LatestConversationJsonlPath();
    const std::filesystem::path latest_budget_path = LatestTaskMemoryBudgetRunPath(config);
    const bool latest_budget_newer =
        !latest_budget_path.empty()
        && (latest_jsonl_path.empty() || DashboardPathIsNewer(latest_budget_path, latest_jsonl_path));
    const long long latest_jsonl_age = DashboardFileAgeSeconds(latest_jsonl_path);
    const bool use_task_memory_default =
        no_explicit_source
        && !latest_budget_path.empty()
        && (latest_budget_newer || latest_jsonl_age < 0 || latest_jsonl_age > 30);
    const std::filesystem::path effective_jsonl_path = use_task_memory_default
        ? latest_budget_path
        : (live_jsonl_mode
        ? latest_jsonl_path
        : std::filesystem::path(requested_jsonl_path));
    const std::string source_mode = use_task_memory_default
        ? "latest_task_memory_budget"
        : (live_jsonl_mode
        ? "live_latest_jsonl"
        : (!requested_jsonl_path.empty() ? "jsonl_replay" : "latest_flow_state"));
    std::string generation_error;
    std::filesystem::path state_path;
    std::string state_json;
    if (use_task_memory_default) {
        state_json = BuildFlowStateFromTaskMemoryBudget(latest_budget_path, &generation_error);
        state_path = latest_budget_path;
    } else {
        state_path = FlowStatePathFromJsonl(config, effective_jsonl_path.string(), &generation_error);
    }
    if (!use_task_memory_default && state_path.empty()) {
        state_path = LatestFlowStatePath(config, goal_id);
    }
    if (state_json.empty() && (state_path.empty() || !ReadTextFileLocal(state_path, &state_json))) {
        state_json = BuildFlowStateFallbackJson(
            config,
            goal_id,
            generation_error.empty() ? "flow_state.json not found" : generation_error);
    }
    state_json = DashboardStripUtf8Bom(state_json);
    state_json = AppendFlowStateDashboardMetadata(
        state_json,
        source_mode,
        std::filesystem::path(requested_jsonl_path),
        effective_jsonl_path,
        latest_jsonl_path,
        state_path,
        generation_error);

    response->status_code = 200;
    response->status_text = "OK";
    response->content_type = "application/json; charset=utf-8";
    response->headers["Cache-Control"] = "no-store";
    response->headers["X-Flow-State-Path"] = JsonEscape(state_path.string());
    response->headers["X-Flow-Jsonl-Path"] = JsonEscape(effective_jsonl_path.string());
    response->body = state_json;
    return true;
}

inline std::string BuildFlowDashboardHtml() {
    return R"(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MCP Flow Dashboard</title>
<style>
body{margin:0;background:#0f1115;color:#e5e7eb;font:14px/1.5 system-ui,Segoe UI,Arial,sans-serif}
header{display:flex;gap:12px;align-items:center;padding:14px 18px;border-bottom:1px solid #2d333b;background:#151922;position:sticky;top:0}
h1{font-size:18px;margin:0}
.pill{border:1px solid #3b4250;border-radius:999px;padding:3px 9px;color:#cbd5e1}
main{display:grid;grid-template-columns:1fr 360px;gap:16px;padding:16px}
.panel{border:1px solid #2d333b;background:#151922;border-radius:8px;padding:14px}
.flow{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
.node{min-width:180px;border:1px solid #3b4250;border-radius:8px;padding:10px;background:#1f2430}
.node b{display:block;font-size:13px;color:#fff}
.node small{display:block;color:#a7b0be;margin-top:4px}
.edge{color:#64748b}
.not_started{background:#29303a}.running{background:#163b63;border-color:#2f81f7}.needs_continue{background:#5a430c;border-color:#d29922}.success{background:#17412b;border-color:#3fb950}.failed,.violated{background:#551d22;border-color:#f85149}
pre{white-space:pre-wrap;word-break:break-word;background:#0f1115;border:1px solid #2d333b;border-radius:8px;padding:10px;max-height:44vh;overflow:auto}
button,input{background:#0f1115;color:#e5e7eb;border:1px solid #3b4250;border-radius:6px;padding:7px 9px}
input{min-width:260px}.jsonl{min-width:min(720px,96%)}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:10px}
.summary{display:grid;grid-template-columns:repeat(4,minmax(120px,1fr));gap:10px;margin-bottom:12px}
.metric{background:#0f1115;border:1px solid #2d333b;border-radius:8px;padding:10px}
.metric span{display:block;color:#9aa4b2;font-size:12px}.metric b{display:block;margin-top:3px;color:#fff;word-break:break-word}
.probe{display:grid;grid-template-columns:repeat(3,minmax(180px,1fr));gap:10px;margin:0 0 12px}
.probe-card{background:#0f1115;border:1px solid #2d333b;border-radius:8px;padding:10px;min-height:80px}
.probe-card h3{font-size:12px;color:#9aa4b2;margin:0 0 6px}
.probe-card b{display:block;color:#fff;word-break:break-word}.probe-card small{display:block;color:#a7b0be;word-break:break-word;margin-top:5px}
.issue{border-left:3px solid #f85149;background:#1a1114;padding:8px 10px;margin:8px 0;border-radius:6px}.issue b{color:#ffb4ad}
.hint{border-left:3px solid #d29922;background:#1a160b;padding:8px 10px;margin:8px 0;border-radius:6px;color:#f2cc60}
.bad{color:#ffb4ad}.ok{color:#9be9a8}.warn{color:#f2cc60}
</style>
</head>
<body>
<header>
  <h1>MCP Flow Dashboard</h1>
  <span class="pill" id="completion">loading</span>
  <span class="pill" id="current"></span>
  <span class="pill" id="next"></span>
  <span class="pill" id="violations"></span>
  <span class="pill" id="runtime"></span>
</header>
<main>
  <section class="panel">
    <div class="row">
      <input id="goal" placeholder="goal_id 可选">
      <input id="jsonl" class="jsonl" placeholder="jsonl_path 可选，例如 D:\2026-08-11_06-59-12_conv_6b195ca0__d_codex_workdir_sea.jsonl">
      <button id="reload">刷新</button>
      <button id="live">追踪最新JSONL</button>
      <button id="toggle">暂停自动刷新</button>
    </div>
    <div class="summary" id="summary"></div>
    <div class="probe" id="probe"></div>
    <div class="flow" id="flow"></div>
  </section>
  <aside class="panel">
    <h2 style="font-size:15px;margin-top:0">研判摘要</h2>
    <div id="issues"></div>
    <h2 style="font-size:15px">状态详情</h2>
    <pre id="detail">{}</pre>
  </aside>
</main>
<script>
const qs = new URLSearchParams(location.search);
const goalInput = document.getElementById('goal');
const jsonlInput = document.getElementById('jsonl');
goalInput.value = qs.get('goal_id') || '';
jsonlInput.value = qs.get('jsonl_path') || '';
let liveMode = qs.get('live_jsonl') === '1';
let paused = false;
function cls(s){return String(s||'not_started').replace(/[^a-zA-Z0-9_-]/g,'_')}
function firstActiveViolation(data){
  const nodes=Array.isArray(data.nodes)?data.nodes:[];
  for(const n of nodes){if(Array.isArray(n.violations)&&n.violations.length){return {node:n.id,violation:n.violations[0]}}}
  return null;
}
function render(data){
  document.getElementById('completion').textContent='completion: '+(data.completion_state||'');
  document.getElementById('completion').className='pill '+cls(data.completion_state);
  document.getElementById('current').textContent='current: '+(data.current_node||'');
  document.getElementById('next').textContent='next: '+(data.next_expected_node||'');
  document.getElementById('violations').textContent='violations: '+(data.violation_count||0);
  document.getElementById('violations').className='pill '+((Number(data.violation_count||0)>0)?'violated':'success');
  document.getElementById('runtime').textContent='runtime: '+(data.dashboard_runtime_status||'unknown');
  document.getElementById('runtime').className='pill '+(data.dashboard_active_recent===true?'running':'not_started');
  const active=firstActiveViolation(data);
  document.getElementById('summary').innerHTML=[
    ['Mode',data.dashboard_source_mode||(liveMode?'live_latest_jsonl':'')],
    ['Runtime',data.dashboard_runtime_status||''],
    ['Source Age',formatAge(data.dashboard_source_age_seconds)],
    ['Flow',data.flow_id||''],
    ['Source',data.dashboard_effective_jsonl_path||data.input_jsonl||data.state_source||'latest flow_state'],
    ['Current',data.current_node||''],
    ['Next',data.next_expected_node||''],
    ['Last Tool',data.dashboard_last_tool_name||data.dashboard_last_tool_node||''],
    ['Last Status',data.dashboard_last_tool_status||data.dashboard_last_budget_status||''],
    ['Required Tool',data.dashboard_last_required_tool_name||''],
    ['Processed Steps',data.dashboard_last_executed_step_count||''],
    ['Latest JSONL',data.dashboard_latest_jsonl_path||''],
    ['State File',data.dashboard_state_path||''],
    ['Refresh',new Date().toLocaleTimeString()]
  ].map(([k,v])=>'<div class="metric"><span>'+escapeHtml(k)+'</span><b>'+escapeHtml(v)+'</b></div>').join('');
  const probeRows=[
    ['运行来源',data.dashboard_runtime_status||'unknown',data.dashboard_source_mode||(liveMode?'live_latest_jsonl':'static')],
    ['最后问题',data.dashboard_last_problem_class||'none',data.dashboard_last_problem_reason||'未检测到显式阻塞原因'],
    ['CLIPS 命中',data.dashboard_last_pre_call_reason||'none','route_target: '+(data.dashboard_last_pre_call_route_target||'')],
    ['任务节点',((data.dashboard_last_flow_current_task_id||data.current_node||'')+' -> '+(data.dashboard_last_flow_next_task_id||data.next_expected_node||'')),data.dashboard_last_flow_id||''],
    ['任务列表',data.dashboard_last_flow_task_list_required==='true'?'required':'not_required',data.dashboard_last_flow_task_list_md_path||data.dashboard_last_flow_task_list_path||''],
    ['下一步工具',data.dashboard_last_required_tool_name||'none',data.dashboard_last_required_tool_arguments_json||'']
  ];
  document.getElementById('probe').innerHTML=probeRows.map(([title,value,detail])=>
    '<div class="probe-card"><h3>'+escapeHtml(title)+'</h3><b>'+escapeHtml(value||'')+'</b><small>'+escapeHtml(detail||'')+'</small></div>'
  ).join('');
  const issues=document.getElementById('issues');
  const gate=data.completion_gate_satisfied===true?'完成门禁已满足':'完成门禁未满足，不能宣称完成';
  let html='<div class="hint">'+escapeHtml(gate)+'</div>';
  if(data.dashboard_static_replay===true){
    html+='<div class="hint warn">当前是固定 jsonl 历史回放，不代表正在运行的新对话。需要看当前处理，请点击“追踪最新JSONL”。</div>';
  }
  if(data.dashboard_runtime_status==='idle_history'){
    html+='<div class="hint warn">当前没有检测到活跃写入；这里显示的是最新聊天记录的最后状态，不表示 MCP 正在后台运行。</div>';
  }
  if(data.dashboard_runtime_status==='idle_task_memory_history'){
    html+='<div class="hint warn">当前显示的是最近 MCP task_memory/budget 存档状态；如果源文件年龄很大，它是历史断点，不代表正在运行。</div>';
  }
  if(data.dashboard_last_problem_class){
    html+='<div class="issue"><b>最后问题：'+escapeHtml(data.dashboard_last_problem_class)+'</b><br>'+escapeHtml(data.dashboard_last_problem_reason||'')+'</div>';
  }
  if(data.dashboard_last_required_tool_name){
    html+='<div class="hint"><b>下一步应调用：</b>'+escapeHtml(data.dashboard_last_required_tool_name)+'<br><small>'+escapeHtml(data.dashboard_last_required_tool_arguments_json||'')+'</small></div>';
  }
  if(data.dashboard_last_tool_content_preview){
    html+='<div class="hint"><b>最后工具返回摘要</b><br><small>'+escapeHtml(data.dashboard_last_tool_content_preview)+'</small></div>';
  }
  if(data.dashboard_last_assistant_text){
    html+='<div class="hint"><b>最后模型文本</b><br><small>'+escapeHtml(data.dashboard_last_assistant_text)+'</small></div>';
  }
  if(data.dashboard_latest_jsonl_newer===true){
    html+='<div class="issue"><b>发现更新的聊天记录</b><br>'+escapeHtml(data.dashboard_latest_jsonl_path||'')+'<br><small>当前页面绑定的 jsonl 不是最新源。</small></div>';
  }
  if(active){html+='<div class="issue"><b>'+escapeHtml(active.violation.class||'violation')+'</b><br>'+escapeHtml(active.violation.reason||'')+'<br><small>node='+escapeHtml(active.node||'')+'</small></div>'}
  const nodesForIssues=Array.isArray(data.nodes)?data.nodes:[];
  nodesForIssues.forEach(n=>(Array.isArray(n.violations)?n.violations:[]).slice(0,3).forEach(v=>{
    html+='<div class="issue"><b>'+escapeHtml(v.class||'violation')+'</b><br>'+escapeHtml(v.reason||'')+'<br><small>step '+escapeHtml(v.step_index||'')+' -> '+escapeHtml(v.next_step_index||'')+'</small></div>';
  }));
  if(!active && Number(data.violation_count||0)===0){html+='<div class="hint ok">未发现流程违规</div>'}
  issues.innerHTML=html;
  const nodes = Array.isArray(data.nodes)?data.nodes:[];
  const flow = document.getElementById('flow');
  flow.innerHTML='';
  if(!nodes.length){
    flow.innerHTML='<div class="hint warn">当前源没有可映射的 MCP flow 节点。请检查 Source/State File，或使用 goal_id/jsonl_path 指定要回放的会话。</div>';
  }
  nodes.forEach((n,i)=>{
    const div=document.createElement('div');
    div.className='node '+cls(n.status);
    div.innerHTML='<b>'+escapeHtml(n.id||'node')+'</b><small>'+escapeHtml(n.status||'')+' visits='+(n.visit_count||0)+'</small><small>'+escapeHtml(n.tool||'')+'</small>';
    flow.appendChild(div);
    if(i<nodes.length-1){const e=document.createElement('span');e.className='edge';e.textContent='→';flow.appendChild(e)}
  });
  document.getElementById('detail').textContent=JSON.stringify(data,null,2);
}
function escapeHtml(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function formatAge(v){
  const n=Number(v);
  if(!Number.isFinite(n)||n<0)return '';
  if(n<60)return n+'s ago';
  if(n<3600)return Math.floor(n/60)+'m '+(n%60)+'s ago';
  return Math.floor(n/3600)+'h '+Math.floor((n%3600)/60)+'m ago';
}
async function load(){
  if(paused)return;
  const params=new URLSearchParams();
  if(goalInput.value)params.set('goal_id',goalInput.value);
  if(liveMode)params.set('live_jsonl','1');
  else if(jsonlInput.value)params.set('jsonl_path',jsonlInput.value);
  const url='/flow-state'+(params.toString()?'?'+params.toString():'');
  try{render(await (await fetch(url,{cache:'no-store'})).json())}
  catch(e){render({completion_state:'failed',current_node:'dashboard_fetch',violation_count:1,nodes:[],error:String(e)})}
}
document.getElementById('reload').onclick=load;
document.getElementById('live').onclick=()=>{liveMode=true;jsonlInput.value='';history.replaceState(null,'','/recent_flow_dashboard.html?live_jsonl=1');load()};
document.getElementById('toggle').onclick=()=>{paused=!paused;document.getElementById('toggle').textContent=paused?'继续自动刷新':'暂停自动刷新';if(!paused)load()};
load(); setInterval(load,1000);
</script>
</body>
</html>)";
}

inline bool HandleFlowDashboardRoute(
    const AgentConfig &,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    if (request.path != "/flow-dashboard" && request.path != "/recent_flow_dashboard.html") {
        return false;
    }

    if (request.path == "/flow-dashboard") {
        if (request.method == "OPTIONS") {
            response->status_code = 204;
            response->status_text = "No Content";
            response->content_type = "text/html; charset=utf-8";
            response->body.clear();
            return true;
        }
        if (request.method != "GET" && request.method != "HEAD") {
            response->status_code = 405;
            response->status_text = "Method Not Allowed";
            response->body = "{\"ok\":false,\"error\":\"method not allowed\"}";
            return true;
        }
        response->status_code = 302;
        response->status_text = "Found";
        response->content_type = "text/html; charset=utf-8";
        response->headers["Cache-Control"] = "no-store";
        response->headers["Location"] = request.query.empty()
            ? "/recent_flow_dashboard.html"
            : ("/recent_flow_dashboard.html?" + request.query);
        response->body = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Redirect</title></head>"
            "<body><a href=\"/recent_flow_dashboard.html\">recent_flow_dashboard.html</a></body></html>";
        if (request.method == "HEAD") {
            response->body.clear();
        }
        return true;
    }

    if (request.method == "HEAD" || request.method == "OPTIONS") {
        response->status_code = request.method == "HEAD" ? 200 : 204;
        response->status_text = request.method == "HEAD" ? "OK" : "No Content";
        response->content_type = "text/html; charset=utf-8";
        response->body.clear();
        return true;
    }
    if (request.method != "GET") {
        response->status_code = 405;
        response->status_text = "Method Not Allowed";
        response->body = "{\"ok\":false,\"error\":\"method not allowed\"}";
        return true;
    }
    response->status_code = 200;
    response->status_text = "OK";
    response->content_type = "text/html; charset=utf-8";
    response->headers["Cache-Control"] = "no-store";
    response->body = BuildFlowDashboardHtml();
    return true;
}

inline bool HandleFlowObservationRoutes(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    return HandleFlowDashboardRoute(config, request, response)
        || HandleFlowStateRoute(config, request, response);
}

}  // namespace mcp_flow_dashboard
}  // namespace codex_lan_agent
