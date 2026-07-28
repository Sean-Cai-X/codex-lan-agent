#pragma once

std::string StableContentChecksum(const std::string & content);
std::vector<std::string> SplitLinesPreserveText(const std::string & text);

constexpr std::size_t kInlineDiffByteLimit = 128 * 1024;

CommandResult RevertSingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & backup_path,
    const std::string & request_id = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & patch_id = std::string(),
    const std::string & reason = std::string());

std::string BuildMcpTraceAuditEventsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "mcp_trace_audit_events.jsonl");
}

std::string BuildPatchAuditEventJson(
    const std::string & event_id,
    const PatchRequest & request,
    const std::string & stage,
    const std::string & actor,
    const std::string & decision,
    const std::string & diff_hash,
    const std::string & log_ref,
    const std::string & backup_path,
    const std::string & status,
    const std::string & detail) {
    std::ostringstream output;
    output << "{"
           << "\"event_id\":\"" << codex_lan_agent::JsonEscape(event_id) << "\","
           << "\"request_id\":\"" << codex_lan_agent::JsonEscape(request.request_id) << "\","
           << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(request.trace_id) << "\","
           << "\"patch_id\":\"" << codex_lan_agent::JsonEscape(request.patch_id) << "\","
           << "\"file_path\":\"" << codex_lan_agent::JsonEscape(request.file_path) << "\","
           << "\"stage\":\"" << codex_lan_agent::JsonEscape(stage) << "\","
           << "\"actor\":\"" << codex_lan_agent::JsonEscape(actor) << "\","
           << "\"decision\":\"" << codex_lan_agent::JsonEscape(decision) << "\","
           << "\"status\":\"" << codex_lan_agent::JsonEscape(status) << "\","
           << "\"diff_hash\":\"" << codex_lan_agent::JsonEscape(diff_hash) << "\","
           << "\"backup_path\":\"" << codex_lan_agent::JsonEscape(backup_path) << "\","
           << "\"log_ref\":\"" << codex_lan_agent::JsonEscape(log_ref) << "\","
           << "\"detail\":\"" << codex_lan_agent::JsonEscape(detail) << "\","
           << "\"recorded_at\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\""
           << "}";
    return output.str();
}

void AppendPatchAuditEvent(
    const AgentConfig & config,
    const PatchRequest & request,
    const std::string & stage,
    const std::string & decision,
    const std::string & diff_hash,
    const std::string & log_ref,
    const std::string & backup_path,
    const std::string & status,
    const std::string & detail) {
    std::filesystem::create_directories(config.log_root);
    std::ofstream output(BuildPatchAuditEventsPath(config), std::ios::out | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    const std::string event_id =
        "patch-event-" + request.patch_id + "-" + SanitizeDispatchToken(stage, "stage") + "-" + BuildRequestTimestampToken();
    output << BuildPatchAuditEventJson(
        event_id,
        request,
        stage,
        "PatchOperations",
        decision,
        diff_hash,
        log_ref,
        backup_path,
        status,
        detail) << "\n";
}

std::string AuditField(
    const CommandResult & result,
    const std::string & key) {
    return GetFieldOrDefault(result, key, "");
}

std::string BuildMcpTraceAuditEventJson(
    const std::string & event_id,
    const std::string & tool_name,
    const CommandResult & result) {
    std::ostringstream output;
    output << "{"
           << "\"event_id\":\"" << codex_lan_agent::JsonEscape(event_id) << "\","
           << "\"source\":\"mcp_tool_result\","
           << "\"stage\":\"MCP_TOOL_RESULT\","
           << "\"tool_name\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
           << "\"request_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "request_id")) << "\","
           << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "trace_id")) << "\","
           << "\"tool_call_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "tool_call_id")) << "\","
           << "\"provider_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "provider_id")) << "\","
           << "\"capability_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "capability_id")) << "\","
           << "\"task_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "task_id")) << "\","
           << "\"patch_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "patch_id")) << "\","
           << "\"file_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "file_path")) << "\","
           << "\"normalized_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "normalized_path")) << "\","
           << "\"directory_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "directory_path")) << "\","
           << "\"status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "status")) << "\","
           << "\"ok\":\"" << (result.ok ? "true" : "false") << "\","
           << "\"exit_code\":\"" << result.exit_code << "\","
           << "\"result\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "result")) << "\","
           << "\"summary\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "summary")) << "\","
           << "\"error\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "error")) << "\","
           << "\"error_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "error_code")) << "\","
           << "\"error_message\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "error_message")) << "\","
           << "\"verification_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "verification_status")) << "\","
           << "\"read_complete\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "read_complete")) << "\","
           << "\"task_completion\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "task_completion")) << "\","
           << "\"analysis_allowed\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "analysis_allowed")) << "\","
           << "\"batch_completion\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "batch_completion")) << "\","
           << "\"known_file_list_complete\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "known_file_list_complete")) << "\","
           << "\"directory_listing_complete\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "directory_listing_complete")) << "\","
           << "\"batch_manifest_complete\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "batch_manifest_complete")) << "\","
           << "\"content_read_completion\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "content_read_completion")) << "\","
           << "\"incomplete_scope\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "incomplete_scope")) << "\","
           << "\"file_extensions_csv\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "file_extensions_csv")) << "\","
           << "\"current_file_index\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "current_file_index")) << "\","
           << "\"current_file_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "current_file_path")) << "\","
           << "\"batch_manifest_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "batch_manifest_path")) << "\","
           << "\"batch_total_files\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "batch_total_files")) << "\","
           << "\"batch_read_file_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "batch_read_file_count")) << "\","
           << "\"remaining_batch_file_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "remaining_batch_file_count")) << "\","
           << "\"next_batch_file_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_batch_file_path")) << "\","
           << "\"clips_pre_call_tool_decision\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "clips_pre_call_tool_decision")) << "\","
           << "\"clips_post_result_decision\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "clips_post_result_decision")) << "\","
           << "\"clips_post_result_verification\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "clips_post_result_verification")) << "\","
           << "\"pre_guard_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_status")) << "\","
           << "\"pre_guard_reason_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_reason_code")) << "\","
           << "\"pre_guard_next_action\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_next_action")) << "\","
           << "\"pre_guard_route_target\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_route_target")) << "\","
           << "\"pre_guard_blocked\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_blocked")) << "\","
           << "\"post_guard_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_status")) << "\","
           << "\"post_guard_decision\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_decision")) << "\","
           << "\"post_guard_reason_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_reason_code")) << "\","
           << "\"post_guard_next_action\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_next_action")) << "\","
           << "\"post_guard_result_valid\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_result_valid")) << "\","
           << "\"supervision_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "supervision_status")) << "\","
           << "\"goal_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "goal_status")) << "\","
           << "\"acceptance_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "acceptance_status")) << "\","
           << "\"acceptance_reason\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "acceptance_reason")) << "\","
           << "\"acceptance_next_action_available\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "acceptance_next_action_available")) << "\","
           << "\"supervision_alarm_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "supervision_alarm_code")) << "\","
           << "\"supervision_alarm_message\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "supervision_alarm_message")) << "\","
           << "\"progress_target_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_target_count")) << "\","
           << "\"progress_completed_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_completed_count")) << "\","
           << "\"progress_pending_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_pending_count")) << "\","
           << "\"progress_failed_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_failed_count")) << "\","
           << "\"progress_skipped_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_skipped_count")) << "\","
           << "\"next_actions_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_actions_count")) << "\","
           << "\"next_action_0_tool_name\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_tool_name")) << "\","
           << "\"next_action_0_safety_class\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_safety_class")) << "\","
           << "\"next_action_0_params_json\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_params_json")) << "\","
           << "\"next_action_0_reason\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_reason")) << "\","
           << "\"next_action_0_source_rule\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_source_rule")) << "\","
           << "\"next_action_0_trace_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_trace_id")) << "\","
           << "\"next_action_0_goal_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_goal_id")) << "\","
           << "\"next_action_0_params_hash\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_params_hash")) << "\","
           << "\"result_hash\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "result_hash")) << "\","
           << "\"task_log_ref\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "task_log_ref")) << "\","
           << "\"log_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "log_path")) << "\","
           << "\"resolved_log_path\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "resolved_log_path")) << "\","
           << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "result_ref")) << "\","
           << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "evidence_ref")) << "\","
           << "\"recorded_at\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\""
           << "}";
    return output.str();
}

std::string BuildMcpSupervisionAlarmEventJson(
    const std::string & event_id,
    const std::string & tool_name,
    const CommandResult & result) {
    std::ostringstream output;
    output << "{"
           << "\"event_id\":\"" << codex_lan_agent::JsonEscape(event_id) << "\","
           << "\"source\":\"supervision_alarm\","
           << "\"stage\":\"SUPERVISION_ALARM\","
           << "\"tool_name\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
           << "\"request_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "request_id")) << "\","
           << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "trace_id")) << "\","
           << "\"goal_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "goal_id")) << "\","
           << "\"tool_call_id\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "tool_call_id")) << "\","
           << "\"supervision_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "supervision_status")) << "\","
           << "\"goal_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "goal_status")) << "\","
           << "\"pre_guard_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_status")) << "\","
           << "\"pre_guard_reason_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_reason_code")) << "\","
           << "\"pre_guard_next_action\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_next_action")) << "\","
           << "\"pre_guard_route_target\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_route_target")) << "\","
           << "\"pre_guard_blocked\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "pre_guard_blocked")) << "\","
           << "\"post_guard_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_status")) << "\","
           << "\"post_guard_decision\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_decision")) << "\","
           << "\"post_guard_reason_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_reason_code")) << "\","
           << "\"post_guard_next_action\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_next_action")) << "\","
           << "\"post_guard_result_valid\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "post_guard_result_valid")) << "\","
           << "\"acceptance_status\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "acceptance_status")) << "\","
           << "\"acceptance_reason\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "acceptance_reason")) << "\","
           << "\"acceptance_next_action_available\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "acceptance_next_action_available")) << "\","
           << "\"alarm_code\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "supervision_alarm_code")) << "\","
           << "\"alarm_message\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "supervision_alarm_message")) << "\","
           << "\"next_actions_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_actions_count")) << "\","
           << "\"next_action_0_tool_name\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_tool_name")) << "\","
           << "\"next_action_0_safety_class\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_safety_class")) << "\","
           << "\"next_action_0_params_json\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "next_action_0_params_json")) << "\","
           << "\"progress_target_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_target_count")) << "\","
           << "\"progress_completed_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_completed_count")) << "\","
           << "\"progress_pending_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_pending_count")) << "\","
           << "\"progress_failed_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_failed_count")) << "\","
           << "\"progress_skipped_count\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "progress_skipped_count")) << "\","
           << "\"result_hash\":\"" << codex_lan_agent::JsonEscape(AuditField(result, "result_hash")) << "\","
           << "\"recorded_at\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\""
           << "}";
    return output.str();
}

void AppendMcpTraceAuditEvent(
    const AgentConfig & config,
    const std::string & tool_name,
    const CommandResult & result) {
    const std::string trace_id = AuditField(result, "trace_id");
    if (trace_id.empty()) {
        return;
    }
    std::filesystem::create_directories(config.log_root);
    std::ofstream output(BuildMcpTraceAuditEventsPath(config), std::ios::out | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    const std::string tool_call_id = AuditField(result, "tool_call_id").empty()
        ? StableContentChecksum(tool_name + "|" + trace_id + "|" + IsoTimestampNow())
        : AuditField(result, "tool_call_id");
    const std::string event_id =
        "mcp-trace-" + StableContentChecksum(trace_id + "|" + tool_name + "|" + tool_call_id);
    output << BuildMcpTraceAuditEventJson(event_id, tool_name, result) << "\n";
}

void AppendMcpSupervisionAlarmEvent(
    const AgentConfig & config,
    const std::string & tool_name,
    const CommandResult & result) {
    const std::string trace_id = AuditField(result, "trace_id");
    const std::string alarm_code = AuditField(result, "supervision_alarm_code");
    if (trace_id.empty() || alarm_code.empty()) {
        return;
    }
    std::filesystem::create_directories(config.log_root);
    std::ofstream output(BuildMcpTraceAuditEventsPath(config), std::ios::out | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    const std::string tool_call_id = AuditField(result, "tool_call_id").empty()
        ? StableContentChecksum(tool_name + "|" + trace_id + "|" + alarm_code + "|" + IsoTimestampNow())
        : AuditField(result, "tool_call_id");
    const std::string event_id =
        "mcp-alarm-" + StableContentChecksum(trace_id + "|" + tool_name + "|" + tool_call_id + "|" + alarm_code);
    output << BuildMcpSupervisionAlarmEventJson(event_id, tool_name, result) << "\n";
}

PatchRequest BuildPatchRequest(
    const std::string & file_path,
    const std::string & new_content,
    const std::string & old_hash,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & patch_id,
    const std::string & reason) {
    PatchRequest request;
    request.file_path = file_path;
    request.new_content = new_content;
    request.old_hash = old_hash;
    request.request_id = request_id.empty()
        ? "patch-request-" + BuildRequestTimestampToken()
        : request_id;
    request.trace_id = trace_id.empty()
        ? "patch-trace-" + SanitizeDispatchToken(file_path, "file") + "-" + BuildRequestTimestampToken()
        : trace_id;
    request.patch_id = patch_id.empty()
        ? "patch-" + SanitizeDispatchToken(file_path, "file") + "-" + BuildRequestTimestampToken()
        : patch_id;
    request.reason = reason.empty() ? "single_file_patch" : reason;
    return request;
}

void PopulatePatchResultFields(
    CommandResult * result,
    const PatchRequest & request,
    const std::string & normalized_path,
    const std::string & old_content,
    const std::string & new_content,
    const std::string & diff_text,
    const std::string & patch_audit_id) {
    if (result == nullptr) {
        return;
    }
    result->fields["request_id"] = request.request_id;
    result->fields["trace_id"] = request.trace_id;
    result->fields["patch_id"] = request.patch_id;
    result->fields["file_path"] = request.file_path;
    result->fields["patch_reason"] = request.reason;
    result->fields["normalized_path"] = normalized_path;
    result->fields["old_hash"] = StableContentChecksum(old_content);
    result->fields["new_hash"] = StableContentChecksum(new_content);
    result->fields["diff_hash"] = StableContentChecksum(diff_text);
    result->fields["patch_audit_id"] = patch_audit_id;
}

std::string BuildPatchBackupPath(const AgentConfig & config) {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    const std::string unique_token = StableContentChecksum(std::to_string(now)).substr(0, 8);
    return codex_lan_agent::JoinPath(
        config.log_root,
        "single_file_patch_backup_" + TimeStampForFileName() + "_" + unique_token + ".bak");
}

std::string JoinLinesWithNewlines(const std::vector<std::string> & lines) {
    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            output << '\n';
        }
        output << lines[index];
    }
    return output.str();
}

