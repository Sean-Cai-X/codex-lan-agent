#pragma once

#include "AgentConfig.h"
#include "comm.h"
#include "types.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace codex_lan_agent {

struct FlowTaskListItem {
    std::string task_id;
    std::string title;
    std::string expected_tool;
    std::string status;
    std::string input_ref;
    std::string expected_output;
    bool allow_file_modification = false;
};

inline int FlowTaskOrdinal(const std::string & task_id) {
    if (task_id == "T1") {
        return 1;
    }
    if (task_id == "T2") {
        return 2;
    }
    if (task_id == "T3") {
        return 3;
    }
    if (task_id == "T4") {
        return 4;
    }
    if (task_id == "T5") {
        return 5;
    }
    if (task_id == "T6") {
        return 6;
    }
    return 1;
}

inline std::string FlowTaskStatusFor(
    const std::string & task_id,
    const std::string & current_task_id) {
    const int task_ord = FlowTaskOrdinal(task_id);
    const int current_ord = FlowTaskOrdinal(current_task_id);
    if (task_ord < current_ord) {
        return "Completed";
    }
    if (task_ord == current_ord) {
        return "Ready";
    }
    return "Pending";
}

inline std::filesystem::path FlowTaskListRoot(
    const AgentConfig & config,
    const std::string & goal_id) {
    return std::filesystem::path(config.log_root)
        / "flow_task_lists"
        / SanitizeDispatchToken(goal_id, "default_goal");
}

inline bool FlowTaskListWriteText(
    const std::filesystem::path & path,
    const std::string & content,
    std::string * error_message) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error_message != nullptr) {
            *error_message = ec.message();
        }
        return false;
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!output.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open " + path.string();
        }
        return false;
    }
    output << content;
    return true;
}

inline std::string FlowTaskListJson(
    const std::string & flow_id,
    const std::string & goal_id,
    const std::string & trace_id,
    const std::string & current_task_id,
    const std::vector<FlowTaskListItem> & tasks) {
    std::ostringstream output;
    output << "{\n"
           << "  \"flow_id\":\"" << JsonEscape(flow_id) << "\",\n"
           << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
           << "  \"trace_id\":\"" << JsonEscape(trace_id) << "\",\n"
           << "  \"current_task_id\":\"" << JsonEscape(current_task_id) << "\",\n"
           << "  \"completion_gate\":\"all tasks Completed and terminal_state=true + completion_claim_allowed=true + final_answer_allowed=true + verification_ok=true\",\n"
           << "  \"tasks\":[\n";
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        const auto & task = tasks[index];
        output << "    {"
               << "\"task_id\":\"" << JsonEscape(task.task_id) << "\","
               << "\"title\":\"" << JsonEscape(task.title) << "\","
               << "\"expected_tool\":\"" << JsonEscape(task.expected_tool) << "\","
               << "\"status\":\"" << JsonEscape(task.status) << "\","
               << "\"input_ref\":\"" << JsonEscape(task.input_ref) << "\","
               << "\"expected_output\":\"" << JsonEscape(task.expected_output) << "\","
               << "\"allow_file_modification\":" << (task.allow_file_modification ? "true" : "false")
               << "}";
        if (index + 1 < tasks.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n}\n";
    return output.str();
}

inline std::string FlowTaskListMarkdown(
    const std::string & flow_id,
    const std::string & goal_id,
    const std::string & current_task_id,
    const std::vector<FlowTaskListItem> & tasks) {
    std::ostringstream output;
    output << "# Flow Task List\n\n"
           << "- flow_id: `" << flow_id << "`\n"
           << "- goal_id: `" << goal_id << "`\n"
           << "- current_task_id: `" << current_task_id << "`\n\n"
           << "| Task | Status | Expected Tool | File Modification | Output |\n"
           << "|---|---|---|---|---|\n";
    for (const auto & task : tasks) {
        output << "| `" << task.task_id << "` " << task.title
               << " | `" << task.status << "`"
               << " | `" << task.expected_tool << "`"
               << " | `" << (task.allow_file_modification ? "yes" : "no") << "`"
               << " | " << task.expected_output << " |\n";
    }
    output << "\nCompletion is forbidden while any task is `Pending`, `Ready`, `Running`, `NeedsContinue`, or `Blocked`.\n";
    return output.str();
}

