#pragma once

std::string BuildOverviewCardJson(
    const std::string & section_id,
    const std::string & title,
    const std::string & tool_name,
    const std::string & http_get_path,
    const std::string & route_hint,
    const std::string & status,
    const std::string & primary_count,
    const std::string & summary,
    const std::string & payload_field) {
    std::ostringstream output;
    output << "{"
           << "\"id\":\"" << codex_lan_agent::JsonEscape(section_id) << "\","
           << "\"title\":\"" << codex_lan_agent::JsonEscape(title) << "\","
           << "\"tool_name\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
           << "\"http_get_path\":\"" << codex_lan_agent::JsonEscape(http_get_path) << "\","
           << "\"browser_route_hint\":\"" << codex_lan_agent::JsonEscape(route_hint) << "\","
           << "\"status\":\"" << codex_lan_agent::JsonEscape(status) << "\","
           << "\"primary_count\":" << JsonValueFromRawOrString(primary_count) << ","
           << "\"summary\":\"" << codex_lan_agent::JsonEscape(summary) << "\","
           << "\"payload_field\":\"" << codex_lan_agent::JsonEscape(payload_field) << "\""
           << "}";
    return output.str();
}

std::string BuildOverviewSectionContractJson(
    const std::string & section_id,
    const std::string & title,
    const std::string & tool_name,
    const std::string & http_get_path,
    const std::string & route_hint,
    const std::string & primary_count_field,
    const std::string & status_field,
    const std::string & summary_field,
    const std::string & payload_field) {
    std::ostringstream output;
    output << "{"
           << "\"id\":\"" << codex_lan_agent::JsonEscape(section_id) << "\","
           << "\"title\":\"" << codex_lan_agent::JsonEscape(title) << "\","
           << "\"tool_name\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
           << "\"http_get_path\":\"" << codex_lan_agent::JsonEscape(http_get_path) << "\","
           << "\"browser_route_hint\":\"" << codex_lan_agent::JsonEscape(route_hint) << "\","
           << "\"primary_count_field\":\"" << codex_lan_agent::JsonEscape(primary_count_field) << "\","
           << "\"status_field\":\"" << codex_lan_agent::JsonEscape(status_field) << "\","
           << "\"summary_field\":\"" << codex_lan_agent::JsonEscape(summary_field) << "\","
           << "\"payload_field\":\"" << codex_lan_agent::JsonEscape(payload_field) << "\""
           << "}";
    return output.str();
}

std::string BuildToolNamesJson(const std::vector<RemoteSessionMcpToolSpec> & specs) {
    std::ostringstream output;
    output << "[";
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (i != 0) {
            output << ",";
        }
        output << "\"" << codex_lan_agent::JsonEscape(specs[i].name) << "\"";
    }
    output << "]";
    return output.str();
}

std::string ExtractLatestJsonArrayItem(const std::string & array_json) {
    const std::vector<std::string> items = ExtractTopLevelJsonArrayItems(array_json);
    if (items.empty()) {
        return std::string();
    }
    return items.back();
}

void ApplyOverviewSurfaceFields(
    CommandResult * result,
    const std::string & overview_id,
    const std::string & section_id,
    const std::string & title,
    const std::string & tool_name,
    const std::string & http_get_path,
    const std::string & route_hint,
    const std::string & status,
    const std::string & primary_count,
    const std::string & summary,
    const std::string & payload_field) {
    if (result == nullptr) {
        return;
    }
    result->fields["overview"] = overview_id;
    result->fields["browser_section_id"] = section_id;
    result->fields["browser_section_title"] = title;
    result->fields["browser_tool_name"] = tool_name;
    result->fields["browser_http_get_path"] = http_get_path;
    result->fields["browser_route_hint"] = route_hint;
    result->fields["browser_status"] = status;
    result->fields["browser_primary_count"] = primary_count;
    result->fields["browser_payload_field"] = payload_field;
    result->fields["browser_card_json"] = BuildOverviewCardJson(
        section_id,
        title,
        tool_name,
        http_get_path,
        route_hint,
        status,
        primary_count,
        summary,
        payload_field);
}