bool IsIgnorableUnifiedDiffLine(const std::string & line) {
    return line.rfind("diff --git ", 0) == 0
        || line.rfind("index ", 0) == 0
        || line.rfind("--- ", 0) == 0
        || line.rfind("+++ ", 0) == 0
        || line.rfind("old mode ", 0) == 0
        || line.rfind("new mode ", 0) == 0
        || line.rfind("deleted file mode ", 0) == 0
        || line.rfind("new file mode ", 0) == 0
        || line.rfind("similarity index ", 0) == 0
        || line.rfind("rename from ", 0) == 0
        || line.rfind("rename to ", 0) == 0
        || line.rfind("copy from ", 0) == 0
        || line.rfind("copy to ", 0) == 0
        || line == "```"
        || line == "```diff"
        || line == "```patch"
        || line == "content_begin<<<"
        || line == ">>>content_end";
}

struct UnifiedDiffHeaderPaths {
    std::string old_path;
    std::string new_path;
};

std::string NormalizeUnifiedDiffHeaderPath(std::string path_text) {
    path_text = Trim(path_text);
    if (path_text.empty()) {
        return std::string();
    }
    if (path_text.front() == '"' && path_text.size() > 1 && path_text.back() == '"') {
        path_text = path_text.substr(1, path_text.size() - 2);
    }
    if (path_text == "/dev/null") {
        return std::string();
    }
    if (path_text.size() > 2 && path_text[1] == '/' &&
        (path_text[0] == 'a' || path_text[0] == 'b')) {
        path_text = path_text.substr(2);
    }
    std::replace(path_text.begin(), path_text.end(), '\\', '/');
    return path_text;
}

std::string NormalizePathForSuffixMatch(const std::filesystem::path & path_value) {
    std::string path_text = path_value.generic_string();
    path_text = ToLowerAscii(Trim(path_text));
    while (!path_text.empty() && path_text.front() == '/') {
        path_text.erase(path_text.begin());
    }
    return path_text;
}

bool EndsWithPathSuffix(const std::string & path_text, const std::string & suffix_text) {
    if (suffix_text.empty()) {
        return false;
    }
    if (path_text == suffix_text) {
        return true;
    }
    if (path_text.size() <= suffix_text.size()) {
        return false;
    }
    const std::size_t offset = path_text.size() - suffix_text.size();
    return path_text.compare(offset, suffix_text.size(), suffix_text) == 0
        && path_text[offset - 1] == '/';
}

std::string BuildJsonStringArray(const std::vector<std::string> & values) {
    std::ostringstream output;
    output << "[";
    bool first = true;
    for (const std::string & value : values) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << "\"" << codex_lan_agent::JsonEscape(value) << "\"";
    }
    output << "]";
    return output.str();
}

int CountUnifiedDiffChangedLines(const std::string & diff_text) {
    const std::vector<std::string> lines = SplitLinesPreserveText(diff_text);
    int count = 0;
    for (const std::string & line : lines) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '+' && line.rfind("+++ ", 0) != 0) {
            ++count;
            continue;
        }
        if (line[0] == '-' && line.rfind("--- ", 0) != 0) {
            ++count;
        }
    }
    return count;
}

bool ShouldOmitInlineDiff(
    const std::string & old_content,
    const std::string & new_content,
    const std::string & diff_text) {
    return old_content.size() + new_content.size() > kInlineDiffByteLimit
        || diff_text.size() > kInlineDiffByteLimit;
}

void CompactLargeInlineDiffResult(
    CommandResult * result,
    const std::string & old_content,
    const std::string & new_content,
    const std::string & diff_text) {
    if (result == nullptr || !ShouldOmitInlineDiff(old_content, new_content, diff_text)) {
        return;
    }
    result->fields["diff_inline_omitted"] = "true";
    result->fields["diff_inline_omitted_reason"] = "large_file_response_compaction";
    result->fields["diff_inline_byte_limit"] = std::to_string(kInlineDiffByteLimit);
    result->fields["diff_bytes"] = std::to_string(diff_text.size());
    result->fields["diff"] = "";
}

std::string BuildCompactDiffPlaceholder(
    const std::string & normalized_path,
    const std::string & old_content,
    const std::string & new_content) {
    std::ostringstream output;
    output << "diff omitted for large file\n"
           << "file_path=" << normalized_path << "\n"
           << "old_bytes=" << old_content.size() << "\n"
           << "new_bytes=" << new_content.size() << "\n"
           << "old_hash=" << StableContentChecksum(old_content) << "\n"
           << "new_hash=" << StableContentChecksum(new_content) << "\n";
    return output.str();
}

std::vector<std::string> FindWorkspaceWritablePathCandidates(
    const AgentConfig & config,
    const std::string & raw_path) {
    std::vector<std::string> matches;
    const std::filesystem::path requested_path(raw_path);
    if (!requested_path.is_relative()) {
        return matches;
    }

    const std::string requested_suffix = NormalizePathForSuffixMatch(requested_path);
    if (requested_suffix.empty()) {
        return matches;
    }

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        config.workspace_root,
        std::filesystem::directory_options::skip_permission_denied,
        iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            iterator_error.clear();
            continue;
        }
        const std::filesystem::directory_entry & entry = *iterator;
        if (!entry.is_regular_file(iterator_error)) {
            if (iterator_error) {
                iterator_error.clear();
            }
            continue;
        }

        std::error_code relative_error;
        const std::filesystem::path relative_path =
            std::filesystem::relative(entry.path(), config.workspace_root, relative_error);
        if (relative_error) {
            continue;
        }
        const std::string normalized_relative = NormalizePathForSuffixMatch(relative_path);
        if (!EndsWithPathSuffix(normalized_relative, requested_suffix)) {
            continue;
        }

        const std::string candidate_path = entry.path().lexically_normal().string();
        if (std::find(matches.begin(), matches.end(), candidate_path) == matches.end()) {
            matches.push_back(candidate_path);
        }
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

UnifiedDiffHeaderPaths ExtractUnifiedDiffHeaderPaths(const std::string & diff_text) {
    UnifiedDiffHeaderPaths paths;
    const std::vector<std::string> lines = SplitLinesPreserveText(diff_text);
    for (const std::string & line : lines) {
        if (line.rfind("--- ", 0) == 0 && paths.old_path.empty()) {
            paths.old_path = NormalizeUnifiedDiffHeaderPath(line.substr(4));
            continue;
        }
        if (line.rfind("+++ ", 0) == 0 && paths.new_path.empty()) {
            paths.new_path = NormalizeUnifiedDiffHeaderPath(line.substr(4));
            continue;
        }
        if (!paths.old_path.empty() && !paths.new_path.empty()) {
            break;
        }
    }
    return paths;
}

std::string NormalizePathForDiffTargetMatch(std::string path_text) {
    std::replace(path_text.begin(), path_text.end(), '\\', '/');
    while (path_text.find("//") != std::string::npos) {
        path_text.replace(path_text.find("//"), 2, "/");
    }
    path_text = ToLowerAscii(Trim(path_text));
    while (!path_text.empty() && path_text.front() == '/') {
        path_text.erase(path_text.begin());
    }
    return path_text;
}

bool PathsWeakMatchForDiffTarget(const std::string & left, const std::string & right) {
    const std::string normalized_left = NormalizePathForDiffTargetMatch(left);
    const std::string normalized_right = NormalizePathForDiffTargetMatch(right);
    if (normalized_left.empty() || normalized_right.empty()) {
        return false;
    }
    if (normalized_left == normalized_right) {
        return true;
    }
    const auto suffix_match = [](const std::string & full_path, const std::string & suffix_path) {
        return full_path.size() > suffix_path.size()
            && full_path.compare(full_path.size() - suffix_path.size(), suffix_path.size(), suffix_path) == 0
            && full_path[full_path.size() - suffix_path.size() - 1] == '/';
    };
    return suffix_match(normalized_left, normalized_right)
        || suffix_match(normalized_right, normalized_left);
}

bool DiffHeaderMatchesRequestedPath(
    const UnifiedDiffHeaderPaths & header_paths,
    const std::string & requested_path) {
    const bool has_old = !header_paths.old_path.empty();
    const bool has_new = !header_paths.new_path.empty();
    if (!has_old && !has_new) {
        return true;
    }
    return (has_old && PathsWeakMatchForDiffTarget(requested_path, header_paths.old_path))
        || (has_new && PathsWeakMatchForDiffTarget(requested_path, header_paths.new_path));
}

std::vector<std::string> ExtractUnifiedDiffTouchedPaths(const std::string & diff_text) {
    std::vector<std::string> paths;
    const std::vector<std::string> lines = SplitLinesPreserveText(diff_text);
    for (const std::string & line : lines) {
        std::string path_text;
        if (line.rfind("+++ ", 0) == 0) {
            path_text = NormalizeUnifiedDiffHeaderPath(line.substr(4));
        } else if (line.rfind("--- ", 0) == 0) {
            path_text = NormalizeUnifiedDiffHeaderPath(line.substr(4));
        } else {
            continue;
        }
        if (path_text.empty()) {
            continue;
        }
        if (std::find(paths.begin(), paths.end(), path_text) == paths.end()) {
            paths.push_back(path_text);
        }
    }
    return paths;
}

bool UnifiedDiffTouchesOnlyRequestedPath(
    const std::string & diff_text,
    const std::string & requested_path) {
    const std::vector<std::string> touched_paths = ExtractUnifiedDiffTouchedPaths(diff_text);
    if (touched_paths.empty()) {
        return true;
    }
    for (const std::string & path_text : touched_paths) {
        if (!PathsWeakMatchForDiffTarget(requested_path, path_text)) {
            return false;
        }
    }
    return true;
}

