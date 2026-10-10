#pragma once

std::string BuildDialogSlicesDir(const AgentConfig & config);

std::string BuildSessionDispatchDir(const AgentConfig & config);

std::string BuildMcpToolsListResponse(
    const std::string & id_raw,
    const AgentConfig & config);

std::string JsonValueFromRawOrString(const std::string & raw_value);

std::vector<std::string> ExtractTopLevelJsonArrayItems(const std::string & array_text);

struct RemoteSessionMcpToolSpec {
    std::string name;
    std::string description;
    std::string input_schema_json;
};

std::size_t FindJsonObjectEnd(const std::string & text, std::size_t brace_start) {
    if (brace_start == std::string::npos || brace_start >= text.size() || text[brace_start] != '{') {
        return std::string::npos;
    }
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (std::size_t i = brace_start; i < text.size(); ++i) {
        const char ch = text[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
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
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::vector<RemoteSessionMcpToolSpec> BuildRemoteSessionMountedToolSpecs(const AgentConfig & config) {
    std::vector<RemoteSessionMcpToolSpec> specs;
    const std::string tools_response = BuildMcpToolsListResponse("1", config);
    std::size_t scan_pos = 0;
    while (true) {
        const std::size_t name_pos = tools_response.find("\"name\":\"", scan_pos);
        if (name_pos == std::string::npos) {
            break;
        }
        const std::size_t name_value_start = name_pos + 8;
        const std::size_t name_value_end = tools_response.find('"', name_value_start);
        if (name_value_end == std::string::npos) {
            break;
        }
        const std::string name = tools_response.substr(name_value_start, name_value_end - name_value_start);

        const std::size_t description_pos = tools_response.find("\"description\":\"", name_value_end);
        if (description_pos == std::string::npos) {
            scan_pos = name_value_end;
            continue;
        }
        const std::size_t description_value_start = description_pos + 15;
        const std::size_t description_value_end = tools_response.find("\",\"inputSchema\":", description_value_start);
        if (description_value_end == std::string::npos) {
            scan_pos = description_value_start;
            continue;
        }
        const std::string description = tools_response.substr(
            description_value_start,
            description_value_end - description_value_start);

        const std::size_t input_schema_pos = tools_response.find("\"inputSchema\":", description_value_end);
        if (input_schema_pos == std::string::npos) {
            scan_pos = description_value_end;
            continue;
        }
        const std::size_t schema_start = tools_response.find('{', input_schema_pos);
        if (schema_start == std::string::npos) {
            scan_pos = input_schema_pos + 14;
            continue;
        }
        const std::size_t schema_end = FindJsonObjectEnd(tools_response, schema_start);
        if (schema_end == std::string::npos) {
            scan_pos = schema_start + 1;
            continue;
        }

        RemoteSessionMcpToolSpec spec;
        spec.name = name;
        spec.description = description;
        spec.input_schema_json = tools_response.substr(schema_start, schema_end - schema_start + 1);
        specs.push_back(spec);
        scan_pos = schema_end + 1;
    }
    return specs;
}

std::vector<std::string> GetRemoteSessionMountedToolNames(const AgentConfig & config) {
    std::vector<std::string> names;
    const std::vector<RemoteSessionMcpToolSpec> specs = BuildRemoteSessionMountedToolSpecs(config);
    for (const RemoteSessionMcpToolSpec & spec : specs) {
        names.push_back(spec.name);
    }
    return names;
}

bool RemoteSessionToolIsMounted(const AgentConfig & config, const std::string & tool_name) {
    const std::vector<std::string> mounted = GetRemoteSessionMountedToolNames(config);
    return std::find(mounted.begin(), mounted.end(), tool_name) != mounted.end();
}

std::string BuildRemoteSessionMountedToolNamesJson(const AgentConfig & config) {
    std::ostringstream output;
    output << "[";
    const std::vector<std::string> mounted = GetRemoteSessionMountedToolNames(config);
    for (std::size_t i = 0; i < mounted.size(); ++i) {
        if (i != 0) {
            output << ",";
        }
        output << "\"" << codex_lan_agent::JsonEscape(mounted[i]) << "\"";
    }
    output << "]";
    return output.str();
}

std::string BuildRemoteSessionSemanticCatalogJson(const AgentConfig & config) {
    std::ostringstream output;
    output << "[";
    bool first = true;

    const std::string tools_response = BuildMcpToolsListResponse("1", config);
    std::size_t scan_pos = 0;
    while (true) {
        const std::size_t name_pos = tools_response.find("\"name\":\"", scan_pos);
        if (name_pos == std::string::npos) {
            break;
        }
        const std::size_t name_value_start = name_pos + 8;
        const std::size_t name_value_end = tools_response.find('"', name_value_start);
        if (name_value_end == std::string::npos) {
            break;
        }
        const std::string name = tools_response.substr(name_value_start, name_value_end - name_value_start);
        const std::size_t description_pos = tools_response.find("\"description\":\"", name_value_end);
        if (description_pos == std::string::npos) {
            scan_pos = name_value_end;
            continue;
        }
        const std::size_t description_value_start = description_pos + 15;
        const std::size_t description_value_end = tools_response.find("\",\"inputSchema\":", description_value_start);
        if (description_value_end == std::string::npos) {
            scan_pos = description_value_start;
            continue;
        }
        const std::string description = tools_response.substr(
            description_value_start,
            description_value_end - description_value_start);
        if (!first) {
            output << ",";
        }
        first = false;
        output << "{"
               << "\"semantic_kind\":\"mcp_tool\","
               << "\"source_plane\":\"mcp_18080\","
               << "\"id\":\"" << codex_lan_agent::JsonEscape(name) << "\","
               << "\"description\":\"" << codex_lan_agent::JsonEscape(description) << "\","
               << "\"mounted_in_current_session\":" << (RemoteSessionToolIsMounted(config, name) ? "true" : "false") << ","
               << "\"visible_in_remote_dialog_list\":true,"
               << "\"callable_in_remote_session\":" << (RemoteSessionToolIsMounted(config, name) ? "true" : "false")
               << "}";
        scan_pos = description_value_end;
    }

    for (const SemanticActionSpec & action : GetSemanticActionSpecs()) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << "{"
               << "\"semantic_kind\":\"semantic_action\","
               << "\"source_plane\":\"semantic_8095\","
               << "\"id\":\"" << codex_lan_agent::JsonEscape(action.action_id) << "\","
               << "\"tool\":\"" << codex_lan_agent::JsonEscape(action.tool) << "\","
               << "\"description\":\"" << codex_lan_agent::JsonEscape(action.description) << "\","
               << "\"mounted_in_current_session\":" << (RemoteSessionToolIsMounted(config, action.tool) ? "true" : "false") << ","
               << "\"visible_in_remote_dialog_list\":true,"
               << "\"callable_in_remote_session\":" << (RemoteSessionToolIsMounted(config, action.tool) ? "true" : "false")
               << "}";
    }
    output << "]";
    return output.str();
}

CommandResult BuildRemoteSessionSemanticCatalogResult(const AgentConfig & config) {
    CommandResult result;
    const std::string catalog_json = BuildRemoteSessionSemanticCatalogJson(config);
    int total_count = 0;
    int callable_count = 0;
    int mcp_tool_count = 0;
    int semantic_action_count = 0;
    std::size_t scan_pos = 0;
    while (true) {
        const std::size_t kind_pos = catalog_json.find("\"semantic_kind\":", scan_pos);
        if (kind_pos == std::string::npos) {
            break;
        }
        ++total_count;
        const std::size_t kind_value_start = catalog_json.find('"', kind_pos + 16);
        if (kind_value_start != std::string::npos) {
            const std::size_t kind_value_end = catalog_json.find('"', kind_value_start + 1);
            if (kind_value_end != std::string::npos) {
                const std::string kind_value = catalog_json.substr(kind_value_start + 1, kind_value_end - kind_value_start - 1);
                if (kind_value == "mcp_tool") {
                    ++mcp_tool_count;
                } else if (kind_value == "semantic_action") {
                    ++semantic_action_count;
                }
            }
        }
        const std::size_t callable_pos = catalog_json.find("\"callable_in_remote_session\":", kind_pos);
        if (callable_pos != std::string::npos) {
            const std::size_t value_start = callable_pos + 29;
            if (catalog_json.compare(value_start, 4, "true") == 0) {
                ++callable_count;
            }
        }
        scan_pos = kind_pos + 1;
    }
    result.fields["status"] = "success";
    result.fields["semantic_binding_mode"] = "strong";
    result.fields["semantic_observability_mode"] = "strict";
    result.fields["semantic_catalog_json"] = catalog_json;
    result.fields["remote_dialog_semantic_list_json"] = catalog_json;
    result.fields["semantic_catalog_count"] = std::to_string(total_count);
    result.fields["remote_dialog_semantic_list_count"] = std::to_string(total_count);
    result.fields["callable_semantic_count"] = std::to_string(callable_count);
    result.fields["non_callable_semantic_count"] = std::to_string(std::max(0, total_count - callable_count));
    result.fields["mcp_tool_count"] = std::to_string(mcp_tool_count);
    result.fields["semantic_action_count"] = std::to_string(semantic_action_count);
    result.fields["mounted_tool_count"] = std::to_string(GetRemoteSessionMountedToolNames(config).size());
    result.fields["mounted_tools_json"] = BuildRemoteSessionMountedToolNamesJson(config);
    result.fields["display_projection_mode"] = "full_catalog";
    result.fields["all_mcp_tools_cataloged"] = "true";
    result.fields["all_semantic_actions_cataloged"] = "true";
    result.fields["all_catalog_entries_visible_in_dialog_list"] = "true";
    result.fields["catalog_is_single_source_of_truth"] = "true";
    result.fields["result"] = "remote_session_semantic_catalog";
    result.fields["summary"] = "remote session semantic catalog returned";
    return result;
}

std::string ExtractLatestRemoteSessionTurn(const std::string & session_body) {
    const std::vector<std::string> turns =
        ExtractTopLevelJsonArrayItems(ExtractJsonObjectRaw(session_body, "turns"));
    if (turns.empty()) {
        return std::string();
    }
    return turns.back();
}

std::string ExtractRemoteSessionToolAvailabilitySnapshotRaw(
    const std::string & latest_turn,
    std::string * source_out) {
    const std::string direct_snapshot = ExtractJsonObjectRaw(latest_turn, "tool_availability_snapshot");
    if (!direct_snapshot.empty()) {
        if (source_out != nullptr) {
            *source_out = "latest_turn.tool_availability_snapshot";
        }
        return direct_snapshot;
    }
    const std::string request_payload = ExtractJsonObjectRaw(latest_turn, "request_payload");
    const std::string request_snapshot =
        request_payload.empty() ? std::string() : ExtractJsonObjectRaw(request_payload, "tool_availability_snapshot");
    if (!request_snapshot.empty()) {
        if (source_out != nullptr) {
            *source_out = "latest_turn.request_payload.tool_availability_snapshot";
        }
        return request_snapshot;
    }
    if (source_out != nullptr) {
        *source_out = "latest_turn.tool_availability_snapshot";
    }
    return std::string();
}

int CountEntriesInJsonArray(const std::string & array_json) {
    return static_cast<int>(ExtractTopLevelJsonArrayItems(array_json).size());
}

std::string ExtractSnapshotArrayRaw(
    const std::string & snapshot,
    const std::vector<std::string> & keys) {
    for (const std::string & key : keys) {
        const std::string value = ExtractJsonObjectRaw(snapshot, key);
        if (!value.empty()) {
            return value;
        }
    }
    return std::string();
}

std::string ExtractSnapshotCount(
    const std::string & snapshot,
    const std::vector<std::string> & count_keys,
    const std::vector<std::string> & array_keys) {
    for (const std::string & key : count_keys) {
        const std::string raw = Trim(ExtractJsonRawValue(snapshot, key));
        if (!raw.empty()) {
            return raw;
        }
    }
    const std::string array_json = ExtractSnapshotArrayRaw(snapshot, array_keys);
    if (!array_json.empty()) {
        return std::to_string(CountEntriesInJsonArray(array_json));
    }
    return std::string();
}

std::string BuildRemoteSessionSemanticProjectionPreviewJson(
    const std::string & session_body,
    const std::string & session_id,
    const std::string & title,
    const std::string & current_summary,
    const std::string & listed_last_turn_id) {
    const std::string latest_turn = ExtractLatestRemoteSessionTurn(session_body);
    std::string projection_source;
    const std::string snapshot = ExtractRemoteSessionToolAvailabilitySnapshotRaw(latest_turn, &projection_source);
    const std::string latest_turn_id = FirstNonEmpty(
        ExtractJsonString(latest_turn, "turn_id"),
        listed_last_turn_id);
    if (snapshot.empty()) {
        std::ostringstream output;
        output << "{"
               << "\"session_id\":\"" << codex_lan_agent::JsonEscape(session_id) << "\","
               << "\"title\":\"" << codex_lan_agent::JsonEscape(title) << "\","
               << "\"last_turn_id\":\"" << codex_lan_agent::JsonEscape(latest_turn_id) << "\","
               << "\"current_summary\":\"" << codex_lan_agent::JsonEscape(current_summary) << "\","
               << "\"session_semantic_projection_ready\":false,"
               << "\"session_semantic_projection_source\":\"" << codex_lan_agent::JsonEscape(projection_source) << "\""
               << "}";
        return output.str();
    }

    const std::string semantic_binding_mode = ExtractJsonString(snapshot, "semantic_binding_mode");
    const std::string semantic_observability_mode = ExtractJsonString(snapshot, "semantic_observability_mode");
    const std::string semantic_catalog_count = ExtractSnapshotCount(
        snapshot,
        {"semantic_catalog_count"},
        {"semantic_catalog", "semantic_catalog_json", "semantic_catalog_entries", "catalog_entries"});
    const std::string remote_dialog_semantic_list_count = ExtractSnapshotCount(
        snapshot,
        {"remote_dialog_semantic_list_count"},
        {"remote_dialog_semantic_list", "remote_dialog_semantic_list_json", "dialog_semantic_list"});
    const std::string callable_semantic_count = ExtractSnapshotCount(
        snapshot,
        {"callable_semantic_count"},
        {});
    const std::string non_callable_semantic_count = ExtractSnapshotCount(
        snapshot,
        {"non_callable_semantic_count"},
        {});
    const std::string mounted_tool_count = ExtractSnapshotCount(
        snapshot,
        {"mounted_tool_count"},
        {"available_tool_classes", "available_tool_classes_json", "mounted_tools_json"});
    const std::string display_projection_mode = ExtractJsonString(snapshot, "display_projection_mode");
    const std::string all_catalog_entries_visible_in_dialog_list =
        ExtractJsonRawValue(snapshot, "all_catalog_entries_visible_in_dialog_list");
    const std::string catalog_is_single_source_of_truth =
        ExtractJsonRawValue(snapshot, "catalog_is_single_source_of_truth");
    const std::string available_tool_classes_json = FirstNonEmpty(
        ExtractSnapshotArrayRaw(snapshot, {"available_tool_classes", "available_tool_classes_json"}),
        Trim(ExtractJsonRawValue(snapshot, "available_tool_classes")));

    std::ostringstream output;
    output << "{"
           << "\"session_id\":\"" << codex_lan_agent::JsonEscape(session_id) << "\","
           << "\"title\":\"" << codex_lan_agent::JsonEscape(title) << "\","
           << "\"last_turn_id\":\"" << codex_lan_agent::JsonEscape(latest_turn_id) << "\","
           << "\"current_summary\":\"" << codex_lan_agent::JsonEscape(current_summary) << "\","
           << "\"session_semantic_projection_ready\":true,"
           << "\"session_semantic_projection_source\":\"" << codex_lan_agent::JsonEscape(projection_source) << "\","
           << "\"semantic_binding_mode\":" << JsonValueFromRawOrString(semantic_binding_mode) << ","
           << "\"semantic_observability_mode\":" << JsonValueFromRawOrString(semantic_observability_mode) << ","
           << "\"semantic_catalog_count\":" << JsonValueFromRawOrString(semantic_catalog_count) << ","
           << "\"remote_dialog_semantic_list_count\":"
           << JsonValueFromRawOrString(remote_dialog_semantic_list_count) << ","
           << "\"callable_semantic_count\":" << JsonValueFromRawOrString(callable_semantic_count) << ","
           << "\"non_callable_semantic_count\":" << JsonValueFromRawOrString(non_callable_semantic_count) << ","
           << "\"mounted_tool_count\":" << JsonValueFromRawOrString(mounted_tool_count) << ","
           << "\"display_projection_mode\":" << JsonValueFromRawOrString(display_projection_mode) << ","
           << "\"all_catalog_entries_visible_in_dialog_list\":"
           << JsonValueFromRawOrString(all_catalog_entries_visible_in_dialog_list) << ","
           << "\"catalog_is_single_source_of_truth\":"
           << JsonValueFromRawOrString(catalog_is_single_source_of_truth) << ","
           << "\"available_tool_classes_json\":" << JsonValueFromRawOrString(available_tool_classes_json)
           << "}";
    return output.str();
}

bool ApplyRemoteSessionSemanticProjectionFields(
    const std::string & session_body,
    CommandResult * result) {
    if (result == nullptr) {
        return false;
    }
    const std::string latest_turn = ExtractLatestRemoteSessionTurn(session_body);
    std::string projection_source;
    const std::string snapshot = ExtractRemoteSessionToolAvailabilitySnapshotRaw(latest_turn, &projection_source);
    result->fields["session_semantic_projection_source"] = projection_source;
    result->fields["session_semantic_projection_ready"] = snapshot.empty() ? "false" : "true";
    result->fields["session_semantic_projection_latest_turn_id"] = ExtractJsonString(latest_turn, "turn_id");
    if (snapshot.empty()) {
        return false;
    }

    result->fields["semantic_binding_mode"] = ExtractJsonString(snapshot, "semantic_binding_mode");
    result->fields["semantic_observability_mode"] = ExtractJsonString(snapshot, "semantic_observability_mode");
    result->fields["semantic_catalog_count"] = ExtractSnapshotCount(
        snapshot,
        {"semantic_catalog_count"},
        {"semantic_catalog", "semantic_catalog_json", "semantic_catalog_entries", "catalog_entries"});
    result->fields["remote_dialog_semantic_list_count"] = ExtractSnapshotCount(
        snapshot,
        {"remote_dialog_semantic_list_count"},
        {"remote_dialog_semantic_list", "remote_dialog_semantic_list_json", "dialog_semantic_list"});
    result->fields["callable_semantic_count"] = ExtractSnapshotCount(
        snapshot,
        {"callable_semantic_count"},
        {});
    result->fields["non_callable_semantic_count"] = ExtractSnapshotCount(
        snapshot,
        {"non_callable_semantic_count"},
        {});
    result->fields["mounted_tool_count"] = ExtractSnapshotCount(
        snapshot,
        {"mounted_tool_count"},
        {"available_tool_classes", "available_tool_classes_json", "mounted_tools_json"});
    result->fields["display_projection_mode"] = ExtractJsonString(snapshot, "display_projection_mode");
    result->fields["all_catalog_entries_visible_in_dialog_list"] =
        Trim(ExtractJsonRawValue(snapshot, "all_catalog_entries_visible_in_dialog_list"));
    result->fields["catalog_is_single_source_of_truth"] =
        Trim(ExtractJsonRawValue(snapshot, "catalog_is_single_source_of_truth"));
    result->fields["available_tool_classes_json"] = FirstNonEmpty(
        ExtractSnapshotArrayRaw(snapshot, {"available_tool_classes", "available_tool_classes_json"}),
        Trim(ExtractJsonRawValue(snapshot, "available_tool_classes")));
    result->fields["semantic_catalog_json"] = FirstNonEmpty(
        ExtractSnapshotArrayRaw(snapshot, {"semantic_catalog", "semantic_catalog_json"}),
        Trim(ExtractJsonRawValue(snapshot, "semantic_catalog")));
    result->fields["remote_dialog_semantic_list_json"] =
        FirstNonEmpty(
            ExtractSnapshotArrayRaw(snapshot, {"remote_dialog_semantic_list", "remote_dialog_semantic_list_json"}),
            result->fields["semantic_catalog_json"]);
    return true;
}

std::string NormalizeVentriloquistConfidence(const std::string & raw_value) {
    std::string normalized = ToLowerAscii(Trim(raw_value));
    if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
        normalized = normalized.substr(1, normalized.size() - 2);
    }
    if (normalized == "confirmed"
        || normalized == "likely"
        || normalized == "unclear"
        || normalized == "blocked") {
        return normalized;
    }
    return std::string();
}

std::string BuildVentriloquistQuestion(
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text) {
    const std::string effective_prompt_text = Trim(prompt_text).empty()
        ? "Provide one compact controlled reply for the current task."
        : Trim(prompt_text);
    std::string context_summary = Trim(context_refs);
    if (context_summary.empty()) {
        context_summary = "No external context summary was provided.";
    } else if (context_summary.size() > 320) {
        context_summary = context_summary.substr(0, 320);
    }
    std::string goal_line = "Return one compact structured reply.";
    if (!prompt_purpose.empty()) {
        goal_line = "Goal: " + prompt_purpose;
        if (!reasoning_level.empty()) {
            goal_line += " at " + reasoning_level + " reasoning.";
        }
    } else if (!reasoning_level.empty()) {
        goal_line = "Goal: respond at " + reasoning_level + " reasoning.";
    }

    std::ostringstream output;
    output
        << "You are a controlled local AI reply tool for CODEX.\n"
        << "Return one compact structured reply only.\n"
        << "Tool-first policy:\n"
        << "1. verify file/path/tool availability with MCP tools before concluding anything about permissions or access\n"
        << "2. do not infer system limits without a real tool error\n"
        << "3. if multiple files must be read, process 3-5 files per batch and then explicitly continue the next batch\n"
        << "4. do not use 'please wait' as a final answer unless an async task_id was actually created\n"
        << "Use exactly these markers:\n"
        << "BEGIN_DIRECT_ANSWER\n...\nEND_DIRECT_ANSWER\n"
        << "BEGIN_EVIDENCE\n...\nEND_EVIDENCE\n"
        << "BEGIN_NEXT_ACTION\n...\nEND_NEXT_ACTION\n"
        << "BEGIN_CONFIDENCE\nconfirmed|likely|unclear|blocked\nEND_CONFIDENCE\n"
        << "Do not add extra sections.\n"
        << "Content-first sending rule:\n"
        << "1. answer the business request first\n"
        << "2. use the context summary only as supporting evidence\n"
        << "3. do not expose internal metadata unless directly needed\n"
        << "4. if the conversation is long, compress task state and drop old safety noise instead of repeating it\n"
        << "BUSINESS_REQUEST:\n"
        << effective_prompt_text << "\n"
        << "CONTEXT_SUMMARY:\n"
        << context_summary << "\n"
        << "TARGET:\n"
        << goal_line << "\n"
        << "If evidence is missing, set confidence=unclear or blocked.";
    return output.str();
}

std::string BuildRemoteSessionToolAvailabilitySnapshot(const AgentConfig & config) {
    std::ostringstream output;
    output << "{"
           << "\"snapshot_source\":\"codex_lan_agent\","
           << "\"semantic_binding_mode\":\"strong\","
           << "\"semantic_observability_mode\":\"strict\","
           << "\"tool_first_required\":true,"
           << "\"path_permission_verification\":\"requires_raw_tool_error\","
           << "\"multi_file_read_batch_size\":\"3-5\","
           << "\"async_final_reply_requires_task_id\":true,"
           << "\"available_tool_classes\":" << BuildRemoteSessionMountedToolNamesJson(config) << ","
           << "\"semantic_catalog\":" << BuildRemoteSessionSemanticCatalogJson(config)
           << "}";
    return output.str();
}

std::string BuildRemoteSessionMountedToolsJson(const AgentConfig & config) {
    std::ostringstream output;
    output << "[";
    const std::vector<RemoteSessionMcpToolSpec> specs = BuildRemoteSessionMountedToolSpecs(config);
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (i != 0) {
            output << ",";
        }
        output << "{"
               << "\"type\":\"function\","
               << "\"function\":{"
               << "\"name\":\"" << codex_lan_agent::JsonEscape(specs[i].name) << "\","
               << "\"description\":\"" << codex_lan_agent::JsonEscape(specs[i].description) << "\","
               << "\"parameters\":" << specs[i].input_schema_json
               << "}"
               << "}";
    }
    output << "]";
    return output.str();
}

