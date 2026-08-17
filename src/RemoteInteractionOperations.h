#pragma once

#include "comm.h"

std::string GetHeaderValue(const HttpRequest & request, const std::string & name) {
    const auto it = request.headers.find(ToLowerAscii(name));
    return it == request.headers.end() ? std::string() : it->second;
}

std::string ExtractResultField(const std::string & response_body, const std::string & key) {
    const std::string value = ExtractJsonString(response_body, key);
    return value;
}

std::string FileNameFromPathString(const std::string & value) {
    if (value.empty()) {
        return std::string();
    }
    std::error_code ec;
    const std::filesystem::path path(value);
    const std::string filename = path.filename().string();
    if (!filename.empty() && !ec) {
        return filename;
    }
    return value;
}

CommandResult ListRecentRemoteEventsResult(
    const AgentConfig & config,
    int max_entries,
    int offset,
    bool include_auto,
    bool include_noise,
    bool ai_only,
    const std::string & since_timestamp,
    const std::string & request_type_filter,
    const std::string & command_name_filter,
    const std::string & session_id_filter,
    const std::string & task_id_filter) {
    CommandResult result;
    result.fields["events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["max_entries"] = std::to_string(max_entries);
    result.fields["offset"] = std::to_string(offset);
    result.fields["include_auto"] = include_auto ? "true" : "false";
    result.fields["include_noise"] = include_noise ? "true" : "false";
    result.fields["ai_only"] = ai_only ? "true" : "false";
    result.fields["since_timestamp"] = since_timestamp;
    result.fields["request_type_filter"] = request_type_filter;
    result.fields["command_name_filter"] = command_name_filter;
    result.fields["session_id_filter"] = session_id_filter;
    result.fields["task_id_filter"] = task_id_filter;

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(result.fields["events_path"], &content, &read_error)) {
        result.ok = false;
        result.exit_code = 44;
        result.fields["error"] = read_error;
        result.fields["summary"] = "remote_control_events unavailable";
        return result;
    }

    std::vector<std::string> matched_lines;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (!include_auto &&
            ToLowerAscii(ExtractJsonString(trimmed, "record_to_latest")) != "true") {
            continue;
        }
        if (!include_noise &&
            ToLowerAscii(ExtractJsonString(trimmed, "observation_visible")) != "true") {
            continue;
        }
        if (ai_only &&
            ToLowerAscii(ExtractJsonString(trimmed, "interaction_kind")) != "ai_interaction") {
            continue;
        }
        if (!since_timestamp.empty()) {
            const std::string event_timestamp = ExtractJsonString(trimmed, "timestamp");
            if (!event_timestamp.empty() && event_timestamp <= since_timestamp) {
                continue;
            }
        }
        if (!request_type_filter.empty() &&
            ExtractJsonString(trimmed, "request_type") != request_type_filter) {
            continue;
        }
        if (!command_name_filter.empty() &&
            ExtractJsonString(trimmed, "command_name") != command_name_filter) {
            continue;
        }
        if (!session_id_filter.empty() &&
            ExtractJsonString(trimmed, "session_id") != session_id_filter) {
            continue;
        }
        if (!task_id_filter.empty() &&
            ExtractJsonString(trimmed, "task_id") != task_id_filter) {
            continue;
        }
        matched_lines.push_back(trimmed);
    }

    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const int bounded_offset = std::max(0, offset);
    const std::size_t reverse_start = matched_lines.size() > static_cast<std::size_t>(bounded_offset)
        ? matched_lines.size() - static_cast<std::size_t>(bounded_offset)
        : 0;
    const std::size_t start = reverse_start > static_cast<std::size_t>(bounded_max_entries)
        ? reverse_start - static_cast<std::size_t>(bounded_max_entries)
        : 0;
    const std::size_t end = reverse_start;

    std::ostringstream events_json;
    events_json << "[";
    int visible_index = 0;
    for (std::size_t index = start; index < end; ++index) {
        const std::string & event_line = matched_lines[index];
        const std::string prefix = "item_" + std::to_string(visible_index) + "_";
        result.fields[prefix + "timestamp"] = ExtractJsonString(event_line, "timestamp");
        result.fields[prefix + "source_thread"] = ExtractJsonString(event_line, "source_thread");
        result.fields[prefix + "source_label"] = ExtractJsonString(event_line, "source_label");
        result.fields[prefix + "takeover_relation"] = ExtractJsonString(event_line, "takeover_relation");
        result.fields[prefix + "task_group"] = ExtractJsonString(event_line, "task_group");
        result.fields[prefix + "interaction_kind"] = ExtractJsonString(event_line, "interaction_kind");
        result.fields[prefix + "observation_visible"] = ExtractJsonString(event_line, "observation_visible");
        result.fields[prefix + "request_type"] = ExtractJsonString(event_line, "request_type");
        result.fields[prefix + "command_name"] = ExtractJsonString(event_line, "command_name");
        result.fields[prefix + "path"] = ExtractJsonString(event_line, "path");
        result.fields[prefix + "status"] = ExtractJsonString(event_line, "status");
        result.fields[prefix + "summary"] = ExtractJsonString(event_line, "summary");
        result.fields[prefix + "task_id"] = ExtractJsonString(event_line, "task_id");
        result.fields[prefix + "session_id"] = ExtractJsonString(event_line, "session_id");
        result.fields[prefix + "turn_id"] = ExtractJsonString(event_line, "turn_id");
        result.fields[prefix + "write_mode"] = ExtractJsonString(event_line, "write_mode");
        result.fields[prefix + "task_log_ref"] = ExtractJsonString(event_line, "task_log_ref");
        result.fields[prefix + "result_ref"] = ExtractJsonString(event_line, "result_ref");
        result.fields[prefix + "evidence_ref"] = ExtractJsonString(event_line, "evidence_ref");
        result.fields[prefix + "duration_ms"] = ExtractJsonString(event_line, "duration_ms");
        if (visible_index > 0) {
            events_json << ",";
        }
        events_json << event_line;
        ++visible_index;
    }
    events_json << "]";

    result.fields["visible_count"] = std::to_string(visible_index);
    result.fields["matched_count"] = std::to_string(matched_lines.size());
    result.fields["has_more"] = start > 0 ? "true" : "false";
    result.fields["next_offset"] = start > 0
        ? std::to_string(bounded_offset + visible_index)
        : std::string();
    result.fields["events_json"] = events_json.str();
    if (visible_index > 0) {
        result.fields["latest_timestamp"] = result.fields["item_" + std::to_string(visible_index - 1) + "_timestamp"];
        result.fields["latest_source_label"] = result.fields["item_" + std::to_string(visible_index - 1) + "_source_label"];
        result.fields["latest_takeover_relation"] = result.fields["item_" + std::to_string(visible_index - 1) + "_takeover_relation"];
        result.fields["latest_task_group"] = result.fields["item_" + std::to_string(visible_index - 1) + "_task_group"];
        result.fields["latest_interaction_kind"] = result.fields["item_" + std::to_string(visible_index - 1) + "_interaction_kind"];
        result.fields["latest_command_name"] = result.fields["item_" + std::to_string(visible_index - 1) + "_command_name"];
        result.fields["latest_status"] = result.fields["item_" + std::to_string(visible_index - 1) + "_status"];
        result.fields["latest_session_id"] = result.fields["item_" + std::to_string(visible_index - 1) + "_session_id"];
        result.fields["latest_turn_id"] = result.fields["item_" + std::to_string(visible_index - 1) + "_turn_id"];
        result.fields["latest_write_mode"] = result.fields["item_" + std::to_string(visible_index - 1) + "_write_mode"];
        result.fields["latest_task_log_ref"] = result.fields["item_" + std::to_string(visible_index - 1) + "_task_log_ref"];
        result.fields["latest_result_ref"] = result.fields["item_" + std::to_string(visible_index - 1) + "_result_ref"];
        result.fields["latest_evidence_ref"] = result.fields["item_" + std::to_string(visible_index - 1) + "_evidence_ref"];
        result.fields["summary"] = "recent remote events listed";
    } else {
        result.fields["summary"] = "no matching remote events";
    }
    return result;
}