std::string QuoteProcessArgumentForPatchTool(const std::string & value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

bool WriteWholeFileBinary(
    const std::filesystem::path & path,
    const std::string & content,
    std::string * error_message) {
    std::error_code create_error;
    std::filesystem::create_directories(path.parent_path(), create_error);
    if (create_error) {
        if (error_message) {
            *error_message = "failed to create parent directory: " + create_error.message();
        }
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (error_message) {
            *error_message = "failed to open file for write";
        }
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output.good()) {
        if (error_message) {
            *error_message = "failed to write file content";
        }
        return false;
    }
    return true;
}

bool TryFindGitRepositoryRoot(
    const std::filesystem::path & target_file,
    std::filesystem::path * repo_root) {
    std::filesystem::path current = target_file.has_parent_path()
        ? target_file.parent_path()
        : target_file;
    current = current.lexically_normal();
    while (!current.empty()) {
        std::error_code exists_error;
        if (std::filesystem::exists(current / ".git", exists_error) && !exists_error) {
            *repo_root = current;
            return true;
        }
        const std::filesystem::path parent = current.parent_path();
        if (parent == current || parent.empty()) {
            break;
        }
        current = parent;
    }
    return false;
}

struct GitDiffPatchAttempt {
    bool attempted = false;
    bool completed = false;
    CommandResult result;
};

void CopyGitAttemptFieldsToResult(
    const GitDiffPatchAttempt & attempt,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::vector<std::string> keys = {
        "patch_backend_primary",
        "git_apply_attempted",
        "git_apply_completed",
        "git_apply_fallback_reason",
        "git_apply_check_exit_code",
        "git_apply_exit_code",
        "git_apply_check_log_path",
        "git_apply_log_path",
        "git_repo_root",
        "git_patch_file_path"
    };
    for (const std::string & key : keys) {
        const auto iter = attempt.result.fields.find(key);
        if (iter != attempt.result.fields.end()) {
            result->fields[key] = iter->second;
        }
    }
}

GitDiffPatchAttempt TryApplyDiffPatchWithGit(
    const AgentConfig & config,
    const std::string & requested_file_path,
    const std::filesystem::path & normalized_path,
    const std::string & diff_text,
    const std::string & old_hash,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & patch_id,
    const std::string & reason,
    const std::string & final_resolved_file_path,
    const std::string & final_target_resolution_reason,
    bool auto_target_resolution_used,
    bool allow_empty_content) {
    GitDiffPatchAttempt attempt;
    CommandResult result;
    result.fields["tool"] = "lan_agent_apply_diff_patch";
    result.fields["action"] = "apply_diff_patch";
    result.fields["patch_backend_primary"] = "git_apply";
    result.fields["git_apply_attempted"] = "false";
    result.fields["git_apply_completed"] = "false";
    result.fields["file_path"] = requested_file_path;
    result.fields["requested_file_path"] = requested_file_path;
    result.fields["normalized_path"] = normalized_path.string();
    result.fields["write_mode"] = "git_diff";
    result.fields["patch_format"] = "git_unified_diff";
    result.fields["patch_execution_mode"] = "git_apply_check_then_apply";
    result.fields["final_write_tool"] = "git apply";
    result.fields["input_diff_hash"] = StableContentChecksum(diff_text);
    result.fields["write_applied"] = "false";
    result.fields["write_verified"] = "false";
    result.fields["disk_write_completed"] = "false";
    result.fields["allow_empty_content"] = allow_empty_content ? "true" : "false";
    result.fields["target_resolution_used"] = final_resolved_file_path.empty() ? "false" : "true";
    result.fields["resolved_file_path"] = final_resolved_file_path;
    result.fields["target_resolution_reason"] = final_target_resolution_reason;
    result.fields["auto_target_resolution_used"] = auto_target_resolution_used ? "true" : "false";

    std::filesystem::path repo_root;
    if (!TryFindGitRepositoryRoot(normalized_path, &repo_root)) {
        attempt.attempted = false;
        result.fields["git_apply_fallback_reason"] = "target file is not under a git repository";
        attempt.result = result;
        return attempt;
    }
    attempt.attempted = true;
    result.fields["git_apply_attempted"] = "true";
    result.fields["git_repo_root"] = repo_root.string();

    std::string old_content;
    std::string read_error;
    if (!ReadWholeFile(normalized_path, &old_content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        result.fields["result"] = "read_failed";
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }
    const std::string actual_old_hash = StableContentChecksum(old_content);
    result.fields["old_hash"] = actual_old_hash;
    result.fields["old_hash_expected"] = old_hash;
    result.fields["old_hash_match"] = old_hash.empty() || old_hash == actual_old_hash ? "true" : "false";
    if (result.fields["old_hash_match"] != "true") {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = "old_hash mismatch";
        result.fields["result"] = "old_hash_mismatch";
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }

    const PatchRequest patch_request = BuildPatchRequest(
        requested_file_path,
        diff_text,
        old_hash,
        request_id,
        trace_id,
        patch_id,
        reason.empty() ? "apply_diff_patch_git_apply" : reason);
    result.fields["request_id"] = patch_request.request_id;
    result.fields["trace_id"] = patch_request.trace_id;
    result.fields["patch_id"] = patch_request.patch_id;
    result.fields["patch_reason"] = patch_request.reason;
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);

    std::string write_error;
    const std::string patch_file_token = SanitizeDispatchToken(patch_request.patch_id, "patch");
    const std::filesystem::path patch_file =
        std::filesystem::path(config.log_root) / ("git_apply_" + patch_file_token + ".patch");
    if (!WriteWholeFileBinary(patch_file, diff_text, &write_error)) {
        result.ok = false;
        result.exit_code = 56;
        result.fields["error"] = write_error;
        result.fields["result"] = "git_patch_file_write_failed";
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }
    result.fields["git_patch_file_path"] = patch_file.string();

    const std::filesystem::path check_log =
        std::filesystem::path(config.log_root) / ("git_apply_check_" + patch_file_token + ".log");
    codex_lan_agent::ProcessRunResult check_process;
    std::string process_error;
    const std::string check_command =
        "git apply --check --whitespace=nowarn " + QuoteProcessArgumentForPatchTool(patch_file.string());
    const bool check_started = codex_lan_agent::RunCommandWithLog(
        check_command,
        repo_root.string(),
        check_log.string(),
        120,
        0,
        &check_process,
        &process_error);
    result.fields["git_apply_check_log_path"] = check_log.string();
    result.fields["git_apply_check_exit_code"] = check_started ? std::to_string(check_process.exit_code) : "-1";
    if (!check_started || check_process.exit_code != 0) {
        result.fields["git_apply_fallback_reason"] = check_started
            ? "git apply --check rejected the diff"
            : process_error;
        AppendPatchAuditEvent(
            config,
            patch_request,
            "PATCH_GIT_CHECK",
            "GIT_CHECK_REJECTED",
            result.fields["input_diff_hash"],
            check_log.string(),
            std::string(),
            "FALLBACK",
            result.fields["git_apply_fallback_reason"]);
        attempt.completed = false;
        attempt.result = result;
        return attempt;
    }

    const std::string backup_path = BuildPatchBackupPath(config);
    if (!WriteWholeFileBinary(backup_path, old_content, &write_error)) {
        result.ok = false;
        result.exit_code = 57;
        result.fields["error"] = write_error;
        result.fields["result"] = "backup_write_failed";
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }
    result.fields["backup_path"] = backup_path;
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_GIT_CHECK",
        "GIT_CHECK_SUCCESS",
        result.fields["input_diff_hash"],
        check_log.string(),
        backup_path,
        "CHECKED",
        "git apply --check succeeded");

    const std::filesystem::path apply_log =
        std::filesystem::path(config.log_root) / ("git_apply_" + patch_file_token + ".log");
    codex_lan_agent::ProcessRunResult apply_process;
    process_error.clear();
    const std::string apply_command =
        "git apply --whitespace=nowarn " + QuoteProcessArgumentForPatchTool(patch_file.string());
    const bool apply_started = codex_lan_agent::RunCommandWithLog(
        apply_command,
        repo_root.string(),
        apply_log.string(),
        120,
        0,
        &apply_process,
        &process_error);
    result.fields["git_apply_log_path"] = apply_log.string();
    result.fields["log_path"] = apply_log.string();
    result.fields["result_ref"] = apply_log.string();
    result.fields["evidence_ref"] = apply_log.string();
    result.fields["git_apply_exit_code"] = apply_started ? std::to_string(apply_process.exit_code) : "-1";
    if (!apply_started || apply_process.exit_code != 0) {
        result.ok = false;
        result.exit_code = apply_started ? apply_process.exit_code : 57;
        result.fields["error"] = apply_started ? "git apply failed after successful check" : process_error;
        result.fields["result"] = "git_apply_failed";
        AppendPatchAuditEvent(
            config,
            patch_request,
            "PATCH_APPLY",
            "GIT_APPLY_FAILED",
            result.fields["input_diff_hash"],
            apply_log.string(),
            backup_path,
            "FAILED",
            result.fields["error"]);
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }

    std::string final_content;
    std::string final_read_error;
    if (!ReadWholeFile(normalized_path, &final_content, &final_read_error)) {
        std::string restore_error;
        WriteWholeFileBinary(normalized_path, old_content, &restore_error);
        result.ok = false;
        result.exit_code = 58;
        result.fields["error"] = final_read_error;
        result.fields["result"] = "readback_failed_after_git_apply";
        result.fields["restore_error"] = restore_error;
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }
    if (final_content.empty() && !allow_empty_content) {
        std::string restore_error;
        WriteWholeFileBinary(normalized_path, old_content, &restore_error);
        result.ok = false;
        result.exit_code = 59;
        result.fields["error"] = "empty patched content rejected";
        result.fields["result"] = "write_blocked_empty_content";
        result.fields["write_guard_blocked"] = "true";
        result.fields["write_guard_reason_code"] = "empty_content_requires_explicit_allow";
        result.fields["restore_error"] = restore_error;
        result.fields["next_action"] =
            "retry with allow_empty_content=true only if replacing the file with empty content is intentional";
        AppendPatchAuditEvent(
            config,
            patch_request,
            "PATCH_VERIFY",
            "VERIFY_FAILED",
            result.fields["input_diff_hash"],
            apply_log.string(),
            backup_path,
            "AUTO_RESTORED",
            "git apply produced empty content without explicit allow");
        attempt.completed = true;
        attempt.result = result;
        return attempt;
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["git_apply_completed"] = "true";
    result.fields["patch_backend_effective"] = "git_apply";
    result.fields["new_hash"] = StableContentChecksum(final_content);
    result.fields["applied_hash"] = result.fields["new_hash"];
    result.fields["applied_hash_match"] = "true";
    result.fields["changed"] = old_content == final_content ? "false" : "true";
    result.fields["diff_applied_effective"] = result.fields["changed"];
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = "true";
    result.fields["disk_write_completed"] = "true";
    result.fields["diff_parse_status"] = "git_apply_checked";
    result.fields["diff_apply_mode"] = "git_apply";
    result.fields["fuzzy_context_used"] = "false";
    result.fields["target_resolution_required"] = "false";
    result.fields["applied_target_file"] = normalized_path.string();
    result.fields["result"] = "diff_applied";
    result.fields["summary"] = result.fields["changed"] == "true"
        ? "git apply checked, applied, and readback verified"
        : "git apply checked and verified; target already matched requested content";
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_APPLY",
        "GIT_APPLY_SUCCESS",
        result.fields["input_diff_hash"],
        apply_log.string(),
        backup_path,
        "APPLIED",
        "git apply succeeded");
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_VERIFY",
        "VERIFY_SUCCESS",
        result.fields["input_diff_hash"],
        apply_log.string(),
        backup_path,
        "VERIFIED",
        "git apply readback verified");
    attempt.completed = true;
    attempt.result = result;
    return attempt;
}

bool IsSourceFilePathForWritePolicy(const std::string & file_path) {
    const std::string lower = ToLowerAscii(file_path);
    const auto ends_with = [&](const char * suffix) {
        const std::size_t suffix_length = std::strlen(suffix);
        return lower.size() >= suffix_length
            && lower.compare(lower.size() - suffix_length, suffix_length, suffix) == 0;
    };
    return ends_with(".c")
        || ends_with(".cpp")
        || ends_with(".h")
        || ends_with(".hpp");
}

std::string DetectEnvelopePollutionMarker(
    const std::string & file_path,
    const std::string & content) {
    const bool is_source_file = IsSourceFilePathForWritePolicy(file_path);
    static const std::vector<std::string> kHardMarkers = {
        "verification_status=",
        "tool_name=lan_agent_",
        "current_file_path=",
        "tags=file,read,paged",
        "goal_status=",
        "result_hash=",
        ">>>content_end",
        "[task_start]",
        "directory_access.exe",
        "directory_access_helper_path=",
        "content_payload_format=",
        "content_text=",
        "public_fields=",
        "request_id=req-",
        "trace_id=trace-",
        "tool_call_id=toolcall-"
    };
    for (const std::string & marker : kHardMarkers) {
        if (content.find(marker) != std::string::npos) {
            return marker;
        }
    }

    if (!is_source_file && content.find("content_begin<<<") != std::string::npos) {
        return "content_begin<<<";
    }

    static const std::vector<std::string> kSuspiciousLinePrefixes = {
        "current_file_path=",
        "task_type=",
        "trigger=",
        "file_path=",
        "normalized_path=",
        "trace_id=",
        "request_id=",
        "tool_call_id=",
        "start_line=",
        "total_lines=",
        "end_line=",
        "line_count=",
        "returned_lines=",
        "remaining_lines=",
        "max_lines=",
        "has_more=",
        "read_complete=",
        "file_complete=",
        "read_status=",
        "page_status=",
        "content_payload_format=",
        "content_begin_marker=",
        "field_catalog_version=",
        "directory_access_helper_used=",
        "directory_access_helper_path=",
        "directory_access_helper_log_path=",
        "structured_body_read_mode=",
        "structured_body_helper_bypassed=",
        "provider_id=",
        "capability_id=",
        "public_fields=",
        "result=",
        "tags=",
        "summary=",
        "status=",
        "exit_code=",
        "tool_name=",
        "result_fields_config_path=",
        "result_fields_config_exists=",
        "result_fields_config_effect="
    };

    std::istringstream input(content);
    std::string line;
    int suspicious_line_count = 0;
    int inspected_lines = 0;
    std::string first_prefix_match;
    while (inspected_lines < 80 && std::getline(input, line)) {
        ++inspected_lines;
        const std::string trimmed = Trim(line);
        for (const std::string & prefix : kSuspiciousLinePrefixes) {
            if (trimmed.rfind(prefix, 0) == 0) {
                ++suspicious_line_count;
                if (first_prefix_match.empty()) {
                    first_prefix_match = prefix;
                }
                break;
            }
        }
        if (suspicious_line_count >= 6) {
            return first_prefix_match.empty()
                ? "suspicious_mcp_envelope_lines"
                : first_prefix_match;
        }
    }
    return std::string();
}