CommandResult BuildRemoteSessionOverviewResult(const AgentConfig & config, int timeout_ms = 10000) {
    CommandResult sessions = ListRemoteSessionsResult(config, timeout_ms);
    CommandResult result;
    result.ok = sessions.ok;
    result.exit_code = sessions.exit_code;
    result.fields["status"] = GetFieldOrDefault(sessions, "status", sessions.ok ? "ok" : "failed");
    result.fields["result"] = sessions.ok ? "remote_session_overview" : "remote_session_overview_failed";
    result.fields["summary"] = sessions.ok ? "remote session overview returned" : "remote session overview failed";
    result.fields["status_code"] = GetFieldOrDefault(sessions, "status_code", "");
    result.fields["session_semantic_projection_source"] =
        GetFieldOrDefault(sessions, "session_semantic_projection_source", "latest_turn.tool_availability_snapshot");
    result.fields["session_semantic_projection_mode"] =
        GetFieldOrDefault(sessions, "session_semantic_projection_mode", "derived_from_session_detail");
    result.fields["session_semantic_projection_visible_count"] =
        GetFieldOrDefault(sessions, "session_semantic_projection_visible_count", "0");
    result.fields["session_semantic_projection_missing_count"] =
        GetFieldOrDefault(sessions, "session_semantic_projection_missing_count", "0");
    result.fields["body"] = GetFieldOrDefault(sessions, "body", "");

    const std::string items_json = ExtractJsonObjectRaw(result.fields["body"], "items");
    const int total_sessions_count = CountEntriesInJsonArray(items_json);
    const std::string projection_json = FirstNonEmpty(
        GetFieldOrDefault(sessions, "session_semantic_projection_list_json", ""),
        "[]");
    const std::string latest_projection = ExtractLatestJsonArrayItem(projection_json);
    result.fields["total_sessions_count"] = std::to_string(total_sessions_count);
    result.fields["session_items_json"] = projection_json;
    result.fields["latest_session_id"] = ExtractJsonString(latest_projection, "session_id");
    result.fields["latest_turn_id"] = ExtractJsonString(latest_projection, "last_turn_id");
    result.fields["latest_title"] = ExtractJsonString(latest_projection, "title");
    result.fields["latest_summary"] = ExtractJsonString(latest_projection, "current_summary");

    ApplyOverviewSurfaceFields(
        &result,
        "remote_session_overview",
        "remote_sessions",
        "Remote Sessions",
        "lan_agent_remote_session_overview",
        "/overview/remote-sessions",
        "/remote-sessions",
        result.fields["status"],
        result.fields["total_sessions_count"],
        result.fields["summary"],
        "session_items_json");
    return result;
}

