#pragma once

std::string StableContentChecksum(const std::string & content);

std::string BuildDialogSlicesDir(const AgentConfig & config);

std::string SanitizeDialogSliceSessionId(const std::string & session_id);

std::filesystem::path BuildDialogSlicePath(
    const AgentConfig & config,
    const std::string & session_id);

std::string FirstNonEmptyText(
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

std::string NormalizeSliceTextField(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed == "{" || trimmed == "[" || trimmed == "}" || trimmed == "]") {
        return std::string();
    }
    return trimmed;
}

bool IsDirtySliceText(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()
        || trimmed == "{"
        || trimmed == "["
        || trimmed == "}"
        || trimmed == "]") {
        return true;
    }
    return trimmed.front() == '{' || trimmed.front() == '[';
}

bool IsEmptySliceBusinessText(const std::string & value) {
    const std::string trimmed = Trim(value);
    return trimmed.empty()
        || trimmed == "{"
        || trimmed == "["
        || trimmed == "}"
        || trimmed == "]";
}

bool IsOnlyBraceSliceText(const std::string & value) {
    const std::string trimmed = Trim(value);
    return trimmed == "{"
        || trimmed == "["
        || trimmed == "}"
        || trimmed == "]";
}

bool LooksLikeJsonFragmentText(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }
    return trimmed.front() == '{' || trimmed.front() == '[';
}

bool LooksLikeControlTemplateText(const std::string & value) {
    const std::string lowered = ToLowerAscii(Trim(value));
    if (lowered.empty()) {
        return false;
    }
    return lowered.find("you are a controlled") != std::string::npos
        || lowered.find("controlled remote-session assistant") != std::string::npos
        || lowered.find("session metadata:") != std::string::npos
        || lowered.find("thinking process") != std::string::npos
        || lowered.find("begin_direct_answer") != std::string::npos
        || lowered.find("always produce a usable final answer") != std::string::npos;
}

bool IsAppendWriteMode(const std::string & value) {
    const std::string lowered = ToLowerAscii(Trim(value));
    return lowered == "append" || lowered == "append_turn";
}

bool LooksLikeAppendTruncatedProjection(
    const std::string & write_mode,
    const std::string & raw_user_text,
    const std::string & raw_assistant_text,
    const std::string & raw_summary) {
    if (!IsAppendWriteMode(write_mode)) {
        return false;
    }
    const std::string combined = ToLowerAscii(raw_user_text + "\n" + raw_assistant_text + "\n" + raw_summary);
    return combined.find("\"truncated\":true") != std::string::npos
        || combined.find("\"truncated\": true") != std::string::npos
        || combined.find("\"finish_reason\":\"length\"") != std::string::npos
        || combined.find("\"finish_reason\": \"length\"") != std::string::npos
        || combined.find("\"stop_type\":\"limit\"") != std::string::npos
        || combined.find("\"stop_type\": \"limit\"") != std::string::npos
        || combined.find("[output truncated]") != std::string::npos
        || combined.find("projection_incomplete") != std::string::npos;
}

std::string BuildVectorSkipReason(
    const std::string & business_user_text,
    const std::string & business_assistant_text,
    const std::string & business_summary) {
    if (IsOnlyBraceSliceText(business_assistant_text)) {
        return "dirty_content_only_brace";
    }
    if (IsOnlyBraceSliceText(business_summary)) {
        return "dirty_content_only_brace";
    }
    if (IsOnlyBraceSliceText(business_user_text)) {
        return "dirty_content_only_brace";
    }
    if (IsEmptySliceBusinessText(business_assistant_text)) {
        return "empty_business_assistant_text";
    }
    if (IsEmptySliceBusinessText(business_summary)) {
        return "empty_business_summary";
    }
    if (LooksLikeControlTemplateText(business_user_text)) {
        return "control_wrapped_user_text";
    }
    if (IsEmptySliceBusinessText(business_user_text)) {
        return "empty_business_user_text";
    }
    if (LooksLikeJsonFragmentText(business_user_text)
        || LooksLikeJsonFragmentText(business_assistant_text)
        || LooksLikeJsonFragmentText(business_summary)) {
        return "json_fragment_not_vectorized";
    }
    return std::string();
}

bool IsVectorReadyBusinessText(
    const std::string & business_user_text,
    const std::string & business_assistant_text,
    const std::string & business_summary) {
    return !IsEmptySliceBusinessText(business_user_text)
        && !IsEmptySliceBusinessText(business_assistant_text)
        && !IsEmptySliceBusinessText(business_summary)
        && !LooksLikeControlTemplateText(business_user_text)
        && !LooksLikeControlTemplateText(business_assistant_text)
        && !LooksLikeJsonFragmentText(business_user_text)
        && !LooksLikeJsonFragmentText(business_assistant_text)
        && !LooksLikeJsonFragmentText(business_summary);
}

bool CheckEmbeddingServiceReady(
    const AgentConfig & config,
    std::string * detail) {
    std::string ignored_endpoint;
    std::string ignored_source;
    return ResolveReachableEndpoint(
        config.embedding_endpoint,
        DeriveEmbeddingFallbackEndpoint(config),
        1000,
        &ignored_endpoint,
        detail,
        &ignored_source);
}

std::string ExtractUsableJsonString(
    const std::string & body,
    const std::string & key);

std::string PreferDialogSliceText(
    const std::string & primary_text,
    const std::string & secondary_text = std::string(),
    const std::string & tertiary_text = std::string()) {
    const std::string normalized_primary = NormalizeSliceTextField(primary_text);
    if (!normalized_primary.empty() && normalized_primary.front() != '{' && normalized_primary.front() != '[') {
        return normalized_primary;
    }
    const std::string direct_answer = ExtractUsableJsonString(primary_text, "direct_answer");
    if (!direct_answer.empty()) {
        return direct_answer;
    }
    const std::string slice_summary = ExtractUsableJsonString(primary_text, "slice_summary");
    if (!slice_summary.empty()) {
        return slice_summary;
    }
    const std::string summary = ExtractUsableJsonString(primary_text, "summary");
    if (!summary.empty()) {
        return summary;
    }
    const std::string normalized_secondary = NormalizeSliceTextField(secondary_text);
    if (!normalized_secondary.empty()) {
        return normalized_secondary;
    }
    const std::string normalized_tertiary = NormalizeSliceTextField(tertiary_text);
    if (!normalized_tertiary.empty()) {
        return normalized_tertiary;
    }
    if (!normalized_primary.empty() && (normalized_primary.front() == '{' || normalized_primary.front() == '[')) {
        return std::string();
    }
    return normalized_primary;
}

std::string ExtractUsableJsonString(
    const std::string & body,
    const std::string & key) {
    return NormalizeSliceTextField(ExtractJsonString(body, key));
}

std::string FindDialogSliceIdByDedupHash(
    const std::string & content,
    const std::string & dedup_hash) {
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (ExtractJsonString(line, "dedup_hash") == dedup_hash) {
            const std::string slice_id = ExtractJsonString(line, "slice_id");
            if (!slice_id.empty()) {
                return slice_id;
            }
        }
    }
    return std::string();
}

std::filesystem::path BuildDialogSliceCanonicalIndexPath(const AgentConfig & config) {
    return std::filesystem::path(BuildDialogSlicesDir(config)) / "_canonical_index.jsonl";
}

int CountJsonlEntries(const std::string & content) {
    int count = 0;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (!Trim(line).empty()) {
            ++count;
        }
    }
    return count;
}

std::string FindCanonicalDialogSliceIdByDedupHash(
    const std::string & content,
    const std::string & dedup_hash) {
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (ExtractJsonString(line, "dedup_hash") == dedup_hash) {
            const std::string canonical_slice_id = ExtractJsonString(line, "canonical_slice_id");
            if (!canonical_slice_id.empty()) {
                return canonical_slice_id;
            }
            const std::string slice_id = ExtractJsonString(line, "slice_id");
            if (!slice_id.empty()) {
                return slice_id;
            }
        }
    }
    return std::string();
}

