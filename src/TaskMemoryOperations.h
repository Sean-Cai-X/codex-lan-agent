#pragma once

#include "comm.h"
#include "JsonRequestView.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace codex_lan_agent {

inline std::string SanitizeTaskMemoryToken(const std::string & value) {
    std::string output;
    for (unsigned char ch : value) {
        const bool safe =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.';
        output.push_back(safe ? static_cast<char>(ch) : '_');
    }
    return output.empty() ? "default_goal" : output;
}

inline std::string TaskMemoryPendingFreezeKey(
    const std::string & goal_id,
    const std::string & trace_id) {
    return SanitizeTaskMemoryToken(goal_id) + "|" + SanitizeTaskMemoryToken(trace_id);
}

inline std::unordered_map<std::string, std::string> & TaskMemoryPendingFreezeArgumentsMap() {
    static std::unordered_map<std::string, std::string> pending;
    return pending;
}

inline std::mutex & TaskMemoryPendingFreezeArgumentsMutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::string TaskMemoryStableChecksum(const std::string & content) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : content) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

inline std::filesystem::path BuildTaskMemoryRoot(
    const AgentConfig & config,
    const std::string & goal_id) {
    return std::filesystem::path(config.data_root) /
        "task_memory" /
        SanitizeTaskMemoryToken(goal_id);
}

inline bool WriteTaskMemoryTextFile(
    const std::filesystem::path & path,
    const std::string & content,
    std::string * error_message) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error_message != nullptr) {
            *error_message = "failed to create parent directory: " + ec.message();
        }
        return false;
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!output.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open file for write: " + path.string();
        }
        return false;
    }
    output << content;
    return true;
}

inline bool AppendTaskMemoryTextFile(
    const std::filesystem::path & path,
    const std::string & content,
    std::string * error_message) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error_message != nullptr) {
            *error_message = "failed to create parent directory: " + ec.message();
        }
        return false;
    }
    std::ofstream output(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!output.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open file for append: " + path.string();
        }
        return false;
    }
    output << content;
    return true;
}

inline std::string ReadTaskMemoryTextFile(const std::filesystem::path & path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        return std::string();
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

inline std::string JsonStringField(
    const std::string & key,
    const std::string & value,
    bool comma = true) {
    std::ostringstream output;
    output << "\"" << JsonEscape(key) << "\":\"" << JsonEscape(value) << "\"";
    if (comma) {
        output << ",";
    }
    return output.str();
}

inline std::string TaskMemoryFirstNonEmpty(
    const std::string & first,
    const std::string & second,
    const std::string & third = std::string()) {
    if (!Trim(first).empty()) {
        return first;
    }
    if (!Trim(second).empty()) {
        return second;
    }
    return third;
}

inline std::string TaskMemoryParamStringOrRaw(
    const JsonRequestView & params,
    const std::string & key,
    const std::string & fallback = std::string()) {
    const std::string string_value = params.GetString(key);
    if (!Trim(string_value).empty()) {
        return string_value;
    }
    const std::string raw_value = params.GetRawJson(key);
    if (!Trim(raw_value).empty()) {
        return raw_value;
    }
    return fallback;
}

inline void TaskMemoryReplaceAll(
    std::string * value,
    const std::string & from,
    const std::string & to) {
    if (value == nullptr || from.empty()) {
        return;
    }
    std::size_t position = 0;
    while ((position = value->find(from, position)) != std::string::npos) {
        value->replace(position, from.size(), to);
        position += to.size();
    }
}

inline void TaskMemoryNormalizeEscapedJsonKey(
    std::string * value,
    const std::string & key) {
    TaskMemoryReplaceAll(
        value,
        "\"\\\"" + key + "\\\"\"",
        "\"" + key + "\"");
}

inline std::string TaskMemoryNormalizeNextCallJson(std::string value) {
    value = Trim(value);
    const std::string lowered = ToLowerAscii(value);
    if (value == "\"\"" || value == "''" || lowered == "null") {
        return std::string();
    }
    if (ExtractJsonString(value, "name").empty()
        && value.find("\\\"name\\\"") != std::string::npos) {
        TaskMemoryNormalizeEscapedJsonKey(&value, "name");
        TaskMemoryNormalizeEscapedJsonKey(&value, "arguments");
        TaskMemoryNormalizeEscapedJsonKey(&value, "file_path");
        TaskMemoryNormalizeEscapedJsonKey(&value, "scan_mode");
        TaskMemoryNormalizeEscapedJsonKey(&value, "start_line");
        TaskMemoryNormalizeEscapedJsonKey(&value, "next_start_line");
        TaskMemoryNormalizeEscapedJsonKey(&value, "max_lines");
        TaskMemoryNormalizeEscapedJsonKey(&value, "primary_intent");
        TaskMemoryNormalizeEscapedJsonKey(&value, "trace_id");
        TaskMemoryNormalizeEscapedJsonKey(&value, "probe_ref");
        TaskMemoryNormalizeEscapedJsonKey(&value, "probe_ready");
        TaskMemoryNormalizeEscapedJsonKey(&value, "directory_manifest_path");
        TaskMemoryNormalizeEscapedJsonKey(&value, "directory_current_file_index");
        TaskMemoryNormalizeEscapedJsonKey(&value, "directory_total_code_file_count");
        TaskMemoryNormalizeEscapedJsonKey(&value, "directory_scope_active");
    }
    return value;
}

inline std::string BuildTaskMemoryFlatContinuationJson(const JsonRequestView & params) {
    const std::string tool_name = TaskMemoryFirstNonEmpty(
        params.GetString("next_tool_name"),
        params.GetString("continuation_tool_name"));
    if (tool_name.empty()) {
        return std::string();
    }
    const std::string file_path = TaskMemoryFirstNonEmpty(
        params.GetString("next_file_path"),
        params.GetString("file_path"),
        params.GetString("current_file"));
    const std::string scan_mode = TaskMemoryFirstNonEmpty(
        params.GetString("next_scan_mode"),
        params.GetString("scan_mode"),
        "comments");
    const std::string primary_intent = TaskMemoryFirstNonEmpty(
        params.GetString("next_primary_intent"),
        params.GetString("primary_intent"),
        "comment_cleanup");
    const std::string trace_id = params.GetString("trace_id");
    const std::string probe_ref = TaskMemoryFirstNonEmpty(
        params.GetString("next_probe_ref"),
        params.GetString("probe_ref"),
        file_path);
    const bool probe_ready = params.GetBool("next_probe_ready", params.GetBool("probe_ready", true));

    if (file_path.empty()) {
        return std::string();
    }

    std::ostringstream output;
    output << "{\"name\":\"" << JsonEscape(tool_name) << "\",\"arguments\":{"
           << "\"file_path\":\"" << JsonEscape(file_path) << "\","
           << "\"scan_mode\":\"" << JsonEscape(scan_mode) << "\","
           << "\"primary_intent\":\"" << JsonEscape(primary_intent) << "\"";
    if (tool_name == "lan_agent_delete_text_range_window_atomic") {
        const int start_line = std::max(
            1,
            params.GetInt("next_start_line", params.GetInt("start_line", 1)));
        const int max_lines = std::max(
            1,
            params.GetInt("next_max_lines", params.GetInt("max_lines", 200)));
        output << ",\"start_line\":" << start_line
               << ",\"next_start_line\":" << start_line
               << ",\"max_lines\":" << max_lines;
    }
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << JsonEscape(trace_id) << "\"";
    }
    const std::string directory_manifest_path = params.GetString("directory_manifest_path");
    if (!directory_manifest_path.empty()) {
        output << ",\"directory_manifest_path\":\"" << JsonEscape(directory_manifest_path) << "\""
               << ",\"directory_current_file_index\":" << std::max(0, params.GetInt("directory_current_file_index", 0))
               << ",\"directory_total_code_file_count\":" << std::max(0, params.GetInt("directory_total_code_file_count", 0))
               << ",\"directory_scope_active\":true";
    }
    if (!probe_ref.empty()) {
        output << ",\"probe_ref\":\"" << JsonEscape(probe_ref) << "\"";
    }
    output << ",\"probe_ready\":" << (probe_ready ? "true" : "false")
           << "}}";
    return output.str();
}

