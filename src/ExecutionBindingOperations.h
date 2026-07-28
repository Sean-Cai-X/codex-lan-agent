#pragma once

std::string BuildExecutionBindingOptFileTargetName() {
    return "execution_binding.jsonl";
}

std::string BuildExecutionBindingJson(
    const std::string & remote_chat_session_id,
    const std::string & local_ai_thread_message_id,
    const std::string & task_id,
    const std::string & evidence_ref,
    const std::string & result_ref,
    const std::string & session_title,
    const std::string & primary_intent,
    const std::string & tool_name,
    const std::string & status) {
    std::ostringstream output;
    output << "{"
           << "\"timestamp\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\","
           << "\"remote_chat_session_id\":\"" << codex_lan_agent::JsonEscape(remote_chat_session_id) << "\","
           << "\"local_ai_thread_message_id\":\"" << codex_lan_agent::JsonEscape(local_ai_thread_message_id) << "\","
           << "\"task_id\":\"" << codex_lan_agent::JsonEscape(task_id) << "\","
           << "\"session_title\":\"" << codex_lan_agent::JsonEscape(session_title) << "\","
           << "\"primary_intent\":\"" << codex_lan_agent::JsonEscape(primary_intent) << "\","
           << "\"tool\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
           << "\"status\":\"" << codex_lan_agent::JsonEscape(status) << "\","
           << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(evidence_ref) << "\","
           << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(result_ref) << "\""
           << "}";
    return output.str();
}

std::string EscapeForSingleQuotedPowerShell(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

CommandResult WriteExecutionBindingResult(
    const AgentConfig & config,
    const std::string & binding_json) {
    CommandResult result;
    result.fields["binding_transport"] = "agent_optfile_apply_write";
    result.fields["target_name"] = BuildExecutionBindingOptFileTargetName();
    if (binding_json.empty()) {
        result.ok = false;
        result.exit_code = 77;
        result.fields["error"] = "binding_json is empty";
        return result;
    }

    CommandResult profile_result = OptFileApplyWriteResult(
        config,
        BuildExecutionBindingOptFileTargetName(),
        binding_json + "\n",
        true);
    result = profile_result;
    result.fields["binding_transport"] = "agent_optfile_apply_write";
    result.fields["target_name"] = BuildExecutionBindingOptFileTargetName();
    result.fields["binding_json"] = binding_json;
    result.fields["recorded"] = profile_result.ok ? "true" : "false";
    return result;
}

CommandResult ReadExecutionBindingResult(
    const AgentConfig & config,
    const std::string & task_id_filter,
    const std::string & remote_chat_session_id_filter,
    int max_entries) {
    CommandResult result = RunCliProfile(
        config,
        "operate_optfile",
        "-Operation read -TargetName \"" + BuildExecutionBindingOptFileTargetName() + "\"");
    result.fields["binding_transport"] = "operate_optfile_to_optfile_exe";
    result.fields["target_name"] = BuildExecutionBindingOptFileTargetName();
    result.fields["task_id_filter"] = task_id_filter;
    result.fields["remote_chat_session_id_filter"] = remote_chat_session_id_filter;
    if (!result.ok) {
        return result;
    }

    std::string log_content;
    std::string read_error;
    if (!ReadWholeFile(GetFieldOrDefault(result, "log_path", ""), &log_content, &read_error)) {
        result.ok = false;
        result.exit_code = 78;
        result.fields["error"] = read_error;
        return result;
    }

    const std::string content = ExtractDelimitedBlock(log_content, "content_begin", "content_end");
    result.fields["content"] = content;
    result.fields["checksum"] = StableContentChecksum(content);
    std::vector<std::string> matched_entries;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (!task_id_filter.empty() &&
            trimmed.find("\"task_id\":\"" + task_id_filter + "\"") == std::string::npos) {
            continue;
        }
        if (!remote_chat_session_id_filter.empty() &&
            trimmed.find("\"remote_chat_session_id\":\"" + remote_chat_session_id_filter + "\"") == std::string::npos) {
            continue;
        }
        matched_entries.push_back(trimmed);
    }

    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const int start_index = matched_entries.size() > static_cast<std::size_t>(bounded_max_entries)
        ? static_cast<int>(matched_entries.size() - bounded_max_entries)
        : 0;
    std::ostringstream tail;
    for (std::size_t index = static_cast<std::size_t>(start_index); index < matched_entries.size(); ++index) {
        if (index > static_cast<std::size_t>(start_index)) {
            tail << "\n";
        }
        tail << matched_entries[index];
    }
    result.fields["matched_entry_count"] = std::to_string(matched_entries.size());
    result.fields["matched_entries_tail"] = tail.str();
    result.fields["latest_entry"] = matched_entries.empty() ? "" : matched_entries.back();
    result.fields["result"] = matched_entries.empty() ? "no_match" : "read";
    return result;
}

CommandResult BuildOptFileBaseResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & operation,
    bool append) {
    CommandResult result;
    const std::filesystem::path runtime_dir(BuildOptFileRuntimeDir(config));
    const std::filesystem::path target_path = BuildOptFileTargetPath(config, target_name);
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "optfile_" + operation;
    result.fields["operation"] = operation;
    result.fields["target_name"] = SanitizeOptFileTargetName(target_name);
    result.fields["target_path"] = target_path.string();
    result.fields["runtime_dir"] = runtime_dir.string();
    result.fields["append"] = append ? "true" : "false";
    result.fields["safety_scope"] = "log_root/optfile_runtime";
    result.fields["optfile_exe_policy"] = "never_modify_optfile_exe";
    result.fields["trace_log_path"] = BuildRemoteControlEventsPath(config);
    return result;
}