CommandResult OptFileReadResult(
    const AgentConfig & config,
    const std::string & target_name,
    int max_bytes) {
    CommandResult result = BuildOptFileBaseResult(config, target_name, "read", false);
    const std::filesystem::path target_path(result.fields["target_path"]);
    if (!std::filesystem::exists(target_path)) {
        result.ok = false;
        result.exit_code = 64;
        result.fields["error"] = "optfile target does not exist";
        result.fields["content"] = "";
        result.fields["bytes"] = "0";
        result.fields["checksum"] = StableContentChecksum("");
        result.fields["next_action"] = "call lan_agent_optfile_apply_write to create the runtime optfile";
        return result;
    }
    std::string content;
    std::string read_error;
    if (!ReadWholeFile(target_path, &content, &read_error)) {
        result.ok = false;
        result.exit_code = 65;
        result.fields["error"] = read_error;
        return result;
    }
    const std::size_t bounded_max = max_bytes > 0
        ? static_cast<std::size_t>(max_bytes)
        : static_cast<std::size_t>(65536);
    result.fields["bytes"] = std::to_string(content.size());
    result.fields["checksum"] = StableContentChecksum(content);
    result.fields["truncated"] = content.size() > bounded_max ? "true" : "false";
    result.fields["content"] = content.substr(0, std::min<std::size_t>(content.size(), bounded_max));
    result.fields["result"] = "read";
    return result;
}

CommandResult OptFileWritePreviewResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & data,
    bool append) {
    CommandResult result = BuildOptFileBaseResult(config, target_name, "write_preview", append);
    std::string old_content;
    std::string read_error;
    const std::filesystem::path target_path(result.fields["target_path"]);
    if (std::filesystem::exists(target_path)) {
        ReadWholeFile(target_path, &old_content, &read_error);
    }
    const std::string new_content = append ? (old_content + data) : data;
    result.fields["would_write"] = "false";
    result.fields["old_bytes"] = std::to_string(old_content.size());
    result.fields["new_bytes"] = std::to_string(new_content.size());
    result.fields["data_bytes"] = std::to_string(data.size());
    result.fields["old_checksum"] = StableContentChecksum(old_content);
    result.fields["new_checksum"] = StableContentChecksum(new_content);
    result.fields["changed"] = old_content == new_content ? "false" : "true";
    result.fields["result"] = "preview";
    result.fields["next_action"] = "call lan_agent_optfile_apply_write only if this preview is intended";
    return result;
}

CommandResult OptFileApplyWriteResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & data,
    bool append) {
    CommandResult result = OptFileWritePreviewResult(config, target_name, data, append);
    const std::filesystem::path runtime_dir(result.fields["runtime_dir"]);
    const std::filesystem::path target_path(result.fields["target_path"]);
    std::error_code ec;
    std::filesystem::create_directories(runtime_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 66;
        result.fields["error"] = "failed to create optfile runtime dir";
        return result;
    }

    const std::string resource_key = "optfile:" + target_path.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "optfile target is busy";
        return result;
    }

    if (append) {
        std::ofstream output(target_path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 67;
            result.fields["error"] = "failed to open optfile target for append";
            return result;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
    } else {
        const std::filesystem::path temp_path =
            target_path.string() + ".optfile." + TimeStampForFileName() + ".tmp";
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 67;
            result.fields["error"] = "failed to open optfile temp file";
            return result;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        std::filesystem::rename(temp_path, target_path, ec);
        if (ec) {
            std::filesystem::remove(target_path, ec);
            ec.clear();
            std::filesystem::rename(temp_path, target_path, ec);
        }
        if (ec) {
            std::filesystem::remove(temp_path, ec);
            result.ok = false;
            result.exit_code = 68;
            result.fields["error"] = "failed to replace optfile target";
            return result;
        }
    }

    std::string final_content;
    std::string read_error;
    ReadWholeFile(target_path, &final_content, &read_error);
    const std::string log_path = BuildLogPath(config, "optfile_apply_write");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "operation=apply_write\n";
    log << "target_path=" << target_path.string() << "\n";
    log << "append=" << (append ? "true" : "false") << "\n";
    log << "data_bytes=" << data.size() << "\n";
    log << "final_bytes=" << final_content.size() << "\n";
    log << "final_checksum=" << StableContentChecksum(final_content) << "\n";
    log.close();

    result.fields["would_write"] = "true";
    result.fields["log_path"] = log_path;
    result.fields["final_bytes"] = std::to_string(final_content.size());
    result.fields["final_checksum"] = StableContentChecksum(final_content);
    result.fields["result"] = "applied";
    result.fields["next_action"] = "call lan_agent_optfile_read to verify content";
    return result;
}