std::string ResolveRemoteSessionSlicesRoot(const AgentConfig & config) {
    if (!config.remote_session_slices_root.empty()) {
        return config.remote_session_slices_root;
    }
    if (!config.data_root.empty()) {
        return (std::filesystem::path(config.data_root) / "remote_session_slices").string();
    }
    return (std::filesystem::path(config.log_root) / "remote_session_slices").string();
}

std::string DeriveRemoteSessionCollectionUrl(const AgentConfig & config) {
    auto derive_from_endpoint = [](const std::string & endpoint) -> std::string {
        if (endpoint.empty()) {
            return std::string();
        }
        const std::string normalized = Trim(endpoint);
        const std::size_t chat_pos = normalized.find("/v1/chat/completions");
        if (chat_pos != std::string::npos) {
            return normalized.substr(0, chat_pos) + "/v1/remote-sessions";
        }
        const std::size_t v1_pos = normalized.find("/v1/");
        if (v1_pos != std::string::npos) {
            return normalized.substr(0, v1_pos) + "/v1/remote-sessions";
        }
        const std::size_t scheme_pos = normalized.find("://");
        const std::size_t host_start = scheme_pos == std::string::npos ? 0 : scheme_pos + 3;
        const std::size_t path_pos = normalized.find('/', host_start);
        if (path_pos != std::string::npos) {
            return normalized.substr(0, path_pos) + "/v1/remote-sessions";
        }
        return normalized + "/v1/remote-sessions";
    };

    std::string derived = derive_from_endpoint(config.generation_endpoint);
    if (!derived.empty()) {
        return derived;
    }
    return derive_from_endpoint(config.local_chat_endpoint);
}