bool ApplyWriteContentGuards(
    CommandResult * result,
    const std::string & tool_name,
    const std::string & file_path,
    const std::string & content,
    bool is_direct_write) {
    if (result == nullptr) {
        return false;
    }
    const std::string marker = DetectEnvelopePollutionMarker(file_path, content);
    if (!marker.empty()) {
        result->ok = false;
        result->exit_code = 56;
        result->fields["error"] = "suspected tool envelope content";
        result->fields["result"] = "write_blocked_envelope_pollution";
        result->fields["write_guard_blocked"] = "true";
        result->fields["write_guard_reason_code"] = "suspected_tool_envelope_content";
        result->fields["write_guard_marker"] = marker;
        result->fields["write_guard_suspected_source"] = "mcp_read_envelope_or_task_log";
        result->fields["tool"] = tool_name;
        result->fields["next_action"] =
            "extract only the intended source payload before retrying; do not pass MCP read envelopes, task logs, or helper wrapper text into file writes";
        return false;
    }
    if (is_direct_write && IsSourceFilePathForWritePolicy(file_path)) {
        result->ok = false;
        result->exit_code = 57;
        result->fields["error"] = "direct write to source file is blocked";
        result->fields["result"] = "write_blocked_source_file_policy";
        result->fields["write_guard_blocked"] = "true";
        result->fields["write_guard_reason_code"] = "source_file_requires_patch_flow";
        result->fields["write_guard_marker"] = "source_extension";
        result->fields["tool"] = tool_name;
        result->fields["next_action"] =
            "for source files call lan_agent_preview_patch, then lan_agent_apply_single_file_patch, then lan_agent_verify_single_file_patch";
        return false;
    }
    return true;
}

bool BuildNewContentFromSimpleUnifiedDiff(
    const std::string & original_content,
    const std::string & diff_text,
    std::string * new_content,
    std::string * error_message,
    std::string * apply_mode = nullptr) {
    const std::vector<std::string> old_lines = SplitLinesPreserveText(original_content);
    const std::vector<std::string> diff_lines = SplitLinesPreserveText(diff_text);
    std::vector<std::string> rebuilt_lines;
    std::size_t old_index = 0;
    bool saw_hunk = false;
    bool fuzzy_anchor_used = false;
    bool hunk_count_mismatch_tolerated = false;

    auto normalize_for_fuzzy_match = [](const std::string & value) {
        return Trim(value);
    };

    auto parse_hunk_start = [&](const std::string & line, std::size_t * old_start, int * old_count) -> bool {
        if (line == "@@") {
            *old_start = old_index + 1;
            *old_count = -1;
            return true;
        }
        if (line.rfind("@@ -", 0) != 0) {
            return false;
        }
        const std::size_t plus_pos = line.find(" +", 4);
        if (plus_pos == std::string::npos) {
            return false;
        }
        const std::string range_text = line.substr(4, plus_pos - 4);
        const std::size_t comma_pos = range_text.find(',');
        try {
            *old_start = static_cast<std::size_t>(std::stoll(
                comma_pos == std::string::npos ? range_text : range_text.substr(0, comma_pos)));
            *old_count = comma_pos == std::string::npos
                ? 1
                : std::stoi(range_text.substr(comma_pos + 1));
        } catch (...) {
            return false;
        }
        return *old_start >= 1;
    };

    auto find_unique_hunk_anchor =
        [&](std::size_t search_start,
            const std::vector<std::string> & expected_old_lines,
            std::size_t * matched_index,
            bool * ambiguous_match) -> bool {
        if (matched_index) {
            *matched_index = 0;
        }
        if (ambiguous_match) {
            *ambiguous_match = false;
        }
        if (expected_old_lines.empty()) {
            return false;
        }

        bool found = false;
        std::size_t found_index = 0;
        for (std::size_t start = search_start; start + expected_old_lines.size() <= old_lines.size(); ++start) {
            bool match = true;
            for (std::size_t i = 0; i < expected_old_lines.size(); ++i) {
                if (old_lines[start + i] != expected_old_lines[i]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }
            if (!found) {
                found = true;
                found_index = start;
                continue;
            }
            if (ambiguous_match) {
                *ambiguous_match = true;
            }
            return false;
        }

        if (!found) {
            return false;
        }
        if (matched_index) {
            *matched_index = found_index;
        }
        return true;
    };

    auto find_unique_hunk_anchor_fuzzy =
        [&](std::size_t search_start,
            const std::vector<std::string> & expected_old_lines,
            std::size_t * matched_index,
            bool * ambiguous_match) -> bool {
        if (matched_index) {
            *matched_index = 0;
        }
        if (ambiguous_match) {
            *ambiguous_match = false;
        }
        if (expected_old_lines.empty()) {
            return false;
        }

        bool found = false;
        std::size_t found_index = 0;
        for (std::size_t start = search_start; start + expected_old_lines.size() <= old_lines.size(); ++start) {
            bool match = true;
            for (std::size_t i = 0; i < expected_old_lines.size(); ++i) {
                if (normalize_for_fuzzy_match(old_lines[start + i]) !=
                    normalize_for_fuzzy_match(expected_old_lines[i])) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }
            if (!found) {
                found = true;
                found_index = start;
                continue;
            }
            if (ambiguous_match) {
                *ambiguous_match = true;
            }
            return false;
        }

        if (!found) {
            return false;
        }
        if (matched_index) {
            *matched_index = found_index;
        }
        return true;
    };

    for (std::size_t diff_index = 0; diff_index < diff_lines.size(); ++diff_index) {
        const std::string & line = diff_lines[diff_index];
        if (IsIgnorableUnifiedDiffLine(line)) {
            continue;
        }

        std::size_t old_start = 0;
        int declared_old_count = -1;
        if (!parse_hunk_start(line, &old_start, &declared_old_count)) {
            continue;
        }
        saw_hunk = true;

        std::vector<std::string> hunk_body_lines;
        std::size_t next_hunk_index = diff_index;
        for (++next_hunk_index; next_hunk_index < diff_lines.size(); ++next_hunk_index) {
            const std::string & body_line = diff_lines[next_hunk_index];
            if (body_line.rfind("@@", 0) == 0) {
                break;
            }
            if (IsIgnorableUnifiedDiffLine(body_line)) {
                break;
            }
            if (body_line.empty()) {
                hunk_body_lines.push_back(body_line);
                continue;
            }
            if (body_line[0] == '\\') {
                continue;
            }
            hunk_body_lines.push_back(body_line);
        }

        std::vector<std::string> expected_old_lines;
        expected_old_lines.reserve(hunk_body_lines.size());
        for (const std::string & body_line : hunk_body_lines) {
            if (body_line.empty()) {
                continue;
            }
            const char op = body_line[0];
            if (op == ' ' || op == '-') {
                expected_old_lines.push_back(body_line.substr(1));
            }
        }

        std::size_t target_old_index = old_start > 0 ? old_start - 1 : 0;
        bool needs_anchor_search = target_old_index < old_index || target_old_index > old_lines.size();
        if (!needs_anchor_search && !expected_old_lines.empty()) {
            if (target_old_index + expected_old_lines.size() > old_lines.size()) {
                needs_anchor_search = true;
            } else {
                for (std::size_t i = 0; i < expected_old_lines.size(); ++i) {
                    if (old_lines[target_old_index + i] != expected_old_lines[i]) {
                        needs_anchor_search = true;
                        break;
                    }
                }
            }
        }

        if (needs_anchor_search) {
            bool ambiguous_anchor = false;
            std::size_t anchored_index = 0;
            if (!find_unique_hunk_anchor(old_index, expected_old_lines, &anchored_index, &ambiguous_anchor)) {
                bool fuzzy_ambiguous_anchor = false;
                if (!find_unique_hunk_anchor_fuzzy(old_index, expected_old_lines, &anchored_index, &fuzzy_ambiguous_anchor)) {
                    if (error_message) {
                        *error_message = (ambiguous_anchor || fuzzy_ambiguous_anchor)
                            ? "hunk context matched multiple locations in target file"
                            : "hunk context could not be located in target file";
                    }
                    return false;
                }
                fuzzy_anchor_used = true;
            }
            target_old_index = anchored_index;
        }

        while (old_index < target_old_index) {
            rebuilt_lines.push_back(old_lines[old_index]);
            ++old_index;
        }

        int consumed_old_count = 0;
        for (const std::string & body_line : hunk_body_lines) {
            if (body_line.empty()) {
                continue;
            }

            const char op = body_line[0];
            const std::string payload = body_line.substr(1);
            if (op == ' ') {
                if (old_index >= old_lines.size()) {
                    if (error_message) {
                        *error_message = "context line does not match target file";
                    }
                    return false;
                }
                if (old_lines[old_index] != payload) {
                    if (normalize_for_fuzzy_match(old_lines[old_index]) != normalize_for_fuzzy_match(payload)) {
                        if (error_message) {
                            *error_message = "context line does not match target file";
                        }
                        return false;
                    }
                    fuzzy_anchor_used = true;
                }
                rebuilt_lines.push_back(old_lines[old_index]);
                ++old_index;
                ++consumed_old_count;
                continue;
            }
            if (op == '-') {
                if (old_index >= old_lines.size()) {
                    if (error_message) {
                        *error_message = "deleted line does not match target file";
                    }
                    return false;
                }
                if (old_lines[old_index] != payload) {
                    if (normalize_for_fuzzy_match(old_lines[old_index]) != normalize_for_fuzzy_match(payload)) {
                        if (error_message) {
                            *error_message = "deleted line does not match target file";
                        }
                        return false;
                    }
                    fuzzy_anchor_used = true;
                }
                ++old_index;
                ++consumed_old_count;
                continue;
            }
            if (op == '+') {
                rebuilt_lines.push_back(payload);
                continue;
            }
            if (error_message) {
                *error_message = std::string("unsupported diff prefix: ") + op;
            }
            return false;
        }

        if (declared_old_count >= 0 && consumed_old_count != declared_old_count) {
            hunk_count_mismatch_tolerated = true;
        }
        diff_index = next_hunk_index - 1;
    }

    if (!saw_hunk) {
        if (error_message) {
            *error_message = "diff is missing @@ hunk marker";
        }
        return false;
    }
    while (old_index < old_lines.size()) {
        rebuilt_lines.push_back(old_lines[old_index]);
        ++old_index;
    }

    if (new_content) {
        *new_content = JoinLinesWithNewlines(rebuilt_lines);
    }
    if (apply_mode) {
        if (fuzzy_anchor_used) {
            *apply_mode = hunk_count_mismatch_tolerated
                ? "fuzzy_unique_trimmed_context_hunk_count_tolerated"
                : "fuzzy_unique_trimmed_context";
        } else {
            *apply_mode = hunk_count_mismatch_tolerated
                ? "exact_hunk_count_tolerated"
                : "exact";
        }
    }
    return true;
}

bool TryResolveWorkspaceWritableFilePath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message) {
    if (raw_path.empty()) {
        if (error_message) {
            *error_message = "file_path is required";
        }
        return false;
    }

    std::filesystem::path requested(raw_path);
    if (requested.is_relative()) {
        requested = std::filesystem::path(config.workspace_root) / requested;
    }
    const std::filesystem::path normalized = requested.lexically_normal();
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, workspace_root)) {
        if (error_message) {
            *error_message = "path is outside workspace_root";
        }
        return false;
    }
    const std::string filename = ToLowerAscii(normalized.filename().string());
    if (filename == "codex_lan_agent.exe") {
        if (error_message) {
            *error_message = "refusing to modify running agent executable";
        }
        return false;
    }
    *normalized_path = normalized;
    return true;
}