inline CommandResult BuildFlowTaskListResult(
    const AgentConfig & config,
    const std::string & flow_id,
    const std::string & goal_id,
    const std::string & trace_id,
    const std::string & current_task_id,
    const std::vector<FlowTaskListItem> & tasks) {
    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "flow_task_list_ready";
    result.fields["flow_id"] = flow_id;
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["current_task_id"] = current_task_id;
    result.fields["flow_task_count"] = std::to_string(tasks.size());

    const std::filesystem::path root = FlowTaskListRoot(config, goal_id.empty() ? trace_id : goal_id);
    const std::filesystem::path json_path = root / "flow_task_list.json";
    const std::filesystem::path md_path = root / "flow_task_list.md";
    std::string error;
    const bool json_ok = FlowTaskListWriteText(
        json_path,
        FlowTaskListJson(flow_id, goal_id, trace_id, current_task_id, tasks),
        &error);
    const bool md_ok = FlowTaskListWriteText(
        md_path,
        FlowTaskListMarkdown(flow_id, goal_id, current_task_id, tasks),
        &error);
    if (!json_ok || !md_ok) {
        result.ok = false;
        result.exit_code = 1;
        result.fields["status"] = "failed";
        result.fields["error"] = error;
        return result;
    }
    result.fields["flow_task_list_path"] = json_path.string();
    result.fields["flow_task_list_md_path"] = md_path.string();
    result.fields["result_ref"] = json_path.string();
    result.fields["evidence_ref"] = md_path.string();
    result.fields["summary"] = "flow task list generated";
    return result;
}

inline CommandResult BuildDirectoryCommentCleanupTaskListResult(
    const AgentConfig & config,
    const std::string & goal_id,
    const std::string & trace_id,
    const std::string & directory_path,
    const std::string & current_task_id = "T1") {
    const std::vector<FlowTaskListItem> tasks = {
        {"T1", "解析请求并确定 canonical_intent", "lan_agent_mcp_route", FlowTaskStatusFor("T1", current_task_id), directory_path, "canonical_intent=comment_cleanup", false},
        {"T2", "生成目录代码文件清单", "lan_agent_list_directory", FlowTaskStatusFor("T2", current_task_id), directory_path, "manifest + first code file", false},
        {"T3", "probe 当前文件", "lan_agent_probe_text_file", FlowTaskStatusFor("T3", current_task_id), "current_file", "probe_ref/probe_ready", false},
        {"T4", "200 行窗口删除当前文件注释", "lan_agent_delete_text_range_window_atomic", FlowTaskStatusFor("T4", current_task_id), "current_file + next_start_line", "has_more/next_start_line", true},
        {"T5", "验证当前文件并移动到下一个文件", "lan_agent_probe_text_file", FlowTaskStatusFor("T5", current_task_id), "directory_manifest", "next file or directory complete", false},
        {"T6", "目录级完成验证与总结", "final_answer", FlowTaskStatusFor("T6", current_task_id), "all previous tasks", "completion gate satisfied", false}
    };
    return BuildFlowTaskListResult(
        config,
        "directory_comment_cleanup_bounded_window_v1",
        goal_id,
        trace_id,
        current_task_id,
        tasks);
}

inline void AttachDirectoryCommentCleanupTaskListFields(
    CommandResult * result,
    const AgentConfig & config,
    const std::string & goal_id,
    const std::string & trace_id,
    const std::string & directory_path,
    const std::string & current_task_id,
    const std::string & next_task_id) {
    if (result == nullptr) {
        return;
    }
    CommandResult task_list = BuildDirectoryCommentCleanupTaskListResult(
        config,
        goal_id.empty() ? FirstNonEmpty(trace_id, "directory_comment_cleanup") : goal_id,
        trace_id,
        directory_path,
        current_task_id);
    result->fields["flow_id"] = "directory_comment_cleanup_bounded_window_v1";
    result->fields["flow_task_list_required"] = "true";
    result->fields["flow_current_task_id"] = current_task_id;
    result->fields["flow_next_task_id"] = next_task_id.empty() ? current_task_id : next_task_id;
    result->fields["flow_task_list_path"] = GetFieldOrDefault(task_list, "flow_task_list_path", "");
    result->fields["flow_task_list_md_path"] = GetFieldOrDefault(task_list, "flow_task_list_md_path", "");
}

}  // namespace codex_lan_agent