inline void RememberTaskMemoryPendingFreezeArguments(
    const std::string & full_freeze_call_json) {
    const std::string tool_name = ExtractJsonString(full_freeze_call_json, "name");
    if (tool_name != "lan_agent_task_memory_freeze") {
        return;
    }
    const std::string arguments_json = ExtractJsonObjectRaw(full_freeze_call_json, "arguments");
    if (Trim(arguments_json).empty()) {
        return;
    }
    const std::string goal_id = ExtractJsonString(arguments_json, "goal_id");
    const std::string trace_id = ExtractJsonString(arguments_json, "trace_id");
    if (Trim(goal_id).empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(TaskMemoryPendingFreezeArgumentsMutex());
    TaskMemoryPendingFreezeArgumentsMap()[TaskMemoryPendingFreezeKey(goal_id, trace_id)] = arguments_json;
    TaskMemoryPendingFreezeArgumentsMap()[TaskMemoryPendingFreezeKey(goal_id, "")] = arguments_json;
}

inline void RememberTaskMemoryPendingFreezeArgumentsFromResult(const CommandResult & result) {
    const std::string full_freeze_call_json = TaskMemoryFirstNonEmpty(
        GetFieldOrDefault(result, "long_loop_freeze_arguments_json", ""),
        GetFieldOrDefault(result, "required_tool_name", "") == "lan_agent_task_memory_freeze"
            ? GetFieldOrDefault(result, "required_tool_arguments_json", "")
            : std::string(),
        GetFieldOrDefault(result, "next_call_json", ""));
    RememberTaskMemoryPendingFreezeArguments(full_freeze_call_json);
}

inline std::string LookupTaskMemoryPendingFreezeArguments(
    const std::string & goal_id,
    const std::string & trace_id) {
    if (Trim(goal_id).empty()) {
        return std::string();
    }
    std::lock_guard<std::mutex> lock(TaskMemoryPendingFreezeArgumentsMutex());
    auto & pending = TaskMemoryPendingFreezeArgumentsMap();
    const auto exact = pending.find(TaskMemoryPendingFreezeKey(goal_id, trace_id));
    if (exact != pending.end()) {
        return exact->second;
    }
    const auto by_goal = pending.find(TaskMemoryPendingFreezeKey(goal_id, ""));
    return by_goal == pending.end() ? std::string() : by_goal->second;
}

inline std::string BuildTaskMemoryResumeAndExecuteCallJson(
    const std::string & goal_id,
    const std::string & trace_id,
    int max_steps) {
    return "{\"name\":\"lan_agent_task_memory_resume_and_execute\",\"arguments\":{"
        "\"goal_id\":\"" + JsonEscape(goal_id) + "\","
        "\"trace_id\":\"" + JsonEscape(trace_id) + "\","
        "\"max_steps\":" + std::to_string(std::max(1, max_steps)) + ","
        "\"execute\":true,"
        "\"dry_run\":false"
        "}}";
}

inline std::string BuildTaskMemoryResumeContextCallJson(
    const std::string & goal_id) {
    return "{\"name\":\"lan_agent_task_memory_resume_context\",\"arguments\":{"
        "\"goal_id\":\"" + JsonEscape(goal_id) + "\""
        "}}";
}

inline void ApplyTaskMemoryCleanHandoffFields(
    CommandResult * result,
    const std::string & goal_id,
    const std::string & trace_id,
    int max_steps,
    bool terminal_state,
    bool continuation_available) {
    if (result == nullptr) {
        return;
    }
    result->fields["clean_chat_close_allowed"] =
        (terminal_state || continuation_available) ? "true" : "false";
    result->fields["conversation_close_allowed"] =
        (terminal_state || continuation_available) ? "true" : "false";
    result->fields["handoff_state"] = terminal_state
        ? "terminal_complete"
        : (continuation_available ? "ready_for_new_chat_resume" : "blocked_missing_continuation");
    result->fields["conversation_close_status"] = terminal_state
        ? "terminal_complete"
        : (continuation_available ? "handoff_ready_not_complete" : "close_blocked_missing_continuation");
    // MCP owns the durable continuation, but it cannot erase the host chat's
    // message history. The chat/client layer must acknowledge the reset.
    const bool reset_required = !terminal_state && continuation_available;
    result->fields["chat_context_reset_required"] = reset_required ? "true" : "false";
    result->fields["chat_context_reset_requested"] = reset_required ? "true" : "false";
    result->fields["chat_context_reset_acknowledged"] = "false";
    result->fields["host_chat_history_mutable_by_mcp"] = "false";
    result->fields["old_context_dropped"] = "false";
    result->fields["mcp_continuation_ready"] = continuation_available ? "true" : "false";
    result->fields["handoff_completion_claim"] = terminal_state
        ? "task_completion_gate_required"
        : "not_task_complete";
    result->fields["next_chat_status_check_required"] =
        (terminal_state || continuation_available) ? "true" : "false";
    result->fields["next_chat_status_check_tool_name"] =
        (terminal_state || continuation_available) ? "lan_agent_task_memory_resume_context" : "";
    result->fields["next_chat_status_check_arguments_json"] =
        (terminal_state || continuation_available) ? BuildTaskMemoryResumeContextCallJson(goal_id) : "";
    result->fields["next_chat_must_verify_fields_json"] =
        "[\"terminal_state\",\"completion_claim_allowed\",\"final_answer_allowed\",\"verification_ok\","
        "\"clean_chat_close_allowed\",\"conversation_close_status\",\"new_chat_entry_arguments_json\"]";
    result->fields["new_chat_entry_tool_name"] = continuation_available
        ? "lan_agent_task_memory_resume_and_execute"
        : "";
    result->fields["new_chat_entry_arguments_json"] = continuation_available
        ? BuildTaskMemoryResumeAndExecuteCallJson(goal_id, trace_id, max_steps)
        : "";
    result->fields["new_chat_resume_instruction"] = continuation_available
        ? "chat/client must reset to a fresh context, then call new_chat_entry_arguments_json; MCP does not reload or retain old chat history"
        : (terminal_state ? "no resume is required; inspect verification fields before final claim" : "repair or freeze continuation before ending the chat");
}

inline std::string BuildDefaultRagMigrationManifest(
    const std::string & goal_id,
    const std::string & trace_id) {
    std::ostringstream output;
    output
        << "{\n"
        << "  \"record_model\":\"rag_thread_incremental_index_manifest_v1\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << JsonEscape(trace_id) << "\",\n"
        << "  \"module_group\":\"rocksdb_incremental_index\",\n"
        << "  \"migration_order\":[\"file_object_store\",\"kv_snapshot\",\"rocksdb_native\"],\n"
        << "  \"source_files\":[\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/rag_storage.h\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/rag_storage.cpp\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/rag_server_runtime.h\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/rag_server_runtime.cpp\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/rag_integration_bridge.h\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/rag_integration_bridge.cpp\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/repo_scanner.h\",\n"
        << "    \"D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/tools/server/RAG/src/repo_scanner.cpp\"\n"
        << "  ],\n"
        << "  \"entry_functions\":[\"ingest_slice\",\"append_step\",\"query_by_trace\",\"query_by_slice\",\"resume_context\"],\n"
        << "  \"dependencies\":[\"filesystem\",\"json parser\",\"hash/checksum\",\"rocksdb optional\"],\n"
        << "  \"storage_schema\":{\n"
        << "    \"slice_key\":\"slice/{slice_id}\",\n"
        << "    \"trace_key\":\"trace/{trace_id}/{step_id}\",\n"
        << "    \"goal_key\":\"goal/{goal_id}\",\n"
        << "    \"latest_key\":\"latest/{goal_id}\"\n"
        << "  },\n"
        << "  \"mcp_landing\":{\n"
        << "    \"file_object_layer\":\"task_memory/{goal_id}\",\n"
        << "    \"step_ledger\":\"step_ledger.jsonl\",\n"
        << "    \"resume_context\":\"latest_resume_context.json\",\n"
        << "    \"rocksdb_status\":\"deferred_optional_backend\"\n"
        << "  }\n"
        << "}\n";
    return output.str();
}

inline std::string BuildGeneratedCurrentStateMarkdown(
    const JsonRequestView & params,
    const std::string & goal_id,
    const std::string & trace_id) {
    std::ostringstream output;
    output
        << "# RAG Thread Migration Current State\n\n"
        << "- goal_id: " << goal_id << "\n"
        << "- trace_id: " << trace_id << "\n"
        << "- current_goal: " << params.GetString("current_goal") << "\n"
        << "- current_scope: " << params.GetString("current_scope") << "\n"
        << "- current_file: " << params.GetString("current_file") << "\n"
        << "- last_status: " << params.GetString("last_status") << "\n"
        << "- last_has_more: " << params.GetString("last_has_more", "unknown") << "\n"
        << "- terminal_state: " << (params.GetBool("terminal_state", false) ? "true" : "false") << "\n"
        << "- completion_claim_allowed: " << (params.GetBool("completion_claim_allowed", false) ? "true" : "false") << "\n"
        << "- completed_step_count: " << params.GetInt("completed_step_count", 0) << "\n"
        << "- remaining_work: " << params.GetString("remaining_work") << "\n\n"
        << "## last next_call_json\n\n"
        << "```json\n"
        << TaskMemoryParamStringOrRaw(params, "next_call_json") << "\n"
        << "```\n";
    return output.str();
}

inline std::string BuildDefaultKeySlicesJsonl(
    const JsonRequestView & params,
    const std::string & goal_id,
    const std::string & trace_id) {
    const std::string summary = TaskMemoryFirstNonEmpty(
        params.GetString("compact_summary"),
        params.GetString("current_goal"),
        "RAG thread migration state captured for MCP resume.");
    const std::string seed = goal_id + "|" + trace_id + "|" + summary;
    std::ostringstream output;
    output
        << "{"
        << JsonStringField("slice_id", "rag-main-" + TaskMemoryStableChecksum(seed))
        << JsonStringField("slice_type", "task_state")
        << JsonStringField("summary", summary)
        << JsonStringField("source_ref", params.GetString("source_ref"))
        << JsonStringField("result_ref", params.GetString("result_ref"))
        << JsonStringField("evidence_ref", params.GetString("evidence_ref"))
        << JsonStringField("trace_id", trace_id)
        << JsonStringField("dedup_hash", TaskMemoryStableChecksum(summary))
        << JsonStringField("importance", "high", false)
        << "}\n";
    return output.str();
}

inline std::string BuildResumeContextJson(
    const JsonRequestView & params,
    const std::string & goal_id,
    const std::string & trace_id,
    int last_verified_step,
    const std::string & next_call_json_override = std::string()) {
    const bool terminal_state = params.GetBool("terminal_state", false);
    const bool completion_claim_allowed = params.GetBool("completion_claim_allowed", false) && terminal_state;
    const std::string next_call_json = TaskMemoryNormalizeNextCallJson(
        Trim(next_call_json_override).empty()
            ? TaskMemoryParamStringOrRaw(params, "next_call_json")
            : next_call_json_override);
    const bool continuation_available = !terminal_state && !Trim(next_call_json).empty();
    const bool reset_required = !terminal_state && continuation_available;
    const int resume_max_steps = std::max(1, params.GetInt("budget_max_steps", 10));
    const std::string conversation_close_status = terminal_state
        ? "terminal_complete"
        : (continuation_available ? "handoff_ready_not_complete" : "close_blocked_missing_continuation");
    std::ostringstream output;
    output
        << "{\n"
        << "  \"record_model\":\"mcp_resume_context_v1\",\n"
        << "  \"updated_at\":\"" << JsonEscape(IsoTimestampNow()) << "\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << JsonEscape(trace_id) << "\",\n"
        << "  \"terminal_state\":" << (terminal_state ? "true" : "false") << ",\n"
        << "  \"completion_claim_allowed\":" << (completion_claim_allowed ? "true" : "false") << ",\n"
        << "  \"final_answer_allowed\":" << (completion_claim_allowed ? "true" : "false") << ",\n"
        << "  \"verification_ok\":" << (params.GetBool("verification_ok", false) && terminal_state ? "true" : "false") << ",\n"
        << "  \"clean_chat_close_allowed\":" << ((terminal_state || continuation_available) ? "true" : "false") << ",\n"
        << "  \"conversation_close_allowed\":" << ((terminal_state || continuation_available) ? "true" : "false") << ",\n"
        << "  \"conversation_close_status\":\"" << JsonEscape(conversation_close_status) << "\",\n"
        << "  \"chat_context_reset_required\":" << (reset_required ? "true" : "false") << ",\n"
        << "  \"chat_context_reset_requested\":" << (reset_required ? "true" : "false") << ",\n"
        << "  \"chat_context_reset_acknowledged\":false,\n"
        << "  \"host_chat_history_mutable_by_mcp\":false,\n"
        << "  \"old_context_dropped\":false,\n"
        << "  \"mcp_continuation_ready\":" << (continuation_available ? "true" : "false") << ",\n"
        << "  \"handoff_completion_claim\":\"" << (terminal_state ? "task_completion_gate_required" : "not_task_complete") << "\",\n"
        << "  \"next_chat_status_check_required\":" << ((terminal_state || continuation_available) ? "true" : "false") << ",\n"
        << "  \"next_chat_status_check_tool_name\":\"" << ((terminal_state || continuation_available) ? "lan_agent_task_memory_resume_context" : "") << "\",\n"
        << "  \"next_chat_status_check_arguments_json\":\"" << JsonEscape((terminal_state || continuation_available) ? BuildTaskMemoryResumeContextCallJson(goal_id) : "") << "\",\n"
        << "  \"next_chat_must_verify_fields_json\":\"[\\\"terminal_state\\\",\\\"completion_claim_allowed\\\",\\\"final_answer_allowed\\\",\\\"verification_ok\\\",\\\"clean_chat_close_allowed\\\",\\\"conversation_close_status\\\",\\\"new_chat_entry_arguments_json\\\"]\",\n"
        << "  \"new_chat_entry_tool_name\":\"" << (continuation_available ? "lan_agent_task_memory_resume_and_execute" : "") << "\",\n"
        << "  \"new_chat_entry_arguments_json\":\"" << JsonEscape(continuation_available ? BuildTaskMemoryResumeAndExecuteCallJson(goal_id, trace_id, resume_max_steps) : "") << "\",\n"
        << "  \"current_tool\":\"" << JsonEscape(params.GetString("current_tool")) << "\",\n"
        << "  \"current_file\":\"" << JsonEscape(params.GetString("current_file")) << "\",\n"
        << "  \"directory_scope_active\":" << (params.GetBool("directory_scope_active", false) ? "true" : "false") << ",\n"
        << "  \"directory_manifest_path\":\"" << JsonEscape(params.GetString("directory_manifest_path")) << "\",\n"
        << "  \"directory_current_file_index\":" << std::max(0, params.GetInt("directory_current_file_index", 0)) << ",\n"
        << "  \"directory_next_file_index\":\"" << JsonEscape(params.GetString("directory_next_file_index")) << "\",\n"
        << "  \"directory_total_code_file_count\":" << std::max(0, params.GetInt("directory_total_code_file_count", 0)) << ",\n"
        << "  \"directory_remaining_code_file_count\":" << std::max(0, params.GetInt("directory_remaining_code_file_count", 0)) << ",\n"
        << "  \"directory_scope_incomplete\":" << (params.GetBool("directory_scope_incomplete", false) ? "true" : "false") << ",\n"
        << "  \"directory_next_probe_call_json\":\"" << JsonEscape(TaskMemoryParamStringOrRaw(params, "directory_next_probe_call_json")) << "\",\n"
        << "  \"clips_post_result_matched_rule\":\"" << JsonEscape(params.GetString("clips_post_result_matched_rule")) << "\",\n"
        << "  \"clips_post_result_reason_code\":\"" << JsonEscape(params.GetString("clips_post_result_reason_code")) << "\",\n"
        << "  \"next_call_json\":\"" << JsonEscape(next_call_json) << "\",\n"
        << "  \"compact_summary\":\"" << JsonEscape(params.GetString("compact_summary")) << "\",\n"
        << "  \"last_verified_step\":" << last_verified_step << ",\n"
        << "  \"remaining_work\":\"" << JsonEscape(params.GetString("remaining_work")) << "\",\n"
        << "  \"next_allowed_action\":\"" << JsonEscape(params.GetString("next_allowed_action", "call next_call_json or append the next verified step")) << "\"\n"
        << "}\n";
    return output.str();
}

inline std::string BuildTaskMemoryJson(
    const std::string & goal_id,
    const std::string & trace_id,
    const std::filesystem::path & root,
    const std::filesystem::path & migration_dir) {
    std::ostringstream output;
    output
        << "{\n"
        << "  \"record_model\":\"mcp_task_memory_v1\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << JsonEscape(trace_id) << "\",\n"
        << "  \"created_at\":\"" << JsonEscape(IsoTimestampNow()) << "\",\n"
        << "  \"storage_order\":[\"file_object_store\",\"kv_snapshot\",\"rocksdb_native\"],\n"
        << "  \"root_path\":\"" << JsonEscape(root.string()) << "\",\n"
        << "  \"migration_dir\":\"" << JsonEscape(migration_dir.string()) << "\",\n"
        << "  \"artifacts\":{\n"
        << "    \"current_state\":\"" << JsonEscape((migration_dir / "1_current_state.md").string()) << "\",\n"
        << "    \"key_slices\":\"" << JsonEscape((migration_dir / "2_key_slices.jsonl").string()) << "\",\n"
        << "    \"incremental_index_manifest\":\"" << JsonEscape((migration_dir / "3_incremental_index_manifest.json").string()) << "\",\n"
        << "    \"migration_handover\":\"" << JsonEscape((migration_dir / "4_migration_handover.md").string()) << "\",\n"
        << "    \"step_ledger\":\"" << JsonEscape((root / "step_ledger.jsonl").string()) << "\",\n"
        << "    \"slices\":\"" << JsonEscape((root / "slices.jsonl").string()) << "\",\n"
        << "    \"index_manifest\":\"" << JsonEscape((root / "index_manifest.json").string()) << "\",\n"
        << "    \"latest_resume_context\":\"" << JsonEscape((root / "latest_resume_context.json").string()) << "\"\n"
        << "  }\n"
        << "}\n";
    return output.str();
}

inline std::string BuildMigrationHandoverMarkdown(
    const std::string & goal_id,
    const std::string & trace_id) {
    std::ostringstream output;
    output
        << "# RAG Thread Migration Handover\n\n"
        << "Goal: make the next model continue from MCP assets instead of full chat history.\n\n"
        << "- goal_id: " << goal_id << "\n"
        << "- trace_id: " << trace_id << "\n"
        << "- read first: latest_resume_context.json\n"
        << "- then inspect: step_ledger.jsonl, slices.jsonl, index_manifest.json\n\n"
        << "Rules:\n"
        << "- Do not claim completion when completion_claim_allowed=false.\n"
        << "- Continue only through next_call_json or a bounded verified step.\n"
        << "- Append every verified step to step_ledger.jsonl.\n"
        << "- Keep RocksDB as a later optional backend; file object storage is the current source of truth.\n";
    return output.str();
}

inline int TaskMemoryExtractIntField(
    const std::string & json,
    const std::string & key,
    int fallback = 0) {
    const std::string raw = ExtractJsonRawValue(json, key);
    if (Trim(raw).empty()) {
        return fallback;
    }
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

inline bool TaskMemoryExtractBoolField(
    const std::string & json,
    const std::string & key,
    bool fallback = false) {
    const std::string raw = ExtractJsonRawValue(json, key);
    if (raw == "true") {
        return true;
    }
    if (raw == "false") {
        return false;
    }
    return fallback;
}

inline bool TaskMemoryFileExistsNonEmpty(const std::filesystem::path & path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec)
        && !ec
        && std::filesystem::is_regular_file(path, ec)
        && !ec
        && std::filesystem::file_size(path, ec) > 0
        && !ec;
}

inline std::string TaskMemoryJoinCsv(const std::vector<std::string> & items) {
    std::ostringstream output;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        output << items[i];
    }
    return output.str();
}