CommandResult QueryRemoteTaskResultRefsResult(
    const AgentConfig & config,
    const std::string & session_id_filter,
    const std::string & task_group_filter,
    const std::string & runner_filter,
    const std::string & command_name_filter,
    int max_entries) {
    CommandResult result;
    result.fields["events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["session_id_filter"] = session_id_filter;
    result.fields["task_group_filter"] = task_group_filter;
    result.fields["runner_filter"] = runner_filter;
    result.fields["command_name_filter"] = command_name_filter;

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(result.fields["events_path"], &content, &read_error)) {
        result.ok = false;
        result.exit_code = 44;
        result.fields["error"] = read_error;
        result.fields["summary"] = "remote_control_events unavailable";
        return result;
    }

    std::vector<std::string> matched_lines;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (ToLowerAscii(ExtractJsonString(trimmed, "record_to_latest")) != "true") {
            continue;
        }
        if (!session_id_filter.empty() &&
            ExtractJsonString(trimmed, "session_id") != session_id_filter) {
            continue;
        }
        if (!task_group_filter.empty() &&
            ExtractJsonString(trimmed, "task_group") != task_group_filter) {
            continue;
        }
        if (!runner_filter.empty() &&
            ExtractJsonString(trimmed, "runner") != runner_filter) {
            continue;
        }
        if (!command_name_filter.empty() &&
            ExtractJsonString(trimmed, "command_name") != command_name_filter) {
            continue;
        }
        matched_lines.push_back(trimmed);
    }

    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const std::size_t start =
        matched_lines.size() > static_cast<std::size_t>(bounded_max_entries)
            ? matched_lines.size() - static_cast<std::size_t>(bounded_max_entries)
            : 0;

    int visible_index = 0;
    for (std::size_t index = start; index < matched_lines.size(); ++index) {
        const std::string & event_line = matched_lines[index];
        const std::string prefix = "item_" + std::to_string(visible_index) + "_";
        const std::string task_id = ExtractJsonString(event_line, "task_id");
        const std::string task_log_ref = ExtractJsonString(event_line, "task_log_ref");
        result.fields[prefix + "timestamp"] = ExtractJsonString(event_line, "timestamp");
        result.fields[prefix + "session_id"] = ExtractJsonString(event_line, "session_id");
        result.fields[prefix + "task_group"] = ExtractJsonString(event_line, "task_group");
        result.fields[prefix + "runner"] = ExtractJsonString(event_line, "runner");
        result.fields[prefix + "command_name"] = ExtractJsonString(event_line, "command_name");
        result.fields[prefix + "status"] = ExtractJsonString(event_line, "status");
        result.fields[prefix + "task_id"] = task_id;
        result.fields[prefix + "task_log_ref"] = task_log_ref;
        result.fields[prefix + "result_ref"] = ExtractJsonString(event_line, "result_ref");
        result.fields[prefix + "evidence_ref"] = ExtractJsonString(event_line, "evidence_ref");
        result.fields[prefix + "summary"] = ExtractJsonString(event_line, "summary");

        if (!task_id.empty() && g_task_manager != nullptr) {
            const CommandResult task_result = g_task_manager->GetTaskResult(task_id);
            result.fields[prefix + "resolved_log_path"] = GetFieldOrDefault(task_result, "resolved_log_path", "");
            result.fields[prefix + "resolved_result_ref"] = GetFieldOrDefault(task_result, "result_ref", "");
            result.fields[prefix + "resolved_evidence_ref"] = GetFieldOrDefault(task_result, "evidence_ref", "");
            result.fields[prefix + "task_status"] = GetFieldOrDefault(task_result, "status", "");
        } else {
            result.fields[prefix + "resolved_log_path"] = "";
            result.fields[prefix + "resolved_result_ref"] = "";
            result.fields[prefix + "resolved_evidence_ref"] = "";
            result.fields[prefix + "task_status"] = "";
        }

        ++visible_index;
    }

    result.fields["visible_count"] = std::to_string(visible_index);
    result.fields["matched_count"] = std::to_string(matched_lines.size());
    if (visible_index > 0) {
        const std::string latest_prefix = "item_" + std::to_string(visible_index - 1) + "_";
        result.fields["latest_timestamp"] = result.fields[latest_prefix + "timestamp"];
        result.fields["latest_session_id"] = result.fields[latest_prefix + "session_id"];
        result.fields["latest_task_group"] = result.fields[latest_prefix + "task_group"];
        result.fields["latest_runner"] = result.fields[latest_prefix + "runner"];
        result.fields["latest_command_name"] = result.fields[latest_prefix + "command_name"];
        result.fields["latest_status"] = result.fields[latest_prefix + "status"];
        result.fields["latest_task_id"] = result.fields[latest_prefix + "task_id"];
        result.fields["latest_task_log_ref"] = result.fields[latest_prefix + "task_log_ref"];
        result.fields["latest_result_ref"] = result.fields[latest_prefix + "result_ref"];
        result.fields["latest_evidence_ref"] = result.fields[latest_prefix + "evidence_ref"];
        result.fields["latest_resolved_log_path"] = result.fields[latest_prefix + "resolved_log_path"];
        result.fields["latest_resolved_result_ref"] = result.fields[latest_prefix + "resolved_result_ref"];
        result.fields["latest_resolved_evidence_ref"] = result.fields[latest_prefix + "resolved_evidence_ref"];
        result.fields["result_ref"] = FirstNonEmpty(
            result.fields["latest_resolved_result_ref"],
            result.fields["latest_result_ref"]);
        result.fields["evidence_ref"] = FirstNonEmpty(
            result.fields["latest_resolved_evidence_ref"],
            result.fields["latest_evidence_ref"]);
        result.fields["resolved_log_path"] = result.fields["latest_resolved_log_path"];
        result.fields["summary"] = "remote task result refs matched";
        result.fields["result"] = "remote_task_result_refs_matched";
    } else {
        result.fields["summary"] = "no matching remote task result refs";
        result.fields["result"] = "remote_task_result_refs_empty";
    }
    return result;
}