std::string JsonValueFromRawOrString(const std::string & raw_value) {
    const std::string trimmed = Trim(raw_value);
    if (trimmed.empty()) {
        return "\"\"";
    }
    if ((trimmed.front() == '[' && trimmed.back() == ']')
        || (trimmed.front() == '{' && trimmed.back() == '}')
        || trimmed == "true"
        || trimmed == "false"
        || trimmed == "null") {
        return trimmed;
    }
    if (trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed;
    }
    bool numeric = true;
    bool has_digit = false;
    for (char ch : trimmed) {
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+') {
            if (ch >= '0' && ch <= '9') {
                has_digit = true;
            }
            continue;
        }
        numeric = false;
        break;
    }
    if (numeric && has_digit) {
        return trimmed;
    }
    return "\"" + codex_lan_agent::JsonEscape(trimmed) + "\"";
}

std::string NormalizeRemoteSessionSliceText(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed == "{" || trimmed == "[" || trimmed == "}" || trimmed == "]") {
        return std::string();
    }
    const std::string lowered = ToLowerAscii(trimmed);
    if (lowered == "direct_answer"
        || lowered == "assistant_text"
        || lowered == "business_assistant_text"
        || lowered == "summary"
        || lowered == "slice_summary"
        || lowered == "business_summary"
        || lowered == "user_text"
        || lowered == "business_user_text"
        || lowered == "prompt_text"
        || lowered == "query"
        || lowered == "question") {
        return std::string();
    }
    return value;
}

