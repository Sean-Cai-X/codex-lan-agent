#pragma once

std::string BuildDialogSlicesDir(const AgentConfig & config);

std::string BuildSessionDispatchDir(const AgentConfig & config);

bool StartsWithNormalizedPathText(
    const std::string & value,
    const std::string & prefix) {
    if (value.empty() || prefix.empty()) {
        return false;
    }
    const std::string normalized_value = ToLowerAscii(std::filesystem::path(value).lexically_normal().string());
    const std::string normalized_prefix = ToLowerAscii(std::filesystem::path(prefix).lexically_normal().string());
    return normalized_value.rfind(normalized_prefix, 0) == 0;
}

std::string NormalizeRemoteTurnVisibleText(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()
        || trimmed == "{"
        || trimmed == "["
        || trimmed == "}"
        || trimmed == "]") {
        return std::string();
    }
    const std::string lowered = ToLowerAscii(trimmed);
    if (lowered == "direct_answer"
        || lowered == "assistant_text"
        || lowered == "business_assistant_text"
        || lowered == "summary"
        || lowered == "slice_summary"
        || lowered == "business_summary") {
        return std::string();
    }
    return trimmed;
}

std::string ExtractRemoteTurnVisibleField(
    const std::string & text,
    const std::vector<std::string> & keys) {
    const std::string normalized_text = Trim(text);
    if (normalized_text.empty()) {
        return std::string();
    }
    if (!normalized_text.empty() && normalized_text.front() == '{') {
        for (const std::string & key : keys) {
            const std::string direct_value = NormalizeRemoteTurnVisibleText(ExtractJsonString(normalized_text, key));
            if (!direct_value.empty()) {
                return direct_value;
            }
        }
        for (const std::string & key : keys) {
            const std::string nested_value = NormalizeRemoteTurnVisibleText(ExtractJsonString(normalized_text, key));
            if (nested_value.empty() || nested_value.front() != '{') {
                continue;
            }
            const std::string recursive_value = ExtractRemoteTurnVisibleField(nested_value, keys);
            if (!recursive_value.empty()) {
                return recursive_value;
            }
        }
    }
    return NormalizeRemoteTurnVisibleText(text);
}

std::string ExtractAssistantRoleContent(const std::string & text) {
    const std::string normalized_text = Trim(text);
    if (normalized_text.empty()) {
        return std::string();
    }
    const std::vector<std::string> assistant_role_markers = {
        "\"role\":\"assistant\"",
        "\"role\": \"assistant\""
    };
    for (const std::string & marker : assistant_role_markers) {
        const std::size_t role_pos = normalized_text.find(marker);
        if (role_pos == std::string::npos) {
            continue;
        }
        const std::string tail = normalized_text.substr(role_pos);
        const std::string content = NormalizeRemoteTurnVisibleText(ExtractJsonString(tail, "content"));
        if (!content.empty()) {
            return content;
        }
    }
    return std::string();
}

std::string ExtractRemoteTurnBusinessPayloadText(const std::string & text) {
    const std::vector<std::string> summary_keys = {
        "business_summary",
        "slice_summary",
        "summary",
        "solution_summary",
        "direct_answer",
        "business_assistant_text",
        "assistant_text"
    };
    std::string payload = ExtractRemoteTurnVisibleField(text, summary_keys);
    if (!payload.empty()) {
        return payload;
    }
    const std::string assistant_content = ExtractAssistantRoleContent(text);
    if (assistant_content.empty()) {
        return std::string();
    }
    payload = ExtractRemoteTurnVisibleField(assistant_content, summary_keys);
    if (!payload.empty()) {
        return payload;
    }
    return NormalizeRemoteTurnVisibleText(assistant_content);
}

std::string BuildBrowserVisibleRemoteTurnSummary(const std::string & response_body) {
    std::string summary = ExtractRemoteTurnBusinessPayloadText(response_body);
    if (summary.size() > 240) {
        summary = summary.substr(0, 240);
    }
    return summary;
}