inline int TaskMemoryCountJsonlRecords(const std::filesystem::path & path) {
    std::istringstream lines(ReadTaskMemoryTextFile(path));
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) {
        if (!Trim(line).empty()) {
            ++count;
        }
    }
    return count;
}

inline int TaskMemoryCountRegularFiles(const std::filesystem::path & path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec || !std::filesystem::is_directory(path, ec) || ec) {
        return 0;
    }
    int count = 0;
    for (const auto & entry : std::filesystem::directory_iterator(path, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && !ec) {
            ++count;
        }
    }
    return count;
}

inline std::string TaskMemoryJsonPreview(const std::string & value, std::size_t max_chars = 240) {
    std::string compact;
    compact.reserve(std::min(value.size(), max_chars));
    bool previous_space = false;
    for (char ch : value) {
        const bool is_space = ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ';
        if (is_space) {
            if (!previous_space && !compact.empty()) {
                compact.push_back(' ');
            }
            previous_space = true;
        } else {
            compact.push_back(ch);
            previous_space = false;
        }
        if (compact.size() >= max_chars) {
            compact.resize(max_chars);
            break;
        }
    }
    return compact;
}

inline void AppendTaskMemoryKvRecord(
    std::ostringstream & output,
    const std::string & key,
    const std::string & kind,
    const std::string & goal_id,
    const std::string & trace_id,
    const std::string & source_path,
    const std::string & value,
    const std::string & step_id = std::string(),
    const std::string & slice_id = std::string(),
    const std::string & budget_run_id = std::string()) {
    output
        << "{"
        << JsonStringField("record_model", "mcp_task_memory_kv_record_v1")
        << JsonStringField("key", key)
        << JsonStringField("kind", kind)
        << JsonStringField("goal_id", goal_id)
        << JsonStringField("trace_id", trace_id)
        << JsonStringField("step_id", step_id)
        << JsonStringField("slice_id", slice_id)
        << JsonStringField("budget_run_id", budget_run_id)
        << JsonStringField("source_path", source_path)
        << JsonStringField("value_ref", source_path)
        << JsonStringField("value_hash", TaskMemoryStableChecksum(value))
        << JsonStringField("preview", TaskMemoryJsonPreview(value), false)
        << "}\n";
}

inline std::string TaskMemoryBuildLookupKey(
    const std::string & goal_id,
    const JsonRequestView & params,
    bool * prefix_match) {
    if (prefix_match != nullptr) {
        *prefix_match = false;
    }
    const std::string explicit_key = params.GetString("key");
    if (!Trim(explicit_key).empty()) {
        if (params.GetBool("prefix", false) && prefix_match != nullptr) {
            *prefix_match = true;
        }
        return explicit_key;
    }

    const std::string kind = params.GetString("kind");
    if (kind == "goal") {
        return "goal/" + goal_id;
    }
    if (kind == "latest" || kind == "resume_context") {
        return "latest/" + goal_id;
    }
    if (kind == "slice") {
        return "slice/" + params.GetString("slice_id");
    }
    if (kind == "budget") {
        return "budget/" + params.GetString("budget_run_id");
    }
    if (kind == "trace_step") {
        return "trace/" + params.GetString("trace_id") + "/step/" + params.GetString("step_id");
    }
    if (kind == "trace_budget") {
        return "trace/" + params.GetString("trace_id") + "/budget/" + params.GetString("budget_run_id");
    }
    if (kind == "trace" || !Trim(params.GetString("trace_id")).empty()) {
        if (Trim(params.GetString("trace_id")).empty()) {
            return std::string();
        }
        if (prefix_match != nullptr) {
            *prefix_match = true;
        }
        return "trace/" + params.GetString("trace_id") + "/";
    }
    return std::string();
}

inline std::filesystem::path TaskMemoryRocksDbPath(
    const AgentConfig & config,
    const std::string & goal_id,
    const JsonRequestView & params) {
    const std::string explicit_path = params.GetString("rocksdb_path");
    if (!Trim(explicit_path).empty()) {
        return std::filesystem::path(explicit_path);
    }
    return BuildTaskMemoryRoot(config, goal_id) / "rocksdb_native";
}

inline std::filesystem::path TaskMemoryRocksDbManifestPath(
    const AgentConfig & config,
    const std::string & goal_id) {
    return BuildTaskMemoryRoot(config, goal_id) / "rocksdb_mirror_manifest.json";
}

inline bool TaskMemoryRocksDbManifestReady(
    const AgentConfig & config,
    const std::string & goal_id) {
    const std::string manifest = ReadTaskMemoryTextFile(
        TaskMemoryRocksDbManifestPath(config, goal_id));
    const std::string mirrored_count = Trim(ExtractJsonRawValue(manifest, "mirrored_count"));
    return !mirrored_count.empty() && mirrored_count != "0";
}

inline CommandResult BuildTaskMemoryFreezeResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = TaskMemoryFirstNonEmpty(
        params.GetString("goal_id"),
        params.GetString("current_goal_id"),
        params.GetString("task_goal_id"));
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        result.fields["accepted_goal_id_aliases"] = "goal_id,current_goal_id,task_goal_id";
        result.fields["next_action"] =
            "provide goal_id before freezing task memory, or pass current_goal_id as a compatible alias";
        return result;
    }

    const std::string trace_id = params.GetString("trace_id", "TRACE-" + TaskMemoryStableChecksum(goal_id));
    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path migration_dir = root / "rag_thread_migration";
    const std::filesystem::path evidence_dir = root / "evidence_refs";

    const std::string current_state = TaskMemoryFirstNonEmpty(
        params.GetString("current_state_markdown"),
        BuildGeneratedCurrentStateMarkdown(params, goal_id, trace_id));
    const std::string slices_jsonl = TaskMemoryFirstNonEmpty(
        params.GetString("key_slices_jsonl"),
        BuildDefaultKeySlicesJsonl(params, goal_id, trace_id));
    const std::string manifest = TaskMemoryFirstNonEmpty(
        TaskMemoryParamStringOrRaw(params, "incremental_index_manifest_json"),
        BuildDefaultRagMigrationManifest(goal_id, trace_id));
    const std::string handover = TaskMemoryFirstNonEmpty(
        params.GetString("migration_handover_markdown"),
        BuildMigrationHandoverMarkdown(goal_id, trace_id));
    const bool terminal_state = params.GetBool("terminal_state", false);
    std::string freeze_next_call_json = TaskMemoryNormalizeNextCallJson(
        TaskMemoryParamStringOrRaw(params, "next_call_json"));
    if (Trim(freeze_next_call_json).empty()) {
        freeze_next_call_json = TaskMemoryNormalizeNextCallJson(
            BuildTaskMemoryFlatContinuationJson(params));
    }
    if (!terminal_state && Trim(freeze_next_call_json).empty()) {
        result.ok = false;
        result.exit_code = 422;
        result.fields["status"] = "failed";
        result.fields["record_model"] = "mcp_task_memory_freeze_response_v1";
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        result.fields["error"] = "NEXT_CALL_JSON_MISSING_FOR_FREEZE";
        result.fields["terminal_state"] = "false";
        result.fields["task_done"] = "false";
        result.fields["completion_claim_allowed"] = "false";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["verification_ok"] = "false";
        result.fields["continue_required"] = "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["must_continue_until"] = "valid_next_call_json";
        ApplyTaskMemoryCleanHandoffFields(
            &result,
            goal_id,
            trace_id,
            std::max(1, params.GetInt("budget_max_steps", 10)),
            false,
            false);
        result.fields["next_action"] =
            "freeze rejected: non-terminal task_memory handoff requires the exact previous next_call_json; do not claim completion";
        return result;
    }
    const std::string resume_context = BuildResumeContextJson(
        params,
        goal_id,
        trace_id,
        std::max(0, params.GetInt("completed_step_count", 0)),
        freeze_next_call_json);

    std::error_code ec;
    std::filesystem::create_directories(evidence_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 501;
        result.fields["error"] = "failed to create evidence_refs directory: " + ec.message();
        return result;
    }

    std::string error;
    const std::vector<std::pair<std::filesystem::path, std::string>> writes = {
        {migration_dir / "1_current_state.md", current_state},
        {migration_dir / "2_key_slices.jsonl", slices_jsonl},
        {migration_dir / "3_incremental_index_manifest.json", manifest},
        {migration_dir / "4_migration_handover.md", handover},
        {root / "task_memory.json", BuildTaskMemoryJson(goal_id, trace_id, root, migration_dir)},
        {root / "slices.jsonl", slices_jsonl},
        {root / "index_manifest.json", manifest},
        {root / "latest_resume_context.json", resume_context}
    };
    for (const auto & item : writes) {
        if (!WriteTaskMemoryTextFile(item.first, item.second, &error)) {
            result.ok = false;
            result.exit_code = 502;
            result.fields["error"] = error;
            result.fields["failed_path"] = item.first.string();
            return result;
        }
    }

    std::ostringstream ledger_record;
    ledger_record
        << "{"
        << JsonStringField("record_model", "mcp_step_ledger_entry_v1")
        << JsonStringField("timestamp", IsoTimestampNow())
        << JsonStringField("goal_id", goal_id)
        << JsonStringField("trace_id", trace_id)
        << JsonStringField("step_kind", "freeze")
        << JsonStringField("status", params.GetString("last_status", "frozen"))
        << JsonStringField("summary", params.GetString("compact_summary"))
        << "\"step_index\":" << std::max(0, params.GetInt("completed_step_count", 0)) << ","
        << "\"terminal_state\":" << (params.GetBool("terminal_state", false) ? "true" : "false") << ","
        << "\"completion_claim_allowed\":" << (params.GetBool("completion_claim_allowed", false) && params.GetBool("terminal_state", false) ? "true" : "false")
        << "}\n";
    if (!AppendTaskMemoryTextFile(root / "step_ledger.jsonl", ledger_record.str(), &error)) {
        result.ok = false;
        result.exit_code = 503;
        result.fields["error"] = error;
        return result;
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["record_model"] = "mcp_task_memory_freeze_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["task_memory_root"] = root.string();
    result.fields["migration_dir"] = migration_dir.string();
    result.fields["current_state_path"] = (migration_dir / "1_current_state.md").string();
    result.fields["key_slices_path"] = (migration_dir / "2_key_slices.jsonl").string();
    result.fields["incremental_index_manifest_path"] = (migration_dir / "3_incremental_index_manifest.json").string();
    result.fields["migration_handover_path"] = (migration_dir / "4_migration_handover.md").string();
    result.fields["task_memory_path"] = (root / "task_memory.json").string();
    result.fields["step_ledger_path"] = (root / "step_ledger.jsonl").string();
    result.fields["slices_path"] = (root / "slices.jsonl").string();
    result.fields["index_manifest_path"] = (root / "index_manifest.json").string();
    result.fields["resume_context_path"] = (root / "latest_resume_context.json").string();
    result.fields["completion_claim_allowed"] =
        (params.GetBool("completion_claim_allowed", false) && params.GetBool("terminal_state", false)) ? "true" : "false";
    const bool completion_claim_allowed =
        params.GetBool("completion_claim_allowed", false) && terminal_state;
    const std::string budget_arguments_json =
        "{\"name\":\"lan_agent_task_memory_execute_continuation_budget\",\"arguments\":{"
        "\"goal_id\":\"" + JsonEscape(goal_id) + "\","
        "\"trace_id\":\"" + JsonEscape(trace_id) + "\","
        "\"max_steps\":" + std::to_string(std::max(1, params.GetInt("budget_max_steps", 10))) + ","
        "\"execute\":true,"
        "\"dry_run\":false"
        "}}";
    result.fields["status"] = terminal_state ? "success" : "needs_continue";
    result.fields["terminal_state"] = terminal_state ? "true" : "false";
    result.fields["task_done"] = terminal_state ? "true" : "false";
    result.fields["final_answer_allowed"] =
        completion_claim_allowed ? "true" : "false";
    result.fields["assistant_response_allowed"] =
        completion_claim_allowed ? "true" : "false";
    result.fields["continue_required"] = terminal_state ? "false" : "true";
    result.fields["auto_continue_required"] = terminal_state ? "false" : "true";
    result.fields["must_continue_until"] = terminal_state ? "" : "terminal_state=true";
    ApplyTaskMemoryCleanHandoffFields(
        &result,
        goal_id,
        trace_id,
        std::max(1, params.GetInt("budget_max_steps", 10)),
        terminal_state,
        !terminal_state && !Trim(freeze_next_call_json).empty());
    if (!terminal_state) {
        result.fields["task_execution_in_mcp_required"] = "true";
        result.fields["forced_task_memory_execution"] = "true";
        result.fields["required_next_action_type"] = "mcp_tool_call";
        result.fields["required_tool_name"] = "lan_agent_task_memory_execute_continuation_budget";
        result.fields["required_tool_arguments_json"] = budget_arguments_json;
        result.fields["next_call_json"] = budget_arguments_json;
    }
    result.fields["semantic_outcome"] = "task_memory_frozen";
    result.fields["next_action"] = terminal_state
        ? "task memory frozen at terminal state"
        : "tool_call_only: run lan_agent_task_memory_execute_continuation_budget with execute=true and dry_run=false";
    result.fields["result_ref"] = (root / "latest_resume_context.json").string();
    result.fields["evidence_ref"] = (migration_dir / "4_migration_handover.md").string();
    return result;
}