CommandResult PreviewPatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & new_content,
    const std::string & old_hash = std::string(),
    const std::string & request_id = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & patch_id = std::string(),
    const std::string & reason = std::string()) {
    const PatchRequest patch_request =
        BuildPatchRequest(file_path, new_content, old_hash, request_id, trace_id, patch_id, reason);
    CommandResult result;
    result.fields["would_write"] = "false";
    result.fields["preview_only"] = "true";
    result.fields["requires_preview"] = "true";
    result.fields["requires_approval"] = "true";
    result.fields["requires_revert_plan"] = "true";
    result.fields["requires_post_verify"] = "true";
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveWorkspaceFilePath(config, patch_request.file_path, &normalized, &path_error)) {
        if (!TryResolveWorkspaceWritableFilePath(config, patch_request.file_path, &normalized, &path_error)) {
            result.ok = false;
            result.exit_code = 45;
            result.fields["error"] = path_error;
            result.fields["summary"] = "patch preview rejected by workspace path guard";
            result.fields["result"] = "invalid_file_path";
            result.fields["next_action"] = "retry with a workspace-root file_path";
            result.fields["path_guard_decision"] = "block";
            result.fields["path_guard_reason"] = path_error;
            result.fields["workspace_guard"] = "path_scope_enforced";
            result.fields["request_id"] = patch_request.request_id;
            result.fields["trace_id"] = patch_request.trace_id;
            result.fields["patch_id"] = patch_request.patch_id;
            result.fields["requested_file_path"] = patch_request.file_path;
            result.fields["file_path"] = patch_request.file_path;
            return result;
        }
    }

    std::string old_content;
    std::string read_error;
    const bool file_existed = std::filesystem::exists(normalized);
    if (file_existed && !ReadWholeFile(normalized, &old_content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        result.fields["request_id"] = patch_request.request_id;
        result.fields["trace_id"] = patch_request.trace_id;
        result.fields["patch_id"] = patch_request.patch_id;
        result.fields["file_path"] = patch_request.file_path;
        return result;
    }
    const bool compact_diff = old_content.size() + patch_request.new_content.size() > kInlineDiffByteLimit;
    const std::string diff_text = compact_diff
        ? BuildCompactDiffPlaceholder(normalized.string(), old_content, patch_request.new_content)
        : BuildSimpleUnifiedDiff(normalized.string(), old_content, patch_request.new_content);
    PopulatePatchResultFields(
        &result,
        patch_request,
        normalized.string(),
        old_content,
        patch_request.new_content,
        diff_text,
        patch_request.patch_id + ":preview");
    result.fields["file_existed"] = file_existed ? "true" : "false";
    result.fields["old_bytes"] = std::to_string(old_content.size());
    result.fields["new_bytes"] = std::to_string(patch_request.new_content.size());
    result.fields["changed"] = old_content == patch_request.new_content ? "false" : "true";
    result.fields["diff_changed_line_count"] =
        compact_diff ? "unknown_compacted" : std::to_string(CountUnifiedDiffChangedLines(diff_text));
    result.fields["diff"] = diff_text;
    CompactLargeInlineDiffResult(&result, old_content, patch_request.new_content, diff_text);
    result.fields["workspace_guard"] = "path_scope_relaxed";
    result.fields["path_canonicalized"] = "true";
    result.fields["old_hash_expected"] = patch_request.old_hash;
    result.fields["old_hash_match"] =
        patch_request.old_hash.empty() || patch_request.old_hash == result.fields["old_hash"] ? "true" : "false";
    result.fields["result"] = "preview_ready";
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_PREVIEW",
        "PREVIEW_READY",
        result.fields["diff_hash"],
        std::string(),
        std::string(),
        "PREVIEW_READY",
        result.fields["old_hash_match"] == "true" ? "preview ready" : "old hash mismatch detected during preview");
    return result;
}

CommandResult ApplySingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & new_content,
    const std::string & old_hash = std::string(),
    const std::string & request_id = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & patch_id = std::string(),
    const std::string & reason = std::string(),
    bool allow_empty_content = false) {
    const PatchRequest patch_request =
        BuildPatchRequest(file_path, new_content, old_hash, request_id, trace_id, patch_id, reason);
    CommandResult result =
        PreviewPatchResult(config, file_path, new_content, old_hash, patch_request.request_id, patch_request.trace_id, patch_request.patch_id, patch_request.reason);
    result.fields["patch_execution_mode"] = "single_file_apply";
    result.fields["final_write_tool"] = "lan_agent_apply_single_file_patch";
    result.fields["write_applied"] = "false";
    result.fields["write_verified"] = "false";
    result.fields["disk_write_completed"] = "false";
    result.fields["allow_empty_content"] = allow_empty_content ? "true" : "false";
    if (!ApplyWriteContentGuards(&result, "lan_agent_apply_single_file_patch", file_path, new_content, false)) {
        return result;
    }
    if (new_content.empty() && !allow_empty_content) {
        result.ok = false;
        result.exit_code = 58;
        result.fields["error"] = "empty new_content rejected";
        result.fields["result"] = "write_blocked_empty_content";
        result.fields["write_guard_blocked"] = "true";
        result.fields["write_guard_reason_code"] = "empty_content_requires_explicit_allow";
        result.fields["next_action"] =
            "retry with allow_empty_content=true only if replacing the file with empty content is intentional";
        return result;
    }
    if (!result.ok) {
        return result;
    }
    if (result.fields["old_hash_match"] != "true") {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = "old_hash mismatch";
        result.fields["result"] = "old_hash_mismatch";
        return result;
    }
    const std::filesystem::path normalized(result.fields["normalized_path"]);
    const std::string resource_key = "file:" + normalized.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "target file is busy";
        return result;
    }

    std::string backup_content;
    std::string backup_read_error;
    const bool file_existed = std::filesystem::exists(normalized);
    if (file_existed && !ReadWholeFile(normalized, &backup_content, &backup_read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = backup_read_error;
        return result;
    }

    const std::string backup_path = BuildPatchBackupPath(config);
    std::filesystem::create_directories(config.log_root);
    std::ofstream backup_output(backup_path, std::ios::binary | std::ios::trunc);
    if (!backup_output.is_open()) {
        result.ok = false;
        result.exit_code = 49;
        result.fields["error"] = "failed to open backup file";
        return result;
    }
    backup_output.write(backup_content.data(), static_cast<std::streamsize>(backup_content.size()));
    backup_output.close();

    const std::filesystem::path temp_path =
        normalized.string() + ".patching." + TimeStampForFileName() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(normalized.parent_path(), ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to create parent directory";
        return result;
    }
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to open temp file";
        return result;
    }
    output.write(new_content.data(), static_cast<std::streamsize>(new_content.size()));
    output.close();

    std::filesystem::rename(temp_path, normalized, ec);
    if (ec) {
        std::filesystem::remove(normalized, ec);
        ec.clear();
        std::filesystem::rename(temp_path, normalized, ec);
    }
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        result.ok = false;
        result.exit_code = 48;
        result.fields["error"] = "failed to replace target file";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "apply_single_file_patch");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "file_path=" << normalized.string() << "\n";
    log << "old_bytes=" << result.fields["old_bytes"] << "\n";
    log << "new_bytes=" << result.fields["new_bytes"] << "\n";
    log << "changed=" << result.fields["changed"] << "\n";
    if (GetFieldOrDefault(result, "diff_inline_omitted", "false") == "true") {
        log << "diff_inline_omitted=true\n";
        log << "diff_bytes=" << result.fields["diff_bytes"] << "\n";
    } else {
        log << "diff=\n" << result.fields["diff"] << "\n";
    }

    result.fields["would_write"] = "true";
    result.fields["backup_path"] = backup_path;
    result.fields["log_path"] = log_path;
    result.fields["applied_target_file"] = normalized.string();
    result.fields["preview_verified"] = "true";
    result.fields["old_hash_verified"] = "true";
    result.fields["post_verify_required"] = "true";
    result.fields["disk_write_completed"] = "true";
    result.fields["patch_audit_id"] = patch_request.patch_id + ":apply";
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    std::string final_content;
    std::string final_read_error;
    if (!ReadWholeFile(normalized, &final_content, &final_read_error)) {
        result.ok = false;
        result.exit_code = 53;
        result.fields["error"] = final_read_error;
        result.fields["result"] = "apply_verify_read_failed";
        AppendPatchAuditEvent(
            config,
            patch_request,
            "PATCH_VERIFY",
            "VERIFY_FAILED",
            result.fields["diff_hash"],
            log_path,
            backup_path,
            "FAILED",
            "failed to read patched file for hash verification");
        return result;
    }
    const std::string applied_hash = StableContentChecksum(final_content);
    result.fields["applied_hash"] = applied_hash;
    result.fields["applied_hash_match"] = applied_hash == result.fields["new_hash"] ? "true" : "false";
    result.fields["diff_applied_effective"] = result.fields["changed"] == "true" ? "true" : "false";
    if (result.fields["applied_hash_match"] != "true") {
        AppendPatchAuditEvent(
            config,
            patch_request,
            "PATCH_VERIFY",
            "VERIFY_FAILED",
            result.fields["diff_hash"],
            log_path,
            backup_path,
            "FAILED",
            "applied hash mismatch");
        CommandResult revert_result = RevertSingleFilePatchResult(
            config,
            normalized.string(),
            backup_path,
            patch_request.request_id,
            patch_request.trace_id,
            patch_request.patch_id,
            "auto_revert_after_hash_mismatch");
        result.ok = false;
        result.exit_code = 54;
        result.fields["error"] = "applied hash mismatch";
        result.fields["result"] = "auto_reverted_after_hash_mismatch";
        result.fields["revert_result"] = GetFieldOrDefault(revert_result, "result", "");
        result.fields["revert_log_path"] = GetFieldOrDefault(revert_result, "log_path", "");
        return result;
    }
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_APPLY",
        "APPLY_SUCCESS",
        result.fields["diff_hash"],
        log_path,
        backup_path,
        file_existed ? "APPLIED" : "CREATED",
        "patch apply succeeded");
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_VERIFY",
        "VERIFY_SUCCESS",
        result.fields["diff_hash"],
        log_path,
        backup_path,
        "VERIFIED",
        "applied hash verified");
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = "true";
    result.fields["result"] = file_existed ? "applied" : "created";
    return result;
}