std::string NormalizeRemoteSessionWriteMode(const std::string & value) {
    const std::string lowered = ToLowerAscii(Trim(value));
    if (lowered == "append" || lowered == "append_turn") {
        return "append";
    }
    if (lowered == "new" || lowered == "new_turn" || lowered == "new_session") {
        return "new";
    }
    return Trim(value);
}

bool IsRemoteAppendWriteMode(const std::string & value) {
    return NormalizeRemoteSessionWriteMode(value) == "append";
}

std::string ExtractRemoteTurnNestedStringField(
    const std::string & text,
    const std::vector<std::string> & keys) {
    const std::string normalized_text = Trim(text);
    if (normalized_text.empty() || normalized_text.front() != '{') {
        return std::string();
    }
    for (const std::string & key : keys) {
        const std::string value = Trim(ExtractJsonString(normalized_text, key));
        if (!value.empty()) {
            return value;
        }
    }
    const std::vector<std::string> wrapper_keys = {
        "response_payload",
        "raw_response",
        "structured_conclusion",
        "metadata",
        "request_payload"
    };
    for (const std::string & wrapper_key : wrapper_keys) {
        const std::string nested = ExtractJsonObjectRaw(normalized_text, wrapper_key);
        if (nested.empty()) {
            continue;
        }
        const std::string nested_value = ExtractRemoteTurnNestedStringField(nested, keys);
        if (!nested_value.empty()) {
            return nested_value;
        }
    }
    return std::string();
}

bool HasRemoteTurnOnlyBraceProjection(const std::string & response_body) {
    const std::vector<std::string> projection_keys = {
        "business_summary",
        "slice_summary",
        "summary",
        "solution_summary",
        "direct_answer",
        "business_assistant_text",
        "assistant_text"
    };
    for (const std::string & key : projection_keys) {
        const std::string value = ExtractRemoteTurnNestedStringField(response_body, {key});
        const std::string trimmed = Trim(value);
        if (trimmed == "{"
            || trimmed == "["
            || trimmed == "}"
            || trimmed == "]") {
            return true;
        }
    }
    return false;
}