CommandResult BuildTaskOverviewResult(const AgentConfig & config, int max_entries = 20) {
    CommandResult runtime = BuildRuntimeOverviewResult(config);
    CommandResult result;
    result.ok = runtime.ok;
    result.exit_code = runtime.exit_code;
    result.fields["status"] = "ok";
    result.fields["queue_depth"] = GetFieldOrDefault(runtime, "queue_depth", "0");
    result.fields["active_resource_lock_count"] =
        GetFieldOrDefault(runtime, "active_resource_lock_count", "0");
    result.fields["latest_task_available"] = "false";
    result.fields["latest_task_json"] = "{}";
    result.fields["tasks_json"] = "[]";
    result.fields["visible_count"] = "0";
    result.fields["total_task_count"] = "0";

    if (g_task_manager == nullptr) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["status"] = "unavailable";
        result.fields["error"] = "task manager is not active";
        result.fields["summary"] = "task overview unavailable";
    } else {
        const CommandResult task_list = g_task_manager->ListTaskResults(max_entries);
        result.fields["tasks_json"] = GetFieldOrDefault(task_list, "tasks_json", "[]");
        result.fields["visible_count"] = GetFieldOrDefault(task_list, "visible_count", "0");
        result.fields["total_task_count"] = GetFieldOrDefault(task_list, "total_task_count", "0");
        result.fields["task_retention_model"] = GetFieldOrDefault(task_list, "task_retention_model", "");
        result.fields["max_completed_history"] = GetFieldOrDefault(task_list, "max_completed_history", "");
        const CommandResult latest_task = g_task_manager->GetLatestTaskResult();
        if (latest_task.ok || GetFieldOrDefault(latest_task, "status", "") != "empty") {
            result.fields["latest_task_available"] = "true";
            result.fields["latest_task_id"] = GetFieldOrDefault(latest_task, "task_id", "");
            result.fields["latest_task_status"] = GetFieldOrDefault(latest_task, "status", "");
            result.fields["latest_task_type"] = GetFieldOrDefault(latest_task, "task_type", "");
            result.fields["latest_task_summary"] = GetFieldOrDefault(latest_task, "summary", "");
            result.fields["latest_task_result_ref"] = GetFieldOrDefault(latest_task, "result_ref", "");
            result.fields["latest_task_evidence_ref"] = GetFieldOrDefault(latest_task, "evidence_ref", "");
            result.fields["latest_task_resolved_log_path"] = GetFieldOrDefault(latest_task, "resolved_log_path", "");
            std::ostringstream latest_task_json;
            latest_task_json << "{"
                             << "\"task_id\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_id"]) << "\","
                             << "\"status\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_status"]) << "\","
                             << "\"task_type\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_type"]) << "\","
                             << "\"summary\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_summary"]) << "\","
                             << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_result_ref"]) << "\","
                             << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_evidence_ref"]) << "\","
                             << "\"resolved_log_path\":\"" << codex_lan_agent::JsonEscape(result.fields["latest_task_resolved_log_path"]) << "\""
                             << "}";
            result.fields["latest_task_json"] = latest_task_json.str();
            result.fields["summary"] = "task overview returned";
        } else {
            result.fields["latest_task_status"] = "empty";
            result.fields["summary"] = "task overview returned";
        }
    }

    result.fields["result"] = result.ok ? "task_overview" : "task_overview_failed";
    ApplyOverviewSurfaceFields(
        &result,
        "task_overview",
        "tasks",
        "Tasks",
        "lan_agent_task_overview",
        "/overview/tasks",
        "/tasks",
        result.fields["status"],
        result.fields["visible_count"],
        result.fields["summary"],
        "tasks_json");
    return result;
}

CommandResult BuildEventOverviewResult(
    const AgentConfig & config,
    int max_entries = 10,
    int offset = 0,
    bool include_auto = false,
    bool include_noise = false,
    const std::string & command_name_filter = std::string(),
    const std::string & session_id_filter = std::string(),
    const std::string & task_id_filter = std::string(),
    const std::string & since_timestamp = std::string()) {
    CommandResult events = ListRecentRemoteEventsResult(
        config,
        max_entries,
        offset,
        include_auto,
        include_noise,
        false,
        since_timestamp,
        std::string(),
        command_name_filter,
        session_id_filter,
        task_id_filter);
    CommandResult result;
    result.ok = events.ok;
    result.exit_code = events.exit_code;
    result.fields["status"] = events.ok ? "ok" : "failed";
    result.fields["visible_count"] = GetFieldOrDefault(events, "visible_count", "0");
    result.fields["matched_count"] = GetFieldOrDefault(events, "matched_count", "0");
    result.fields["max_entries"] = GetFieldOrDefault(events, "max_entries", std::to_string(max_entries));
    result.fields["offset"] = GetFieldOrDefault(events, "offset", std::to_string(offset));
    result.fields["has_more"] = GetFieldOrDefault(events, "has_more", "false");
    result.fields["next_offset"] = GetFieldOrDefault(events, "next_offset", "");
    result.fields["command_name_filter"] = command_name_filter;
    result.fields["session_id_filter"] = session_id_filter;
    result.fields["task_id_filter"] = task_id_filter;
    result.fields["since_timestamp"] = since_timestamp;
    result.fields["events_json"] = GetFieldOrDefault(events, "events_json", "[]");
    result.fields["latest_timestamp"] = GetFieldOrDefault(events, "latest_timestamp", "");
    result.fields["latest_command_name"] = GetFieldOrDefault(events, "latest_command_name", "");
    result.fields["latest_status"] = GetFieldOrDefault(events, "latest_status", "");
    result.fields["latest_session_id"] = GetFieldOrDefault(events, "latest_session_id", "");
    result.fields["latest_turn_id"] = GetFieldOrDefault(events, "latest_turn_id", "");
    result.fields["latest_result_ref"] = GetFieldOrDefault(events, "latest_result_ref", "");
    result.fields["latest_evidence_ref"] = GetFieldOrDefault(events, "latest_evidence_ref", "");
    result.fields["result"] = events.ok ? "event_overview" : "event_overview_failed";
    result.fields["summary"] = events.ok ? "event overview returned" : "event overview failed";
    if (!events.ok) {
        result.fields["error"] = GetFieldOrDefault(events, "error", "");
    }
    ApplyOverviewSurfaceFields(
        &result,
        "event_overview",
        "events",
        "Events",
        "lan_agent_event_overview",
        "/overview/events",
        "/events",
        result.fields["status"],
        result.fields["visible_count"],
        result.fields["summary"],
        "events_json");
    return result;
}