bool LooksManualSourceThread(const std::string & source_thread) {
    const std::string lowered = ToLowerAscii(source_thread);
    return lowered.empty() ||
        lowered == "unknown" ||
        lowered == "manual" ||
        lowered == "human" ||
        lowered.find("edge") != std::string::npos ||
        lowered.find("browser") != std::string::npos;
}

std::string ClassifyUnifiedSourceLabel(
    const std::string & source_thread,
    const std::string & trigger,
    const HttpRequest & request) {
    const bool manual_source = LooksManualSourceThread(source_thread);
    const bool is_mcp = request.path == "/mcp";
    if (!manual_source && trigger == "manual" && is_mcp) {
        return "mixed";
    }
    if (!manual_source) {
        return "codex";
    }
    return "manual";
}

std::string BuildTakeoverRelation(
    const std::string & source_label,
    const std::string & source_thread,
    const HttpRequest & request) {
    if (source_label == "mixed") {
        return "manual_request_with_codex_execution";
    }
    if (source_label == "codex") {
        return "codex_managed";
    }
    if (request.path == "/mcp") {
        return "manual_direct_mcp";
    }
    return source_thread.empty() || source_thread == "unknown"
        ? "manual_direct_remote"
        : "manual_thread_managed";
}

std::string BuildRemoteTaskGroup(
    const std::string & explicit_task_group,
    const std::string & task_id,
    const std::string & command_name,
    const std::string & request_type,
    const std::string & result_ref,
    const std::string & evidence_ref) {
    if (!explicit_task_group.empty()) {
        return explicit_task_group;
    }
    if (!task_id.empty()) {
        return "task:" + task_id;
    }
    if (!result_ref.empty()) {
        return request_type + ":" + command_name + ":" + FileNameFromPathString(result_ref);
    }
    if (!evidence_ref.empty()) {
        return request_type + ":" + command_name + ":" + FileNameFromPathString(evidence_ref);
    }
    return request_type + ":" + command_name + ":direct";
}