inline CommandResult BuildTaskMemoryAppendStepResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }
    const std::string trace_id = params.GetString("trace_id", "TRACE-" + TaskMemoryStableChecksum(goal_id));
    const int step_index = std::max(0, params.GetInt("step_index", 0));
    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path ledger_path = root / "step_ledger.jsonl";
    const bool terminal_state = params.GetBool("terminal_state", false);
    const bool has_more = params.GetBool("has_more", false);
    const std::string append_next_call_json = TaskMemoryNormalizeNextCallJson(
        TaskMemoryParamStringOrRaw(params, "next_call_json"));
    if (!terminal_state && has_more && Trim(append_next_call_json).empty()) {
        result.ok = false;
        result.exit_code = 422;
        result.fields["status"] = "failed";
        result.fields["record_model"] = "mcp_task_memory_append_step_response_v1";
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        result.fields["error"] = "NEXT_CALL_JSON_MISSING_FOR_APPEND_STEP";
        result.fields["terminal_state"] = "false";
        result.fields["completion_claim_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["verification_ok"] = "false";
        result.fields["must_continue_until"] = "valid_next_call_json";
        ApplyTaskMemoryCleanHandoffFields(
            &result,
            goal_id,
            trace_id,
            10,
            false,
            false);
        result.fields["next_action"] =
            "append_step rejected: has_more=true requires next_call_json; do not claim completion";
        return result;
    }

    std::ostringstream record;
    record
        << "{"
        << JsonStringField("record_model", "mcp_step_ledger_entry_v1")
        << JsonStringField("timestamp", IsoTimestampNow())
        << JsonStringField("goal_id", goal_id)
        << JsonStringField("trace_id", trace_id)
        << JsonStringField("step_id", params.GetString("step_id", "step-" + std::to_string(step_index)))
        << JsonStringField("step_kind", params.GetString("step_kind", "verified_step"))
        << JsonStringField("current_tool", params.GetString("current_tool"))
        << JsonStringField("status", params.GetString("status", "observed"))
        << JsonStringField("summary", params.GetString("summary"))
        << JsonStringField("result_ref", params.GetString("result_ref"))
        << JsonStringField("evidence_ref", params.GetString("evidence_ref"))
        << JsonStringField("clips_post_result_matched_rule", params.GetString("clips_post_result_matched_rule"))
        << JsonStringField("clips_post_result_reason_code", params.GetString("clips_post_result_reason_code"))
        << JsonStringField("next_call_json", append_next_call_json)
        << "\"step_index\":" << step_index << ","
        << "\"directory_scope_active\":" << (params.GetBool("directory_scope_active", false) ? "true" : "false") << ","
        << "\"directory_scope_incomplete\":" << (params.GetBool("directory_scope_incomplete", false) ? "true" : "false") << ","
        << "\"directory_current_file_index\":" << std::max(0, params.GetInt("directory_current_file_index", 0)) << ","
        << "\"directory_next_file_index\":\"" << JsonEscape(params.GetString("directory_next_file_index")) << "\","
        << "\"directory_remaining_code_file_count\":" << std::max(0, params.GetInt("directory_remaining_code_file_count", 0)) << ","
        << "\"has_more\":" << (params.GetBool("has_more", false) ? "true" : "false") << ","
        << "\"terminal_state\":" << (params.GetBool("terminal_state", false) ? "true" : "false") << ","
        << "\"completion_claim_allowed\":" << (params.GetBool("completion_claim_allowed", false) && params.GetBool("terminal_state", false) ? "true" : "false")
        << "}\n";

    std::string error;
    if (!AppendTaskMemoryTextFile(ledger_path, record.str(), &error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["error"] = error;
        return result;
    }

    const std::string resume_context = BuildResumeContextJson(params, goal_id, trace_id, step_index);
    if (!WriteTaskMemoryTextFile(root / "latest_resume_context.json", resume_context, &error)) {
        result.ok = false;
        result.exit_code = 503;
        result.fields["error"] = error;
        return result;
    }

    result.fields["record_model"] = "mcp_task_memory_append_step_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["step_index"] = std::to_string(step_index);
    result.fields["step_ledger_path"] = ledger_path.string();
    result.fields["resume_context_path"] = (root / "latest_resume_context.json").string();
    result.fields["completion_claim_allowed"] =
        (params.GetBool("completion_claim_allowed", false) && params.GetBool("terminal_state", false)) ? "true" : "false";
    result.fields["terminal_state"] = params.GetBool("terminal_state", false) ? "true" : "false";
    result.fields["task_done"] = params.GetBool("terminal_state", false) ? "true" : "false";
    result.fields["final_answer_allowed"] =
        (params.GetBool("completion_claim_allowed", false) && params.GetBool("terminal_state", false)) ? "true" : "false";
    result.fields["assistant_response_allowed"] =
        (params.GetBool("completion_claim_allowed", false) && params.GetBool("terminal_state", false)) ? "true" : "false";
    result.fields["must_continue_until"] = params.GetBool("terminal_state", false) ? "" : "terminal_state=true";
    ApplyTaskMemoryCleanHandoffFields(
        &result,
        goal_id,
        trace_id,
        10,
        params.GetBool("terminal_state", false),
        !TaskMemoryParamStringOrRaw(params, "next_call_json").empty());
    result.fields["semantic_outcome"] = "task_memory_step_appended";
    result.fields["next_action"] = "continue with next_call_json while completion_claim_allowed=false";
    result.fields["result_ref"] = (root / "latest_resume_context.json").string();
    result.fields["evidence_ref"] = ledger_path.string();
    return result;
}

inline CommandResult BuildTaskMemoryExecuteContinuationBudgetResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    const int max_steps = std::min(64, std::max(1, params.GetInt("max_steps", params.GetInt("step_budget", 1))));
    const bool dry_run = params.GetBool("dry_run", true);
    const bool execute = params.GetBool("execute", false);
    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path resume_path = root / "latest_resume_context.json";
    const std::string resume_context = ReadTaskMemoryTextFile(resume_path);
    if (resume_context.empty()) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["status"] = "failed";
        result.fields["record_model"] = "mcp_task_memory_execute_continuation_budget_response_v1";
        result.fields["goal_id"] = goal_id;
        result.fields["budget_status"] = "blocked_missing_resume_context";
        result.fields["terminal_state"] = "false";
        result.fields["task_done"] = "false";
        result.fields["completion_claim_allowed"] = "false";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["continue_required"] = "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["budget_requires_frozen_resume_context"] = "true";
        result.fields["error"] = "resume context not found";
        result.fields["resume_context_path"] = resume_path.string();
        ApplyTaskMemoryCleanHandoffFields(
            &result,
            goal_id,
            "",
            max_steps,
            false,
            false);
        result.fields["next_action"] =
            "do not call budget directly before freeze; call the previous required_tool_arguments_json for lan_agent_task_memory_freeze first";
        return result;
    }

    const std::string trace_id = TaskMemoryFirstNonEmpty(
        params.GetString("trace_id"),
        ExtractJsonString(resume_context, "trace_id"),
        "TRACE-" + TaskMemoryStableChecksum(goal_id));
    const int last_verified_step = TaskMemoryExtractIntField(resume_context, "last_verified_step", 0);
    const bool terminal_state = TaskMemoryExtractBoolField(resume_context, "terminal_state", false);
    const bool completion_claim_allowed =
        TaskMemoryExtractBoolField(resume_context, "completion_claim_allowed", false) && terminal_state;
    const std::string next_call_json = ExtractJsonString(resume_context, "next_call_json");
    const bool has_next_call = !Trim(next_call_json).empty();
    const bool can_plan_next = !terminal_state && has_next_call;
    const int planned_step_count = can_plan_next ? 1 : 0;
    const std::string now = IsoTimestampNow();
    const std::string budget_run_id = "budget-" + TaskMemoryStableChecksum(
        goal_id + "|" + trace_id + "|" + std::to_string(last_verified_step) + "|" + now);
    const std::filesystem::path budget_dir = root / "budget_runs";
    const std::filesystem::path budget_path = budget_dir / (budget_run_id + ".json");
    const std::string execution_mode = (execute && !dry_run)
        ? "execute_requested_but_deferred_to_explicit_tool_call"
        : "plan_only";
    const std::string budget_status = terminal_state
        ? "terminal_no_work"
        : (has_next_call ? "next_call_planned" : "blocked_missing_next_call_json");

    std::ostringstream plan;
    plan
        << "{\n"
        << "  \"record_model\":\"mcp_continuation_budget_plan_v1\",\n"
        << "  \"budget_run_id\":\"" << JsonEscape(budget_run_id) << "\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << JsonEscape(trace_id) << "\",\n"
        << "  \"created_at\":\"" << JsonEscape(now) << "\",\n"
        << "  \"execution_mode\":\"" << JsonEscape(execution_mode) << "\",\n"
        << "  \"budget_status\":\"" << JsonEscape(budget_status) << "\",\n"
        << "  \"max_steps\":" << max_steps << ",\n"
        << "  \"planned_step_count\":" << planned_step_count << ",\n"
        << "  \"last_verified_step\":" << last_verified_step << ",\n"
        << "  \"terminal_state\":" << (terminal_state ? "true" : "false") << ",\n"
        << "  \"completion_claim_allowed\":" << (completion_claim_allowed ? "true" : "false") << ",\n"
        << "  \"next_call_json\":\"" << JsonEscape(next_call_json) << "\",\n"
        << "  \"required_followup\":\""
        << JsonEscape(can_plan_next ? "call next_call_json explicitly, then append verified result" : "inspect resume_context and repair continuation state")
        << "\"\n"
        << "}\n";

    std::string error;
    if (!WriteTaskMemoryTextFile(budget_path, plan.str(), &error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["error"] = error;
        result.fields["failed_path"] = budget_path.string();
        return result;
    }

    std::ostringstream ledger_record;
    ledger_record
        << "{"
        << JsonStringField("record_model", "mcp_step_ledger_entry_v1")
        << JsonStringField("timestamp", now)
        << JsonStringField("goal_id", goal_id)
        << JsonStringField("trace_id", trace_id)
        << JsonStringField("step_id", budget_run_id)
        << JsonStringField("step_kind", "continuation_budget_plan")
        << JsonStringField("status", budget_status)
        << JsonStringField("summary", "continuation budget evaluated from resume_context")
        << JsonStringField("result_ref", resume_path.string())
        << JsonStringField("evidence_ref", budget_path.string())
        << JsonStringField("next_call_json", next_call_json)
        << "\"step_index\":" << last_verified_step << ","
        << "\"planned_step_count\":" << planned_step_count << ","
        << "\"max_steps\":" << max_steps << ","
        << "\"terminal_state\":" << (terminal_state ? "true" : "false") << ","
        << "\"completion_claim_allowed\":" << (completion_claim_allowed ? "true" : "false")
        << "}\n";
    if (!AppendTaskMemoryTextFile(root / "step_ledger.jsonl", ledger_record.str(), &error)) {
        result.ok = false;
        result.exit_code = 503;
        result.fields["error"] = error;
        return result;
    }

    result.fields["record_model"] = "mcp_task_memory_execute_continuation_budget_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["budget_run_id"] = budget_run_id;
    result.fields["budget_status"] = budget_status;
    result.fields["execution_mode"] = execution_mode;
    result.fields["dry_run"] = dry_run ? "true" : "false";
    result.fields["execute_requested"] = execute ? "true" : "false";
    result.fields["execution_deferred"] = "true";
    result.fields["max_steps"] = std::to_string(max_steps);
    result.fields["planned_step_count"] = std::to_string(planned_step_count);
    result.fields["last_verified_step"] = std::to_string(last_verified_step);
    result.fields["resume_context_path"] = resume_path.string();
    result.fields["budget_plan_path"] = budget_path.string();
    result.fields["step_ledger_path"] = (root / "step_ledger.jsonl").string();
    result.fields["terminal_state"] = terminal_state ? "true" : "false";
    result.fields["completion_claim_allowed"] = completion_claim_allowed ? "true" : "false";
    result.fields["task_done"] = terminal_state ? "true" : "false";
    result.fields["continue_required"] = can_plan_next ? "true" : "false";
    result.fields["auto_continue_required"] = can_plan_next ? "true" : "false";
    result.fields["assistant_response_allowed"] = completion_claim_allowed ? "true" : "false";
    result.fields["final_answer_allowed"] = completion_claim_allowed ? "true" : "false";
    result.fields["must_continue_until"] = terminal_state ? "" : "terminal_state=true";
    ApplyTaskMemoryCleanHandoffFields(
        &result,
        goal_id,
        trace_id,
        max_steps,
        terminal_state,
        can_plan_next);
    result.fields["next_call_json"] = next_call_json;
    result.fields["semantic_outcome"] = "continuation_budget_planned";
    result.fields["next_action"] = can_plan_next
        ? "call next_call_json explicitly, then lan_agent_task_memory_append_step with verified evidence"
        : "repair or refresh latest_resume_context before continuing";
    result.fields["result_ref"] = resume_path.string();
    result.fields["evidence_ref"] = budget_path.string();
    return result;
}