CommandResult BuildPatchOverviewResult(
    const AgentConfig & config,
    int max_entries = 20,
    int offset = 0,
    const std::string & patch_id_filter = std::string(),
    const std::string & trace_id_filter = std::string(),
    const std::string & file_path_filter = std::string()) {
    CommandResult result;
    result.fields["audit_event_path"] = BuildPatchAuditEventsPath(config);
    result.fields["max_entries"] = std::to_string(max_entries);
    result.fields["offset"] = std::to_string(offset);
    result.fields["patch_id_filter"] = patch_id_filter;
    result.fields["trace_id_filter"] = trace_id_filter;
    result.fields["file_path_filter"] = file_path_filter;

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(result.fields["audit_event_path"], &content, &read_error)) {
        result.ok = false;
        result.exit_code = 44;
        result.fields["status"] = "unavailable";
        result.fields["error"] = read_error;
        result.fields["patch_events_json"] = "[]";
        result.fields["summary"] = "patch audit overview unavailable";
    } else {
        std::vector<std::string> matched_lines;
        std::istringstream input(content);
        std::string line;
        while (std::getline(input, line)) {
            const std::string trimmed = Trim(line);
            if (trimmed.empty()) {
                continue;
            }
            if (!patch_id_filter.empty() && ExtractJsonString(trimmed, "patch_id") != patch_id_filter) {
                continue;
            }
            if (!trace_id_filter.empty() && ExtractJsonString(trimmed, "trace_id") != trace_id_filter) {
                continue;
            }
            if (!file_path_filter.empty() && ExtractJsonString(trimmed, "file_path") != file_path_filter) {
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

        std::ostringstream patches_json;
        patches_json << "[";
        int visible_index = 0;
        for (std::size_t index = start; index < reverse_start; ++index) {
            const std::string & event_line = matched_lines[index];
            const std::string prefix = "item_" + std::to_string(visible_index) + "_";
            result.fields[prefix + "event_id"] = ExtractJsonString(event_line, "event_id");
            result.fields[prefix + "request_id"] = ExtractJsonString(event_line, "request_id");
            result.fields[prefix + "trace_id"] = ExtractJsonString(event_line, "trace_id");
            result.fields[prefix + "patch_id"] = ExtractJsonString(event_line, "patch_id");
            result.fields[prefix + "file_path"] = ExtractJsonString(event_line, "file_path");
            result.fields[prefix + "stage"] = ExtractJsonString(event_line, "stage");
            result.fields[prefix + "decision"] = ExtractJsonString(event_line, "decision");
            result.fields[prefix + "status"] = ExtractJsonString(event_line, "status");
            result.fields[prefix + "diff_hash"] = ExtractJsonString(event_line, "diff_hash");
            result.fields[prefix + "backup_path"] = ExtractJsonString(event_line, "backup_path");
            result.fields[prefix + "log_path"] = ExtractJsonString(event_line, "log_ref");
            result.fields[prefix + "recorded_at"] = ExtractJsonString(event_line, "recorded_at");
            if (visible_index > 0) {
                patches_json << ",";
            }
            patches_json << event_line;
            ++visible_index;
        }
        patches_json << "]";

        result.fields["status"] = "ok";
        result.fields["visible_count"] = std::to_string(visible_index);
        result.fields["matched_count"] = std::to_string(matched_lines.size());
        result.fields["has_more"] = start > 0 ? "true" : "false";
        result.fields["next_offset"] = start > 0
            ? std::to_string(bounded_offset + visible_index)
            : std::string();
        result.fields["patch_events_json"] = patches_json.str();
        if (visible_index > 0) {
            const std::string latest_prefix = "item_" + std::to_string(visible_index - 1) + "_";
            result.fields["latest_patch_id"] = result.fields[latest_prefix + "patch_id"];
            result.fields["latest_trace_id"] = result.fields[latest_prefix + "trace_id"];
            result.fields["latest_file_path"] = result.fields[latest_prefix + "file_path"];
            result.fields["latest_stage"] = result.fields[latest_prefix + "stage"];
            result.fields["latest_status"] = result.fields[latest_prefix + "status"];
            result.fields["latest_log_path"] = result.fields[latest_prefix + "log_path"];
        }
        result.fields["summary"] = "patch audit overview returned";
    }

    result.fields["result"] = result.ok ? "patch_overview" : "patch_overview_failed";
    ApplyOverviewSurfaceFields(
        &result,
        "patch_overview",
        "patches",
        "Patches",
        "lan_agent_patch_overview",
        "/overview/patches",
        "/patches",
        result.fields["status"],
        GetFieldOrDefault(result, "visible_count", "0"),
        result.fields["summary"],
        "patch_events_json");
    return result;
}

CommandResult BuildMcpOverviewResult(const AgentConfig & config) {
    CommandResult runtime = BuildRuntimeOverviewResult(config);
    const std::vector<RemoteSessionMcpToolSpec> specs = BuildRemoteSessionMountedToolSpecs(config);

    CommandResult result;
    result.ok = runtime.ok;
    result.exit_code = runtime.exit_code;
    result.fields["status"] = "ok";
    result.fields["mcp_endpoint"] = "/mcp";
    result.fields["mcp_transport"] = "streamable-http-minimal";
    result.fields["tool_count"] = std::to_string(specs.size());
    result.fields["tool_names_json"] = BuildToolNamesJson(specs);
    result.fields["semantic_action_count"] = std::to_string(GetSemanticActionSpecs().size());
    result.fields["profile_count"] = GetFieldOrDefault(runtime, "profile_count", "0");
    result.fields["tool_config_exists"] = GetFieldOrDefault(runtime, "tool_config_exists", "false");
    result.fields["tool_config_mode"] = GetFieldOrDefault(runtime, "tool_config_mode", "");
    result.fields["local_chat_ready"] = GetFieldOrDefault(runtime, "local_chat_ready", "false");
    result.fields["local_ai_mcp_guidance_version"] = "local_ai_mcp_guidance_v1";
    result.fields["local_ai_required_entry"] =
        "start with lan_agent_mcp_overview and tools/list; use lan_agent_clips_decide before uncertain write/edit/build/test routing";
    result.fields["local_ai_common_file_operation_policy"] =
        "probe first, operate on one file, use bounded windows or one atomic mutation, verify each step, and follow next_call_json until terminal";
    result.fields["local_ai_comment_cleanup_policy"] =
        "for comment deletion use lan_agent_probe_text_file then lan_agent_delete_text_range_window_atomic max_lines=200; do not read the whole file and batch edit from model memory";
    result.fields["local_ai_long_loop_policy"] =
        "when continuation may exceed model context, freeze task_memory and use lan_agent_task_memory_execute_continuation_budget; the model may reset but MCP memory remains source state";
    result.fields["local_ai_completion_gate"] =
        "only claim completion when terminal_state=true, completion_claim_allowed=true, final_answer_allowed=true, and verification_ok=true";
    result.fields["local_ai_guidance_json"] =
        "{\"version\":\"local_ai_mcp_guidance_v1\","
        "\"entry\":[\"lan_agent_mcp_overview\",\"tools/list\",\"lan_agent_clips_decide when routing is uncertain\"],"
        "\"file_ops\":[\"probe first\",\"single file\",\"bounded window or one atomic mutation\",\"verify each step\",\"follow next_call_json until terminal\"],"
        "\"comment_cleanup\":[\"lan_agent_probe_text_file\",\"lan_agent_delete_text_range_window_atomic max_lines=200\",\"repeat next_call_json until has_more=false\"],"
        "\"long_loop\":[\"lan_agent_task_memory_freeze\",\"lan_agent_task_memory_execute_continuation_budget\",\"lan_agent_task_memory_resume_context\"],"
        "\"completion_gate\":[\"terminal_state=true\",\"completion_claim_allowed=true\",\"final_answer_allowed=true\",\"verification_ok=true\"]}";
    result.fields["result"] = "mcp_overview";
    result.fields["summary"] = "mcp overview returned";
    if (result.fields["tool_config_exists"] != "true" || result.fields["local_chat_ready"] != "true") {
        result.fields["status"] = "degraded";
    }
    ApplyOverviewSurfaceFields(
        &result,
        "mcp_overview",
        "mcp",
        "MCP",
        "lan_agent_mcp_overview",
        "/overview/mcp",
        "/mcp",
        result.fields["status"],
        result.fields["tool_count"],
        result.fields["summary"],
        "tool_names_json");
    return result;
}

CommandResult BuildRagOverviewResult(const AgentConfig & config) {
    CommandResult rag = BuildRagIndexStatusResult(config);
    CommandResult result;
    result.ok = rag.ok;
    result.exit_code = rag.exit_code;
    result.fields["status"] = GetFieldOrDefault(rag, "status", rag.ok ? "ok" : "failed");
    result.fields["enabled"] = GetFieldOrDefault(rag, "enabled", "false");
    result.fields["ready"] = GetFieldOrDefault(rag, "ready", "false");
    result.fields["pending"] = GetFieldOrDefault(rag, "pending", "false");
    result.fields["chunk_count"] = GetFieldOrDefault(rag, "chunk_count", "0");
    result.fields["clips_meta"] = GetFieldOrDefault(rag, "clips_meta", "false");
    result.fields["capabilities"] = GetFieldOrDefault(rag, "capabilities", "");
    result.fields["next_action"] = GetFieldOrDefault(rag, "next_action", "");
    result.fields["result"] = rag.ok ? "rag_overview" : "rag_overview_failed";
    result.fields["summary"] = rag.ok ? "rag overview returned" : "rag overview failed";
    ApplyOverviewSurfaceFields(
        &result,
        "rag_overview",
        "rag",
        "RAG",
        "lan_agent_rag_overview",
        "/overview/rag",
        "/rag",
        result.fields["status"],
        result.fields["chunk_count"],
        result.fields["summary"],
        "capabilities");
    return result;
}

CommandResult BuildBrowserListOverviewResult(
    const AgentConfig & config,
    int task_max_entries = 20,
    int event_max_entries = 10,
    int patch_max_entries = 20) {
    const CommandResult runtime = BuildRuntimeOverviewResult(config);
    const CommandResult remote_sessions = BuildRemoteSessionOverviewResult(config);
    const CommandResult tasks = BuildTaskOverviewResult(config, task_max_entries);
    const CommandResult events = BuildEventOverviewResult(config, event_max_entries);
    const CommandResult mcp = BuildMcpOverviewResult(config);
    const CommandResult rag = BuildRagOverviewResult(config);
    const CommandResult patches = BuildPatchOverviewResult(config, patch_max_entries);

    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "ok";
    result.fields["consumer_contract_version"] = "browser_list_overview_v1";
    result.fields["default_section_id"] = "remote_sessions";
    result.fields["default_route"] = "/remote-sessions";
    result.fields["overview_tools_json"] =
        "[\"lan_agent_runtime_overview\",\"lan_agent_remote_session_overview\","
        "\"lan_agent_task_overview\",\"lan_agent_event_overview\","
        "\"lan_agent_mcp_overview\",\"lan_agent_rag_overview\","
        "\"lan_agent_patch_overview\"]";
    result.fields["consumer_http_paths_json"] =
        "{\"health\":\"/runtime-overview\","
        "\"remote_sessions\":\"/overview/remote-sessions\","
        "\"tasks\":\"/overview/tasks\","
        "\"events\":\"/overview/events\","
        "\"patches\":\"/overview/patches\","
        "\"mcp\":\"/overview/mcp\","
        "\"rag\":\"/overview/rag\","
        "\"browser_list\":\"/overview/browser-list\"}";

    std::ostringstream sections;
    sections << "["
             << BuildOverviewCardJson(
                    "health",
                    "Health",
                    "lan_agent_runtime_overview",
                    "/runtime-overview",
                    "/health",
                    GetFieldOrDefault(runtime, "status", "ok"),
                    GetFieldOrDefault(runtime, "queue_depth", "0"),
                    "runtime overview returned",
                    "last_request_entry")
             << ","
             << GetFieldOrDefault(remote_sessions, "browser_card_json", "{}")
             << ","
             << GetFieldOrDefault(tasks, "browser_card_json", "{}")
             << ","
             << GetFieldOrDefault(events, "browser_card_json", "{}")
             << ","
             << GetFieldOrDefault(patches, "browser_card_json", "{}")
             << ","
             << GetFieldOrDefault(mcp, "browser_card_json", "{}")
             << ","
             << GetFieldOrDefault(rag, "browser_card_json", "{}")
             << "]";
    result.fields["sections_json"] = sections.str();

    std::ostringstream section_contract;
    section_contract << "["
                     << BuildOverviewSectionContractJson(
                            "health",
                            "Health",
                            "lan_agent_runtime_overview",
                            "/runtime-overview",
                            "/health",
                            "queue_depth",
                            "status",
                            "summary",
                            "last_request_entry")
                     << ","
                     << BuildOverviewSectionContractJson(
                            "remote_sessions",
                            "Remote Sessions",
                            "lan_agent_remote_session_overview",
                            "/overview/remote-sessions",
                            "/remote-sessions",
                            "total_sessions_count",
                            "status",
                            "summary",
                            "session_items_json")
                     << ","
                     << BuildOverviewSectionContractJson(
                            "tasks",
                            "Tasks",
                            "lan_agent_task_overview",
                            "/overview/tasks",
                            "/tasks",
                            "queue_depth",
                            "status",
                            "summary",
                            "tasks_json")
                     << ","
                     << BuildOverviewSectionContractJson(
                            "events",
                            "Events",
                            "lan_agent_event_overview",
                            "/overview/events",
                            "/events",
                            "visible_count",
                            "status",
                            "summary",
                            "events_json")
                     << ","
                     << BuildOverviewSectionContractJson(
                            "patches",
                            "Patches",
                            "lan_agent_patch_overview",
                            "/overview/patches",
                            "/patches",
                            "visible_count",
                            "status",
                            "summary",
                            "patch_events_json")
                     << ","
                     << BuildOverviewSectionContractJson(
                            "mcp",
                            "MCP",
                            "lan_agent_mcp_overview",
                            "/overview/mcp",
                            "/mcp",
                            "tool_count",
                            "status",
                            "summary",
                            "tool_names_json")
                     << ","
                     << BuildOverviewSectionContractJson(
                            "rag",
                            "RAG",
                            "lan_agent_rag_overview",
                            "/overview/rag",
                            "/rag",
                            "chunk_count",
                            "status",
                            "summary",
                            "capabilities")
                     << "]";
    result.fields["section_contract_json"] = section_contract.str();
    result.fields["section_count"] = "7";
    result.fields["result"] = "browser_list_overview";
    result.fields["summary"] = "browser list overview returned";
    ApplyOverviewSurfaceFields(
        &result,
        "browser_list_overview",
        "browser_list",
        "Browser List",
        "lan_agent_browser_list_overview",
        "/overview/browser-list",
        "/browser-list",
        result.fields["status"],
        result.fields["section_count"],
        result.fields["summary"],
        "sections_json");
    return result;
}