std::string ExtractRemoteSessionJsonTextField(
    const std::string & text,
    const std::vector<std::string> & keys) {
    const std::string trimmed = Trim(text);
    if (trimmed.empty() || trimmed.front() != '{') {
        return std::string();
    }
    for (const std::string & key : keys) {
        const std::string value = NormalizeRemoteSessionSliceText(ExtractJsonString(trimmed, key));
        if (!Trim(value).empty()) {
            return value;
        }
    }
    return std::string();
}

std::string ExtractRemoteSessionSliceText(
    const std::string & body,
    const std::vector<std::string> & keys) {
    for (const std::string & key : keys) {
        const std::string value = NormalizeRemoteSessionSliceText(ExtractJsonString(body, key));
        if (!Trim(value).empty()) {
            return value;
        }
    }
    return std::string();
}

std::string ExtractRemoteSessionMarkedBlock(
    const std::string & text,
    const std::string & begin_marker,
    const std::string & end_marker) {
    const std::size_t begin_pos = text.find(begin_marker);
    if (begin_pos == std::string::npos) {
        return std::string();
    }
    const std::size_t content_pos = begin_pos + begin_marker.size();
    const std::size_t end_pos = text.find(end_marker, content_pos);
    if (end_pos == std::string::npos) {
        return std::string();
    }
    return Trim(text.substr(content_pos, end_pos - content_pos));
}

std::vector<std::string> ExtractTopLevelJsonArrayItems(const std::string & array_text) {
    std::vector<std::string> items;
    const std::string trimmed = Trim(array_text);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return items;
    }

    bool in_string = false;
    bool escaping = false;
    int object_depth = 0;
    int array_depth = 0;
    std::size_t item_start = std::string::npos;

    for (std::size_t index = 1; index + 1 < trimmed.size(); ++index) {
        const char current = trimmed[index];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (current == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (current == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (current == '{') {
            if (object_depth == 0 && array_depth == 0 && item_start == std::string::npos) {
                item_start = index;
            }
            ++object_depth;
            continue;
        }
        if (current == '}') {
            if (object_depth > 0) {
                --object_depth;
                if (object_depth == 0 && array_depth == 0 && item_start != std::string::npos) {
                    items.push_back(trimmed.substr(item_start, index - item_start + 1));
                    item_start = std::string::npos;
                }
            }
            continue;
        }
        if (current == '[') {
            ++array_depth;
            continue;
        }
        if (current == ']') {
            if (array_depth > 0) {
                --array_depth;
            }
            continue;
        }
    }

    return items;
}

std::string ExtractRemoteSessionBusinessRequest(const std::string & text) {
    const std::string object_text = ExtractRemoteSessionJsonTextField(
        text,
        {"business_user_text", "prompt_text", "user_text", "question", "query", "content"});
    if (!object_text.empty()) {
        return ExtractRemoteSessionBusinessRequest(object_text);
    }
    std::string business = ExtractRemoteSessionMarkedBlock(text, "BUSINESS_REQUEST:", "CONTEXT_SUMMARY:");
    if (!business.empty()) {
        return business;
    }
    business = ExtractRemoteSessionMarkedBlock(text, "BUSINESS_REQUEST", "CONTEXT_SUMMARY");
    if (!business.empty()) {
        return business;
    }
    return NormalizeRemoteSessionSliceText(text);
}

std::string ExtractRemoteSessionBusinessAnswer(const std::string & text) {
    const std::string object_text = ExtractRemoteSessionJsonTextField(
        text,
        {"business_assistant_text", "direct_answer", "assistant_text", "answer", "response", "summary"});
    if (!object_text.empty()) {
        return ExtractRemoteSessionBusinessAnswer(object_text);
    }
    std::string answer = ExtractRemoteSessionMarkedBlock(text, "BEGIN_DIRECT_ANSWER", "END_DIRECT_ANSWER");
    if (!answer.empty()) {
        return answer;
    }
    return NormalizeRemoteSessionSliceText(text);
}

std::string BuildRemoteSessionBusinessSummary(
    const std::string & summary,
    const std::string & assistant_text) {
    std::string business_summary = ExtractRemoteSessionJsonTextField(
        summary,
        {"business_summary", "slice_summary", "summary", "direct_answer", "business_assistant_text"});
    if (!business_summary.empty()) {
        business_summary = ExtractRemoteSessionBusinessAnswer(business_summary);
    }
    if (business_summary.empty()) {
        business_summary = ExtractRemoteSessionBusinessAnswer(summary);
    }
    if (business_summary.empty() || business_summary.find("BEGIN_DIRECT_ANSWER") != std::string::npos) {
        business_summary = ExtractRemoteSessionBusinessAnswer(assistant_text);
    }
    if (business_summary.size() > 240) {
        business_summary = business_summary.substr(0, 240);
    }
    return business_summary;
}

bool LooksLikeRemoteSessionTaskRef(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }
    const std::string lowered = ToLowerAscii(trimmed);
    return lowered.rfind("task-", 0) == 0
        || lowered.rfind("task:", 0) == 0
        || lowered.rfind("task-log(", 0) == 0
        || lowered.rfind("chat-module-task-", 0) == 0
        || lowered.rfind("trace-lan_agent_", 0) == 0
        || lowered.rfind("trace-codex_", 0) == 0
        || lowered.rfind("trace-mcp_", 0) == 0;
}

bool LooksLikeRemoteSessionAllocationId(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }
    const std::string lowered = ToLowerAscii(trimmed);
    return lowered.rfind("chat-", 0) == 0
        || lowered.rfind("chat-module-", 0) == 0;
}

bool RemoteSessionProjectionOnlyBrace(const std::string & value) {
    const std::string trimmed = Trim(value);
    return trimmed == "{"
        || trimmed == "["
        || trimmed == "}"
        || trimmed == "]";
}