inline CommandResult BuildTaskMemoryBuildKvSnapshotResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    if (!std::filesystem::exists(root)) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "task memory root not found";
        result.fields["task_memory_root"] = root.string();
        result.fields["next_action"] = "call lan_agent_task_memory_freeze first";
        return result;
    }

    const std::filesystem::path snapshot_dir = root / "kv_snapshot";
    const std::filesystem::path index_path = snapshot_dir / "index.jsonl";
    const std::filesystem::path manifest_path = snapshot_dir / "manifest.json";
    std::ostringstream index;
    int record_count = 0;
    int step_record_count = 0;
    int slice_record_count = 0;
    int budget_record_count = 0;

    const auto add_file_record = [&](const std::filesystem::path & path, const std::string & key, const std::string & kind) {
        const std::string value = ReadTaskMemoryTextFile(path);
        if (value.empty()) {
            return;
        }
        AppendTaskMemoryKvRecord(
            index,
            key,
            kind,
            goal_id,
            ExtractJsonString(value, "trace_id"),
            path.string(),
            value);
        ++record_count;
    };

    add_file_record(root / "task_memory.json", "goal/" + goal_id, "goal");
    add_file_record(root / "latest_resume_context.json", "latest/" + goal_id, "latest");

    const std::filesystem::path ledger_path = root / "step_ledger.jsonl";
    {
        std::istringstream lines(ReadTaskMemoryTextFile(ledger_path));
        std::string line;
        int fallback_index = 0;
        while (std::getline(lines, line)) {
            line = Trim(line);
            if (line.empty()) {
                continue;
            }
            const std::string trace_id = ExtractJsonString(line, "trace_id");
            std::string step_id = ExtractJsonString(line, "step_id");
            if (step_id.empty()) {
                const std::string step_index = ExtractJsonRawValue(line, "step_index");
                step_id = step_index.empty() ? std::to_string(fallback_index) : step_index;
            }
            AppendTaskMemoryKvRecord(
                index,
                "trace/" + trace_id + "/step/" + step_id,
                "trace_step",
                goal_id,
                trace_id,
                ledger_path.string(),
                line,
                step_id);
            ++record_count;
            ++step_record_count;
            ++fallback_index;
        }
    }

    const std::filesystem::path slices_path = root / "slices.jsonl";
    {
        std::istringstream lines(ReadTaskMemoryTextFile(slices_path));
        std::string line;
        while (std::getline(lines, line)) {
            line = Trim(line);
            if (line.empty()) {
                continue;
            }
            const std::string slice_id = ExtractJsonString(line, "slice_id");
            const std::string trace_id = ExtractJsonString(line, "trace_id");
            if (!slice_id.empty()) {
                AppendTaskMemoryKvRecord(
                    index,
                    "slice/" + slice_id,
                    "slice",
                    goal_id,
                    trace_id,
                    slices_path.string(),
                    line,
                    std::string(),
                    slice_id);
                ++record_count;
                ++slice_record_count;
            }
        }
    }

    const std::filesystem::path budget_dir = root / "budget_runs";
    if (std::filesystem::exists(budget_dir)) {
        for (const auto & entry : std::filesystem::directory_iterator(budget_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }
            const std::string value = ReadTaskMemoryTextFile(entry.path());
            if (value.empty()) {
                continue;
            }
            const std::string budget_run_id = TaskMemoryFirstNonEmpty(
                ExtractJsonString(value, "budget_run_id"),
                entry.path().stem().string());
            const std::string trace_id = ExtractJsonString(value, "trace_id");
            AppendTaskMemoryKvRecord(
                index,
                "budget/" + budget_run_id,
                "budget",
                goal_id,
                trace_id,
                entry.path().string(),
                value,
                std::string(),
                std::string(),
                budget_run_id);
            ++record_count;
            ++budget_record_count;
            if (!trace_id.empty()) {
                AppendTaskMemoryKvRecord(
                    index,
                    "trace/" + trace_id + "/budget/" + budget_run_id,
                    "trace_budget",
                    goal_id,
                    trace_id,
                    entry.path().string(),
                    value,
                    std::string(),
                    std::string(),
                    budget_run_id);
                ++record_count;
            }
        }
    }

    std::string error;
    if (!WriteTaskMemoryTextFile(index_path, index.str(), &error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["error"] = error;
        result.fields["failed_path"] = index_path.string();
        return result;
    }

    std::ostringstream manifest;
    manifest
        << "{\n"
        << "  \"record_model\":\"mcp_task_memory_kv_snapshot_manifest_v1\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"created_at\":\"" << JsonEscape(IsoTimestampNow()) << "\",\n"
        << "  \"backend\":\"file_jsonl_snapshot\",\n"
        << "  \"index_path\":\"" << JsonEscape(index_path.string()) << "\",\n"
        << "  \"record_count\":" << record_count << ",\n"
        << "  \"step_record_count\":" << step_record_count << ",\n"
        << "  \"slice_record_count\":" << slice_record_count << ",\n"
        << "  \"budget_record_count\":" << budget_record_count << ",\n"
        << "  \"rocksdb_status\":\"deferred_optional_backend\"\n"
        << "}\n";
    if (!WriteTaskMemoryTextFile(manifest_path, manifest.str(), &error)) {
        result.ok = false;
        result.exit_code = 503;
        result.fields["error"] = error;
        result.fields["failed_path"] = manifest_path.string();
        return result;
    }

    result.fields["record_model"] = "mcp_task_memory_kv_snapshot_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["task_memory_root"] = root.string();
    result.fields["kv_backend"] = "file_jsonl_snapshot";
    result.fields["kv_snapshot_dir"] = snapshot_dir.string();
    result.fields["kv_index_path"] = index_path.string();
    result.fields["kv_manifest_path"] = manifest_path.string();
    result.fields["record_count"] = std::to_string(record_count);
    result.fields["step_record_count"] = std::to_string(step_record_count);
    result.fields["slice_record_count"] = std::to_string(slice_record_count);
    result.fields["budget_record_count"] = std::to_string(budget_record_count);
    result.fields["rocksdb_status"] = "deferred_optional_backend";
    result.fields["semantic_outcome"] = "task_memory_kv_snapshot_built";
    result.fields["next_action"] = "call lan_agent_task_memory_kv_lookup with key or kind filters";
    result.fields["result_ref"] = index_path.string();
    result.fields["evidence_ref"] = manifest_path.string();
    return result;
}

inline CommandResult BuildTaskMemoryKvLookupResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    bool prefix_match = false;
    const std::string lookup_key = TaskMemoryBuildLookupKey(goal_id, params, &prefix_match);
    if (Trim(lookup_key).empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "key or lookup selector is required";
        result.fields["next_action"] = "provide key, kind=latest, kind=goal, kind=trace with trace_id, kind=slice with slice_id, or kind=budget with budget_run_id";
        return result;
    }

    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path index_path = root / "kv_snapshot" / "index.jsonl";
    const std::string index_text = ReadTaskMemoryTextFile(index_path);
    if (index_text.empty()) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "kv snapshot index not found";
        result.fields["kv_index_path"] = index_path.string();
        result.fields["next_action"] = "call lan_agent_task_memory_build_kv_snapshot first";
        return result;
    }

    const int limit = std::max(1, params.GetInt("limit", 16));
    const int offset = std::max(0, params.GetInt("offset", 0));
    const bool include_value = params.GetBool("include_value", false);
    std::istringstream lines(index_text);
    std::string line;
    std::ostringstream matches;
    int total_matches = 0;
    int emitted = 0;
    std::string first_value;
    std::string first_value_ref;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        const std::string key = ExtractJsonString(line, "key");
        const bool matched = prefix_match
            ? key.rfind(lookup_key, 0) == 0
            : key == lookup_key;
        if (!matched) {
            continue;
        }
        if (total_matches >= offset && emitted < limit) {
            matches << line << "\n";
            ++emitted;
            if (include_value && first_value.empty()) {
                first_value_ref = ExtractJsonString(line, "value_ref");
                first_value = ReadTaskMemoryTextFile(std::filesystem::path(first_value_ref));
            }
        }
        ++total_matches;
    }

    result.fields["record_model"] = "mcp_task_memory_kv_lookup_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["lookup_key"] = lookup_key;
    result.fields["prefix_match"] = prefix_match ? "true" : "false";
    result.fields["kv_backend"] = "file_jsonl_snapshot";
    result.fields["kv_index_path"] = index_path.string();
    result.fields["limit"] = std::to_string(limit);
    result.fields["offset"] = std::to_string(offset);
    result.fields["matched_count"] = std::to_string(total_matches);
    result.fields["returned_count"] = std::to_string(emitted);
    result.fields["has_more"] = (offset + emitted < total_matches) ? "true" : "false";
    result.fields["next_offset"] = std::to_string(offset + emitted);
    result.fields["matches_jsonl"] = matches.str();
    result.fields["include_value"] = include_value ? "true" : "false";
    result.fields["value_ref"] = first_value_ref;
    result.fields["value_text"] = first_value;
    result.fields["semantic_outcome"] = total_matches > 0 ? "task_memory_kv_lookup_hit" : "task_memory_kv_lookup_miss";
    result.fields["next_action"] = (offset + emitted < total_matches)
        ? "repeat lan_agent_task_memory_kv_lookup with next_offset"
        : "use matches_jsonl value_ref to inspect source evidence";
    result.fields["result_ref"] = index_path.string();
    result.fields["evidence_ref"] = index_path.string();
    return result;
}

CommandResult BuildTaskMemoryRocksDbMirrorResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildTaskMemoryRocksDbLookupResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildTaskMemoryRocksDbParityCheckResult(
    const AgentConfig & config,
    const JsonRequestView & params);