CommandResult RecordDialogSliceResult(
    const AgentConfig & config,
    const std::string & session_id,
    const std::string & turn_id,
    const std::string & user_text,
    const std::string & assistant_text,
    const std::string & business_summary_override,
    const std::string & tags,
    const std::string & task_id,
    const std::string & provider_id,
    const std::string & capability_id,
    const std::string & source_type,
    const std::string & write_mode,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & confidence,
    const std::string & result_ref,
    const std::string & evidence_ref) {
    CommandResult result;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "record_dialog_slice";
    result.fields["provider_id"] = provider_id.empty() ? "codex_lan_agent_dialog_slice" : provider_id;
    result.fields["capability_id"] = capability_id.empty() ? "dialog_slice_store" : capability_id;
    result.fields["session_id"] = SanitizeDialogSliceSessionId(session_id);
    result.fields["turn_id"] = turn_id;
    result.fields["slice_version"] = "rag_memory_slice_v1";
    result.fields["analysis_root"] = BuildDialogSlicesDir(config);
    result.fields["trace_log_path"] = BuildRemoteControlEventsPath(config);
    const std::string normalized_task_id = task_id.empty() ? "unbound" : task_id;
    const std::string normalized_reasoning_level = reasoning_level.empty() ? "unspecified" : reasoning_level;
    const std::string normalized_primary_intent = primary_intent.empty() ? "unspecified" : primary_intent;
    const std::string normalized_confidence = confidence.empty() ? "unclear" : confidence;
    const std::string normalized_result_ref = result_ref.empty() ? "unbound" : result_ref;
    const std::string normalized_evidence_ref = evidence_ref.empty() ? "unbound" : evidence_ref;
    const std::string safe_user_text = NormalizeSliceTextField(user_text);
    const std::string safe_assistant_text = PreferDialogSliceText(assistant_text);
    const std::string stored_user_text = Trim(user_text);
    const std::string stored_assistant_text = Trim(assistant_text);
    const bool user_text_control_wrapped =
        LooksLikeControlTemplateText(safe_user_text) || LooksLikeControlTemplateText(stored_user_text);
    const std::string business_user_text = user_text_control_wrapped ? std::string() : safe_user_text;
    const std::string business_assistant_text = safe_assistant_text;

    result.fields["task_id"] = normalized_task_id;
    result.fields["source_type"] = source_type.empty() ? "codex" : source_type;
    result.fields["write_mode"] = write_mode.empty() ? "append" : write_mode;
    result.fields["reasoning_level"] = normalized_reasoning_level;
    result.fields["primary_intent"] = normalized_primary_intent;
    result.fields["confidence"] = normalized_confidence;
    result.fields["result_ref"] = normalized_result_ref;
    result.fields["evidence_ref"] = normalized_evidence_ref;

    if (session_id.empty() || turn_id.empty() || (stored_user_text.empty() && stored_assistant_text.empty())) {
        result.ok = false;
        result.exit_code = 69;
        result.fields["error"] = "session_id, turn_id, and at least one audit text field are required";
        result.fields["next_action"] = "provide complete dialog turn fields";
        return result;
    }

    const std::string normalized_session_id = SanitizeDialogSliceSessionId(session_id);
    const std::string normalized_source_type = source_type.empty() ? "codex" : source_type;
    const std::string normalized_write_mode = write_mode.empty() ? "append" : write_mode;
    const std::string normalized_provider_id = provider_id.empty() ? "codex_lan_agent_dialog_slice" : provider_id;
    const std::string normalized_capability_id = capability_id.empty() ? "dialog_slice_store" : capability_id;
    const std::string strategy_family = normalized_primary_intent;
    const std::string normalized_business_summary = PreferDialogSliceText(business_summary_override);
    const std::string slice_summary = FirstNonEmptyText(normalized_business_summary, business_assistant_text);
    const std::string error_signature = normalized_confidence == "unclear" ? slice_summary : "";
    const std::string solution_summary =
        (normalized_confidence == "confirmed" || normalized_confidence == "likely") ? slice_summary : "";
    const bool business_text_ready = IsVectorReadyBusinessText(
        business_user_text,
        business_assistant_text,
        slice_summary);
    const bool append_truncated_projection_incomplete = LooksLikeAppendTruncatedProjection(
        normalized_write_mode,
        stored_user_text,
        stored_assistant_text,
        business_summary_override);
    const std::string content_vector_skip_reason = BuildVectorSkipReason(
        user_text_control_wrapped ? stored_user_text : business_user_text,
        business_assistant_text,
        slice_summary);
    std::string embedding_service_detail;
    const bool embedding_service_ready = CheckEmbeddingServiceReady(config, &embedding_service_detail);
    std::string vector_skip_reason;
    std::string quality_guard;
    if (append_truncated_projection_incomplete) {
        vector_skip_reason = "append_truncated_projection_incomplete";
        quality_guard = "append_truncated_projection_incomplete";
    } else if (!business_text_ready) {
        vector_skip_reason = content_vector_skip_reason;
        quality_guard = vector_skip_reason == "dirty_content_only_brace"
            ? "dirty_content_only_brace"
            : "accepted_dirty_text_without_vectorization";
    } else if (!embedding_service_ready) {
        vector_skip_reason = "embedding_service_unavailable";
        quality_guard = "accepted_clean_text_embedding_down";
    } else {
        quality_guard = "business_text_ready";
    }
    const bool vector_ready = business_text_ready && embedding_service_ready;
    const std::string dedup_user_text = business_user_text.empty() ? stored_user_text : business_user_text;
    const std::string dedup_summary = slice_summary.empty() ? stored_assistant_text : slice_summary;
    const std::string dedup_hash = StableContentChecksum(
        dedup_user_text + "\n" + dedup_summary + "\n" + strategy_family + "\n" + normalized_source_type + "\n" + normalized_write_mode);
    const std::string slice_id = "slice:" + normalized_session_id + ":" + turn_id;
    const std::string audit_ref = "session:" + normalized_session_id + "/turn:" + turn_id;
    const std::string vector_payload = vector_ready ? (business_user_text + "\n" + slice_summary) : std::string();
    const std::string summary_text = slice_summary.substr(0, std::min<std::size_t>(slice_summary.size(), 240));

    const std::filesystem::path slice_dir(BuildDialogSlicesDir(config));
    const std::filesystem::path slice_path = BuildDialogSlicePath(config, session_id);
    const std::filesystem::path canonical_index_path = BuildDialogSliceCanonicalIndexPath(config);
    result.fields["expected_session_slice_path"] = slice_path.string();
    std::error_code ec;
    std::filesystem::create_directories(slice_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 70;
        result.fields["error"] = "failed to create dialog_slices dir";
        return result;
    }

    const std::string resource_key = "dialog_slice_canonical:" + canonical_index_path.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "dialog slice target is busy";
        result.fields["resource_key"] = resource_key;
        return result;
    }

    std::string existing_content;
    std::string existing_read_error;
    ReadWholeFile(canonical_index_path, &existing_content, &existing_read_error);
    const std::string existing_slice_id = FindCanonicalDialogSliceIdByDedupHash(existing_content, dedup_hash);
    const int index_entry_count_before = CountJsonlEntries(existing_content);
    const bool is_duplicate = !existing_slice_id.empty();
    const std::string canonical_slice_id = is_duplicate ? existing_slice_id : slice_id;
    const std::string canonical_status = is_duplicate ? "duplicate" : "canonical";
    const std::string dedup_status = canonical_status;
    const std::string dedup_reason = is_duplicate ? "duplicate_of_canonical" : "new_canonical";
    const std::string dup_of = is_duplicate ? canonical_slice_id : "";
    const std::string slice_refs =
        "[\"" + codex_lan_agent::JsonEscape(canonical_slice_id) + "\"]";
    const std::string storage_refs =
        "[\"" + codex_lan_agent::JsonEscape(slice_path.string()) + "\",\"" + codex_lan_agent::JsonEscape(canonical_index_path.string()) + "\"]";
    std::ostringstream clips_fact_input;
    clips_fact_input
        << "{"
        << "\"task_id\":\"" << codex_lan_agent::JsonEscape(normalized_task_id) << "\","
        << "\"session_id\":\"" << codex_lan_agent::JsonEscape(normalized_session_id) << "\","
        << "\"turn_id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
        << "\"provider_id\":\"" << codex_lan_agent::JsonEscape(normalized_provider_id) << "\","
        << "\"capability_id\":\"" << codex_lan_agent::JsonEscape(normalized_capability_id) << "\","
        << "\"business_user_text\":\"" << codex_lan_agent::JsonEscape(business_user_text) << "\","
        << "\"business_assistant_text\":\"" << codex_lan_agent::JsonEscape(business_assistant_text) << "\","
        << "\"business_summary\":\"" << codex_lan_agent::JsonEscape(summary_text) << "\","
        << "\"dedup_hash\":\"" << codex_lan_agent::JsonEscape(dedup_hash) << "\","
        << "\"canonical_slice_id\":\"" << codex_lan_agent::JsonEscape(is_duplicate ? canonical_slice_id : std::string()) << "\","
        << "\"dedup_status\":\"" << codex_lan_agent::JsonEscape(canonical_status) << "\","
        << "\"source_type\":\"" << codex_lan_agent::JsonEscape(normalized_source_type) << "\""
        << "}";
    const JsonRequestView clips_slice_view(clips_fact_input.str());
    const ClipsDecision clips_slice_decision = EvaluateClipsDecision(
        config,
        "slice_ingest_guard",
        "lan_agent_record_dialog_slice",
        &clips_slice_view,
        nullptr);

    if (is_duplicate) {
        std::ostringstream duplicate_line;
        duplicate_line << "{"
            << "\"timestamp\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\","
            << "\"slice_id\":\"" << codex_lan_agent::JsonEscape(slice_id) << "\","
            << "\"slice_version\":\"rag_memory_slice_v1\","
            << "\"canonical_slice_id\":\"" << codex_lan_agent::JsonEscape(canonical_slice_id) << "\","
            << "\"dup_of\":\"" << codex_lan_agent::JsonEscape(canonical_slice_id) << "\","
            << "\"canonical_status\":\"" << codex_lan_agent::JsonEscape(canonical_status) << "\","
            << "\"dedup_status\":\"" << codex_lan_agent::JsonEscape(dedup_status) << "\","
            << "\"dedup_reason\":\"" << codex_lan_agent::JsonEscape(dedup_reason) << "\","
            << "\"slice_type\":\"dialog_slice_dup_pointer\","
            << "\"task_id\":\"" << codex_lan_agent::JsonEscape(normalized_task_id) << "\","
            << "\"session_id\":\"" << codex_lan_agent::JsonEscape(normalized_session_id) << "\","
            << "\"turn_id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
            << "\"provider_id\":\"" << codex_lan_agent::JsonEscape(normalized_provider_id) << "\","
            << "\"capability_id\":\"" << codex_lan_agent::JsonEscape(normalized_capability_id) << "\","
            << "\"user_text\":\"" << codex_lan_agent::JsonEscape(stored_user_text) << "\","
            << "\"assistant_text\":\"" << codex_lan_agent::JsonEscape(stored_assistant_text) << "\","
            << "\"raw_user_text\":\"" << codex_lan_agent::JsonEscape(stored_user_text) << "\","
            << "\"raw_assistant_text\":\"" << codex_lan_agent::JsonEscape(stored_assistant_text) << "\","
            << "\"business_user_text\":\"" << codex_lan_agent::JsonEscape(business_user_text) << "\","
            << "\"business_assistant_text\":\"" << codex_lan_agent::JsonEscape(business_assistant_text) << "\","
            << "\"business_summary\":\"" << codex_lan_agent::JsonEscape(summary_text) << "\","
            << "\"slice_summary\":\"" << codex_lan_agent::JsonEscape(summary_text) << "\","
            << "\"reasoning_level\":\"" << codex_lan_agent::JsonEscape(normalized_reasoning_level) << "\","
            << "\"primary_intent\":\"" << codex_lan_agent::JsonEscape(normalized_primary_intent) << "\","
            << "\"confidence\":\"" << codex_lan_agent::JsonEscape(normalized_confidence) << "\","
            << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(normalized_result_ref) << "\","
            << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(normalized_evidence_ref) << "\","
            << "\"audit_ref\":\"" << codex_lan_agent::JsonEscape(audit_ref) << "\","
            << "\"source_type\":\"" << codex_lan_agent::JsonEscape(normalized_source_type) << "\","
            << "\"write_mode\":\"" << codex_lan_agent::JsonEscape(normalized_write_mode) << "\","
            << "\"business_text_ready\":\"" << (business_text_ready ? "true" : "false") << "\","
            << "\"projection_incomplete\":\"" << (append_truncated_projection_incomplete ? "true" : "false") << "\","
            << "\"embedding_endpoint\":\"" << codex_lan_agent::JsonEscape(config.embedding_endpoint) << "\","
            << "\"embedding_service_ready\":\"" << (embedding_service_ready ? "true" : "false") << "\","
            << "\"embedding_service_detail\":\"" << codex_lan_agent::JsonEscape(embedding_service_detail) << "\","
            << "\"vector_ready\":\"false\","
            << "\"vector_skip_reason\":\"" << codex_lan_agent::JsonEscape(vector_skip_reason.empty() ? "duplicate_of_canonical" : vector_skip_reason) << "\","
            << "\"quality_guard\":\"" << codex_lan_agent::JsonEscape(
                append_truncated_projection_incomplete
                    ? "append_truncated_projection_incomplete"
                    : (business_text_ready
                        ? (embedding_service_ready ? "accepted_duplicate_without_vectorization" : "accepted_clean_text_embedding_down")
                        : (vector_skip_reason == "dirty_content_only_brace"
                            ? "dirty_content_only_brace"
                            : "accepted_dirty_text_without_vectorization"))) << "\","
            << "\"dedup_hash\":\"" << codex_lan_agent::JsonEscape(dedup_hash) << "\","
            << "\"slice_refs\":" << slice_refs << ","
            << "\"storage_refs\":" << storage_refs << ","
            << "\"dup_pointer\":\"true\""
            << "}\n";
        const std::string duplicate_line_text = duplicate_line.str();
        std::ofstream duplicate_output(slice_path, std::ios::binary | std::ios::app);
        if (!duplicate_output.is_open()) {
            result.ok = false;
            result.exit_code = 71;
            result.fields["error"] = "failed to open dialog slice file for duplicate pointer write";
            result.fields["next_action"] = "check expected_session_slice_path and dialog_slices permissions";
            return result;
        }
        duplicate_output.write(
            duplicate_line_text.data(),
            static_cast<std::streamsize>(duplicate_line_text.size()));
        duplicate_output.close();
        const std::string log_path = BuildLogPath(config, "record_dialog_slice_dedup");
        std::ofstream log(log_path, std::ios::out | std::ios::trunc);
        log << "slice_path=" << slice_path.string() << "\n";
        log << "session_id=" << normalized_session_id << "\n";
        log << "turn_id=" << turn_id << "\n";
        log << "slice_id=" << slice_id << "\n";
        log << "canonical_slice_id=" << canonical_slice_id << "\n";
        log << "dedup_hash=" << dedup_hash << "\n";
        log << "canonical_status=" << canonical_status << "\n";
        log << "dedup_reason=" << dedup_reason << "\n";
        log << "dup_pointer_written=true\n";
        log << "dup_pointer_bytes=" << duplicate_line_text.size() << "\n";
        log.close();
        result.fields["slice_id"] = slice_id;
        result.fields["slice_path"] = slice_path.string();
        result.fields["slice_index_path"] = canonical_index_path.string();
        result.fields["index_entry_count_before"] = std::to_string(index_entry_count_before);
        result.fields["index_entry_count_after"] = std::to_string(index_entry_count_before);
        result.fields["storage_ref"] = slice_path.string();
        result.fields["slice_refs"] = slice_refs;
        result.fields["storage_refs"] = storage_refs;
        result.fields["vector_payload"] = vector_payload;
        result.fields["business_text_ready"] = business_text_ready ? "true" : "false";
        result.fields["projection_incomplete"] = append_truncated_projection_incomplete ? "true" : "false";
        result.fields["embedding_endpoint"] = config.embedding_endpoint;
        result.fields["embedding_service_ready"] = embedding_service_ready ? "true" : "false";
        result.fields["embedding_service_detail"] = embedding_service_detail;
        result.fields["vector_ready"] = "false";
        result.fields["vector_skip_reason"] = vector_skip_reason.empty()
            ? "duplicate_of_canonical"
            : vector_skip_reason;
        result.fields["quality_guard"] = append_truncated_projection_incomplete
            ? "append_truncated_projection_incomplete"
            : (business_text_ready
                ? (embedding_service_ready ? "accepted_duplicate_without_vectorization" : "accepted_clean_text_embedding_down")
                : (vector_skip_reason == "dirty_content_only_brace"
                    ? "dirty_content_only_brace"
                    : "accepted_dirty_text_without_vectorization"));
        result.fields["dedup_hash"] = dedup_hash;
        result.fields["clips_slice_fact"] = clips_fact_input.str();
        result.fields["canonical_slice_id"] = canonical_slice_id;
        result.fields["canonical_status"] = canonical_status;
        result.fields["dedup_status"] = dedup_status;
        result.fields["dedup_reason"] = dedup_reason;
        result.fields["dup_of"] = dup_of;
        result.fields["audit_ref"] = audit_ref;
        ApplyClipsDecisionFields(clips_slice_decision, "slice", &result);
        result.fields["error_signature"] = error_signature;
        result.fields["solution_summary"] = solution_summary;
        result.fields["strategy_family"] = strategy_family;
        result.fields["similarity_score"] = "0";
        result.fields["business_user_text"] = business_user_text;
        result.fields["business_assistant_text"] = business_assistant_text;
        result.fields["business_summary"] = summary_text;
        result.fields["raw_user_text"] = stored_user_text;
        result.fields["raw_assistant_text"] = stored_assistant_text;
        result.fields["bytes"] = std::to_string(duplicate_line_text.size());
        result.fields["log_path"] = log_path;
        result.fields["result"] = "duplicate_pointer_recorded";
        result.fields["dup_pointer"] = "true";
        result.fields["next_action"] = "reuse canonical_slice_id or analyze the current session duplicate pointer entry";
        return result;
    }

    std::ostringstream line;
    line << "{"
        << "\"timestamp\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\","
        << "\"slice_id\":\"" << codex_lan_agent::JsonEscape(slice_id) << "\","
        << "\"slice_version\":\"rag_memory_slice_v1\","
        << "\"canonical_slice_id\":\"" << codex_lan_agent::JsonEscape(canonical_slice_id) << "\","
        << "\"canonical_status\":\"" << codex_lan_agent::JsonEscape(canonical_status) << "\","
        << "\"dedup_status\":\"" << codex_lan_agent::JsonEscape(dedup_status) << "\","
        << "\"dedup_reason\":\"" << codex_lan_agent::JsonEscape(dedup_reason) << "\","
        << "\"slice_type\":\"dialog_slice\"" << ","
        << "\"task_id\":\"" << codex_lan_agent::JsonEscape(normalized_task_id) << "\","
        << "\"session_id\":\"" << codex_lan_agent::JsonEscape(normalized_session_id) << "\","
        << "\"turn_id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
        << "\"provider_id\":\"" << codex_lan_agent::JsonEscape(normalized_provider_id) << "\","
        << "\"capability_id\":\"" << codex_lan_agent::JsonEscape(normalized_capability_id) << "\","
        << "\"user_text\":\"" << codex_lan_agent::JsonEscape(stored_user_text) << "\","
        << "\"assistant_text\":\"" << codex_lan_agent::JsonEscape(stored_assistant_text) << "\","
        << "\"raw_user_text\":\"" << codex_lan_agent::JsonEscape(stored_user_text) << "\","
        << "\"raw_assistant_text\":\"" << codex_lan_agent::JsonEscape(stored_assistant_text) << "\","
        << "\"slice_summary\":\"" << codex_lan_agent::JsonEscape(summary_text) << "\","
        << "\"business_user_text\":\"" << codex_lan_agent::JsonEscape(business_user_text) << "\","
        << "\"business_assistant_text\":\"" << codex_lan_agent::JsonEscape(business_assistant_text) << "\","
        << "\"business_summary\":\"" << codex_lan_agent::JsonEscape(summary_text) << "\","
        << "\"error_signature\":\"" << codex_lan_agent::JsonEscape(error_signature) << "\","
        << "\"solution_summary\":\"" << codex_lan_agent::JsonEscape(solution_summary) << "\","
        << "\"strategy_family\":\"" << codex_lan_agent::JsonEscape(strategy_family) << "\","
        << "\"reasoning_level\":\"" << codex_lan_agent::JsonEscape(normalized_reasoning_level) << "\","
        << "\"primary_intent\":\"" << codex_lan_agent::JsonEscape(normalized_primary_intent) << "\","
        << "\"confidence\":\"" << codex_lan_agent::JsonEscape(normalized_confidence) << "\","
        << "\"similarity_score\":\"0\","
        << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(normalized_result_ref) << "\","
        << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(normalized_evidence_ref) << "\","
        << "\"audit_ref\":\"" << codex_lan_agent::JsonEscape(audit_ref) << "\","
        << "\"source_type\":\"" << codex_lan_agent::JsonEscape(normalized_source_type) << "\","
        << "\"write_mode\":\"" << codex_lan_agent::JsonEscape(normalized_write_mode) << "\","
        << "\"vector_payload\":\"" << codex_lan_agent::JsonEscape(vector_payload) << "\","
        << "\"business_text_ready\":\"" << (business_text_ready ? "true" : "false") << "\","
        << "\"projection_incomplete\":\"" << (append_truncated_projection_incomplete ? "true" : "false") << "\","
        << "\"embedding_endpoint\":\"" << codex_lan_agent::JsonEscape(config.embedding_endpoint) << "\","
        << "\"embedding_service_ready\":\"" << (embedding_service_ready ? "true" : "false") << "\","
        << "\"embedding_service_detail\":\"" << codex_lan_agent::JsonEscape(embedding_service_detail) << "\","
        << "\"vector_ready\":\"" << (vector_ready ? "true" : "false") << "\","
        << "\"vector_skip_reason\":\"" << codex_lan_agent::JsonEscape(vector_skip_reason) << "\","
        << "\"quality_guard\":\"" << codex_lan_agent::JsonEscape(quality_guard) << "\","
        << "\"dedup_hash\":\"" << codex_lan_agent::JsonEscape(dedup_hash) << "\","
        << "\"slice_refs\":" << slice_refs << ","
        << "\"clips_fact\":" << clips_fact_input.str() << ","
        << "\"clips_domain\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.domain) << "\","
        << "\"clips_decision\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.decision) << "\","
        << "\"clips_verification\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.verification) << "\","
        << "\"clips_reason_code\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.reason_code) << "\","
        << "\"clips_matched_rule\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.matched_rule) << "\","
        << "\"clips_route_target\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.route_target) << "\","
        << "\"clips_engine_status\":\"" << codex_lan_agent::JsonEscape(clips_slice_decision.engine_status) << "\","
        << "\"storage_refs\":" << storage_refs << ","
        << "\"tags\":\"" << codex_lan_agent::JsonEscape(tags) << "\""
        << "}\n";
    const std::string line_text = line.str();

    std::ofstream output(slice_path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 71;
        result.fields["error"] = "failed to open dialog slice file";
        return result;
    }
    output.write(line_text.data(), static_cast<std::streamsize>(line_text.size()));
    output.close();

    std::ofstream canonical_output(canonical_index_path, std::ios::binary | std::ios::app);
    if (!canonical_output.is_open()) {
        result.ok = false;
        result.exit_code = 71;
        result.fields["error"] = "failed to open dialog slice canonical index";
        return result;
    }
    canonical_output << "{"
                     << "\"timestamp\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\","
                     << "\"dedup_hash\":\"" << codex_lan_agent::JsonEscape(dedup_hash) << "\","
                     << "\"canonical_slice_id\":\"" << codex_lan_agent::JsonEscape(canonical_slice_id) << "\","
                     << "\"slice_id\":\"" << codex_lan_agent::JsonEscape(slice_id) << "\","
                     << "\"session_id\":\"" << codex_lan_agent::JsonEscape(normalized_session_id) << "\","
                     << "\"turn_id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
                     << "\"provider_id\":\"" << codex_lan_agent::JsonEscape(normalized_provider_id) << "\","
                     << "\"capability_id\":\"" << codex_lan_agent::JsonEscape(normalized_capability_id) << "\","
                     << "\"source_type\":\"" << codex_lan_agent::JsonEscape(normalized_source_type) << "\","
                     << "\"write_mode\":\"" << codex_lan_agent::JsonEscape(normalized_write_mode) << "\","
                     << "\"dedup_status\":\"" << codex_lan_agent::JsonEscape(dedup_status) << "\","
                     << "\"canonical_status\":\"" << codex_lan_agent::JsonEscape(canonical_status) << "\","
                     << "\"business_text_ready\":\"" << (business_text_ready ? "true" : "false") << "\","
                     << "\"projection_incomplete\":\"" << (append_truncated_projection_incomplete ? "true" : "false") << "\","
                     << "\"embedding_endpoint\":\"" << codex_lan_agent::JsonEscape(config.embedding_endpoint) << "\","
                     << "\"embedding_service_ready\":\"" << (embedding_service_ready ? "true" : "false") << "\","
                     << "\"embedding_service_detail\":\"" << codex_lan_agent::JsonEscape(embedding_service_detail) << "\","
                     << "\"vector_ready\":\"" << (vector_ready ? "true" : "false") << "\","
                     << "\"vector_skip_reason\":\"" << codex_lan_agent::JsonEscape(vector_skip_reason) << "\","
                     << "\"quality_guard\":\"" << codex_lan_agent::JsonEscape(quality_guard) << "\","
                     << "\"storage_refs\":" << storage_refs
                     << "}\n";
    canonical_output.close();

    std::string final_content;
    std::string read_error;
    ReadWholeFile(slice_path, &final_content, &read_error);
    const std::string log_path = BuildLogPath(config, "record_dialog_slice");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "slice_path=" << slice_path.string() << "\n";
    log << "session_id=" << SanitizeDialogSliceSessionId(session_id) << "\n";
    log << "turn_id=" << turn_id << "\n";
    log << "slice_id=" << slice_id << "\n";
    log << "dedup_hash=" << dedup_hash << "\n";
    log << "canonical_status=" << canonical_status << "\n";
    log << "bytes=" << line_text.size() << "\n";
    log << "final_checksum=" << StableContentChecksum(final_content) << "\n";
    log.close();

    result.fields["slice_id"] = slice_id;
    result.fields["canonical_slice_id"] = canonical_slice_id;
    result.fields["canonical_status"] = canonical_status;
    result.fields["dedup_reason"] = dedup_reason;
    result.fields["slice_path"] = slice_path.string();
    result.fields["slice_index_path"] = canonical_index_path.string();
    result.fields["index_entry_count_before"] = std::to_string(index_entry_count_before);
    result.fields["index_entry_count_after"] = std::to_string(index_entry_count_before + 1);
    result.fields["storage_ref"] = slice_path.string();
    result.fields["slice_refs"] = slice_refs;
    result.fields["storage_refs"] = storage_refs;
    result.fields["vector_payload"] = vector_payload;
    result.fields["business_text_ready"] = business_text_ready ? "true" : "false";
    result.fields["projection_incomplete"] = append_truncated_projection_incomplete ? "true" : "false";
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["embedding_service_ready"] = embedding_service_ready ? "true" : "false";
    result.fields["embedding_service_detail"] = embedding_service_detail;
    result.fields["business_user_text"] = business_user_text;
    result.fields["business_assistant_text"] = business_assistant_text;
    result.fields["business_summary"] = summary_text;
    result.fields["raw_user_text"] = stored_user_text;
    result.fields["raw_assistant_text"] = stored_assistant_text;
    result.fields["vector_ready"] = vector_ready ? "true" : "false";
    result.fields["vector_skip_reason"] = vector_skip_reason;
    result.fields["quality_guard"] = quality_guard;
    result.fields["dedup_hash"] = dedup_hash;
    result.fields["clips_slice_fact"] = clips_fact_input.str();
    result.fields["canonical_slice_id"] = canonical_slice_id;
    result.fields["canonical_status"] = canonical_status;
    result.fields["dedup_status"] = dedup_status;
    result.fields["dedup_reason"] = dedup_reason;
    result.fields["dup_of"] = dup_of;
    result.fields["audit_ref"] = audit_ref;
    ApplyClipsDecisionFields(clips_slice_decision, "slice", &result);
    result.fields["error_signature"] = error_signature;
    result.fields["solution_summary"] = solution_summary;
    result.fields["strategy_family"] = strategy_family;
    result.fields["similarity_score"] = "0";
    result.fields["bytes"] = std::to_string(line_text.size());
    result.fields["checksum"] = StableContentChecksum(line_text);
    result.fields["file_checksum"] = StableContentChecksum(final_content);
    result.fields["log_path"] = log_path;
    result.fields["tags"] = tags;
    result.fields["result"] = "recorded";
    result.fields["next_action"] = "call lan_agent_analyze_dialog_slices to inspect stored turns";
    return result;
}