bool IsAiInteractionCommandName(const std::string & command_name) {
    return command_name == "lan_agent_run_local_chat"
        || command_name == "lan_agent_enqueue_local_chat"
        || command_name == "rag.query"
        || command_name == "lan_agent_ventriloquist_reply"
        || command_name == "lan_agent_remote_session_new_turn"
        || command_name == "lan_agent_remote_session_append_turn"
        || command_name == "remote-session/new-turn"
        || command_name == "remote-session/append-turn"
        || command_name == "llama.observer_smoke";
}

bool IsObservationNoiseCommandName(const std::string & command_name) {
    return command_name == "tools/list"
        || command_name == "initialize"
        || command_name == "notifications/initialized"
        || command_name == "initialized"
        || command_name == "llama-webui-mcp";
}

std::string ClassifyRemoteCommandName(const HttpRequest & request) {
    if (request.path == "/mcp" || request.path == "/tools") {
        const std::string tool_name = ExtractJsonString(request.body, "name");
        if (!tool_name.empty()) {
            if (tool_name == "local_cli" || tool_name == "codex_local_cli" || tool_name == "lan_agent_run_command") {
                const std::string local_command = ExtractJsonString(request.body, "command");
                if (!local_command.empty()) {
                    return tool_name + ":" + local_command;
                }
            }
            return tool_name;
        }
        const std::string method = ExtractJsonString(request.body, "method");
        return method.empty() ? "mcp" : method;
    }
    if (request.path.rfind("/tasks/", 0) == 0) {
        return "task";
    }
    if (request.path == "/task-log") {
        return "task-log";
    }
    if (request.path == "/health" || request.path == "/healthz") {
        return "health";
    }
    if (request.path.size() > 1 && request.path[0] == '/') {
        return request.path.substr(1);
    }
    return request.path.empty() ? "unknown" : request.path;
}