inline CommandResult BuildTaskMemoryMigrationAssessResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path migration_dir = root / "rag_thread_migration";
    const std::filesystem::path resume_path = root / "latest_resume_context.json";
    const std::filesystem::path ledger_path = root / "step_ledger.jsonl";
    const std::filesystem::path slices_path = root / "slices.jsonl";
    const std::filesystem::path task_memory_path = root / "task_memory.json";
    const std::filesystem::path index_manifest_path = root / "index_manifest.json";
    const std::filesystem::path kv_index_path = root / "kv_snapshot" / "index.jsonl";
    const std::filesystem::path kv_manifest_path = root / "kv_snapshot" / "manifest.json";

    const std::vector<std::pair<std::string, std::filesystem::path>> file_object_files = {
        {"task_memory", task_memory_path},
        {"step_ledger", ledger_path},
        {"slices", slices_path},
        {"index_manifest", index_manifest_path},
        {"latest_resume_context", resume_path}
    };
    const std::vector<std::pair<std::string, std::filesystem::path>> migration_files = {
        {"current_state", migration_dir / "1_current_state.md"},
        {"key_slices", migration_dir / "2_key_slices.jsonl"},
        {"incremental_index_manifest", migration_dir / "3_incremental_index_manifest.json"},
        {"migration_handover", migration_dir / "4_migration_handover.md"}
    };

    int ready_file_count = 0;
    std::vector<std::string> missing_file_objects;
    for (const auto & item : file_object_files) {
        if (TaskMemoryFileExistsNonEmpty(item.second)) {
            ++ready_file_count;
        } else {
            missing_file_objects.push_back(item.first);
        }
    }

    int ready_migration_count = 0;
    std::vector<std::string> missing_migration_files;
    for (const auto & item : migration_files) {
        if (TaskMemoryFileExistsNonEmpty(item.second)) {
            ++ready_migration_count;
        } else {
            missing_migration_files.push_back(item.first);
        }
    }

    const bool file_object_ready = missing_file_objects.empty();
    const bool migration_bundle_ready = missing_migration_files.empty();
    const bool kv_snapshot_ready =
        TaskMemoryFileExistsNonEmpty(kv_index_path) &&
        TaskMemoryFileExistsNonEmpty(kv_manifest_path);
    const std::string kv_manifest = ReadTaskMemoryTextFile(kv_manifest_path);
    const int kv_record_count = TaskMemoryExtractIntField(kv_manifest, "record_count", 0);
    const int step_record_count = TaskMemoryExtractIntField(kv_manifest, "step_record_count", 0);
    const int slice_record_count = TaskMemoryExtractIntField(kv_manifest, "slice_record_count", 0);
    const int budget_record_count = TaskMemoryExtractIntField(kv_manifest, "budget_record_count", 0);
    const bool kv_contract_ready = kv_snapshot_ready && kv_record_count > 0;
    const bool rocksdb_native_ready = TaskMemoryRocksDbManifestReady(config, goal_id);
    const std::filesystem::path rocksdb_manifest_path = TaskMemoryRocksDbManifestPath(config, goal_id);
    const std::string rocksdb_manifest = ReadTaskMemoryTextFile(rocksdb_manifest_path);
    const std::string rocksdb_path = ExtractJsonString(rocksdb_manifest, "rocksdb_path");
    const int rocksdb_mirrored_count = TaskMemoryExtractIntField(rocksdb_manifest, "mirrored_count", 0);
    const bool safe_to_enable_rocksdb_adapter = file_object_ready && migration_bundle_ready && kv_contract_ready;
    const bool safe_to_replace_source_of_truth = false;

    std::string adaptation_decision;
    std::string next_action;
    if (!std::filesystem::exists(root)) {
        adaptation_decision = "BLOCKED_TASK_MEMORY_ROOT_MISSING";
        next_action = "call lan_agent_task_memory_freeze first";
    } else if (!file_object_ready || !migration_bundle_ready) {
        adaptation_decision = "BLOCKED_FILE_OBJECT_LAYER_INCOMPLETE";
        next_action = "repair missing task_memory files or rerun lan_agent_task_memory_freeze";
    } else if (!kv_contract_ready) {
        adaptation_decision = "BLOCKED_KV_SNAPSHOT_MISSING_OR_EMPTY";
        next_action = "call lan_agent_task_memory_build_kv_snapshot";
    } else if (rocksdb_native_ready) {
        adaptation_decision = "ROCKSDB_NATIVE_MIRROR_READY";
        next_action = "use lan_agent_task_memory_rocksdb_lookup for high-frequency reads and parity-check critical keys; keep file_object_store as source of truth";
    } else {
        adaptation_decision = "READY_FOR_ROCKSDB_ADAPTER_IMPLEMENTATION";
        next_action = "call lan_agent_task_memory_rocksdb_mirror to populate the optional native mirror; keep file object store as source of truth";
    }

    const std::string active_backend = rocksdb_native_ready
        ? "rocksdb_native_mirror"
        : (kv_contract_ready ? "file_jsonl_snapshot" : "file_object_store");
    const std::string resume_context = ReadTaskMemoryTextFile(resume_path);
    result.fields["record_model"] = "mcp_task_memory_migration_assessment_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["task_memory_root"] = root.string();
    result.fields["migration_stage"] = "stage4_pre_rocksdb_adapter_assessment";
    result.fields["adaptation_decision"] = adaptation_decision;
    result.fields["active_backend"] = active_backend;
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["backend_order"] = "[\"file_object_store\",\"kv_snapshot\",\"rocksdb_native\"]";
    result.fields["file_object_ready"] = file_object_ready ? "true" : "false";
    result.fields["migration_bundle_ready"] = migration_bundle_ready ? "true" : "false";
    result.fields["kv_snapshot_ready"] = kv_snapshot_ready ? "true" : "false";
    result.fields["kv_contract_ready"] = kv_contract_ready ? "true" : "false";
    result.fields["rocksdb_native_ready"] = rocksdb_native_ready ? "true" : "false";
    result.fields["rocksdb_status"] = rocksdb_native_ready ? "enabled" : "deferred_optional_backend";
    result.fields["rocksdb_path"] = rocksdb_path;
    result.fields["rocksdb_manifest_path"] = rocksdb_manifest_path.string();
    result.fields["rocksdb_mirrored_count"] = std::to_string(rocksdb_mirrored_count);
    result.fields["safe_to_enable_rocksdb_adapter"] = safe_to_enable_rocksdb_adapter ? "true" : "false";
    result.fields["safe_to_replace_source_of_truth"] = safe_to_replace_source_of_truth ? "true" : "false";
    result.fields["ready_file_count"] = std::to_string(ready_file_count);
    result.fields["required_file_count"] = std::to_string(static_cast<int>(file_object_files.size()));
    result.fields["ready_migration_file_count"] = std::to_string(ready_migration_count);
    result.fields["required_migration_file_count"] = std::to_string(static_cast<int>(migration_files.size()));
    result.fields["missing_file_objects_csv"] = TaskMemoryJoinCsv(missing_file_objects);
    result.fields["missing_migration_files_csv"] = TaskMemoryJoinCsv(missing_migration_files);
    result.fields["kv_record_count"] = std::to_string(kv_record_count);
    result.fields["step_record_count"] = std::to_string(step_record_count);
    result.fields["slice_record_count"] = std::to_string(slice_record_count);
    result.fields["budget_record_count"] = std::to_string(budget_record_count);
    result.fields["resume_context_path"] = resume_path.string();
    result.fields["step_ledger_path"] = ledger_path.string();
    result.fields["slices_path"] = slices_path.string();
    result.fields["index_manifest_path"] = index_manifest_path.string();
    result.fields["kv_index_path"] = kv_index_path.string();
    result.fields["kv_manifest_path"] = kv_manifest_path.string();
    result.fields["terminal_state"] = ExtractJsonRawValue(resume_context, "terminal_state");
    result.fields["completion_claim_allowed"] = ExtractJsonRawValue(resume_context, "completion_claim_allowed");
    result.fields["semantic_outcome"] = safe_to_enable_rocksdb_adapter
        ? "task_memory_ready_for_rocksdb_adapter"
        : "task_memory_migration_assessment_blocked";
    result.fields["next_action"] = next_action;
    result.fields["result_ref"] = kv_manifest_path.string();
    result.fields["evidence_ref"] = root.string();
    return result;
}