CommandResult ApplyDiffPatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & diff_text,
    const std::string & old_hash = std::string(),
    const std::string & request_id = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & patch_id = std::string(),
    const std::string & reason = std::string(),
    const std::string & resolved_file_path = std::string(),
    const std::string & target_resolution_reason = std::string(),
    bool allow_empty_content = false) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["requested_file_path"] = file_path;
    result.fields["write_mode"] = "git_diff";
    result.fields["tool"] = "lan_agent_apply_diff_patch";
    result.fields["action"] = "apply_diff_patch";
    result.fields["patch_format"] = "simple_unified_diff";
    result.fields["patch_contract"] = "expects git-style unified diff with ---, +++, and @@ markers";
    result.fields["patch_execution_mode"] = "internal_diff_then_single_file_apply";
    result.fields["final_write_tool"] = "lan_agent_apply_single_file_patch";
    result.fields["diff_parse_status"] = "pending";
    result.fields["write_applied"] = "false";
    result.fields["write_verified"] = "false";
    result.fields["disk_write_completed"] = "false";
    result.fields["allow_empty_content"] = allow_empty_content ? "true" : "false";
    result.fields["diff_write_contract"] =
        "diff_applied_effective only means the diff produced changed content; use disk_write_completed, write_verified, and applied_hash_match to confirm the file was written and verified.";
    if (diff_text.empty()) {
        result.ok = false;
        result.exit_code = 54;
        result.fields["error"] = "diff_text is required";
        result.fields["result"] = "diff_text_missing";
        return result;
    }

    const UnifiedDiffHeaderPaths header_paths = ExtractUnifiedDiffHeaderPaths(diff_text);
    result.fields["diff_old_path"] = header_paths.old_path;
    result.fields["diff_new_path"] = header_paths.new_path;

    const std::string effective_file_path = resolved_file_path.empty() ? file_path : resolved_file_path;
    result.fields["resolved_file_path"] = resolved_file_path;
    result.fields["target_resolution_reason"] = target_resolution_reason;
    result.fields["target_resolution_used"] = resolved_file_path.empty() ? "false" : "true";
    std::string final_resolved_file_path = resolved_file_path;
    std::string final_target_resolution_reason = target_resolution_reason;
    bool auto_target_resolution_used = false;

    if (!UnifiedDiffTouchesOnlyRequestedPath(diff_text, effective_file_path)) {
        result.ok = false;
        result.exit_code = 60;
        result.fields["error"] = "multi-file diff or mismatched diff target is not supported by single-file apply_diff_patch";
        result.fields["result"] = "diff_target_resolution_required";
        result.fields["target_resolution_required"] = "true";
        result.fields["touched_diff_paths_json"] = BuildJsonStringArray(ExtractUnifiedDiffTouchedPaths(diff_text));
        result.fields["next_action"] =
            "split the diff per target file or retry with the exact resolved_file_path for this single-file patch";
        return result;
    }

    if (!DiffHeaderMatchesRequestedPath(header_paths, effective_file_path)) {
        result.ok = false;
        result.exit_code = 58;
        result.fields["error"] = "diff header file does not match requested file_path";
        result.fields["result"] = "diff_target_resolution_required";
        result.fields["target_resolution_required"] = "true";
        result.fields["next_action"] =
            "confirm the intended target file and retry with resolved_file_path and target_resolution_reason";
        return result;
    }
    if (!resolved_file_path.empty() && target_resolution_reason.empty()) {
        result.ok = false;
        result.exit_code = 59;
        result.fields["error"] = "target_resolution_reason is required when resolved_file_path is provided";
        result.fields["result"] = "diff_target_resolution_reason_missing";
        result.fields["target_resolution_required"] = "true";
        result.fields["next_action"] =
            "retry with a non-empty target_resolution_reason describing why resolved_file_path matches the diff";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveWorkspaceWritableFilePath(config, effective_file_path, &normalized, &path_error)) {
        const std::vector<std::string> candidate_paths =
            FindWorkspaceWritablePathCandidates(config, effective_file_path);
        if (candidate_paths.size() == 1) {
            normalized = std::filesystem::path(candidate_paths.front()).lexically_normal();
            result.fields["candidate_file_count"] = "1";
            result.fields["candidate_file_paths_json"] = BuildJsonStringArray(candidate_paths);
            result.fields["auto_target_resolution_used"] = "true";
            result.fields["auto_target_resolution_reason"] =
                "unique workspace suffix match for requested relative file_path";
            final_resolved_file_path = normalized.string();
            final_target_resolution_reason =
                "unique workspace suffix match for requested relative file_path";
            auto_target_resolution_used = true;
        } else {
            result.ok = false;
            result.exit_code = 45;
            result.fields["error"] = candidate_paths.empty()
                ? path_error
                : "relative file_path matched multiple workspace files; retry with resolved_file_path";
            result.fields["summary"] = candidate_paths.empty()
                ? "diff patch rejected by workspace path guard"
                : "diff patch target resolution is required";
            result.fields["result"] = candidate_paths.empty()
                ? "invalid_file_path"
                : "diff_target_resolution_required";
            result.fields["path_guard_decision"] = candidate_paths.empty() ? "block" : "review";
            result.fields["path_guard_reason"] = candidate_paths.empty()
                ? path_error
                : "multiple workspace candidates matched the requested relative path";
            result.fields["workspace_guard"] = candidate_paths.empty()
                ? "path_scope_enforced"
                : "path_scope_relaxed";
            result.fields["candidate_file_count"] = std::to_string(candidate_paths.size());
            result.fields["candidate_file_paths_json"] = BuildJsonStringArray(candidate_paths);
            result.fields["target_resolution_required"] = candidate_paths.empty() ? "false" : "true";
            result.fields["next_action"] = candidate_paths.empty()
                ? "retry with an absolute file_path or an existing workspace-relative file_path"
                : "choose one candidate_file_paths_json entry and retry with resolved_file_path and target_resolution_reason";
            return result;
        }
    }

    GitDiffPatchAttempt git_attempt = TryApplyDiffPatchWithGit(
        config,
        file_path,
        normalized,
        diff_text,
        old_hash,
        request_id,
        trace_id,
        patch_id,
        reason,
        final_resolved_file_path,
        final_target_resolution_reason,
        auto_target_resolution_used,
        allow_empty_content);
    if (git_attempt.completed) {
        return git_attempt.result;
    }

    std::string old_content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &old_content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        result.fields["result"] = "read_failed";
        return result;
    }

    std::string new_content;
    std::string diff_error;
    std::string diff_apply_mode;
    if (!BuildNewContentFromSimpleUnifiedDiff(old_content, diff_text, &new_content, &diff_error, &diff_apply_mode)) {
        result.ok = false;
        result.exit_code = 55;
        result.fields["error"] = diff_error;
        result.fields["result"] = "diff_apply_rejected";
        result.fields["normalized_path"] = normalized.string();
        result.fields["diff_parse_status"] = "failed";
        result.fields["diff_apply_mode"] = "failed";
        result.fields["fallback_strategy"] =
            "retry with a refreshed git diff, resolved_file_path, or full-content apply_single_file_patch";
        CopyGitAttemptFieldsToResult(git_attempt, &result);
        return result;
    }
    result.fields["diff_parse_status"] = "applied";

    result = ApplySingleFilePatchResult(
        config,
        normalized.string(),
        new_content,
        old_hash,
        request_id,
        trace_id,
        patch_id,
        reason.empty() ? "apply_diff_patch" : reason,
        allow_empty_content);
    result.fields["file_path"] = file_path;
    result.fields["requested_file_path"] = file_path;
    result.fields["normalized_path"] = normalized.string();
    result.fields["write_mode"] = "git_diff";
    result.fields["tool"] = "lan_agent_apply_diff_patch";
    result.fields["action"] = "apply_diff_patch";
    result.fields["patch_format"] = "simple_unified_diff";
    result.fields["patch_execution_mode"] = "internal_diff_then_single_file_apply";
    result.fields["final_write_tool"] = "lan_agent_apply_single_file_patch";
    result.fields["patch_backend_primary"] = "git_apply";
    result.fields["patch_backend_effective"] = "internal_diff_then_single_file_apply";
    result.fields["diff_parse_status"] = "applied";
    result.fields["diff_apply_mode"] = diff_apply_mode.empty() ? "exact" : diff_apply_mode;
    result.fields["fuzzy_context_used"] =
        result.fields["diff_apply_mode"].rfind("fuzzy_", 0) == 0 ? "true" : "false";
    result.fields["diff_write_contract"] =
        "diff_applied_effective only means the diff produced changed content; use disk_write_completed, write_verified, and applied_hash_match to confirm the file was written and verified.";
    result.fields["input_diff_hash"] = StableContentChecksum(diff_text);
    CopyGitAttemptFieldsToResult(git_attempt, &result);
    result.fields["diff_old_path"] = header_paths.old_path;
    result.fields["diff_new_path"] = header_paths.new_path;
    result.fields["resolved_file_path"] = final_resolved_file_path;
    result.fields["target_resolution_reason"] = final_target_resolution_reason;
    result.fields["target_resolution_used"] = final_resolved_file_path.empty() ? "false" : "true";
    result.fields["target_resolution_required"] = "false";
    result.fields["applied_target_file"] = normalized.string();
    result.fields["diff_applied_effective"] = GetFieldOrDefault(result, "changed", "false");
    if (GetFieldOrDefault(result, "candidate_file_count", "").empty()) {
        result.fields["candidate_file_count"] = "0";
    }
    if (GetFieldOrDefault(result, "candidate_file_paths_json", "").empty()) {
        result.fields["candidate_file_paths_json"] = "[]";
    }
    if (GetFieldOrDefault(result, "auto_target_resolution_used", "").empty()) {
        result.fields["auto_target_resolution_used"] = auto_target_resolution_used ? "true" : "false";
    }
    if (GetFieldOrDefault(result, "auto_target_resolution_reason", "").empty()) {
        result.fields["auto_target_resolution_reason"] = auto_target_resolution_used
            ? "unique workspace suffix match for requested relative file_path"
            : std::string();
    }
    result.fields["result"] = result.ok ? "diff_applied" : GetFieldOrDefault(result, "result", "diff_apply_failed");
    if (result.ok) {
        result.fields["summary"] = GetFieldOrDefault(result, "changed", "false") == "true"
            ? "git diff parsed, file written, and readback verified"
            : "git diff parsed and verified; target already matched requested content";
    }
    return result;
}

CommandResult WriteTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & content,
    bool append) {
    std::filesystem::path normalized;
    std::string path_error;
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["append"] = append ? "true" : "false";
    result.fields["action"] = "write_text_file";
    result.fields["write_contract"] = "use this tool for generate/create/write/append text files; do not use local_cli echo or shell redirection";
    if (!ApplyWriteContentGuards(&result, "lan_agent_write_text_file", file_path, content, true)) {
        return result;
    }
    if (!TryResolveWorkspaceWritableFilePath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = path_error;
        return result;
    }

    std::string old_content;
    std::string read_error;
    const bool file_existed = std::filesystem::exists(normalized);
    if (file_existed && !ReadWholeFile(normalized, &old_content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        return result;
    }
    const std::string new_content = append ? (old_content + content) : content;
    const std::string resource_key = "file:" + normalized.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "target file is busy";
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(normalized.parent_path(), ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to create parent directory";
        return result;
    }

    const std::filesystem::path temp_path =
        normalized.string() + ".writing." + TimeStampForFileName() + ".tmp";
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to open temp file";
        return result;
    }
    output.write(new_content.data(), static_cast<std::streamsize>(new_content.size()));
    output.close();

    std::filesystem::rename(temp_path, normalized, ec);
    if (ec) {
        std::filesystem::remove(normalized, ec);
        ec.clear();
        std::filesystem::rename(temp_path, normalized, ec);
    }
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        result.ok = false;
        result.exit_code = 48;
        result.fields["error"] = "failed to replace target file";
        return result;
    }

    std::string final_content;
    std::string final_read_error;
    if (!ReadWholeFile(normalized, &final_content, &final_read_error)) {
        result.ok = false;
        result.exit_code = 53;
        result.fields["error"] = final_read_error;
        result.fields["result"] = "write_verify_read_failed";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "write_text_file");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "file_path=" << normalized.string() << "\n";
    log << "append=" << (append ? "true" : "false") << "\n";
    log << "old_bytes=" << old_content.size() << "\n";
    log << "written_text_bytes=" << content.size() << "\n";
    log << "final_bytes=" << final_content.size() << "\n";
    log << "final_hash=" << StableContentChecksum(final_content) << "\n";
    log.close();

    result.ok = true;
    result.exit_code = 0;
    result.fields["file_path"] = file_path;
    result.fields["normalized_path"] = normalized.string();
    result.fields["append"] = append ? "true" : "false";
    result.fields["file_existed"] = file_existed ? "true" : "false";
    result.fields["written_text_bytes"] = std::to_string(content.size());
    result.fields["old_bytes"] = std::to_string(old_content.size());
    result.fields["final_bytes"] = std::to_string(final_content.size());
    result.fields["old_hash"] = StableContentChecksum(old_content);
    result.fields["new_hash"] = StableContentChecksum(new_content);
    result.fields["applied_hash"] = StableContentChecksum(final_content);
    result.fields["applied_hash_match"] =
        result.fields["new_hash"] == result.fields["applied_hash"] ? "true" : "false";
    if (result.fields["applied_hash_match"] != "true") {
        result.ok = false;
        result.exit_code = 54;
    }
    result.fields["write_applied"] = "true";
    result.fields["write_verified"] = result.fields["applied_hash_match"];
    result.fields["disk_write_completed"] = "true";
    result.fields["log_path"] = log_path;
    result.fields["action"] = "write_text_file";
    result.fields["tool"] = "lan_agent_write_text_file";
    result.fields["result"] = result.ok
        ? (append ? "appended" : (file_existed ? "overwritten" : "created"))
        : GetFieldOrDefault(result, "result", "write_failed");
    result.fields["next_action"] = result.ok
        ? "verify with lan_agent_read_text_file"
        : "inspect error and log_path";
    return result;
}

CommandResult EnsureDirectoryResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_path,
    bool ensure_parent) {
    CommandResult result;
    result.fields["tool"] = "lan_agent_ensure_directory";
    result.fields["action"] = "ensure_directory";
    result.fields["directory_path"] = directory_path;
    result.fields["file_path"] = file_path;
    result.fields["ensure_parent"] = ensure_parent ? "true" : "false";

    std::filesystem::path target;
    if (!file_path.empty() && (ensure_parent || directory_path.empty())) {
        std::filesystem::path normalized_file;
        std::string path_error;
        if (!TryResolveWorkspaceWritableFilePath(config, file_path, &normalized_file, &path_error)) {
            result.ok = false;
            result.exit_code = 45;
            result.fields["error"] = path_error;
            return result;
        }
        target = normalized_file.parent_path();
        result.fields["resolved_from"] = "file_parent";
    } else {
        if (directory_path.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "directory_path or file_path is required";
            return result;
        }
        target = std::filesystem::path(directory_path);
        if (target.is_relative()) {
            target = std::filesystem::path(config.workspace_root) / target;
        }
        target = target.lexically_normal();
        result.fields["resolved_from"] = "directory_path";
    }

    std::error_code ec;
    const bool existed_before = std::filesystem::is_directory(target, ec);
    ec.clear();
    std::filesystem::create_directories(target, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to create directory";
        result.fields["error_message"] = ec.message();
        result.fields["normalized_path"] = target.string();
        return result;
    }
    const bool exists_after = std::filesystem::is_directory(target, ec);
    result.ok = exists_after && !ec;
    result.exit_code = result.ok ? 0 : 47;
    result.fields["normalized_path"] = target.string();
    result.fields["existed_before"] = existed_before ? "true" : "false";
    result.fields["exists_after"] = exists_after ? "true" : "false";
    result.fields["result"] = existed_before ? "already_exists" : "created";
    result.fields["summary"] = result.ok
        ? "directory is available"
        : "directory creation could not be verified";
    result.fields["next_action"] = result.ok
        ? "proceed with file write or patch operation"
        : "inspect error_message and parent path permissions";

    const std::string log_path = BuildLogPath(config, "ensure_directory");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "normalized_path=" << target.string() << "\n";
    log << "existed_before=" << (existed_before ? "true" : "false") << "\n";
    log << "exists_after=" << (exists_after ? "true" : "false") << "\n";
    log.close();
    result.fields["log_path"] = log_path;
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;
    return result;
}