bool RemoteSessionProjectionIncomplete(
    const std::string & write_mode,
    const std::string & body,
    const std::string & raw_user_text,
    const std::string & raw_assistant_text,
    const std::string & raw_summary) {
    const std::string lowered_write_mode = ToLowerAscii(Trim(write_mode));
    if (lowered_write_mode == "append" || lowered_write_mode == "append_turn") {
        const std::string combined = ToLowerAscii(body + "\n" + raw_user_text + "\n" + raw_assistant_text + "\n" + raw_summary);
        if (combined.find("\"truncated\":true") != std::string::npos
            || combined.find("\"truncated\": true") != std::string::npos
            || combined.find("\"finish_reason\":\"length\"") != std::string::npos
            || combined.find("\"finish_reason\": \"length\"") != std::string::npos
            || combined.find("\"stop_type\":\"limit\"") != std::string::npos
            || combined.find("\"stop_type\": \"limit\"") != std::string::npos
            || combined.find("[output truncated]") != std::string::npos
            || combined.find("projection_incomplete") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string BuildRemoteSessionTurnBody(
    const AgentConfig & config,
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text,
    const std::string & write_mode,
    const std::string & codex_request_id,
    const std::string & agent_dispatch_id) {
    const std::string effective_prompt_text = prompt_text.empty()
        ? "Provide one compact controlled reply for CODEX based on the supplied task fields and context_refs."
        : prompt_text;
    const std::string provider_id = "llama_cpp_b8851_remote_session";
    const std::string capability_id = "remote_session_turn";
    const std::string tool_availability_snapshot = BuildRemoteSessionToolAvailabilitySnapshot(config);
    const std::string preferred_remote_session_slices_root = ResolveRemoteSessionSlicesRoot(config);
    const std::string preferred_dialog_slices_root = BuildDialogSlicesDir(config);
    const std::string preferred_session_dispatch_root = BuildSessionDispatchDir(config);
    const std::string task_id_json = JsonValueFromRawOrString(task_id);
    const std::string session_id_json = JsonValueFromRawOrString(session_id);
    const std::string speaker_mode_json = JsonValueFromRawOrString(speaker_mode);
    const std::string reasoning_level_json = JsonValueFromRawOrString(reasoning_level);
    const std::string prompt_purpose_json = JsonValueFromRawOrString(prompt_purpose);
    const std::string context_refs_json = JsonValueFromRawOrString(context_refs);
    const std::string response_mode_json = JsonValueFromRawOrString(response_mode);
    const std::string prompt_text_json = JsonValueFromRawOrString(effective_prompt_text);
    const std::string write_mode_json = JsonValueFromRawOrString(write_mode);
    const std::string provider_id_json = JsonValueFromRawOrString(provider_id);
    const std::string capability_id_json = JsonValueFromRawOrString(capability_id);
    const std::string tool_availability_snapshot_json = JsonValueFromRawOrString(tool_availability_snapshot);
    const std::string mounted_tools_json = BuildRemoteSessionMountedToolsJson(config);
    const std::string preferred_remote_session_slices_root_json = JsonValueFromRawOrString(preferred_remote_session_slices_root);
    const std::string preferred_dialog_slices_root_json = JsonValueFromRawOrString(preferred_dialog_slices_root);
    const std::string preferred_session_dispatch_root_json = JsonValueFromRawOrString(preferred_session_dispatch_root);
    const std::string preferred_workspace_root_json = JsonValueFromRawOrString(config.workspace_root);
    const std::string preferred_log_root_json = JsonValueFromRawOrString(config.log_root);
    const std::string codex_request_id_json = JsonValueFromRawOrString(codex_request_id);
    const std::string agent_dispatch_id_json = JsonValueFromRawOrString(agent_dispatch_id);

    std::ostringstream body;
    body
        << "{"
        << "\"task_id\":" << task_id_json << ","
        << "\"session_id\":" << session_id_json << ","
        << "\"speaker_mode\":" << speaker_mode_json << ","
        << "\"reasoning_level\":" << reasoning_level_json << ","
        << "\"prompt_purpose\":" << prompt_purpose_json << ","
        << "\"context_refs\":" << context_refs_json << ","
        << "\"response_mode\":" << response_mode_json << ","
        << "\"prompt_text\":" << prompt_text_json << ","
        << "\"question\":" << prompt_text_json << ","
        << "\"user_text\":" << prompt_text_json << ","
        << "\"query\":" << prompt_text_json << ","
        << "\"content\":" << prompt_text_json << ","
        << "\"write_mode\":" << write_mode_json << ","
        << "\"backend_sampling\":false,"
        << "\"chat_template_kwargs\":{\"enable_thinking\":false},"
        << "\"source_type\":\"codex\","
        << "\"source_label\":\"codex\","
        << "\"handoff_from\":\"codex\","
        << "\"handoff_to\":\"llama.cpp\","
        << "\"takeover_relation\":\"codex_managed\","
        << "\"provider_id\":" << provider_id_json << ","
        << "\"capability_id\":" << capability_id_json << ","
        << "\"tools\":" << mounted_tools_json << ","
        << "\"tool_choice\":{\"type\":\"auto\"},"
        << "\"preferred_remote_session_slices_root\":" << preferred_remote_session_slices_root_json << ","
        << "\"preferred_dialog_slices_root\":" << preferred_dialog_slices_root_json << ","
        << "\"preferred_session_dispatch_root\":" << preferred_session_dispatch_root_json << ","
        << "\"preferred_workspace_root\":" << preferred_workspace_root_json << ","
        << "\"preferred_log_root\":" << preferred_log_root_json << ","
        << "\"codex_request_id\":" << codex_request_id_json << ","
        << "\"agent_dispatch_id\":" << agent_dispatch_id_json << ","
        << "\"tool_availability_snapshot\":" << tool_availability_snapshot_json << ","
        << "\"tool_first_policy\":\"path_permissions_require_tool_verification\","
        << "\"multi_file_read_policy\":\"batch_3_to_5_then_continue\","
        << "\"long_session_policy\":\"compress_task_state_drop_old_safety_noise\","
        << "\"permission_policy\":\"tool_error_required_for_permission_conclusion\","
        << "\"async_reply_policy\":\"no_please_wait_final_without_task_id\","
        << "\"metadata\":{"
        << "\"task_id\":" << task_id_json << ","
        << "\"session_id\":" << session_id_json << ","
        << "\"source_type\":\"codex\","
        << "\"speaker_mode\":" << speaker_mode_json << ","
        << "\"reasoning_level\":" << reasoning_level_json << ","
        << "\"prompt_purpose\":" << prompt_purpose_json << ","
        << "\"context_refs\":" << context_refs_json << ","
        << "\"response_mode\":" << response_mode_json << ","
        << "\"write_mode\":" << write_mode_json << ","
        << "\"provider_id\":" << provider_id_json << ","
        << "\"capability_id\":" << capability_id_json << ","
        << "\"preferred_remote_session_slices_root\":" << preferred_remote_session_slices_root_json << ","
        << "\"preferred_dialog_slices_root\":" << preferred_dialog_slices_root_json << ","
        << "\"preferred_session_dispatch_root\":" << preferred_session_dispatch_root_json << ","
        << "\"preferred_workspace_root\":" << preferred_workspace_root_json << ","
        << "\"preferred_log_root\":" << preferred_log_root_json << ","
        << "\"tool_availability_snapshot\":" << tool_availability_snapshot_json << ","
        << "\"multi_file_read_policy\":\"batch_3_to_5_then_continue\","
        << "\"long_session_policy\":\"compress_task_state_drop_old_safety_noise\","
        << "\"permission_policy\":\"tool_error_required_for_permission_conclusion\","
        << "\"async_reply_policy\":\"no_please_wait_final_without_task_id\","
        << "\"codex_request_id\":" << codex_request_id_json << ","
        << "\"agent_dispatch_id\":" << agent_dispatch_id_json
        << "}"
        << "}";
    return body.str();
}

CommandResult RunRemoteSessionNewTurn(
    const AgentConfig & config,
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text,
    int timeout_ms) {
    const std::string codex_request_id =
        "codex-request-" + SanitizeDispatchToken(task_id, "task") + "-" + BuildRequestTimestampToken();
    const std::string agent_dispatch_id =
        "agent-dispatch-" + SanitizeDispatchToken(task_id, "task") + "-" + BuildRequestTimestampToken();
    const std::string collection_url = DeriveRemoteSessionCollectionUrl(config);
    const std::string endpoint = collection_url.empty() ? std::string() : collection_url + "/new-turn";
    return BuildRemoteSessionTurnResult(
        config,
        endpoint,
        BuildRemoteSessionTurnBody(
            config,
            task_id,
            session_id,
            speaker_mode,
            reasoning_level,
            prompt_purpose,
            context_refs,
            response_mode,
            prompt_text,
            "new",
            codex_request_id,
            agent_dispatch_id),
        "new_turn",
        timeout_ms);
}

CommandResult RunRemoteSessionAppendTurn(
    const AgentConfig & config,
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text,
    int timeout_ms) {
    CommandResult result;
    if (session_id.empty()) {
        result.ok = false;
        result.exit_code = 88;
        result.fields["error"] = "session_id is required for append_turn";
        return result;
    }
    if (LooksLikeRemoteSessionAllocationId(session_id)) {
        result.ok = false;
        result.exit_code = 96;
        result.fields["status"] = "invalid_request";
        result.fields["session_id"] = session_id;
        result.fields["direct_answer"] =
            "The provided session_id is an allocation id, not a usable remote-session id.";
        result.fields["error"] =
            "session_id looks like an allocation id, not a remote-session id";
        result.fields["result"] = "remote_session_id_rejected";
        result.fields["summary"] = "append_turn rejected because the id looks like an allocation id";
        result.fields["next_action"] =
            "call lan_agent_remote_session_new_turn first, then use its returned session_id";
        result.fields["expected_id_kind"] = "remote_session_id";
        result.fields["received_id_kind"] = "allocation_id";
        return result;
    }
    if (LooksLikeRemoteSessionTaskRef(session_id)) {
        result.ok = false;
        result.exit_code = 95;
        result.fields["status"] = "invalid_request";
        result.fields["session_id"] = session_id;
        result.fields["direct_answer"] =
            "The provided session_id is a task reference, not a usable remote-session id.";
        result.fields["error"] =
            "session_id looks like a task reference, not a remote-session id";
        result.fields["result"] = "remote_session_id_rejected";
        result.fields["summary"] = "append_turn rejected because the id looks like a task ref";
        result.fields["next_action"] =
            "call lan_agent_list_remote_session_tasks or "
            "lan_agent_resolve_remote_session_task_refs first, then use the returned session_id";
        result.fields["expected_id_kind"] = "remote_session_id";
        result.fields["received_id_kind"] = "task_ref";
        return result;
    }
    const std::string codex_request_id =
        "codex-request-" + SanitizeDispatchToken(task_id, "task") + "-" + BuildRequestTimestampToken();
    const std::string agent_dispatch_id =
        "agent-dispatch-" + SanitizeDispatchToken(task_id, "task") + "-" + BuildRequestTimestampToken();
    const std::string collection_url = DeriveRemoteSessionCollectionUrl(config);
    const std::string endpoint = collection_url.empty()
        ? std::string()
        : collection_url + "/" + session_id + "/append-turn";
    return BuildRemoteSessionTurnResult(
        config,
        endpoint,
        BuildRemoteSessionTurnBody(
            config,
            task_id,
            session_id,
            speaker_mode,
            reasoning_level,
            prompt_purpose,
            context_refs,
            response_mode,
            prompt_text,
            "append",
            codex_request_id,
            agent_dispatch_id),
        "append_turn",
        timeout_ms);
}

CommandResult ListRemoteSessionsResult(const AgentConfig & config, int timeout_ms) {
    CommandResult result;
    const std::string endpoint = DeriveRemoteSessionCollectionUrl(config);
    if (endpoint.empty()) {
        result.ok = false;
        result.exit_code = 89;
        result.fields["error"] = "remote-session endpoint is not configured";
        return result;
    }
    const codex_lan_agent::HttpResponse response = codex_lan_agent::GetUrl(endpoint, timeout_ms);
    const std::string log_path = BuildLogPath(config, "list_remote_sessions");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "timeout_ms=" << timeout_ms << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";
    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 90;
    result.fields["status"] = response.ok ? "ok" : "failed";
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    result.fields["session_semantic_projection_source"] = "latest_turn.tool_availability_snapshot";
    result.fields["session_semantic_projection_mode"] = "derived_from_session_detail";
    result.fields["session_semantic_projection_list_json"] = "[]";
    result.fields["session_semantic_projection_visible_count"] = "0";
    result.fields["session_semantic_projection_missing_count"] = "0";
    if (response.ok) {
        const std::vector<std::string> items =
            ExtractTopLevelJsonArrayItems(ExtractJsonObjectRaw(response.body, "items"));
        std::ostringstream projection_list;
        projection_list << "[";
        bool first_projection = true;
        int ready_count = 0;
        int missing_count = 0;
        for (const std::string & item : items) {
            const std::string session_id = ExtractJsonString(item, "session_id");
            if (session_id.empty()) {
                continue;
            }
            const std::string detail_endpoint = endpoint + "/" + session_id;
            const codex_lan_agent::HttpResponse detail_response =
                codex_lan_agent::GetUrl(detail_endpoint, timeout_ms);
            const std::string projection_json = detail_response.ok
                ? BuildRemoteSessionSemanticProjectionPreviewJson(
                    detail_response.body,
                    session_id,
                    ExtractJsonString(item, "title"),
                    ExtractJsonString(item, "current_summary"),
                    ExtractJsonString(item, "last_turn_id"))
                : std::string();
            if (!first_projection) {
                projection_list << ",";
            }
            first_projection = false;
            if (!projection_json.empty()) {
                projection_list << projection_json;
                if (projection_json.find("\"session_semantic_projection_ready\":true") != std::string::npos) {
                    ++ready_count;
                } else {
                    ++missing_count;
                }
            } else {
                ++missing_count;
                projection_list
                    << "{"
                    << "\"session_id\":\"" << codex_lan_agent::JsonEscape(session_id) << "\","
                    << "\"title\":\"" << codex_lan_agent::JsonEscape(ExtractJsonString(item, "title")) << "\","
                    << "\"last_turn_id\":\"" << codex_lan_agent::JsonEscape(ExtractJsonString(item, "last_turn_id")) << "\","
                    << "\"current_summary\":\"" << codex_lan_agent::JsonEscape(ExtractJsonString(item, "current_summary")) << "\","
                    << "\"session_semantic_projection_ready\":false,"
                    << "\"session_semantic_projection_source\":\"latest_turn.tool_availability_snapshot\""
                    << "}";
            }
        }
        projection_list << "]";
        result.fields["session_semantic_projection_list_json"] = projection_list.str();
        result.fields["session_semantic_projection_visible_count"] = std::to_string(ready_count);
        result.fields["session_semantic_projection_missing_count"] = std::to_string(missing_count);
    }
    result.fields["result"] = response.ok ? "remote_sessions_listed" : "remote_sessions_list_failed";
    result.fields["summary"] = response.ok ? "remote sessions listed" : "remote sessions list failed";
    return result;
}

CommandResult GetRemoteSessionResult(
    const AgentConfig & config,
    const std::string & session_id,
    int timeout_ms) {
    CommandResult result;
    if (session_id.empty()) {
        result.ok = false;
        result.exit_code = 91;
        result.fields["error"] = "session_id is required";
        return result;
    }
    if (LooksLikeRemoteSessionAllocationId(session_id)) {
        result.ok = false;
        result.exit_code = 96;
        result.fields["status"] = "invalid_request";
        result.fields["session_id"] = session_id;
        result.fields["direct_answer"] =
            "The provided session_id is an allocation id, not a usable remote-session id.";
        result.fields["error"] =
            "session_id looks like an allocation id, not a remote-session id";
        result.fields["result"] = "remote_session_id_rejected";
        result.fields["summary"] = "session lookup rejected because the id looks like an allocation id";
        result.fields["next_action"] =
            "call lan_agent_remote_session_new_turn first, then use its returned session_id";
        result.fields["expected_id_kind"] = "remote_session_id";
        result.fields["received_id_kind"] = "allocation_id";
        return result;
    }
    if (LooksLikeRemoteSessionTaskRef(session_id)) {
        result.ok = false;
        result.exit_code = 95;
        result.fields["status"] = "invalid_request";
        result.fields["session_id"] = session_id;
        result.fields["direct_answer"] =
            "The provided session_id is a task reference, not a usable remote-session id.";
        result.fields["error"] =
            "session_id looks like a task reference, not a remote-session id";
        result.fields["result"] = "remote_session_id_rejected";
        result.fields["summary"] = "session lookup rejected because the id looks like a task ref";
        result.fields["next_action"] =
            "call lan_agent_list_remote_session_tasks or "
            "lan_agent_resolve_remote_session_task_refs first, then use the returned session_id";
        result.fields["expected_id_kind"] = "remote_session_id";
        result.fields["received_id_kind"] = "task_ref";
        return result;
    }
    const std::string collection_url = DeriveRemoteSessionCollectionUrl(config);
    if (collection_url.empty()) {
        result.ok = false;
        result.exit_code = 89;
        result.fields["error"] = "remote-session endpoint is not configured";
        return result;
    }
    const std::string endpoint = collection_url + "/" + session_id;
    const codex_lan_agent::HttpResponse response = codex_lan_agent::GetUrl(endpoint, timeout_ms);
    const std::string log_path = BuildLogPath(config, "get_remote_session");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "timeout_ms=" << timeout_ms << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";
    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 92;
    result.fields["status"] = response.ok ? "ok" : "failed";
    result.fields["session_id"] = session_id;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    ApplyRemoteSessionSemanticProjectionFields(response.body, &result);
    result.fields["result"] = response.ok ? "remote_session_loaded" : "remote_session_load_failed";
    result.fields["summary"] = response.ok ? "remote session loaded" : "remote session load failed";
    return result;
}

CommandResult ReadRemoteSessionSliceResult(
    const AgentConfig & config,
    const std::string & session_id,
    int timeout_ms) {
    CommandResult session_result = GetRemoteSessionResult(config, session_id, timeout_ms);
    CommandResult result;
    result.ok = session_result.ok;
    result.exit_code = session_result.ok ? 0 : session_result.exit_code;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "read_remote_session_slice";
    result.fields["provider_id"] = "codex_lan_agent_remote_session_reader";
    result.fields["capability_id"] = "remote_session_slice_read";
    result.fields["session_id"] = session_id;
    result.fields["source_type"] = "remote_session";
    result.fields["result_ref"] = GetFieldOrDefault(session_result, "log_path", "");
    result.fields["evidence_ref"] = GetFieldOrDefault(session_result, "log_path", "");
    result.fields["remote_session_status"] = GetFieldOrDefault(session_result, "status", "");
    result.fields["remote_session_body"] = GetFieldOrDefault(session_result, "body", "");
    result.fields["log_path"] = GetFieldOrDefault(session_result, "log_path", "");
    if (!session_result.ok) {
        result.fields["error"] = GetFieldOrDefault(session_result, "error", "remote session load failed");
        result.fields["result"] = "remote_session_slice_read_failed";
        return result;
    }

    const std::string body = GetFieldOrDefault(session_result, "body", "");
    const std::string extracted_session_id = ExtractRemoteSessionSliceText(body, {"session_id", "id"});
    const std::string effective_session_id = extracted_session_id.empty() ? session_id : extracted_session_id;
    const std::string turn_id = ExtractRemoteSessionSliceText(body, {"turn_id", "last_turn_id", "message_id"});
    const std::string effective_turn_id = turn_id.empty() ? "remote-session-latest" : turn_id;
    const std::string task_id = ExtractRemoteSessionSliceText(body, {"task_id"});
    const std::string write_mode = ExtractRemoteSessionSliceText(body, {"write_mode"});
    const std::string raw_user_text = ExtractRemoteSessionSliceText(body, {"prompt_text", "business_user_text", "user_text", "question", "query"});
    const std::string business_user_text = ExtractRemoteSessionBusinessRequest(raw_user_text);
    std::string raw_assistant_text = ExtractRemoteSessionSliceText(body, {"direct_answer", "business_assistant_text", "assistant_text", "answer", "response"});
    if (raw_assistant_text.empty()) {
        raw_assistant_text = ExtractRemoteSessionSliceText(body, {"summary"});
    }
    const std::string business_assistant_text = ExtractRemoteSessionBusinessAnswer(raw_assistant_text);
    std::string raw_slice_summary = ExtractRemoteSessionSliceText(body, {"business_summary", "slice_summary", "summary", "direct_answer"});
    std::string business_summary = BuildRemoteSessionBusinessSummary(raw_slice_summary, business_assistant_text);
    const bool projection_incomplete = RemoteSessionProjectionIncomplete(
        write_mode,
        body,
        raw_user_text,
        raw_assistant_text,
        raw_slice_summary);
    if (business_summary.empty() && !business_assistant_text.empty()) {
        business_summary = business_assistant_text.substr(0, std::min<std::size_t>(business_assistant_text.size(), 240));
    }
    if (business_assistant_text.empty()) {
        result.ok = false;
        result.exit_code = projection_incomplete ? 94 : 93;
        result.fields["error"] = projection_incomplete
            ? "remote-session append projection is incomplete"
            : "remote-session slice has no usable business assistant_text";
        result.fields["raw_user_text"] = raw_user_text;
        result.fields["raw_assistant_text"] = raw_assistant_text;
        result.fields["raw_slice_summary"] = raw_slice_summary;
        result.fields["projection_incomplete"] = projection_incomplete ? "true" : "false";
        result.fields["projection_status"] = projection_incomplete ? "incomplete" : "failed";
        result.fields["vector_ready"] = "false";
        result.fields["vector_skip_reason"] = projection_incomplete
            ? "append_truncated_projection_incomplete"
            : (RemoteSessionProjectionOnlyBrace(raw_assistant_text)
                ? "dirty_content_only_brace"
                : "empty_business_assistant_text");
        result.fields["quality_guard"] = projection_incomplete
            ? "append_truncated_projection_incomplete"
            : (RemoteSessionProjectionOnlyBrace(raw_assistant_text)
                ? "dirty_content_only_brace"
                : "business_assistant_text_required");
        result.fields["result"] = projection_incomplete
            ? "remote_session_slice_projection_incomplete"
            : "remote_session_slice_projection_failed";
        result.fields["next_action"] = projection_incomplete
            ? "retry append with a shorter result summary or write a browser-visible summary turn before slice projection"
            : "inspect remote_session_body or retry remote-session turn";
        return result;
    }
    const std::string reasoning_level = ExtractRemoteSessionSliceText(body, {"reasoning_level"});
    const std::string primary_intent = ExtractRemoteSessionSliceText(body, {"primary_intent"});
    const std::string confidence = ExtractRemoteSessionSliceText(body, {"confidence"});
    const std::string result_ref = FirstNonEmpty(
        ExtractRemoteSessionSliceText(body, {"result_ref"}),
        GetFieldOrDefault(session_result, "log_path", ""),
        "unbound");
    const std::string evidence_ref = FirstNonEmpty(
        ExtractRemoteSessionSliceText(body, {"evidence_ref"}),
        GetFieldOrDefault(session_result, "log_path", ""),
        "unbound");
    const std::string slice_id = "remote-session-slice:" + SanitizeDispatchToken(effective_session_id, "session")
        + ":" + SanitizeDispatchToken(effective_turn_id, "turn");
    const std::string vector_payload = business_user_text + "\n" + business_assistant_text;
    const std::string dedup_hash = StableContentChecksum(
        effective_session_id + "\n" + effective_turn_id + "\n" + business_user_text + "\n" + business_assistant_text);

    result.fields["slice_id"] = slice_id;
    result.fields["canonical_slice_id"] = slice_id;
    result.fields["dup_of"] = "";
    result.fields["dedup_status"] = "read_only";
    result.fields["dedup_reason"] = "remote_session_slice_read_does_not_write";
    result.fields["slice_type"] = "remote_session_slice";
    result.fields["task_id"] = task_id.empty() ? "unbound" : task_id;
    result.fields["session_id"] = effective_session_id;
    result.fields["turn_id"] = effective_turn_id;
    result.fields["user_text"] = business_user_text;
    result.fields["assistant_text"] = business_assistant_text;
    result.fields["slice_summary"] = business_summary;
    result.fields["business_user_text"] = business_user_text;
    result.fields["business_assistant_text"] = business_assistant_text;
    result.fields["business_summary"] = business_summary;
    result.fields["raw_user_text"] = raw_user_text;
    result.fields["raw_assistant_text"] = raw_assistant_text;
    result.fields["raw_slice_summary"] = raw_slice_summary;
    result.fields["reasoning_level"] = reasoning_level.empty() ? "unspecified" : reasoning_level;
    result.fields["primary_intent"] = primary_intent.empty() ? "unspecified" : primary_intent;
    result.fields["confidence"] = confidence.empty() ? "unclear" : confidence;
    result.fields["result_ref"] = result_ref;
    result.fields["evidence_ref"] = evidence_ref;
    result.fields["write_mode"] = write_mode;
    result.fields["vector_payload"] = vector_payload;
    result.fields["vector_ready"] = "false";
    result.fields["vector_skip_reason"] = projection_incomplete
        ? "append_truncated_projection_incomplete"
        : "read_only_remote_session_slice";
    result.fields["dedup_hash"] = dedup_hash;
    result.fields["projection_incomplete"] = projection_incomplete ? "true" : "false";
    result.fields["projection_status"] = projection_incomplete ? "incomplete" : "complete";
    result.fields["quality_guard"] = projection_incomplete
        ? "append_truncated_projection_incomplete"
        : "business_projection_v1";
    result.fields["result"] = "remote_session_slice_read";
    result.fields["summary"] = business_summary.empty() ? "remote session slice read" : business_summary;
    result.fields["next_action"] = "call lan_agent_record_dialog_slice if this read-only remote-session slice should be persisted";
    return result;
}

CommandResult ResolveRemoteSessionTaskRefsResult(
    const AgentConfig & config,
    const std::string & session_id,
    const std::string & task_group_id,
    const std::string & task_id,
    const std::string & runner,
    int timeout_ms) {
    CommandResult session_result = GetRemoteSessionResult(config, session_id, timeout_ms);
    CommandResult result;
    result.ok = session_result.ok;
    result.exit_code = session_result.ok ? 0 : session_result.exit_code;
    result.fields["session_id"] = session_id;
    result.fields["task_group_id"] = task_group_id;
    result.fields["task_id_filter"] = task_id;
    result.fields["runner_filter"] = runner;
    result.fields["query_source"] = "remote_session_8095";
    result.fields["result_ref"] = GetFieldOrDefault(session_result, "log_path", "");
    result.fields["evidence_ref"] = GetFieldOrDefault(session_result, "log_path", "");
    if (!session_result.ok) {
        result.fields["error"] = GetFieldOrDefault(session_result, "error", "remote session load failed");
        result.fields["result"] = "remote_session_task_refs_failed";
        result.fields["summary"] = "remote session task refs unavailable";
        return result;
    }

    const std::string body = GetFieldOrDefault(session_result, "body", "");
    const std::vector<std::string> turn_items =
        ExtractTopLevelJsonArrayItems(ExtractJsonObjectRaw(body, "turns"));
    if (turn_items.empty()) {
        result.ok = false;
        result.exit_code = 96;
        result.fields["error"] = "remote session has no turns";
        result.fields["result"] = "remote_session_task_refs_empty";
        result.fields["summary"] = "remote session has no task refs";
        return result;
    }

    std::string matched_turn;
    for (auto it = turn_items.rbegin(); it != turn_items.rend(); ++it) {
        const std::string & turn = *it;
        const std::string turn_task_id = Trim(ExtractJsonString(turn, "task_id"));
        const std::string turn_task_group_id = Trim(ExtractJsonString(turn, "task_group_id"));
        const std::string turn_runner = FirstNonEmpty(
            Trim(ExtractJsonString(turn, "runner")),
            Trim(ExtractJsonString(turn, "runner_label")));
        if (!task_id.empty() && turn_task_id != task_id) {
            continue;
        }
        if (!task_group_id.empty() && turn_task_group_id != task_group_id) {
            continue;
        }
        if (!runner.empty() && turn_runner != runner) {
            continue;
        }
        matched_turn = turn;
        break;
    }

    if (matched_turn.empty()) {
        result.ok = false;
        result.exit_code = 97;
        result.fields["error"] = "no matching remote session turn found";
        result.fields["result"] = "remote_session_task_refs_empty";
        result.fields["summary"] = "no matching remote session task refs";
        return result;
    }

    const std::string matched_turn_id = FirstNonEmpty(
        ExtractJsonString(matched_turn, "turn_id"),
        ExtractJsonString(matched_turn, "id"),
        "remote-session-turn");
    const std::string matched_task_id = Trim(ExtractJsonString(matched_turn, "task_id"));
    const std::string matched_task_group_id = Trim(ExtractJsonString(matched_turn, "task_group_id"));
    const std::string matched_runner = FirstNonEmpty(
        Trim(ExtractJsonString(matched_turn, "runner")),
        Trim(ExtractJsonString(matched_turn, "runner_label")));
    const std::string matched_result_ref = Trim(ExtractJsonString(matched_turn, "result_ref"));
    const std::string matched_evidence_ref = Trim(ExtractJsonString(matched_turn, "evidence_ref"));
    const std::string effective_task_id = matched_task_id.empty() ? "unbound" : matched_task_id;
    const std::string audit_ref = "session:" + session_id + "/turn:" + matched_turn_id;

    result.fields["turn_id"] = matched_turn_id;
    result.fields["task_id"] = effective_task_id;
    result.fields["task_group_id"] = matched_task_group_id;
    result.fields["runner"] = matched_runner;
    result.fields["audit_ref"] = audit_ref;
    result.fields["turn_result_ref"] = matched_result_ref;
    result.fields["turn_evidence_ref"] = matched_evidence_ref;
    result.fields["summary"] = Trim(ExtractJsonString(matched_turn, "summary"));
    result.fields["direct_answer"] = Trim(ExtractJsonString(matched_turn, "direct_answer"));
    result.fields["task_log_ref"] = matched_task_id.empty()
        ? ""
        : ("task-log(" + matched_task_id + ")");

    if (!matched_task_id.empty() && g_task_manager != nullptr) {
        const CommandResult task_result = g_task_manager->GetTaskResult(matched_task_id);
        result.fields["resolved_log_path"] = GetFieldOrDefault(task_result, "resolved_log_path", "");
        result.fields["resolved_result_ref"] = GetFieldOrDefault(task_result, "result_ref", "");
        result.fields["resolved_evidence_ref"] = GetFieldOrDefault(task_result, "evidence_ref", "");
        result.fields["task_status"] = GetFieldOrDefault(task_result, "status", "");
    } else {
        result.fields["resolved_log_path"] = "";
        result.fields["resolved_result_ref"] = "";
        result.fields["resolved_evidence_ref"] = "";
        result.fields["task_status"] = "";
    }

    result.fields["result_ref"] = FirstNonEmpty(
        result.fields["resolved_result_ref"],
        matched_result_ref,
        audit_ref,
        GetFieldOrDefault(session_result, "log_path", ""));
    result.fields["evidence_ref"] = FirstNonEmpty(
        result.fields["resolved_evidence_ref"],
        matched_evidence_ref,
        result.fields["task_log_ref"],
        audit_ref,
        GetFieldOrDefault(session_result, "log_path", ""));
    result.fields["result"] = "remote_session_task_refs_resolved";
    result.fields["next_action"] = GetFieldOrDefault(result, "resolved_log_path", "").empty()
        ? "inspect result_ref or evidence_ref"
        : "use lan_agent_read_text_file with resolved_log_path or result_ref";
    return result;
}

CommandResult ListRemoteSessionTasksResult(
    const AgentConfig & config,
    const std::string & session_id_filter,
    const std::string & task_group_id_filter,
    const std::string & runner_filter,
    int max_entries,
    int timeout_ms) {
    CommandResult sessions_result = ListRemoteSessionsResult(config, timeout_ms);
    CommandResult result;
    result.ok = sessions_result.ok;
    result.exit_code = sessions_result.ok ? 0 : sessions_result.exit_code;
    result.fields["query_source"] = "remote_session_8095";
    result.fields["session_id_filter"] = session_id_filter;
    result.fields["task_group_id_filter"] = task_group_id_filter;
    result.fields["runner_filter"] = runner_filter;
    result.fields["result_ref"] = GetFieldOrDefault(sessions_result, "log_path", "");
    result.fields["evidence_ref"] = GetFieldOrDefault(sessions_result, "log_path", "");
    if (!sessions_result.ok) {
        result.fields["error"] = GetFieldOrDefault(sessions_result, "error", "remote session list failed");
        result.fields["result"] = "remote_session_tasks_failed";
        result.fields["summary"] = "remote session task list unavailable";
        return result;
    }

    const std::vector<std::string> items =
        ExtractTopLevelJsonArrayItems(ExtractJsonObjectRaw(GetFieldOrDefault(sessions_result, "body", "{}"), "items"));
    if (items.empty()) {
        result.ok = false;
        result.exit_code = 99;
        result.fields["error"] = "remote session list items are not available";
        result.fields["result"] = "remote_session_tasks_failed";
        result.fields["summary"] = "remote session task list unavailable";
        return result;
    }

    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    int visible_index = 0;
    for (const std::string & item : items) {
        const std::string session_id = ExtractJsonString(item, "session_id");
        const std::string task_group_id = ExtractJsonString(item, "task_group_id");
        const std::string last_task_id = ExtractJsonString(item, "last_task_id");
        if (!session_id_filter.empty() && session_id != session_id_filter) {
            continue;
        }
        if (!task_group_id_filter.empty() && task_group_id != task_group_id_filter) {
            continue;
        }
        if (last_task_id.empty() && task_group_id.empty()) {
            continue;
        }

        CommandResult refs = ResolveRemoteSessionTaskRefsResult(
            config,
            session_id,
            task_group_id_filter.empty() ? task_group_id : task_group_id_filter,
            last_task_id,
            runner_filter,
            timeout_ms);
        if (!refs.ok && !runner_filter.empty()) {
            continue;
        }

        const std::string prefix = "item_" + std::to_string(visible_index) + "_";
        result.fields[prefix + "session_id"] = session_id;
        result.fields[prefix + "task_group_id"] = task_group_id;
        result.fields[prefix + "updated_at"] = ExtractJsonRawValue(item, "updated_at");
        result.fields[prefix + "title"] = ExtractJsonString(item, "title");
        result.fields[prefix + "turn_count"] = ExtractJsonRawValue(item, "turn_count");
        result.fields[prefix + "last_turn_id"] = ExtractJsonString(item, "last_turn_id");
        result.fields[prefix + "task_id"] = GetFieldOrDefault(refs, "task_id", last_task_id);
        result.fields[prefix + "runner"] = GetFieldOrDefault(refs, "runner", "");
        result.fields[prefix + "task_log_ref"] = GetFieldOrDefault(refs, "task_log_ref", "");
        result.fields[prefix + "result_ref"] = GetFieldOrDefault(refs, "result_ref", ExtractJsonString(item, "last_result_ref"));
        result.fields[prefix + "evidence_ref"] = GetFieldOrDefault(refs, "evidence_ref", ExtractJsonString(item, "last_evidence_ref"));
        result.fields[prefix + "resolved_log_path"] = GetFieldOrDefault(refs, "resolved_log_path", "");
        result.fields[prefix + "resolved_result_ref"] = GetFieldOrDefault(refs, "resolved_result_ref", "");
        result.fields[prefix + "resolved_evidence_ref"] = GetFieldOrDefault(refs, "resolved_evidence_ref", "");
        result.fields[prefix + "task_status"] = GetFieldOrDefault(refs, "task_status", "");
        result.fields[prefix + "summary"] = FirstNonEmpty(
            GetFieldOrDefault(refs, "summary", ""),
            ExtractJsonString(item, "current_summary"));

        ++visible_index;
        if (visible_index >= bounded_max_entries) {
            break;
        }
    }

    result.fields["visible_count"] = std::to_string(visible_index);
    if (visible_index > 0) {
        const std::string latest_prefix = "item_0_";
        result.fields["latest_session_id"] = result.fields[latest_prefix + "session_id"];
        result.fields["latest_task_group_id"] = result.fields[latest_prefix + "task_group_id"];
        result.fields["latest_task_id"] = result.fields[latest_prefix + "task_id"];
        result.fields["latest_runner"] = result.fields[latest_prefix + "runner"];
        result.fields["latest_task_log_ref"] = result.fields[latest_prefix + "task_log_ref"];
        result.fields["latest_result_ref"] = result.fields[latest_prefix + "result_ref"];
        result.fields["latest_evidence_ref"] = result.fields[latest_prefix + "evidence_ref"];
        result.fields["latest_resolved_log_path"] = result.fields[latest_prefix + "resolved_log_path"];
        result.fields["latest_resolved_result_ref"] = result.fields[latest_prefix + "resolved_result_ref"];
        result.fields["latest_resolved_evidence_ref"] = result.fields[latest_prefix + "resolved_evidence_ref"];
        result.fields["result"] = "remote_session_tasks_listed";
        result.fields["summary"] = "remote session tasks listed";
    } else {
        result.fields["result"] = "remote_session_tasks_empty";
        result.fields["summary"] = "no matching remote session tasks";
    }
    return result;
}