inline CommandResult BuildTaskMemoryStructureManifestResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    if (!std::filesystem::exists(root)) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "task memory root not found";
        result.fields["task_memory_root"] = root.string();
        result.fields["next_action"] = "call lan_agent_task_memory_freeze first";
        return result;
    }

    const std::filesystem::path migration_dir = root / "rag_thread_migration";
    const std::filesystem::path evidence_dir = root / "evidence_refs";
    const std::filesystem::path budget_dir = root / "budget_runs";
    const std::filesystem::path kv_dir = root / "kv_snapshot";
    const std::filesystem::path structure_path = root / "memory_structure.json";
    const std::filesystem::path rocksdb_manifest_path = TaskMemoryRocksDbManifestPath(config, goal_id);
    const std::filesystem::path resume_path = root / "latest_resume_context.json";
    const std::filesystem::path ledger_path = root / "step_ledger.jsonl";
    const std::filesystem::path slices_path = root / "slices.jsonl";
    const std::filesystem::path task_memory_path = root / "task_memory.json";
    const std::filesystem::path index_manifest_path = root / "index_manifest.json";
    const std::filesystem::path kv_index_path = kv_dir / "index.jsonl";
    const std::filesystem::path kv_manifest_path = kv_dir / "manifest.json";

    std::error_code ec;
    std::filesystem::create_directories(evidence_dir, ec);
    if (!ec) {
        std::filesystem::create_directories(budget_dir, ec);
    }
    if (!ec) {
        std::filesystem::create_directories(kv_dir, ec);
    }
    if (ec) {
        result.ok = false;
        result.exit_code = 501;
        result.fields["error"] = "failed to create task memory structure directories: " + ec.message();
        return result;
    }

    const bool task_memory_ready = TaskMemoryFileExistsNonEmpty(task_memory_path);
    const bool ledger_ready = TaskMemoryFileExistsNonEmpty(ledger_path);
    const bool slices_ready = TaskMemoryFileExistsNonEmpty(slices_path);
    const bool index_manifest_ready = TaskMemoryFileExistsNonEmpty(index_manifest_path);
    const bool resume_ready = TaskMemoryFileExistsNonEmpty(resume_path);
    const bool kv_ready = TaskMemoryFileExistsNonEmpty(kv_index_path) && TaskMemoryFileExistsNonEmpty(kv_manifest_path);
    const bool rocksdb_ready = TaskMemoryRocksDbManifestReady(config, goal_id);
    const bool migration_ready =
        TaskMemoryFileExistsNonEmpty(migration_dir / "1_current_state.md") &&
        TaskMemoryFileExistsNonEmpty(migration_dir / "2_key_slices.jsonl") &&
        TaskMemoryFileExistsNonEmpty(migration_dir / "3_incremental_index_manifest.json") &&
        TaskMemoryFileExistsNonEmpty(migration_dir / "4_migration_handover.md");
    const bool structure_ready =
        task_memory_ready && ledger_ready && slices_ready && index_manifest_ready && resume_ready && migration_ready;

    const std::string resume_context = ReadTaskMemoryTextFile(resume_path);
    const std::string kv_manifest = ReadTaskMemoryTextFile(kv_manifest_path);
    const std::string rocksdb_manifest = ReadTaskMemoryTextFile(rocksdb_manifest_path);
    const int step_count = TaskMemoryCountJsonlRecords(ledger_path);
    const int slice_count = TaskMemoryCountJsonlRecords(slices_path);
    const int budget_file_count = TaskMemoryCountRegularFiles(budget_dir);
    const int evidence_file_count = TaskMemoryCountRegularFiles(evidence_dir);
    const int kv_record_count = TaskMemoryExtractIntField(kv_manifest, "record_count", 0);
    const int rocksdb_mirrored_count = TaskMemoryExtractIntField(rocksdb_manifest, "mirrored_count", 0);
    const std::string active_read_backend = rocksdb_ready
        ? "rocksdb_native_mirror"
        : (kv_ready ? "kv_snapshot" : "file_object_store");
    const std::string read_backend_order = rocksdb_ready
        ? "[\"rocksdb_native_mirror\",\"kv_snapshot\",\"file_object_store\"]"
        : (kv_ready ? "[\"kv_snapshot\",\"file_object_store\"]" : "[\"file_object_store\"]");
    const bool fresh_model_bootstrap_ready = structure_ready && resume_ready;
    const bool backend_policy_ready =
        structure_ready &&
        (active_read_backend == "file_object_store" || kv_ready || rocksdb_ready);

    std::ostringstream manifest;
    manifest
        << "{\n"
        << "  \"record_model\":\"mcp_task_memory_structure_manifest_v1\",\n"
        << "  \"structure_version\":\"stage5.memory_structure.v1\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"created_at\":\"" << JsonEscape(IsoTimestampNow()) << "\",\n"
        << "  \"source_of_truth\":\"file_object_store\",\n"
        << "  \"active_read_backend\":\"" << active_read_backend << "\",\n"
        << "  \"write_backend\":\"file_object_store\",\n"
        << "  \"native_backend_role\":\"mirror_read_backend\",\n"
        << "  \"read_backend_order\":" << read_backend_order << ",\n"
        << "  \"required_model_read\":\"latest_resume_context.json\",\n"
        << "  \"rocksdb_status\":\"" << (rocksdb_ready ? "enabled" : "deferred_optional_backend") << "\",\n"
        << "  \"structure_ready\":" << (structure_ready ? "true" : "false") << ",\n"
        << "  \"fresh_model_bootstrap_ready\":" << (fresh_model_bootstrap_ready ? "true" : "false") << ",\n"
        << "  \"backend_policy_ready\":" << (backend_policy_ready ? "true" : "false") << ",\n"
        << "  \"safe_to_replace_source_of_truth\":false,\n"
        << "  \"parity_required_for_native_reads\":true,\n"
        << "  \"kv_snapshot_ready\":" << (kv_ready ? "true" : "false") << ",\n"
        << "  \"rocksdb_native_ready\":" << (rocksdb_ready ? "true" : "false") << ",\n"
        << "  \"fresh_model_bootstrap\":{\n"
        << "    \"first_read\":\"latest_resume_context.json\",\n"
        << "    \"second_read\":\"memory_structure.json\",\n"
        << "    \"query_read\":\"" << (rocksdb_ready ? "lan_agent_task_memory_rocksdb_lookup" : (kv_ready ? "lan_agent_task_memory_kv_lookup" : "lan_agent_task_memory_resume_context")) << "\",\n"
        << "    \"evidence_read\":\"on_demand_only\",\n"
        << "    \"full_history_read\":\"forbidden_by_default\"\n"
        << "  },\n"
        << "  \"backend_policy\":{\n"
        << "    \"source_of_truth\":\"file_object_store\",\n"
        << "    \"write_backend\":\"file_object_store\",\n"
        << "    \"primary_read_backend\":\"" << active_read_backend << "\",\n"
        << "    \"native_backend_role\":\"mirror_read_backend\",\n"
        << "    \"safe_to_replace_source_of_truth\":false,\n"
        << "    \"parity_required_for_native_reads\":true\n"
        << "  },\n"
        << "  \"query_contract\":{\n"
        << "    \"key_schema\":[\"goal/{goal_id}\",\"latest/{goal_id}\",\"trace/{trace_id}/step/{step_id}\",\"slice/{slice_id}\",\"budget/{budget_run_id}\",\"trace/{trace_id}/budget/{budget_run_id}\"],\n"
        << "    \"file_lookup_tool\":\"lan_agent_task_memory_kv_lookup\",\n"
        << "    \"native_lookup_tool\":\"lan_agent_task_memory_rocksdb_lookup\",\n"
        << "    \"native_parity_tool\":\"lan_agent_task_memory_rocksdb_parity_check\"\n"
        << "  },\n"
        << "  \"paths\":{\n"
        << "    \"task_memory_root\":\"" << JsonEscape(root.string()) << "\",\n"
        << "    \"memory_structure\":\"" << JsonEscape(structure_path.string()) << "\",\n"
        << "    \"task_memory\":\"" << JsonEscape(task_memory_path.string()) << "\",\n"
        << "    \"step_ledger\":\"" << JsonEscape(ledger_path.string()) << "\",\n"
        << "    \"slices\":\"" << JsonEscape(slices_path.string()) << "\",\n"
        << "    \"index_manifest\":\"" << JsonEscape(index_manifest_path.string()) << "\",\n"
        << "    \"latest_resume_context\":\"" << JsonEscape(resume_path.string()) << "\",\n"
        << "    \"evidence_refs\":\"" << JsonEscape(evidence_dir.string()) << "\",\n"
        << "    \"budget_runs\":\"" << JsonEscape(budget_dir.string()) << "\",\n"
        << "    \"kv_snapshot\":\"" << JsonEscape(kv_dir.string()) << "\",\n"
        << "    \"kv_index\":\"" << JsonEscape(kv_index_path.string()) << "\",\n"
        << "    \"kv_manifest\":\"" << JsonEscape(kv_manifest_path.string()) << "\",\n"
        << "    \"rocksdb_manifest\":\"" << JsonEscape(rocksdb_manifest_path.string()) << "\",\n"
        << "    \"rocksdb_path\":\"" << JsonEscape(ExtractJsonString(rocksdb_manifest, "rocksdb_path")) << "\",\n"
        << "    \"rag_thread_migration\":\"" << JsonEscape(migration_dir.string()) << "\"\n"
        << "  },\n"
        << "  \"counts\":{\n"
        << "    \"step_count\":" << step_count << ",\n"
        << "    \"slice_count\":" << slice_count << ",\n"
        << "    \"budget_file_count\":" << budget_file_count << ",\n"
        << "    \"evidence_file_count\":" << evidence_file_count << ",\n"
        << "    \"kv_record_count\":" << kv_record_count << ",\n"
        << "    \"rocksdb_mirrored_count\":" << rocksdb_mirrored_count << "\n"
        << "  },\n"
        << "  \"resume\":{\n"
        << "    \"terminal_state\":" << (TaskMemoryExtractBoolField(resume_context, "terminal_state", false) ? "true" : "false") << ",\n"
        << "    \"completion_claim_allowed\":" << (TaskMemoryExtractBoolField(resume_context, "completion_claim_allowed", false) ? "true" : "false") << ",\n"
        << "    \"last_verified_step\":" << TaskMemoryExtractIntField(resume_context, "last_verified_step", 0) << ",\n"
        << "    \"next_call_json_present\":" << (!Trim(ExtractJsonString(resume_context, "next_call_json")).empty() ? "true" : "false") << "\n"
        << "  }\n"
        << "}\n";

    std::string error;
    if (!WriteTaskMemoryTextFile(structure_path, manifest.str(), &error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["error"] = error;
        result.fields["failed_path"] = structure_path.string();
        return result;
    }

    result.fields["record_model"] = "mcp_task_memory_structure_manifest_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["structure_version"] = "stage5.memory_structure.v1";
    result.fields["task_memory_root"] = root.string();
    result.fields["memory_structure_path"] = structure_path.string();
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["active_read_backend"] = active_read_backend;
    result.fields["write_backend"] = "file_object_store";
    result.fields["native_backend_role"] = "mirror_read_backend";
    result.fields["read_backend_order"] = read_backend_order;
    result.fields["required_model_read"] = "latest_resume_context.json";
    result.fields["structure_ready"] = structure_ready ? "true" : "false";
    result.fields["fresh_model_bootstrap_ready"] = fresh_model_bootstrap_ready ? "true" : "false";
    result.fields["backend_policy_ready"] = backend_policy_ready ? "true" : "false";
    result.fields["safe_to_replace_source_of_truth"] = "false";
    result.fields["parity_required_for_native_reads"] = "true";
    result.fields["migration_bundle_ready"] = migration_ready ? "true" : "false";
    result.fields["kv_snapshot_ready"] = kv_ready ? "true" : "false";
    result.fields["rocksdb_status"] = rocksdb_ready ? "enabled" : "deferred_optional_backend";
    result.fields["rocksdb_native_ready"] = rocksdb_ready ? "true" : "false";
    result.fields["rocksdb_manifest_path"] = rocksdb_manifest_path.string();
    result.fields["rocksdb_path"] = ExtractJsonString(rocksdb_manifest, "rocksdb_path");
    result.fields["task_memory_path"] = task_memory_path.string();
    result.fields["step_ledger_path"] = ledger_path.string();
    result.fields["slices_path"] = slices_path.string();
    result.fields["index_manifest_path"] = index_manifest_path.string();
    result.fields["resume_context_path"] = resume_path.string();
    result.fields["evidence_refs_dir"] = evidence_dir.string();
    result.fields["budget_runs_dir"] = budget_dir.string();
    result.fields["kv_snapshot_dir"] = kv_dir.string();
    result.fields["kv_index_path"] = kv_index_path.string();
    result.fields["kv_manifest_path"] = kv_manifest_path.string();
    result.fields["step_count"] = std::to_string(step_count);
    result.fields["slice_count"] = std::to_string(slice_count);
    result.fields["budget_file_count"] = std::to_string(budget_file_count);
    result.fields["evidence_file_count"] = std::to_string(evidence_file_count);
    result.fields["kv_record_count"] = std::to_string(kv_record_count);
    result.fields["rocksdb_mirrored_count"] = std::to_string(rocksdb_mirrored_count);
    result.fields["terminal_state"] = ExtractJsonRawValue(resume_context, "terminal_state");
    result.fields["completion_claim_allowed"] = ExtractJsonRawValue(resume_context, "completion_claim_allowed");
    result.fields["semantic_outcome"] = structure_ready
        ? "task_memory_structure_manifest_ready"
        : "task_memory_structure_manifest_incomplete";
    result.fields["next_action"] = structure_ready
        ? "fresh models should read latest_resume_context.json first, then use memory_structure.json and native/file KV lookup only as needed"
        : "repair missing task memory artifacts before relying on this goal";
    result.fields["result_ref"] = structure_path.string();
    result.fields["evidence_ref"] = structure_path.string();
    return result;
}

inline CommandResult BuildTaskMemoryResumeContextResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }
    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path resume_path = root / "latest_resume_context.json";
    const std::string resume_context = ReadTaskMemoryTextFile(resume_path);
    if (resume_context.empty()) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "resume context not found";
        result.fields["resume_context_path"] = resume_path.string();
        result.fields["next_action"] = "call lan_agent_task_memory_freeze first";
        return result;
    }

    result.fields["record_model"] = "mcp_task_memory_resume_context_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["task_memory_root"] = root.string();
    result.fields["resume_context_path"] = resume_path.string();
    result.fields["resume_context"] = resume_context;
    result.fields["step_ledger_path"] = (root / "step_ledger.jsonl").string();
    result.fields["slices_path"] = (root / "slices.jsonl").string();
    result.fields["index_manifest_path"] = (root / "index_manifest.json").string();
    result.fields["migration_handover_path"] = (root / "rag_thread_migration" / "4_migration_handover.md").string();
    result.fields["terminal_state"] = ExtractJsonRawValue(resume_context, "terminal_state");
    result.fields["completion_claim_allowed"] = ExtractJsonRawValue(resume_context, "completion_claim_allowed");
    result.fields["final_answer_allowed"] = ExtractJsonRawValue(resume_context, "final_answer_allowed");
    result.fields["verification_ok"] = ExtractJsonRawValue(resume_context, "verification_ok");
    result.fields["clean_chat_close_allowed"] = ExtractJsonRawValue(resume_context, "clean_chat_close_allowed");
    result.fields["conversation_close_allowed"] = ExtractJsonRawValue(resume_context, "conversation_close_allowed");
    result.fields["conversation_close_status"] = ExtractJsonString(resume_context, "conversation_close_status");
    result.fields["handoff_completion_claim"] = ExtractJsonString(resume_context, "handoff_completion_claim");
    result.fields["next_chat_status_check_required"] = ExtractJsonRawValue(resume_context, "next_chat_status_check_required");
    result.fields["next_chat_status_check_tool_name"] = ExtractJsonString(resume_context, "next_chat_status_check_tool_name");
    result.fields["next_chat_status_check_arguments_json"] = ExtractJsonString(resume_context, "next_chat_status_check_arguments_json");
    result.fields["next_chat_must_verify_fields_json"] = ExtractJsonString(resume_context, "next_chat_must_verify_fields_json");
    result.fields["new_chat_entry_tool_name"] = ExtractJsonString(resume_context, "new_chat_entry_tool_name");
    result.fields["new_chat_entry_arguments_json"] = ExtractJsonString(resume_context, "new_chat_entry_arguments_json");
    result.fields["project_flow_role"] = "mcp_project_step_ledger_and_state_machine";
    result.fields["single_round_flow"] = "read_state,run_one_bounded_slice,write_step_record,summarize,next_round_entry";
    result.fields["slice_execution_policy"] = "one_chat_one_bounded_slice_then_record_state";
    result.fields["model_context_policy"] = "use resume state and refs only; do not reload full historical chat";
    result.fields["current_tool"] = ExtractJsonString(resume_context, "current_tool");
    result.fields["next_call_json"] = ExtractJsonString(resume_context, "next_call_json");
    result.fields["compact_summary"] = ExtractJsonString(resume_context, "compact_summary");
    result.fields["remaining_work"] = ExtractJsonString(resume_context, "remaining_work");
    result.fields["next_allowed_action"] = ExtractJsonString(resume_context, "next_allowed_action");
    result.fields["semantic_outcome"] = "task_memory_resume_context_ready";
    result.fields["next_action"] = "use resume_context only; do not reload full historical chat unless evidence is missing";
    result.fields["result_ref"] = resume_path.string();
    result.fields["evidence_ref"] = (root / "step_ledger.jsonl").string();
    return result;
}

}  // namespace codex_lan_agent