CommandResult RevertSingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & backup_path,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & patch_id,
    const std::string & reason) {
    const PatchRequest patch_request =
        BuildPatchRequest(file_path, std::string(), std::string(), request_id, trace_id, patch_id, reason.empty() ? "single_file_patch_revert" : reason);
    CommandResult result;
    result.fields["backup_path"] = backup_path;

    if (file_path.empty() || backup_path.empty()) {
        result.ok = false;
        result.exit_code = 50;
        result.fields["error"] = "file_path and backup_path are required";
        result.fields["request_id"] = patch_request.request_id;
        result.fields["trace_id"] = patch_request.trace_id;
        result.fields["patch_id"] = patch_request.patch_id;
        result.fields["file_path"] = patch_request.file_path;
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveWorkspaceWritableFilePath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = path_error;
        result.fields["request_id"] = patch_request.request_id;
        result.fields["trace_id"] = patch_request.trace_id;
        result.fields["patch_id"] = patch_request.patch_id;
        result.fields["file_path"] = patch_request.file_path;
        return result;
    }

    std::string backup_content;
    std::string backup_read_error;
    if (!ReadWholeFile(backup_path, &backup_content, &backup_read_error)) {
        result.ok = false;
        result.exit_code = 51;
        result.fields["error"] = backup_read_error;
        result.fields["request_id"] = patch_request.request_id;
        result.fields["trace_id"] = patch_request.trace_id;
        result.fields["patch_id"] = patch_request.patch_id;
        result.fields["file_path"] = patch_request.file_path;
        return result;
    }

    const std::string resource_key = "file:" + normalized.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "target file is busy";
        return result;
    }

    const std::filesystem::path temp_path =
        normalized.string() + ".revert." + TimeStampForFileName() + ".tmp";
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to open temp file";
        return result;
    }
    output.write(backup_content.data(), static_cast<std::streamsize>(backup_content.size()));
    output.close();

    std::error_code ec;
    std::filesystem::rename(temp_path, normalized, ec);
    if (ec) {
        std::filesystem::remove(normalized, ec);
        ec.clear();
        std::filesystem::rename(temp_path, normalized, ec);
    }
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        result.ok = false;
        result.exit_code = 48;
        result.fields["error"] = "failed to replace target file";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "revert_single_file_patch");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "file_path=" << normalized.string() << "\n";
    log << "backup_path=" << backup_path << "\n";
    log << "backup_bytes=" << backup_content.size() << "\n";

    PopulatePatchResultFields(
        &result,
        patch_request,
        normalized.string(),
        std::string(),
        backup_content,
        std::string(),
        patch_request.patch_id + ":revert");
    result.fields["normalized_path"] = normalized.string();
    result.fields["backup_bytes"] = std::to_string(backup_content.size());
    result.fields["log_path"] = log_path;
    result.fields["workspace_guard"] = "path_scope_relaxed";
    result.fields["path_canonicalized"] = "true";
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    result.fields["write_applied"] = "true";
    result.fields["disk_write_completed"] = "true";
    std::string final_content;
    std::string final_read_error;
    if (!ReadWholeFile(normalized, &final_content, &final_read_error)) {
        result.ok = false;
        result.exit_code = 53;
        result.fields["error"] = final_read_error;
        result.fields["result"] = "revert_verify_read_failed";
        return result;
    }
    result.fields["applied_hash"] = StableContentChecksum(final_content);
    result.fields["final_bytes"] = std::to_string(final_content.size());
    result.fields["applied_hash_match"] =
        result.fields["applied_hash"] == StableContentChecksum(backup_content) ? "true" : "false";
    result.fields["write_verified"] = result.fields["applied_hash_match"];
    result.fields["result"] = "reverted";
    if (result.fields["applied_hash_match"] != "true") {
        result.ok = false;
        result.exit_code = 54;
        result.fields["error"] = "revert hash mismatch";
        result.fields["result"] = "revert_verify_hash_mismatch";
        return result;
    }
    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_REVERT",
        "REVERT_SUCCESS",
        result.fields["diff_hash"],
        log_path,
        backup_path,
        "REVERTED",
        "backup restored");
    return result;
}

CommandResult GetPatchAuditTrailResult(
    const AgentConfig & config,
    const std::string & patch_id) {
    CommandResult result;
    result.fields["patch_id"] = patch_id;
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    if (patch_id.empty()) {
        result.ok = false;
        result.exit_code = 55;
        result.fields["error"] = "patch_id is required";
        return result;
    }

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(BuildPatchAuditEventsPath(config), &content, &read_error)) {
        result.ok = false;
        result.exit_code = 56;
        result.fields["error"] = read_error;
        result.fields["result"] = "patch_audit_unavailable";
        return result;
    }

    std::istringstream input(content);
    std::ostringstream matched;
    std::string line;
    int event_count = 0;
    const std::string marker = "\"patch_id\":\"" + codex_lan_agent::JsonEscape(patch_id) + "\"";
    while (std::getline(input, line)) {
        if (line.find(marker) == std::string::npos) {
            continue;
        }
        if (event_count > 0) {
            matched << "\n";
        }
        matched << line;
        ++event_count;
    }

    result.fields["event_count"] = std::to_string(event_count);
    result.fields["events_jsonl"] = matched.str();
    result.fields["result"] = event_count > 0 ? "patch_audit_found" : "patch_audit_missing";
    result.fields["summary"] = event_count > 0
        ? "patch audit trail located"
        : "no patch audit events found for patch_id";
    result.ok = event_count > 0;
    result.exit_code = event_count > 0 ? 0 : 57;
    return result;
}

CommandResult GetTraceAuditTrailResult(
    const AgentConfig & config,
    const std::string & trace_id) {
    CommandResult result;
    result.fields["trace_id"] = trace_id;
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    result.fields["mcp_trace_audit_event_path"] = BuildMcpTraceAuditEventsPath(config);
    result.fields["audit_sources"] = "patch_audit_events,mcp_trace_audit_events";
    if (trace_id.empty()) {
        result.ok = false;
        result.exit_code = 58;
        result.fields["error"] = "trace_id is required";
        result.fields["result"] = "trace_audit_invalid_request";
        return result;
    }

    std::ostringstream matched;
    int event_count = 0;
    int available_source_count = 0;
    int matched_source_count = 0;
    const std::string marker = "\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"";
    const std::vector<std::string> audit_paths = {
        BuildPatchAuditEventsPath(config),
        BuildMcpTraceAuditEventsPath(config)
    };
    result.fields["source_count"] = std::to_string(audit_paths.size());
    for (const std::string & audit_path : audit_paths) {
        if (!std::filesystem::exists(audit_path)) {
            continue;
        }
        ++available_source_count;
        std::string content;
        std::string read_error;
        if (!ReadWholeFile(audit_path, &content, &read_error)) {
            continue;
        }
        std::istringstream input(content);
        std::string line;
        bool matched_this_source = false;
        while (std::getline(input, line)) {
            if (line.find(marker) == std::string::npos) {
                continue;
            }
            if (event_count > 0) {
                matched << "\n";
            }
            matched << line;
            ++event_count;
            matched_this_source = true;
        }
        if (matched_this_source) {
            ++matched_source_count;
        }
    }

    result.fields["event_count"] = std::to_string(event_count);
    result.fields["available_source_count"] = std::to_string(available_source_count);
    result.fields["matched_source_count"] = std::to_string(matched_source_count);
    result.fields["events_jsonl"] = matched.str();
    result.fields["result"] = event_count > 0 ? "trace_audit_found" : "trace_audit_missing";
    result.fields["summary"] = event_count > 0
        ? "trace audit trail located"
        : "no audit events found for trace_id";
    result.fields["next_action"] = event_count > 0
        ? (matched_source_count == static_cast<int>(audit_paths.size())
            ? "use events_jsonl to replay the trace stages"
            : "use events_jsonl to replay the available trace stages; some audit sources did not contain this trace_id")
        : "run a traced tool call or verify trace_id";
    result.ok = event_count > 0;
    result.exit_code = event_count > 0 ? 0 : 57;
    return result;
}