std::string ClassifyRemoteRequestType(const HttpRequest & request) {
    if (request.path == "/health" || request.path == "/healthz" || request.path == "/runtime-overview") {
        return "health";
    }
    if (request.path == "/mcp" || request.path == "/tools") {
        const std::string tool_name = ExtractJsonString(request.body, "name");
        if (tool_name == "local_cli" || tool_name == "codex_local_cli" || tool_name == "lan_agent_run_command") {
            return "local_cli";
        }
        return "mcp";
    }
    if (request.path.rfind("/tasks/", 0) == 0) {
        return "task";
    }
    if (request.path == "/task-log") {
        return "task_log";
    }
    if (request.path.find("upload") != std::string::npos) {
        return "upload";
    }
    if (request.path.find("download") != std::string::npos) {
        return "download";
    }
    return "action";
}

std::string DefaultRemoteTrigger(const HttpRequest & request) {
    if (request.method == "GET" || request.method == "HEAD" || request.method == "OPTIONS") {
        return "auto";
    }
    return "manual";
}

std::string BuildRemoteControlEventJson(
    const std::unordered_map<std::string, std::string> & fields) {
    std::ostringstream output;
    output << "{";
    bool first = true;
    for (const auto & entry : fields) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << "\"" << codex_lan_agent::JsonEscape(entry.first) << "\":\""
               << codex_lan_agent::JsonEscape(entry.second) << "\"";
    }
    output << "}";
    return output.str();
}

void WriteToolUseStdoutEvent(
    const AgentConfig & config,
    const HttpRequest & request,
    const HttpResponseSpec & response,
    const std::string & request_finished_at,
    const std::string & command_name,
    const std::string & status,
    long long duration_ms) {
    if (!IsAgentServerStdoutFileLogEnabled()) {
        return;
    }
    std::unordered_map<std::string, std::string> event;
    const std::string result_tool_name = ExtractResultField(response.body, "tool_name");
    const std::string visible_tool_name = ExtractResultField(response.body, "visible_tool_name");
    event["timestamp"] = request_finished_at;
    event["record_model"] = "agent_server_stdout_tool_event_v1";
    event["event_type"] = "tool_call";
    event["tool_name"] = FirstNonEmpty(visible_tool_name, result_tool_name, command_name);
    event["command_name"] = command_name;
    event["routed_tool_name"] = ExtractResultField(response.body, "routed_tool_name");
    event["required_tool_name"] = ExtractResultField(response.body, "required_tool_name");
    event["mcp_route_mode"] = ExtractResultField(response.body, "mcp_route_mode");
    event["entry_name"] = request.method + " " + request.path;
    event["method"] = request.method;
    event["path"] = request.path;
    event["status"] = status;
    event["http_status"] = std::to_string(response.status_code);
    event["duration_ms"] = std::to_string(duration_ms);
    event["trace_id"] = ExtractResultField(response.body, "trace_id");
    event["goal_id"] = ExtractResultField(response.body, "goal_id");
    event["result"] = ExtractResultField(response.body, "result");
    event["request_id"] = FirstNonEmpty(
        ExtractResultField(response.body, "request_id"),
        ExtractJsonString(request.body, "request_id"));
    event["primary_intent"] = FirstNonEmpty(
        ExtractResultField(response.body, "primary_intent"),
        ExtractJsonString(request.body, "primary_intent"));
    event["request_target"] = FirstNonEmpty(
        ExtractJsonString(request.body, "file_path"),
        ExtractJsonString(request.body, "directory_path"),
        ExtractJsonString(request.body, "target_name"));
    event["resolved_target"] = FirstNonEmpty(
        ExtractResultField(response.body, "normalized_path"),
        ExtractResultField(response.body, "file_path"),
        ExtractResultField(response.body, "target_path"),
        ExtractResultField(response.body, "requested_path"));
    AppendAgentServerStdoutFileLogLine(
        config,
        std::string("[tool_event] ") + BuildRemoteControlEventJson(event));
}