CommandResult BuildRemoteSessionTurnResult(
    const AgentConfig & config,
    const std::string & endpoint,
    const std::string & request_body,
    const std::string & requested_write_mode,
    int timeout_ms) {
    CommandResult result;
    const std::string normalized_requested_write_mode = NormalizeRemoteSessionWriteMode(requested_write_mode);
    if (endpoint.empty()) {
        result.ok = false;
        result.exit_code = 86;
        result.fields["error"] = "remote-session endpoint is not configured";
        result.fields["requested_write_mode"] = normalized_requested_write_mode.empty()
            ? requested_write_mode
            : normalized_requested_write_mode;
        return result;
    }

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(endpoint, request_body, timeout_ms);
    const std::string log_path = BuildLogPath(
        config,
        IsRemoteAppendWriteMode(normalized_requested_write_mode)
            ? "call_remote_session_append_turn"
            : "call_remote_session_new_turn");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "requested_write_mode=" << requested_write_mode << "\n";
    output << "timeout_ms=" << timeout_ms << "\n";
    output << "request_body=\n" << request_body << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 87;
    result.fields["status"] = response.ok ? "ok" : "failed";
    result.fields["status_code"] = std::to_string(response.status_code);
    const std::string request_task_id = ExtractJsonString(request_body, "task_id");
    const std::string request_session_id = ExtractJsonString(request_body, "session_id");
    const std::string request_source_type = FirstNonEmpty(
        ExtractJsonString(request_body, "source_type"),
        "codex");
    const std::string request_provider_id = FirstNonEmpty(
        ExtractJsonString(request_body, "provider_id"),
        "llama_cpp_b8851_remote_session");
    const std::string request_capability_id = FirstNonEmpty(
        ExtractJsonString(request_body, "capability_id"),
        "remote_session_turn");
    result.fields["requested_write_mode"] = normalized_requested_write_mode.empty()
        ? requested_write_mode
        : normalized_requested_write_mode;
    result.fields["request_session_id"] = request_session_id;
    result.fields["request_source_type"] = request_source_type;
    result.fields["request_provider_id"] = request_provider_id;
    result.fields["request_capability_id"] = request_capability_id;
    result.fields["task_id"] = request_task_id;
    result.fields["source_type"] = request_source_type;
    result.fields["provider_id"] = request_provider_id;
    result.fields["capability_id"] = request_capability_id;
    result.fields["slice_refs"] = ExtractJsonRawValue(request_body, "slice_refs");
    result.fields["storage_refs"] = ExtractJsonRawValue(request_body, "storage_refs");
    result.fields["request_content_policy"] = "content_first_v1";
    result.fields["metadata_delivery"] = "metadata_tail";
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    result.fields["result"] = response.ok ? "remote_session_turn_completed" : "remote_session_turn_failed";
    result.fields["preferred_workspace_root"] = config.workspace_root;
    result.fields["preferred_log_root"] = config.log_root;
    result.fields["preferred_dialog_slices_root"] = BuildDialogSlicesDir(config);
    result.fields["preferred_session_dispatch_root"] = BuildSessionDispatchDir(config);
    result.fields["preferred_remote_session_slices_root"] = config.remote_session_slices_root;
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }

    auto copy_string_field = [&](const std::string & key) {
        const std::string value = ExtractJsonString(response.body, key);
        if (!value.empty()) {
            result.fields[key] = value;
        }
    };
    auto copy_raw_field = [&](const std::string & key) {
        const std::string raw = ExtractJsonRawValue(response.body, key);
        if (!raw.empty()) {
            result.fields[key] = raw;
        }
    };

    copy_string_field("session_id");
    copy_string_field("turn_id");
    copy_string_field("write_mode");
    copy_string_field("direct_answer");
    copy_string_field("next_action");
    copy_string_field("confidence");
    copy_string_field("result_ref");
    copy_string_field("evidence_ref");
    copy_string_field("codex_request_id");
    copy_string_field("agent_dispatch_id");
    copy_string_field("provider_id");
    copy_string_field("capability_id");
    copy_raw_field("evidence");
    copy_raw_field("timings");
    copy_raw_field("slice_refs");
    copy_raw_field("storage_refs");
    copy_string_field("slice_id");
    copy_string_field("slice_path");
    copy_string_field("dedup_key");
    copy_string_field("dedup_hash");
    copy_string_field("canonical_slice_id");
    copy_string_field("canonical_status");
    copy_string_field("error_signature");
    copy_string_field("solution_summary");
    copy_string_field("strategy_family");
    copy_string_field("vector_skip_reason");
    copy_raw_field("vector_ready");
    copy_raw_field("similarity_score");

    const std::string slice_path = GetFieldOrDefault(result, "slice_path", "");
    const std::string preferred_remote_session_slices_root = GetFieldOrDefault(result, "preferred_remote_session_slices_root", "");
    if (!slice_path.empty()) {
        const bool root_match =
            StartsWithNormalizedPathText(slice_path, preferred_remote_session_slices_root)
            || StartsWithNormalizedPathText(slice_path, GetFieldOrDefault(result, "preferred_dialog_slices_root", ""))
            || StartsWithNormalizedPathText(slice_path, GetFieldOrDefault(result, "preferred_session_dispatch_root", ""))
            || StartsWithNormalizedPathText(slice_path, GetFieldOrDefault(result, "preferred_log_root", ""))
            || StartsWithNormalizedPathText(slice_path, GetFieldOrDefault(result, "preferred_workspace_root", ""));
        result.fields["storage_root_mismatch"] = root_match ? "false" : "true";
        if (!root_match) {
            result.fields["storage_root_mismatch_reason"] =
                "slice_path is outside preferred workspace/log/data/session roots";
        }
    }
    copy_string_field("source_type");

    const std::string response_body_lower = ToLowerAscii(response.body);
    const bool append_truncated_projection_incomplete =
        IsRemoteAppendWriteMode(normalized_requested_write_mode)
        && (ExtractJsonBool(response.body, "truncated", false)
            || response_body_lower.find("\"finish_reason\":\"length\"") != std::string::npos
            || response_body_lower.find("\"finish_reason\": \"length\"") != std::string::npos
            || response_body_lower.find("\"stop_type\":\"limit\"") != std::string::npos
            || response_body_lower.find("\"stop_type\": \"limit\"") != std::string::npos
            || response_body_lower.find("[output truncated]") != std::string::npos
            || response_body_lower.find("projection_incomplete") != std::string::npos);
    const bool dirty_content_only_brace = !append_truncated_projection_incomplete
        && HasRemoteTurnOnlyBraceProjection(response.body);

    const std::string nested_session_id = ExtractRemoteTurnNestedStringField(response.body, {"session_id", "writeback_session_id", "id"});
    const std::string nested_turn_id = ExtractRemoteTurnNestedStringField(response.body, {"turn_id", "writeback_turn_id", "last_turn_id", "message_id"});
    const std::string nested_write_mode = ExtractRemoteTurnNestedStringField(response.body, {"write_mode"});
    const std::string nested_source_type = ExtractRemoteTurnNestedStringField(response.body, {"source_type"});
    const std::string nested_provider_id = ExtractRemoteTurnNestedStringField(response.body, {"provider_id"});
    const std::string nested_capability_id = ExtractRemoteTurnNestedStringField(response.body, {"capability_id"});
    if (GetFieldOrDefault(result, "session_id", "").empty() && !nested_session_id.empty()) {
        result.fields["session_id"] = nested_session_id;
    }
    if (GetFieldOrDefault(result, "turn_id", "").empty() && !nested_turn_id.empty()) {
        result.fields["turn_id"] = nested_turn_id;
    }
    if (GetFieldOrDefault(result, "write_mode", "").empty() && !nested_write_mode.empty()) {
        result.fields["write_mode"] = nested_write_mode;
    }
    if (GetFieldOrDefault(result, "source_type", "").empty() && !nested_source_type.empty()) {
        result.fields["source_type"] = nested_source_type;
    }
    if (GetFieldOrDefault(result, "provider_id", "").empty() && !nested_provider_id.empty()) {
        result.fields["provider_id"] = nested_provider_id;
    }
    if (GetFieldOrDefault(result, "capability_id", "").empty() && !nested_capability_id.empty()) {
        result.fields["capability_id"] = nested_capability_id;
    }

    if (result.fields.find("vector_ready") != result.fields.end()) {
        result.fields["remote_vector_ready"] = result.fields["vector_ready"];
    }
    if (result.fields.find("vector_skip_reason") != result.fields.end()) {
        result.fields["remote_vector_skip_reason"] = result.fields["vector_skip_reason"];
    }
    std::string embedding_detail;
    std::string effective_embedding_endpoint;
    std::string embedding_endpoint_source;
    const bool embedding_ready = ResolveReachableEndpoint(
        config.embedding_endpoint,
        DeriveEmbeddingFallbackEndpoint(config),
        1000,
        &effective_embedding_endpoint,
        &embedding_detail,
        &embedding_endpoint_source);
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["embedding_endpoint_effective"] = effective_embedding_endpoint;
    result.fields["embedding_endpoint_source"] = embedding_endpoint_source;
    result.fields["embedding_ready"] = embedding_ready ? "true" : "false";
    result.fields["embedding_detail"] = embedding_detail;
    const std::string browser_visible_summary = BuildBrowserVisibleRemoteTurnSummary(response.body);
    result.fields["business_text_ready"] =
        (GetFieldOrDefault(result, "remote_vector_ready", "") == "true" || !browser_visible_summary.empty())
            && !dirty_content_only_brace
        ? "true"
        : "false";
    if (append_truncated_projection_incomplete) {
        result.fields["vector_ready"] = "false";
        result.fields["vector_skip_reason"] = "append_truncated_projection_incomplete";
        result.fields["vectorization_status"] = embedding_ready ? "available" : "blocked";
        result.fields["vectorization_block_reason"] = embedding_ready ? "" : "embedding_ready_false";
        result.fields["ingest_status"] = "blocked";
        result.fields["ingest_state"] = "projection_incomplete";
        result.fields["projection_incomplete"] = "true";
        result.fields["quality_guard"] = "append_truncated_projection_incomplete";
        result.fields["recall_status"] = "not_evaluated";
        result.fields["recall_reason"] = "append_truncated_projection_incomplete";
        result.fields["similarity_score"] = "";
    } else if (dirty_content_only_brace) {
        result.fields["vector_ready"] = "false";
        result.fields["vector_skip_reason"] = "dirty_content_only_brace";
        result.fields["vectorization_status"] = embedding_ready ? "available" : "blocked";
        result.fields["vectorization_block_reason"] = embedding_ready ? "" : "embedding_ready_false";
        result.fields["ingest_status"] = "blocked";
        result.fields["ingest_state"] = "dirty_content_blocked";
        result.fields["projection_incomplete"] = "false";
        result.fields["quality_guard"] = "dirty_content_only_brace";
        result.fields["recall_status"] = "not_evaluated";
        result.fields["recall_reason"] = "dirty_content_only_brace";
        result.fields["similarity_score"] = "";
    } else if (!embedding_ready) {
        result.fields["vector_ready"] = "false";
        result.fields["vector_skip_reason"] = "embedding_service_unavailable";
        result.fields["vectorization_status"] = "blocked";
        result.fields["vectorization_block_reason"] = "embedding_ready_false";
        result.fields["ingest_status"] = "blocked";
        result.fields["ingest_state"] = "vectorization_blocked";
        result.fields["projection_incomplete"] = "false";
        result.fields["quality_guard"] = GetFieldOrDefault(result, "business_text_ready", "") == "true"
            ? "accepted_clean_text_embedding_down"
            : "accepted_audit_only_embedding_down";
        result.fields["recall_status"] = "not_evaluated";
        result.fields["recall_reason"] = "vectorization_blocked";
        result.fields["similarity_score"] = "";
    } else {
        result.fields["vectorization_status"] = "available";
        result.fields["vectorization_block_reason"] = "";
        if (GetFieldOrDefault(result, "vector_ready", "").empty()) {
            result.fields["vector_ready"] = "false";
        }
    }

    const std::string final_write_mode = FirstNonEmpty(
        NormalizeRemoteSessionWriteMode(GetFieldOrDefault(result, "write_mode", "")),
        NormalizeRemoteSessionWriteMode(nested_write_mode),
        normalized_requested_write_mode);
    result.fields["write_mode"] = final_write_mode.empty() ? requested_write_mode : final_write_mode;
    result.fields["session_id"] = FirstNonEmpty(
        GetFieldOrDefault(result, "session_id", ""),
        nested_session_id,
        request_session_id);
    result.fields["turn_id"] = FirstNonEmpty(
        GetFieldOrDefault(result, "turn_id", ""),
        nested_turn_id);
    result.fields["source_type"] = FirstNonEmpty(
        GetFieldOrDefault(result, "source_type", ""),
        nested_source_type,
        request_source_type);
    result.fields["provider_id"] = FirstNonEmpty(
        GetFieldOrDefault(result, "provider_id", ""),
        nested_provider_id,
        request_provider_id);
    result.fields["capability_id"] = FirstNonEmpty(
        GetFieldOrDefault(result, "capability_id", ""),
        nested_capability_id,
        request_capability_id);
    if (NormalizeRemoteTurnVisibleText(GetFieldOrDefault(result, "direct_answer", "")).empty()
        && !browser_visible_summary.empty()) {
        result.fields["direct_answer"] = browser_visible_summary;
    }
    if (GetFieldOrDefault(result, "summary", "").empty()) {
        const std::string direct_answer = GetFieldOrDefault(result, "direct_answer", "");
        result.fields["summary"] = direct_answer.empty()
            ? (response.ok ? "remote session turn ok" : "remote session turn failed")
            : direct_answer;
    }
    if (!browser_visible_summary.empty()) {
        result.fields["business_summary"] = browser_visible_summary;
        result.fields["browser_visible_summary"] = browser_visible_summary;
        result.fields["summary"] = browser_visible_summary;
    }
    if (GetFieldOrDefault(result, "next_action", "").empty()) {
        result.fields["next_action"] = response.ok
            ? "inspect direct_answer or result_ref"
            : "inspect log_path";
    }
    if (GetFieldOrDefault(result, "result_ref", "").empty()) {
        result.fields["result_ref"] = log_path;
    }
    if (GetFieldOrDefault(result, "evidence", "").empty()) {
        result.fields["evidence"] = GetFieldOrDefault(result, "evidence_ref", "");
    }
    return result;
}