CommandResult GetSupervisionStatusResult(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & goal_id) {
    CommandResult result;
    result.fields["trace_id"] = trace_id;
    result.fields["goal_id"] = goal_id;
    result.fields["mcp_trace_audit_event_path"] = BuildMcpTraceAuditEventsPath(config);
    result.fields["trace_context_found"] = "false";
    result.fields["can_continue"] = "false";
    result.fields["completion_state"] = "unknown";
    result.fields["interruption_reason"] = "";
    result.fields["interruption_stage"] = "";
    result.fields["supervision_query_status"] = "unknown";
    result.fields["supervision_lookup_ok"] = "false";
    result.fields["supervision_decision"] = "block_alarm";
    result.fields["supervision_explanation"] = "";
    result.fields["pre_guard_status"] = "unknown";
    result.fields["pre_guard_reason_code"] = "";
    result.fields["pre_guard_next_action"] = "";
    result.fields["pre_guard_route_target"] = "";
    result.fields["pre_guard_blocked"] = "false";
    result.fields["post_guard_status"] = "unknown";
    result.fields["post_guard_decision"] = "";
    result.fields["post_guard_reason_code"] = "";
    result.fields["post_guard_next_action"] = "";
    result.fields["post_guard_result_valid"] = "false";
    result.fields["acceptance_status"] = "alarm";
    result.fields["acceptance_reason"] = "";
    result.fields["acceptance_next_action_available"] = "false";

    if (trace_id.empty()) {
        result.ok = false;
        result.exit_code = 69;
        result.fields["error"] = "trace_id is required";
        result.fields["result"] = "supervision_status_invalid_request";
        result.fields["summary"] = "trace_id is required for supervision status lookup";
        result.fields["supervision_status"] = "alarm";
        result.fields["goal_status"] = "failed";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["supervision_alarm"] = "true";
        result.fields["supervision_alarm_code"] = "TRACE_CONTEXT_MISSING";
        result.fields["supervision_alarm_message"] = "trace_id is required for supervision status lookup.";
        result.fields["interruption_reason"] = "trace_context_missing";
        result.fields["interruption_stage"] = "supervision_lookup";
        result.fields["supervision_query_status"] = "invalid_request";
        result.fields["supervision_lookup_ok"] = "false";
        result.fields["supervision_decision"] = "block_alarm";
        result.fields["supervision_explanation"] = "trace_id is required, so supervision lookup cannot continue.";
        result.fields["acceptance_reason"] = "TRACE_CONTEXT_MISSING";
        return result;
    }

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(BuildMcpTraceAuditEventsPath(config), &content, &read_error)) {
        result.ok = false;
        result.exit_code = 70;
        result.fields["error"] = read_error;
        result.fields["result"] = "supervision_status_unavailable";
        result.fields["summary"] = "supervision audit log is unavailable";
        result.fields["supervision_status"] = "alarm";
        result.fields["goal_status"] = "failed";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["supervision_alarm"] = "true";
        result.fields["supervision_alarm_code"] = "TRACE_CONTEXT_MISSING";
        result.fields["supervision_alarm_message"] = "supervision audit log is unavailable.";
        result.fields["interruption_reason"] = "supervision_audit_unavailable";
        result.fields["interruption_stage"] = "supervision_lookup";
        result.fields["supervision_query_status"] = "audit_unavailable";
        result.fields["supervision_lookup_ok"] = "false";
        result.fields["supervision_decision"] = "block_alarm";
        result.fields["supervision_explanation"] = "supervision audit log is unavailable, so the trace cannot be validated.";
        result.fields["acceptance_reason"] = "TRACE_CONTEXT_MISSING";
        return result;
    }

    const std::string trace_marker = "\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"";
    const std::string goal_marker = goal_id.empty()
        ? std::string()
        : "\"goal_id\":\"" + codex_lan_agent::JsonEscape(goal_id) + "\"";

    std::istringstream input(content);
    std::string line;
    std::string last_result_line;
    std::string last_alarm_line;
    int event_count = 0;
    while (std::getline(input, line)) {
        if (line.find(trace_marker) == std::string::npos) {
            continue;
        }
        if (!goal_marker.empty() && line.find(goal_marker) == std::string::npos) {
            continue;
        }
        if (ExtractJsonString(line, "tool_name") == "lan_agent_get_supervision_status") {
            continue;
        }
        ++event_count;
        const std::string stage = ExtractJsonString(line, "stage");
        if (stage == "SUPERVISION_ALARM") {
            last_alarm_line = line;
        } else if (stage == "MCP_TOOL_RESULT") {
            last_result_line = line;
        }
    }

    result.fields["event_count"] = std::to_string(event_count);
    if (event_count <= 0) {
        result.ok = false;
        result.exit_code = 71;
        result.fields["result"] = "supervision_status_missing";
        result.fields["summary"] = "no supervision audit events found for trace_id";
        result.fields["supervision_status"] = "alarm";
        result.fields["goal_status"] = "failed";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["supervision_alarm"] = "true";
        result.fields["supervision_alarm_code"] = "TRACE_CONTEXT_MISSING";
        result.fields["supervision_alarm_message"] = "no supervision audit events found for trace_id.";
        result.fields["interruption_reason"] = "trace_context_missing";
        result.fields["interruption_stage"] = "supervision_lookup";
        result.fields["next_action"] = "run a traced tool call or verify trace_id";
        result.fields["supervision_query_status"] = "missing";
        result.fields["supervision_lookup_ok"] = "false";
        result.fields["supervision_decision"] = "block_alarm";
        result.fields["supervision_explanation"] = "No non-query supervision audit events were found for this trace_id.";
        result.fields["acceptance_reason"] = "TRACE_CONTEXT_MISSING";
        result.fields["clips_first_decision"] = "alarm";
        result.fields["clips_first_next_tool"] = "";
        result.fields["clips_first_reason"] = "TRACE_CONTEXT_MISSING";
        return result;
    }

    const std::string chosen_line = !last_alarm_line.empty() ? last_alarm_line : last_result_line;
    const std::string context_line = !last_result_line.empty() ? last_result_line : chosen_line;
    result.fields["last_event_json"] = chosen_line;
    result.fields["trace_context_found"] = "true";
    result.fields["goal_id"] = FirstNonEmpty(goal_id, ExtractJsonString(chosen_line, "goal_id"), "");
    result.fields["tool_call_id"] = ExtractJsonString(chosen_line, "tool_call_id");
    result.fields["supervision_status"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "supervision_status"),
        !last_alarm_line.empty() ? std::string("alarm") : std::string("closed_loop_complete"),
        "closed_loop_complete");
    result.fields["goal_status"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "goal_status"),
        result.fields["supervision_status"] == "closed_loop_complete" ? std::string("complete") : std::string("failed"),
        "complete");
    result.fields["supervision_alarm_code"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "alarm_code"),
        ExtractJsonString(chosen_line, "supervision_alarm_code"),
        "");
    result.fields["supervision_alarm_message"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "alarm_message"),
        ExtractJsonString(chosen_line, "supervision_alarm_message"),
        "");
    result.fields["supervision_alarm"] = result.fields["supervision_alarm_code"].empty() ? "false" : "true";
    result.fields["progress_target_count"] = ExtractJsonString(chosen_line, "progress_target_count");
    result.fields["progress_completed_count"] = ExtractJsonString(chosen_line, "progress_completed_count");
    result.fields["progress_pending_count"] = ExtractJsonString(chosen_line, "progress_pending_count");
    result.fields["progress_failed_count"] = ExtractJsonString(chosen_line, "progress_failed_count");
    result.fields["progress_skipped_count"] = ExtractJsonString(chosen_line, "progress_skipped_count");
    result.fields["next_actions_count"] = FirstNonEmpty(ExtractJsonString(chosen_line, "next_actions_count"), "0", "0");
    result.fields["next_action_0_tool_name"] = ExtractJsonString(chosen_line, "next_action_0_tool_name");
    result.fields["next_action_0_safety_class"] = ExtractJsonString(chosen_line, "next_action_0_safety_class");
    result.fields["next_action_0_params_json"] = ExtractJsonString(chosen_line, "next_action_0_params_json");
    result.fields["next_action_0_reason"] = ExtractJsonString(chosen_line, "next_action_0_reason");
    result.fields["next_action_0_source_rule"] = ExtractJsonString(chosen_line, "next_action_0_source_rule");
    result.fields["next_action_0_trace_id"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "next_action_0_trace_id"),
        trace_id,
        trace_id);
    result.fields["next_action_0_goal_id"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "next_action_0_goal_id"),
        result.fields["goal_id"],
        result.fields["goal_id"]);
    result.fields["next_action_0_params_hash"] = ExtractJsonString(chosen_line, "next_action_0_params_hash");
    result.fields["pre_guard_status"] = FirstNonEmpty(
        FirstNonEmpty(
            ExtractJsonString(chosen_line, "pre_guard_status"),
            ExtractJsonString(context_line, "pre_guard_status"),
            ExtractJsonString(chosen_line, "clips_pre_call_tool_decision")),
        ExtractJsonString(context_line, "clips_pre_call_tool_decision"),
        "allow");
    result.fields["pre_guard_reason_code"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "pre_guard_reason_code"),
        ExtractJsonString(context_line, "pre_guard_reason_code"),
        "");
    result.fields["pre_guard_next_action"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "pre_guard_next_action"),
        ExtractJsonString(context_line, "pre_guard_next_action"),
        "");
    result.fields["pre_guard_route_target"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "pre_guard_route_target"),
        ExtractJsonString(context_line, "pre_guard_route_target"),
        "");
    result.fields["pre_guard_blocked"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "pre_guard_blocked"),
        ExtractJsonString(context_line, "pre_guard_blocked"),
        (result.fields["pre_guard_status"] == "block" || result.fields["pre_guard_status"] == "route")
            ? std::string("true")
            : std::string("false"),
        "false");
    result.fields["post_guard_status"] = FirstNonEmpty(
        FirstNonEmpty(
            ExtractJsonString(chosen_line, "post_guard_status"),
            ExtractJsonString(context_line, "post_guard_status"),
            ExtractJsonString(chosen_line, "verification_status")),
        ExtractJsonString(context_line, "verification_status"),
        "unknown");
    result.fields["post_guard_decision"] = FirstNonEmpty(
        FirstNonEmpty(
            ExtractJsonString(chosen_line, "post_guard_decision"),
            ExtractJsonString(context_line, "post_guard_decision"),
            ExtractJsonString(chosen_line, "clips_post_result_decision")),
        ExtractJsonString(context_line, "clips_post_result_decision"),
        "");
    result.fields["post_guard_reason_code"] = FirstNonEmpty(
        FirstNonEmpty(
            ExtractJsonString(chosen_line, "post_guard_reason_code"),
            ExtractJsonString(context_line, "post_guard_reason_code"),
            ExtractJsonString(chosen_line, "not_verified_reason")),
        FirstNonEmpty(
            ExtractJsonString(context_line, "not_verified_reason"),
            ExtractJsonString(chosen_line, "supervision_alarm_code"),
            ""),
        "");
    result.fields["post_guard_next_action"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "post_guard_next_action"),
        ExtractJsonString(context_line, "post_guard_next_action"),
        "");
    result.fields["post_guard_result_valid"] = FirstNonEmpty(
        ExtractJsonString(chosen_line, "post_guard_result_valid"),
        ExtractJsonString(context_line, "post_guard_result_valid"),
        result.fields["post_guard_status"] == "verified" ? std::string("true") : std::string("false"),
        "false");

    const bool allow_answer = result.fields["supervision_status"] == "closed_loop_complete";
    const bool can_continue =
        result.fields["supervision_status"] == "closed_loop_continue"
        && !result.fields["next_action_0_tool_name"].empty()
        && !result.fields["next_action_0_params_json"].empty();
    result.fields["assistant_response_allowed"] = allow_answer ? "true" : "false";
    result.fields["final_answer_allowed"] = allow_answer ? "true" : "false";
    result.fields["can_continue"] = can_continue ? "true" : "false";
    result.fields["completion_state"] = allow_answer
        ? "complete"
        : (can_continue ? "incomplete" : "interrupted");
    result.fields["interruption_reason"] = allow_answer
        ? ""
        : (result.fields["supervision_status"] == "alarm"
            ? FirstNonEmpty(result.fields["supervision_alarm_code"], "supervision_alarm", "supervision_alarm")
            : (can_continue ? "pending_next_action" : "next_action_unavailable"));
    result.fields["interruption_stage"] = allow_answer
        ? ""
        : (result.fields["supervision_status"] == "alarm"
            ? "supervision_alarm"
            : "closed_loop_continue");
    result.fields["supervision_query_status"] = "found";
    result.fields["supervision_lookup_ok"] = "true";
    result.fields["supervision_decision"] = allow_answer
        ? "allow_final"
        : (can_continue ? "allow_continue" : "block_alarm");
    result.fields["supervision_explanation"] = allow_answer
        ? "Closed loop is complete and final answer is allowed."
        : (can_continue
            ? "Closed loop is incomplete but a supervised next action is available."
            : "Supervision alarm is active or no executable next action is available.");
    result.fields["acceptance_status"] = allow_answer
        ? "complete"
        : (can_continue ? "continue" : "alarm");
    result.fields["acceptance_reason"] = allow_answer
        ? "closed_loop_complete"
        : (can_continue
            ? FirstNonEmpty(result.fields["next_action_0_reason"], "next_action_required", "next_action_required")
            : FirstNonEmpty(result.fields["supervision_alarm_code"], result.fields["post_guard_reason_code"], "acceptance_alarm"));
    result.fields["acceptance_next_action_available"] = can_continue ? "true" : "false";
    result.fields["clips_first_decision"] = allow_answer
        ? "complete"
        : (can_continue ? "continue" : "alarm");
    result.fields["clips_first_next_tool"] = result.fields["next_action_0_tool_name"];
    result.fields["clips_first_reason"] = allow_answer
        ? "closed_loop_complete"
        : (can_continue
            ? FirstNonEmpty(result.fields["next_action_0_reason"], "next_action_required", "next_action_required")
            : FirstNonEmpty(result.fields["supervision_alarm_code"], result.fields["post_guard_reason_code"], "acceptance_alarm"));
    result.fields["result"] = "supervision_status_found";
    result.fields["summary"] = allow_answer
        ? "supervision closed loop is complete"
        : (result.fields["supervision_status"] == "alarm"
            ? "supervision alarm is active"
            : "supervision closed loop requires continuation");
    result.fields["next_action"] = allow_answer
        ? "final answer is allowed"
        : (!result.fields["next_action_0_tool_name"].empty()
            ? "execute the next supervised action before allowing any assistant conclusion"
            : "investigate supervision alarm before allowing any assistant conclusion");
    result.ok = true;
    result.exit_code = 0;
    return result;
}

CommandResult VerifySingleFilePatchResult(
    const AgentConfig & config,
    const std::string & patch_id,
    const std::string & file_path,
    const std::string & expected_hash,
    const std::string & contains_text,
    const std::string & forbidden_text,
    const std::string & request_id = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & reason = std::string()) {
    const PatchRequest patch_request = BuildPatchRequest(
        file_path,
        std::string(),
        expected_hash,
        request_id,
        trace_id,
        patch_id,
        reason.empty() ? "single_file_patch_verify" : reason);
    CommandResult result;
    result.fields["patch_id"] = patch_request.patch_id;
    result.fields["request_id"] = patch_request.request_id;
    result.fields["trace_id"] = patch_request.trace_id;
    result.fields["file_path"] = patch_request.file_path;
    result.fields["expected_hash"] = expected_hash;
    result.fields["contains_text"] = contains_text;
    result.fields["forbidden_text"] = forbidden_text;
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    result.fields["verify_id"] =
        patch_request.patch_id + ":verify:" + BuildRequestTimestampToken();

    if (patch_id.empty() || file_path.empty()) {
        result.ok = false;
        result.exit_code = 58;
        result.fields["error"] = "patch_id and file_path are required";
        result.fields["result"] = "patch_verify_invalid_request";
        result.fields["summary"] = "patch verify request is missing patch_id or file_path";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveWorkspaceFilePath(config, file_path, &normalized, &path_error)) {
        if (!TryResolveWorkspaceWritableFilePath(config, file_path, &normalized, &path_error)) {
            result.ok = false;
            result.exit_code = 45;
            result.fields["error"] = path_error;
            result.fields["result"] = "patch_verify_invalid_path";
            result.fields["summary"] = path_error;
            return result;
        }
    }

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        result.fields["result"] = "patch_verify_read_failed";
        result.fields["summary"] = "failed to read file for patch verification";
        return result;
    }

    const std::string actual_hash = StableContentChecksum(content);
    const bool hash_ok = expected_hash.empty() || expected_hash == actual_hash;
    const bool contains_ok = contains_text.empty() || content.find(contains_text) != std::string::npos;
    const bool forbidden_ok = forbidden_text.empty() || content.find(forbidden_text) == std::string::npos;
    const bool verification_ok = hash_ok && contains_ok && forbidden_ok;

    result.fields["normalized_path"] = normalized.string();
    result.fields["actual_hash"] = actual_hash;
    result.fields["hash_match"] = hash_ok ? "true" : "false";
    result.fields["contains_text_found"] = contains_ok ? "true" : "false";
    result.fields["forbidden_text_found"] = forbidden_text.empty()
        ? "false"
        : (forbidden_ok ? "false" : "true");
    result.fields["verify_stage"] = "readback_hash_and_text";
    result.fields["verification_ok"] = verification_ok ? "true" : "false";
    result.fields["verification_mode"] = "hash_and_text";
    result.fields["result"] = verification_ok ? "patch_verify_passed" : "patch_verify_failed";
    result.fields["semantic_outcome"] = verification_ok
        ? "patch_content_verified"
        : "patch_content_needs_repair";
    result.fields["summary"] = verification_ok
        ? "patch verification passed"
        : "patch verification failed";
    result.ok = verification_ok;
    result.exit_code = verification_ok ? 0 : 59;

    if (!verification_ok) {
        const std::string repair_candidate_id =
            patch_request.patch_id + ":repair:" + BuildRequestTimestampToken();
        result.fields["repair_candidate_id"] = repair_candidate_id;
        result.fields["repair_candidate_status"] = "candidate";
        result.fields["repair_candidate_tool"] = "lan_agent_preview_patch";
        result.fields["repair_candidate_scope"] = "single_file_patch";
        result.fields["repair_candidate_reason"] =
            !hash_ok ? "expected_hash_mismatch"
            : (!contains_ok ? "required_text_missing" : "forbidden_text_present");
        result.fields["repair_reason"] = result.fields["repair_candidate_reason"];
        result.fields["max_repair_retry"] = "2";
        result.fields["next_action"] =
            "review patch_audit_trail and generate a repaired preview with the same patch_id";
        result.fields["repair_candidate_arguments_json"] =
            std::string("{\"file_path\":\"")
            + codex_lan_agent::JsonEscape(file_path)
            + "\",\"patch_id\":\""
            + codex_lan_agent::JsonEscape(patch_request.patch_id)
            + "\"}";
    } else {
        result.fields["next_action"] = "patch verification complete";
    }

    AppendPatchAuditEvent(
        config,
        patch_request,
        "PATCH_VERIFY_PIPELINE",
        verification_ok ? "VERIFY_PIPELINE_SUCCESS" : "VERIFY_PIPELINE_FAILED",
        actual_hash,
        std::string(),
        std::string(),
        verification_ok ? "VERIFIED" : "FAILED",
        verification_ok ? "hash/text verification passed" : "hash/text verification failed");
    return result;
}