void AppendRemoteControlEvent(
    const AgentConfig & config,
    const HttpRequest & request,
    const HttpResponseSpec & response,
    const std::string & request_started_at,
    const std::string & request_finished_at,
    long long duration_ms) {
    std::unordered_map<std::string, std::string> event;
    const std::string source_thread = FirstNonEmpty(
        GetHeaderValue(request, "x-source-thread"),
        ExtractJsonString(request.body, "source_thread"),
        GetQueryParamValue(request, "source_thread"));
    const std::string trigger = FirstNonEmpty(
        GetHeaderValue(request, "x-trigger"),
        ExtractJsonString(request.body, "trigger"),
        FirstNonEmpty(GetQueryParamValue(request, "trigger"), DefaultRemoteTrigger(request)));
    const std::string task_id = FirstNonEmpty(
        ExtractResultField(response.body, "task_id"),
        ExtractTaskIdFromPath(request.path),
        ExtractJsonString(request.body, "task_id"));
    const std::string status = FirstNonEmpty(
        ExtractResultField(response.body, "status"),
        response.status_text,
        std::to_string(response.status_code));
    const std::string command_name = ClassifyRemoteCommandName(request);
    const std::string request_type = ClassifyRemoteRequestType(request);
    const std::string interaction_kind = IsAiInteractionCommandName(command_name)
        ? "ai_interaction"
        : "remote_operation";
    const bool is_noise = IsObservationNoiseCommandName(command_name);
    const std::string result_ref = FirstNonEmpty(
        ExtractResultField(response.body, "result_log_path"),
        ExtractResultField(response.body, "result_ref"),
        ExtractResultField(response.body, "log_path"),
        FirstNonEmpty(
            ExtractResultField(response.body, "file_path"),
            ExtractResultField(response.body, "experience_card_path")));
    const std::string task_log_ref = task_id.empty()
        ? std::string()
        : ("task-log(" + task_id + ")");
    const std::string evidence_ref = FirstNonEmpty(
        ExtractResultField(response.body, "evidence_ref"),
        ExtractResultField(response.body, "trace_log_path"),
        result_ref,
        task_log_ref);
    const std::string source_label = ClassifyUnifiedSourceLabel(source_thread, trigger, request);
    const std::string takeover_relation = BuildTakeoverRelation(source_label, source_thread, request);
    const std::string explicit_task_group = FirstNonEmpty(
        ExtractResultField(response.body, "task_group"),
        ExtractResultField(response.body, "task_group_id"),
        ExtractJsonString(request.body, "task_group"),
        ExtractJsonString(request.body, "task_group_id"));
    const std::string task_group = BuildRemoteTaskGroup(
        explicit_task_group,
        task_id,
        command_name,
        request_type,
        result_ref,
        evidence_ref);
    const std::string runner = FirstNonEmpty(
        ExtractResultField(response.body, "runner"),
        ExtractResultField(response.body, "runner_label"),
        ExtractJsonString(request.body, "runner"),
        ExtractJsonString(request.body, "runner_label"),
        ExtractJsonString(request.body, "profile"));
    const bool record_to_latest = trigger != "auto";
    const bool observation_visible = record_to_latest && !is_noise;
    const std::string session_id = FirstNonEmpty(
        ExtractResultField(response.body, "session_id"),
        GetHeaderValue(request, "mcp-session-id"),
        ExtractJsonString(request.body, "session_id"));
    const std::string turn_id = FirstNonEmpty(
        ExtractResultField(response.body, "turn_id"),
        ExtractJsonString(request.body, "turn_id"));
    const std::string write_mode = FirstNonEmpty(
        ExtractResultField(response.body, "write_mode"),
        ExtractJsonString(request.body, "write_mode"));

    event["timestamp"] = request_finished_at;
    event["request_started_at"] = request_started_at;
    event["request_finished_at"] = request_finished_at;
    event["remote_timestamp"] = request_finished_at;
    event["observed_at"] = request_finished_at;
    event["source_thread"] = source_thread.empty() ? "unknown" : source_thread;
    event["target_thread"] = "codex_lan_agent";
    event["message_type"] = "remote_control";
    event["record_model"] = "remote_interaction_v1";
    event["source_label"] = source_label;
    event["source_detail"] = source_thread.empty() ? "unknown" : source_thread;
    event["takeover_relation"] = takeover_relation;
    event["task_group"] = task_group;
    event["runner"] = runner;
    event["interaction_kind"] = interaction_kind;
    event["is_noise"] = is_noise ? "true" : "false";
    event["observation_visible"] = observation_visible ? "true" : "false";
    event["task_id"] = task_id;
    event["session_id"] = session_id;
    event["turn_id"] = turn_id;
    event["write_mode"] = write_mode;
    event["status"] = status;
    event["command_name"] = command_name;
    event["entry_name"] = request.method + " " + request.path;
    event["request_type"] = request_type;
    event["duration_ms"] = std::to_string(duration_ms);
    event["summary"] = ExtractResultField(response.body, "summary");
    event["next_action"] = ExtractResultField(response.body, "next_action");
    event["task_log_ref"] = task_log_ref;
    event["evidence_ref"] = evidence_ref;
    event["result_ref"] = result_ref;
    event["trigger"] = trigger;
    event["record_to_latest"] = record_to_latest ? "true" : "false";
    event["http_status"] = std::to_string(response.status_code);
    event["method"] = request.method;
    event["path"] = request.path;

    const std::string event_path = BuildRemoteControlEventsPath(config);
    {
        std::lock_guard<std::mutex> lock(g_remote_control_event_mutex);
        std::filesystem::create_directories(config.log_root);
        std::ofstream output(event_path, std::ios::out | std::ios::app);
        if (output.is_open()) {
            output << BuildRemoteControlEventJson(event) << "\n";
        }
        g_last_remote_control_event = event;
    }
    WriteToolUseStdoutEvent(
        config,
        request,
        response,
        request_finished_at,
        command_name,
        status,
        duration_ms);
}

std::string BuildMcpSessionId() {
    static std::mutex mutex;
    static unsigned long long next_id = 1;
    std::lock_guard<std::mutex> lock(mutex);
    return "mcp-" + TimeStampForFileName() + "-" + std::to_string(next_id++);
}

void ApplyMcpSessionHeaders(
    const HttpRequest & request,
    HttpResponseSpec * response,
    bool create_if_missing) {
    std::string session_id = GetHeaderValue(request, "mcp-session-id");
    if (session_id.empty() && create_if_missing) {
        session_id = BuildMcpSessionId();
    }
    if (!session_id.empty()) {
        response->headers["Mcp-Session-Id"] = session_id;
    }
}

void ApplyMcpCorsHeaders(HttpResponseSpec * response) {
    response->headers["Access-Control-Allow-Origin"] = "*";
    response->headers["Access-Control-Allow-Methods"] = "GET, HEAD, OPTIONS, POST";
    response->headers["Access-Control-Allow-Headers"] =
        "Accept, Authorization, Content-Type, Mcp-Session-Id, X-Source-Thread, X-Trigger";
    response->headers["Access-Control-Expose-Headers"] = "Mcp-Session-Id";
}