CommandResult BuildVentriloquistReplyResult(
    const AgentConfig & config,
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text) {
    CommandResult result;
    result.fields["tool"] = "lan_agent_ventriloquist_reply";
    result.fields["task_id"] = task_id;
    result.fields["session_id"] = session_id;
    result.fields["speaker_mode"] = speaker_mode;
    result.fields["reasoning_level"] = reasoning_level;
    result.fields["prompt_purpose"] = prompt_purpose;
    result.fields["context_refs"] = context_refs;
    result.fields["response_mode"] = response_mode;
    result.fields["source_type"] = "codex";
    result.fields["provider_id"] = "codex_lan_agent_ventriloquist";
    result.fields["capability_id"] = "ventriloquist_reply";
    result.fields["slice_refs"] = "[]";
    result.fields["storage_refs"] = "[\"execution_binding.jsonl\"]";
    result.fields["request_content_policy"] = "content_first_v1";
    result.fields["metadata_delivery"] = "metadata_tail";

    if (task_id.empty()) {
        result.ok = false;
        result.exit_code = 81;
        result.fields["error"] = "task_id is required";
        result.fields["format_ok"] = "false";
        result.fields["format_mismatch"] = "true";
        result.fields["fallback_reason"] = "ventriloquist_invalid_input";
        result.fields["next_action"] = "provide task_id";
        return result;
    }
    const std::string normalized_reasoning = ToLowerAscii(reasoning_level);
    if (normalized_reasoning != "low"
        && normalized_reasoning != "medium"
        && normalized_reasoning != "high") {
        result.ok = false;
        result.exit_code = 82;
        result.fields["error"] = "reasoning_level must be low, medium, or high";
        result.fields["format_ok"] = "false";
        result.fields["format_mismatch"] = "true";
        result.fields["fallback_reason"] = "ventriloquist_invalid_input";
        result.fields["next_action"] = "provide a valid reasoning_level";
        return result;
    }
    if (speaker_mode.empty() || prompt_purpose.empty() || response_mode.empty()) {
        result.ok = false;
        result.exit_code = 83;
        result.fields["error"] = "speaker_mode, prompt_purpose, and response_mode are required";
        result.fields["format_ok"] = "false";
        result.fields["format_mismatch"] = "true";
        result.fields["fallback_reason"] = "ventriloquist_invalid_input";
        result.fields["next_action"] = "provide required control fields";
        return result;
    }
    if (context_refs.empty()) {
        result.ok = false;
        result.exit_code = 84;
        result.fields["error"] = "context_refs is required for controlled ventriloquist reply";
        result.fields["format_ok"] = "false";
        result.fields["format_mismatch"] = "true";
        result.fields["fallback_reason"] = "ventriloquist_missing_context_refs";
        result.fields["next_action"] = "provide one or more context_refs";
        return result;
    }

    const bool prefer_remote_session = !task_id.empty();
    CommandResult chat_result;
    if (prefer_remote_session) {
        chat_result = session_id.empty()
            ? RunRemoteSessionNewTurn(
                config,
                task_id,
                session_id,
                speaker_mode,
                normalized_reasoning,
                prompt_purpose,
                context_refs,
                response_mode,
                prompt_text)
            : RunRemoteSessionAppendTurn(
                config,
                task_id,
                session_id,
                speaker_mode,
                normalized_reasoning,
                prompt_purpose,
                context_refs,
                response_mode,
                prompt_text);
        chat_result.fields["dispatch_mode"] = session_id.empty()
            ? "remote_session_new_turn"
            : "remote_session_append_turn";
        if (!chat_result.ok) {
            chat_result.fields["dispatch_fallback"] = "plain_local_chat";
            chat_result.fields["dispatch_fallback_reason"] = "remote_session_turn_failed";
            chat_result = RunLocalChat(
                config,
                session_id.empty() ? "workspace" : session_id,
                BuildVentriloquistQuestion(
                    task_id,
                    session_id,
                    speaker_mode,
                    normalized_reasoning,
                    prompt_purpose,
                    context_refs,
                    response_mode,
                    prompt_text),
                "ventriloquist_reply");
            chat_result.fields["dispatch_mode"] = "plain_local_chat";
            chat_result.fields["dispatch_fallback"] = "true";
            chat_result.fields["dispatch_fallback_reason"] = "remote_session_turn_failed";
        }
    } else {
        chat_result = RunLocalChat(
            config,
            session_id.empty() ? "workspace" : session_id,
            BuildVentriloquistQuestion(
                task_id,
                session_id,
                speaker_mode,
                normalized_reasoning,
                prompt_purpose,
                context_refs,
                response_mode,
                prompt_text),
            "ventriloquist_reply");
        chat_result.fields["dispatch_mode"] = "plain_local_chat";
    }

    result = chat_result;
    result.fields["tool"] = "lan_agent_ventriloquist_reply";
    result.fields["task_id"] = task_id;
    result.fields["session_id"] = session_id;
    result.fields["speaker_mode"] = speaker_mode;
    result.fields["reasoning_level"] = normalized_reasoning;
    result.fields["prompt_purpose"] = prompt_purpose;
    result.fields["context_refs"] = context_refs;
    result.fields["response_mode"] = response_mode;
    result.fields["source_type"] = "codex";
    result.fields["provider_id"] = GetFieldOrDefault(chat_result, "provider_id", "codex_lan_agent_ventriloquist");
    result.fields["capability_id"] = GetFieldOrDefault(chat_result, "capability_id", "ventriloquist_reply");
    result.fields["slice_refs"] = GetFieldOrDefault(chat_result, "slice_refs", "[]");
    result.fields["storage_refs"] = GetFieldOrDefault(chat_result, "storage_refs", "[\"execution_binding.jsonl\"]");
    result.fields["dispatch_mode"] = GetFieldOrDefault(chat_result, "dispatch_mode", "");
    result.fields["dispatch_fallback"] = GetFieldOrDefault(chat_result, "dispatch_fallback", "false");
    result.fields["dispatch_fallback_reason"] = GetFieldOrDefault(chat_result, "dispatch_fallback_reason", "");
    result.fields["request_content_policy"] = GetFieldOrDefault(chat_result, "request_content_policy", "content_first_v1");
    result.fields["metadata_delivery"] = GetFieldOrDefault(chat_result, "metadata_delivery", "metadata_tail");

    const std::string output_text = ExtractOutputTextFallback(chat_result);
    std::string direct_answer = GetFieldOrDefault(chat_result, "direct_answer", "");
    std::string evidence = GetFieldOrDefault(chat_result, "evidence", "");
    std::string next_action = GetFieldOrDefault(chat_result, "next_action", "");
    std::string confidence = NormalizeVentriloquistConfidence(GetFieldOrDefault(chat_result, "confidence", ""));

    if (direct_answer.empty()) {
        direct_answer = ExtractDelimitedBlock(output_text, "BEGIN_DIRECT_ANSWER", "END_DIRECT_ANSWER");
    }
    if (evidence.empty()) {
        evidence = ExtractDelimitedBlock(output_text, "BEGIN_EVIDENCE", "END_EVIDENCE");
    }
    if (next_action.empty()) {
        next_action = ExtractDelimitedBlock(output_text, "BEGIN_NEXT_ACTION", "END_NEXT_ACTION");
    }
    if (confidence.empty()) {
        confidence = NormalizeVentriloquistConfidence(
            ExtractDelimitedBlock(output_text, "BEGIN_CONFIDENCE", "END_CONFIDENCE"));
    }

    if (direct_answer.empty()) {
        direct_answer = GetFieldOrDefault(chat_result, "summary", "");
    }
    if (direct_answer.empty()) {
        direct_answer = output_text.substr(0, std::min<std::size_t>(output_text.size(), 240));
    }
    if (evidence.empty()) {
        evidence = GetFieldOrDefault(chat_result, "source_refs", "");
        if (!GetFieldOrDefault(chat_result, "evidence_lines", "").empty()) {
            if (!evidence.empty()) {
                evidence += " | ";
            }
            evidence += GetFieldOrDefault(chat_result, "evidence_lines", "").substr(
                0,
                std::min<std::size_t>(GetFieldOrDefault(chat_result, "evidence_lines", "").size(), 240));
        }
        if (evidence.empty()) {
            evidence = context_refs;
        }
    }
    if (next_action.empty()) {
        next_action = GetFieldOrDefault(chat_result, "next_action", "");
    }
    if (confidence.empty()) {
        if (!chat_result.ok) {
            confidence = "blocked";
        } else if (ToLowerAscii(GetFieldOrDefault(chat_result, "insufficient_context", "")) == "true") {
            confidence = "unclear";
        } else {
            confidence = "likely";
        }
    }

    const bool format_ok =
        !direct_answer.empty()
        && !evidence.empty()
        && !next_action.empty()
        && !confidence.empty();
    result.fields["direct_answer"] = direct_answer;
    result.fields["evidence"] = evidence;
    result.fields["next_action"] = next_action;
    result.fields["confidence"] = confidence;
    result.fields["format_ok"] = format_ok ? "true" : "false";
    result.fields["format_mismatch"] = format_ok ? "false" : "true";
    result.fields["result"] = format_ok ? "ventriloquist_reply_ready" : "ventriloquist_reply_format_mismatch";
    result.fields["summary"] = direct_answer.empty() ? "ventriloquist reply unavailable" : direct_answer;
    result.fields["evidence_ref"] = GetFieldOrDefault(chat_result, "evidence_ref", context_refs);
    result.fields["result_ref"] = GetFieldOrDefault(chat_result, "result_ref", GetFieldOrDefault(chat_result, "log_path", ""));
    if (!format_ok) {
        result.ok = false;
        result.exit_code = chat_result.ok ? 85 : chat_result.exit_code;
        result.fields["fallback_reason"] = "ventriloquist_format_mismatch";
        result.fields["next_action"] = "fallback to rag.query or inspect log_path";
    }

    const std::string local_ai_thread_message_id = GetFieldOrDefault(
        chat_result,
        "turn_id",
        "ventriloquist-" + SanitizeDispatchToken(task_id, "task") + "-" + BuildRequestTimestampToken());
    result.fields["session_id"] = GetFieldOrDefault(chat_result, "session_id", session_id);
    result.fields["turn_id"] = local_ai_thread_message_id;
    result.fields["write_mode"] = FirstNonEmpty(
        NormalizeRemoteSessionWriteMode(GetFieldOrDefault(chat_result, "write_mode", "")),
        session_id.empty() ? "new" : "append");
    result.fields["codex_request_id"] = GetFieldOrDefault(chat_result, "codex_request_id", "");
    result.fields["agent_dispatch_id"] = GetFieldOrDefault(chat_result, "agent_dispatch_id", "");
    result.fields["timings"] = GetFieldOrDefault(chat_result, "timings", "");
    const std::string binding_json = BuildExecutionBindingJson(
        result.fields["session_id"],
        local_ai_thread_message_id,
        task_id,
        result.fields["evidence_ref"],
        result.fields["result_ref"],
        "ventriloquist_reply",
        prompt_purpose,
        "lan_agent_ventriloquist_reply",
        result.ok ? "ready" : "fallback");
    CommandResult binding_result = WriteExecutionBindingResult(config, binding_json);
    result.fields["binding_recorded"] = binding_result.ok ? "true" : "false";
    result.fields["binding_log_path"] = GetFieldOrDefault(binding_result, "log_path", "");
    result.fields["binding_result"] = GetFieldOrDefault(binding_result, "result", "");
    result.fields["execution_binding"] = binding_json;
    result.fields["local_ai_thread_message_id"] = local_ai_thread_message_id;
    return result;
}