CommandResult AnalyzeDialogSlicesResult(
    const AgentConfig & config,
    const std::string & session_id,
    int max_entries) {
    CommandResult result;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "analyze_dialog_slices";
    result.fields["provider_id"] = "codex_lan_agent_dialog_slice_reader";
    result.fields["capability_id"] = "dialog_slice_analysis";
    result.fields["analysis_root"] = BuildDialogSlicesDir(config);
    result.fields["session_id"] = SanitizeDialogSliceSessionId(session_id);
    result.fields["expected_session_slice_path"] = BuildDialogSlicePath(config, session_id).string();
    result.fields["ingest_filter"] = "canonical_status=canonical && vector_ready=true && vector_skip_reason=empty";
    result.fields["recall_filter"] = result.fields["ingest_filter"];
    result.fields["memory_acceptance_key"] = "canonical_slice_id";
    result.fields["memory_evidence_source"] = "canonical_index_first";
    result.fields["session_file_role"] = "audit_projection_not_unique_source";

    std::string embedding_detail;
    std::string effective_embedding_endpoint;
    std::string embedding_endpoint_source;
    const bool embedding_ready = ResolveReachableEndpoint(
        config.embedding_endpoint,
        DeriveEmbeddingFallbackEndpoint(config),
        2000,
        &effective_embedding_endpoint,
        &embedding_detail,
        &embedding_endpoint_source);
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["embedding_endpoint_effective"] = effective_embedding_endpoint;
    result.fields["embedding_endpoint_source"] = embedding_endpoint_source;
    result.fields["embedding_ready"] = embedding_ready ? "true" : "false";
    result.fields["embedding_detail"] = embedding_detail;
    result.fields["ingest_state"] = embedding_ready ? "audit_ready_vectorization_possible" : "vectorization_blocked";
    result.fields["vectorization_status"] = embedding_ready ? "available" : "blocked";
    result.fields["vectorization_block_reason"] = embedding_ready ? "" : "embedding_ready_false";

    const std::filesystem::path slice_dir(BuildDialogSlicesDir(config));
    const std::filesystem::path canonical_index_path = BuildDialogSliceCanonicalIndexPath(config);
    result.fields["slice_index_path"] = canonical_index_path.string();
    result.fields["index_kind"] = "dialog_slice_canonical_index_jsonl";
    result.fields["index_usage"] = "dedup_canonical_lookup_and_ingest_audit";
    std::error_code ec;
    if (!std::filesystem::exists(slice_dir, ec)) {
        result.ok = false;
        result.exit_code = 72;
        result.fields["error"] = "dialog_slices dir does not exist";
        result.fields["slice_file_count"] = "0";
        result.fields["result"] = "empty";
        result.fields["next_action"] = "call lan_agent_record_dialog_slice first";
        return result;
    }

    std::string canonical_index_content;
    std::string canonical_index_read_error;
    const bool canonical_index_read = ReadWholeFile(canonical_index_path, &canonical_index_content, &canonical_index_read_error);
    result.fields["slice_index_exists"] = canonical_index_read ? "true" : "false";
    result.fields["slice_index_entry_count"] = canonical_index_read
        ? std::to_string(CountJsonlEntries(canonical_index_content))
        : "0";
    result.fields["slice_index_read_error"] = canonical_index_read ? "" : canonical_index_read_error;
    if (canonical_index_read) {
        std::vector<std::string> index_lines;
        std::istringstream index_input(canonical_index_content);
        std::string index_line;
        while (std::getline(index_input, index_line)) {
            const std::string trimmed = Trim(index_line);
            if (!trimmed.empty()) {
                index_lines.push_back(trimmed);
            }
        }
        const int bounded_index_entries = max_entries > 0 ? max_entries : 20;
        const int index_start = index_lines.size() > static_cast<std::size_t>(bounded_index_entries)
            ? static_cast<int>(index_lines.size() - bounded_index_entries)
            : 0;
        std::ostringstream index_tail;
        for (std::size_t index = static_cast<std::size_t>(index_start); index < index_lines.size(); ++index) {
            if (index > static_cast<std::size_t>(index_start)) {
                index_tail << "\n";
            }
            index_tail << index_lines[index];
        }
        const std::string latest_index_entry = index_lines.empty() ? std::string() : index_lines.back();
        result.fields["slice_index_tail"] = index_tail.str();
        result.fields["latest_index_entry"] = latest_index_entry;
        result.fields["latest_index_slice_id"] = ExtractJsonString(latest_index_entry, "slice_id");
        result.fields["latest_index_canonical_slice_id"] = ExtractJsonString(latest_index_entry, "canonical_slice_id");
        result.fields["latest_index_dedup_hash"] = ExtractJsonString(latest_index_entry, "dedup_hash");
        result.fields["latest_index_provider_id"] = ExtractJsonString(latest_index_entry, "provider_id");
        result.fields["latest_index_capability_id"] = ExtractJsonString(latest_index_entry, "capability_id");
        result.fields["latest_index_source_type"] = ExtractJsonString(latest_index_entry, "source_type");
        result.fields["latest_index_write_mode"] = ExtractJsonString(latest_index_entry, "write_mode");
        result.fields["latest_index_vector_ready"] = ExtractJsonString(latest_index_entry, "vector_ready");
        result.fields["latest_index_vector_skip_reason"] = ExtractJsonString(latest_index_entry, "vector_skip_reason");
    }

    struct SliceEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type write_time;
    };

    std::vector<SliceEntry> entries;
    if (!session_id.empty()) {
        const std::filesystem::path session_path = BuildDialogSlicePath(config, session_id);
        if (std::filesystem::exists(session_path, ec) && !ec) {
            entries.push_back({session_path, std::filesystem::last_write_time(session_path, ec)});
            if (ec) {
                entries.clear();
                ec.clear();
            }
        }
    } else {
        for (const auto & entry : std::filesystem::directory_iterator(slice_dir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() != ".jsonl") {
                continue;
            }
            if (entry.path().filename() == "_canonical_index.jsonl") {
                continue;
            }
            entries.push_back({entry.path(), entry.last_write_time(ec)});
            if (ec) {
                entries.pop_back();
                ec.clear();
            }
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const SliceEntry & left, const SliceEntry & right) {
            return left.write_time > right.write_time;
        });

    result.fields["slice_file_count"] = std::to_string(entries.size());
    result.fields["max_entries"] = std::to_string(max_entries > 0 ? max_entries : 20);
    if (entries.empty()) {
        result.ok = false;
        result.exit_code = 73;
        result.fields["error"] = "no dialog slice files found";
        result.fields["result"] = "empty";
        result.fields["summary"] = "dialog slice folder is empty";
        const std::string requested_session_id = SanitizeDialogSliceSessionId(session_id);
        if (!requested_session_id.empty() && canonical_index_read) {
            std::istringstream index_input(canonical_index_content);
            std::string index_line;
            bool found_session_reference = false;
            std::string found_canonical_slice_id;
            std::string found_dedup_hash;
            std::string found_storage_refs;
            while (std::getline(index_input, index_line)) {
                const std::string trimmed = Trim(index_line);
                if (trimmed.empty()) {
                    continue;
                }
                if (ExtractJsonString(trimmed, "session_id") == requested_session_id) {
                    found_session_reference = true;
                    found_canonical_slice_id = ExtractJsonString(trimmed, "canonical_slice_id");
                    found_dedup_hash = ExtractJsonString(trimmed, "dedup_hash");
                    found_storage_refs = ExtractJsonRawValue(trimmed, "storage_refs");
                }
            }
            if (found_session_reference) {
                result.ok = true;
                result.exit_code = 0;
                result.fields["canonical_fallback_session_reference"] = "true";
                result.fields["canonical_fallback_canonical_slice_id"] = found_canonical_slice_id;
                result.fields["canonical_slice_id"] = found_canonical_slice_id;
                result.fields["dedup_hash"] = found_dedup_hash;
                result.fields["storage_refs"] = found_storage_refs;
                result.fields["audit_replay_source"] = "canonical_index_first";
                result.fields["audit_replay_canonical_slice_id"] = found_canonical_slice_id;
                result.fields["audit_replay_dedup_hash"] = found_dedup_hash;
                result.fields["audit_replay_storage_refs"] = found_storage_refs;
                result.fields["ingest_status"] = "blocked";
                result.fields["ingest_block_reason"] = embedding_ready ? "" : "embedding_ready_false";
                result.fields["recall_status"] = "not_evaluated";
                result.fields["recall_reason"] = embedding_ready ? "canonical_index_only" : "vectorization_blocked";
                result.fields["result"] = "canonical_index_only";
                result.fields["summary"] = "session slice file missing but canonical index contains session references";
                result.fields["next_action"] = "use canonical_fallback_canonical_slice_id as the memory acceptance key; session file is only an audit projection";
            } else {
                result.fields["canonical_fallback_session_reference"] = "false";
                result.fields["next_action"] = "record a dialog slice before analysis";
            }
        } else {
            result.fields["next_action"] = "record a dialog slice before analysis";
        }
        return result;
    }

    const std::filesystem::path latest_path = entries.front().path;
    result.fields["latest_slice_path"] = latest_path.string();
    result.fields["latest_slice_name"] = latest_path.filename().string();

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(latest_path, &content, &read_error)) {
        result.ok = false;
        result.exit_code = 74;
        result.fields["error"] = read_error;
        return result;
    }

    std::vector<std::string> lines;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (!trimmed.empty()) {
            lines.push_back(trimmed);
        }
    }

    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const int start_index = lines.size() > static_cast<std::size_t>(bounded_max_entries)
        ? static_cast<int>(lines.size() - bounded_max_entries)
        : 0;
    std::ostringstream tail;
    for (std::size_t index = static_cast<std::size_t>(start_index); index < lines.size(); ++index) {
        if (index > static_cast<std::size_t>(start_index)) {
            tail << "\n";
        }
        tail << lines[index];
    }

    const std::string latest_entry = lines.empty() ? std::string() : lines.back();
    result.fields["latest_entry"] = latest_entry;
    result.fields["latest_slice_id"] = ExtractJsonString(latest_entry, "slice_id");
    result.fields["latest_slice_session_id"] = ExtractJsonString(latest_entry, "session_id");
    result.fields["latest_slice_turn_id"] = ExtractJsonString(latest_entry, "turn_id");
    result.fields["latest_slice_summary"] = ExtractJsonString(latest_entry, "slice_summary");
    result.fields["latest_slice_provider_id"] = ExtractJsonString(latest_entry, "provider_id");
    result.fields["latest_slice_capability_id"] = ExtractJsonString(latest_entry, "capability_id");
    result.fields["latest_slice_task_id"] = ExtractJsonString(latest_entry, "task_id");
    result.fields["latest_slice_reasoning_level"] = ExtractJsonString(latest_entry, "reasoning_level");
    result.fields["latest_slice_primary_intent"] = ExtractJsonString(latest_entry, "primary_intent");
    result.fields["latest_slice_confidence"] = ExtractJsonString(latest_entry, "confidence");
    result.fields["latest_slice_business_user_text"] = ExtractJsonString(latest_entry, "business_user_text");
    result.fields["latest_slice_business_assistant_text"] = ExtractJsonString(latest_entry, "business_assistant_text");
    result.fields["latest_slice_business_summary"] = ExtractJsonString(latest_entry, "business_summary");
    result.fields["latest_slice_result_ref"] = ExtractJsonString(latest_entry, "result_ref");
    result.fields["latest_slice_evidence_ref"] = ExtractJsonString(latest_entry, "evidence_ref");
    result.fields["latest_slice_source_type"] = ExtractJsonString(latest_entry, "source_type");
    result.fields["latest_slice_write_mode"] = ExtractJsonString(latest_entry, "write_mode");
    result.fields["latest_slice_error_signature"] = ExtractJsonString(latest_entry, "error_signature");
    result.fields["latest_slice_solution_summary"] = ExtractJsonString(latest_entry, "solution_summary");
    result.fields["latest_slice_strategy_family"] = ExtractJsonString(latest_entry, "strategy_family");
    result.fields["latest_slice_similarity_score"] = ExtractJsonString(latest_entry, "similarity_score");
    result.fields["latest_slice_canonical_slice_id"] = ExtractJsonString(latest_entry, "canonical_slice_id");
    result.fields["latest_slice_canonical_status"] = ExtractJsonString(latest_entry, "canonical_status");
    result.fields["latest_slice_dedup_reason"] = ExtractJsonString(latest_entry, "dedup_reason");
    result.fields["latest_slice_vector_ready"] = ExtractJsonString(latest_entry, "vector_ready");
    result.fields["latest_slice_vector_skip_reason"] = ExtractJsonString(latest_entry, "vector_skip_reason");
    result.fields["latest_slice_quality_guard"] = ExtractJsonString(latest_entry, "quality_guard");
    result.fields["latest_slice_dedup_hash"] = ExtractJsonString(latest_entry, "dedup_hash");
    result.fields["latest_slice_storage_refs"] = ExtractJsonRawValue(latest_entry, "storage_refs");
    result.fields["provider_id"] = result.fields["latest_slice_provider_id"];
    result.fields["capability_id"] = result.fields["latest_slice_capability_id"];
    result.fields["session_id"] = result.fields["latest_slice_session_id"];
    result.fields["turn_id"] = result.fields["latest_slice_turn_id"];
    result.fields["task_id"] = result.fields["latest_slice_task_id"];
    result.fields["result_ref"] = result.fields["latest_slice_result_ref"];
    result.fields["evidence_ref"] = result.fields["latest_slice_evidence_ref"];
    result.fields["vector_ready"] = result.fields["latest_slice_vector_ready"];
    result.fields["vector_skip_reason"] = result.fields["latest_slice_vector_skip_reason"];
    result.fields["dedup_hash"] = result.fields["latest_slice_dedup_hash"];
    result.fields["canonical_slice_id"] = result.fields["latest_slice_canonical_slice_id"];
    result.fields["storage_refs"] = result.fields["latest_slice_storage_refs"];
    result.fields["audit_replay_source"] = "canonical_index_first";
    result.fields["audit_replay_canonical_slice_id"] = result.fields["latest_slice_canonical_slice_id"];
    result.fields["audit_replay_dedup_hash"] = result.fields["latest_slice_dedup_hash"];
    result.fields["audit_replay_storage_refs"] = result.fields["latest_slice_storage_refs"];
    result.fields["ingest_status"] = embedding_ready ? "eligible_for_vectorization_check" : "blocked";
    result.fields["ingest_block_reason"] = embedding_ready ? "" : "embedding_ready_false";

    result.fields["matched_entries_tail"] = tail.str();
    if (!embedding_ready) {
        result.fields["recall_status"] = "not_evaluated";
        result.fields["recall_reason"] = "vectorization_blocked";
        result.fields["recall_interpretation"] = "embedding service is unavailable; audit replay fields are valid but similarity and recall are not promised";
        result.fields["recall_candidate_count"] = "";
        result.fields["latest_slice_similarity_score"] = "";
        result.fields["similarity_score"] = "";
        result.fields["retrieval_provider_id"] = "";
        result.fields["retrieval_capability_id"] = "";
        result.fields["summary"] = lines.empty()
            ? "no dialog slice entries"
            : ("latest dialog slice audit replay from " + result.fields["latest_slice_session_id"]);
        result.fields["result"] = "analyzed_audit_only";
        result.fields["next_action"] = "restore embedding_endpoint before validating ingest, vectorization, similarity, or recall";
        return result;
    }

    struct RecallHit {
        std::string slice_id;
        std::string canonical_slice_id;
        std::string provider_id;
        std::string capability_id;
        std::string error_signature;
        std::string strategy_family;
        std::string solution_summary;
        std::string business_summary;
        double similarity_score = 0.0;
    };
    const std::string recall_query_error = ToLowerAscii(result.fields["latest_slice_error_signature"]);
    const std::string recall_query_strategy = ToLowerAscii(result.fields["latest_slice_strategy_family"]);
    const std::string latest_canonical_id = result.fields["latest_slice_canonical_slice_id"];
    std::unordered_set<std::string> seen_canonical_ids;
    std::vector<RecallHit> recall_hits;
    int recall_skipped_not_canonical = 0;
    int recall_skipped_not_vector_ready = 0;
    int recall_skipped_vector_skip_reason = 0;
    int recall_skipped_duplicate_canonical = 0;
    int recall_skipped_no_similarity = 0;
    std::error_code recall_ec;
    for (const auto & entry : std::filesystem::directory_iterator(slice_dir, recall_ec)) {
        if (recall_ec) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl") {
            continue;
        }
        if (entry.path().filename() == "_canonical_index.jsonl") {
            continue;
        }
        std::string recall_content;
        std::string recall_read_error;
        if (!ReadWholeFile(entry.path(), &recall_content, &recall_read_error)) {
            continue;
        }
        std::istringstream recall_input(recall_content);
        std::string recall_line;
        while (std::getline(recall_input, recall_line)) {
            const std::string trimmed = Trim(recall_line);
            if (trimmed.empty()) {
                continue;
            }
            const std::string canonical_status = ExtractJsonString(trimmed, "canonical_status");
            const std::string vector_ready = ToLowerAscii(ExtractJsonString(trimmed, "vector_ready"));
            const std::string vector_skip_reason = ExtractJsonString(trimmed, "vector_skip_reason");
            const std::string candidate_canonical_id = ExtractJsonString(trimmed, "canonical_slice_id");
            if (canonical_status != "canonical" || candidate_canonical_id.empty()) {
                ++recall_skipped_not_canonical;
                continue;
            }
            if (vector_ready != "true") {
                ++recall_skipped_not_vector_ready;
                continue;
            }
            if (!vector_skip_reason.empty()) {
                ++recall_skipped_vector_skip_reason;
                continue;
            }
            if (candidate_canonical_id == latest_canonical_id || seen_canonical_ids.count(candidate_canonical_id) > 0) {
                ++recall_skipped_duplicate_canonical;
                continue;
            }
            const std::string candidate_error = ToLowerAscii(ExtractJsonString(trimmed, "error_signature"));
            const std::string candidate_strategy = ToLowerAscii(ExtractJsonString(trimmed, "strategy_family"));
            double score = 0.0;
            if (!recall_query_error.empty() && candidate_error == recall_query_error) {
                score += 0.85;
            }
            if (!recall_query_strategy.empty() && candidate_strategy == recall_query_strategy) {
                score += 0.15;
            }
            if (score <= 0.0) {
                ++recall_skipped_no_similarity;
                continue;
            }
            seen_canonical_ids.insert(candidate_canonical_id);
            recall_hits.push_back(RecallHit{
                ExtractJsonString(trimmed, "slice_id"),
                candidate_canonical_id,
                ExtractJsonString(trimmed, "provider_id"),
                ExtractJsonString(trimmed, "capability_id"),
                ExtractJsonString(trimmed, "error_signature"),
                ExtractJsonString(trimmed, "strategy_family"),
                ExtractJsonString(trimmed, "solution_summary"),
                ExtractJsonString(trimmed, "business_summary"),
                score});
        }
    }
    std::sort(
        recall_hits.begin(),
        recall_hits.end(),
        [](const RecallHit & left, const RecallHit & right) {
            if (left.similarity_score != right.similarity_score) {
                return left.similarity_score > right.similarity_score;
            }
            return left.canonical_slice_id < right.canonical_slice_id;
        });
    result.fields["ingest_filter"] = "canonical_status=canonical && vector_ready=true && vector_skip_reason=empty";
    result.fields["recall_filter"] = result.fields["ingest_filter"];
    result.fields["recall_candidate_count"] = std::to_string(recall_hits.size());
    result.fields["retrieval_provider_id"] = "codex_lan_agent_dialog_slice_analyzer";
    result.fields["retrieval_capability_id"] = "dialog_slice_analysis";
    result.fields["display_provider_id"] = "llama_cpp_b8851_remote_session";
    result.fields["display_capability_id"] = "remote_session_turn";
    result.fields["recall_skipped_not_canonical"] = std::to_string(recall_skipped_not_canonical);
    result.fields["recall_skipped_not_vector_ready"] = std::to_string(recall_skipped_not_vector_ready);
    result.fields["recall_skipped_vector_skip_reason"] = std::to_string(recall_skipped_vector_skip_reason);
    result.fields["recall_skipped_duplicate_canonical"] = std::to_string(recall_skipped_duplicate_canonical);
    result.fields["recall_skipped_no_similarity"] = std::to_string(recall_skipped_no_similarity);
    const bool latest_is_vector_candidate =
        ToLowerAscii(result.fields["latest_slice_vector_ready"]) == "true"
        && result.fields["latest_slice_vector_skip_reason"].empty()
        && result.fields["latest_slice_canonical_status"] == "canonical";
    if (!latest_is_vector_candidate) {
        result.fields["recall_reason"] = "latest_slice_not_vector_candidate";
        result.fields["recall_interpretation"] = "pending_vector_skip_reason_fix";
    } else if (recall_hits.empty()) {
        result.fields["recall_reason"] = "no_canonical_vector_ready_match";
        result.fields["recall_interpretation"] = "transition_metric_not_top1_acceptance";
    } else {
        result.fields["recall_reason"] = "canonical_vector_ready_candidates_available";
        result.fields["recall_interpretation"] = "top1_ready_for_duplicate_write_validation";
        const RecallHit & top_hit = recall_hits.front();
        std::ostringstream top_score;
        top_score << std::fixed << std::setprecision(2) << top_hit.similarity_score;
        result.fields["recall_top_1_slice_id"] = top_hit.slice_id;
        result.fields["recall_top_1_canonical_slice_id"] = top_hit.canonical_slice_id;
        result.fields["recall_top_1_provider_id"] = top_hit.provider_id;
        result.fields["recall_top_1_capability_id"] = top_hit.capability_id;
        result.fields["recall_top_1_retrieval_provider_id"] = result.fields["retrieval_provider_id"];
        result.fields["recall_top_1_display_provider_id"] = result.fields["display_provider_id"];
        result.fields["recall_top_1_error_signature"] = top_hit.error_signature;
        result.fields["recall_top_1_strategy_family"] = top_hit.strategy_family;
        result.fields["recall_top_1_solution_summary"] = top_hit.solution_summary;
        result.fields["recall_top_1_business_summary"] = top_hit.business_summary;
        result.fields["recall_top_1_similarity_score"] = top_score.str();
    }
    result.fields["summary"] = lines.empty()
        ? "no dialog slice entries"
        : ("latest dialog slice from " + result.fields["latest_slice_session_id"]);
    result.fields["result"] = "analyzed";
    result.fields["next_action"] = "use latest_slice_path or matched_entries_tail for downstream review";
    return result;
}
